/* SPDX-License-Identifier: MIT */
#include "server.h"
#include "websocket.h"
#include "web_assets.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "format.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET buzz_socket;
#define BUZZ_BAD INVALID_SOCKET
static int nonblock(buzz_socket fd) { u_long on=1; return ioctlsocket(fd,FIONBIO,&on); }
static void close_fd(buzz_socket fd) { closesocket(fd); }
static int would_block(void) { int e=WSAGetLastError(); return e==WSAEWOULDBLOCK; }
static int socket_error(void) { return WSAGetLastError(); }
#elif defined(BUZZ_PS3)
#include <cell/libnet.h>
#include <net/errno.h>
/* PS3DK's raw PRX socket imports. Avoid the process-owned BSD/initialization
 * compatibility runtime: the running game already owns network initialization. */
extern int __ps3dk_raw_socket(int,int,int);
extern int __ps3dk_raw_accept(int,struct sockaddr *,socklen_t *);
extern int __ps3dk_raw_bind(int,const struct sockaddr *,socklen_t);
extern int __ps3dk_raw_listen(int,int);
extern ssize_t __ps3dk_raw_recv(int,void *,size_t,int);
extern ssize_t __ps3dk_raw_send(int,const void *,size_t,int);
extern int __ps3dk_raw_setsockopt(int,int,int,const void *,socklen_t);
extern int __ps3dk_raw_socketclose(int);
extern int __ps3dk_raw_socketpoll(struct pollfd *,nfds_t,int);
extern int __ps3dk_raw_getsockopt(int,int,int,void *,socklen_t *);
typedef int buzz_socket;
#define BUZZ_BAD (-1)
#define socket __ps3dk_raw_socket
#define accept __ps3dk_raw_accept
#define bind __ps3dk_raw_bind
#define listen __ps3dk_raw_listen
#define recv __ps3dk_raw_recv
#define send __ps3dk_raw_send
#define setsockopt __ps3dk_raw_setsockopt
static int nonblock(buzz_socket fd)
{
 int on=1; socklen_t size=sizeof(on);
 /* Firmware fallback if a library ignores nonblocking flags. Multiplexed peers
  * still use SO_NBIO/MSG_DONTWAIT, never a blocking recv per idle client. */
 struct timeval timeout={0,100000};
 /* The timeouts and the read-back are best effort: 0.6 failed to start with
  * them mandatory, 0.5 (SO_NBIO only) listened fine on the console. */
 (void)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 (void)setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 if(setsockopt(fd,SOL_SOCKET,SO_NBIO,&on,sizeof(on))) return -1;
 on=0; (void)__ps3dk_raw_getsockopt(fd,SOL_SOCKET,SO_NBIO,&on,&size);
 return 0;
}
static void close_fd(buzz_socket fd) { __ps3dk_raw_socketclose(fd); }
static int would_block(void) { int e=*_sys_net_errno_loc(); return e==NET_EWOULDBLOCK || e==NET_EINTR; }
static int socket_error(void) { return *_sys_net_errno_loc(); }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
typedef int buzz_socket;
#define BUZZ_BAD (-1)
static int nonblock(buzz_socket fd) { return fcntl(fd,F_SETFL,O_NONBLOCK); }
static void close_fd(buzz_socket fd) { close(fd); }
static int would_block(void) { return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR; }
static int socket_error(void) { return errno; }
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#define CLIENTS 16
#define RX_CAP 4096
#define TX_CAP 16384
typedef struct {
 buzz_socket fd; unsigned active,ws,closing;
 size_t rxlen,txlen,txoff;
 uint64_t seen_ms,ping_ms;
 uint32_t generation;
 uint8_t rx[RX_CAP],tx[TX_CAP];
} client;
static struct { buzz_socket listener; buzz_hub *hub; client peers[CLIENTS]; unsigned started; int fail_step; int fail_errno; } server;
static struct {
 void (*lock)(void *),(*unlock)(void *); void *arg;
 void (*trace)(const char *,int,int,const char *); void (*progress)(const char *);
} hooks;
static int (*stop_requested)(void);
void buzz_server_set_stop(int (*fn)(void)) { stop_requested=fn; }
void buzz_server_set_hooks(void (*lock)(void *),void (*unlock)(void *),void *arg,
 void (*trace)(const char *,int,int,const char *),void (*progress)(const char *))
{ hooks.lock=lock; hooks.unlock=unlock; hooks.arg=arg; hooks.trace=trace; hooks.progress=progress; }
static void hub_lock(void) { if(hooks.lock) hooks.lock(hooks.arg); }
static void hub_unlock(void) { if(hooks.unlock) hooks.unlock(hooks.arg); }
static void progress(const char *state) { if(hooks.progress) hooks.progress(state); }
static void trace(const char *event,buzz_socket fd,int result,const char *detail)
{ if(hooks.trace) hooks.trace(event,(int)fd,result,detail); }
static void snapshot(buzz_hub *out) { hub_lock(); *out=*server.hub; hub_unlock(); }
static int listener_ready(void)
{
 /* Readiness plus verified SO_NBIO; this is never a blocking accept loop. */
#ifdef _WIN32
 fd_set reads; struct timeval timeout={0,10000}; FD_ZERO(&reads); FD_SET(server.listener,&reads);
 return select(0,&reads,NULL,NULL,&timeout);
#else
 struct pollfd fd={server.listener,POLLIN,0};
#ifdef BUZZ_PS3
 int result=__ps3dk_raw_socketpoll(&fd,1,10);
#else
 int result=poll(&fd,1,10);
#endif
 return result<0 ? -1 : (fd.revents&POLLIN)!=0;
#endif
}
static void drop(unsigned index)
{
 client *c=&server.peers[index]; if(!c->active) return;
 progress("close-call"); close_fd(c->fd); progress("close-return");
 hub_lock(); buzz_release(server.hub,BUZZ_PHONE,index+1); hub_unlock(); memset(c,0,sizeof(*c));
}
static int queue(client *c,const void *data,size_t n)
{
 if(c->txoff) { memmove(c->tx,c->tx+c->txoff,c->txlen-c->txoff); c->txlen-=c->txoff; c->txoff=0; }
 if(n>TX_CAP-c->txlen) return 0;
 memcpy(c->tx+c->txlen,data,n); c->txlen+=n; return 1;
}
static int frame(client *c,unsigned op,const void *data,size_t n)
{
 uint8_t buf[BUZZ_WS_MAX+4]; size_t bytes=buzz_ws_encode((uint8_t)op,data,n,buf,sizeof(buf));
 return bytes && queue(c,buf,bytes);
}
static int lights(client *c,const buzz_hub *hub)
{
 char buf[256]; unsigned i; size_t used=0; int n;
 n=snprintf(buf,sizeof(buf),"{\"type\":\"lights\",\"slots\":{"); if(n<0) return 0; used=(size_t)n;
 for(i=0;i<buzz_capacity(hub);i++) {
  n=snprintf(buf+used,sizeof(buf)-used,"%s\"%u\":%s",i?",":"",i+1,hub->slots[i].light?"true":"false");
  if(n<0 || (size_t)n>=sizeof(buf)-used) return 0; used+=(size_t)n;
 }
 if(used+3>sizeof(buf)) return 0; memcpy(buf+used,"}}",3); return frame(c,1,buf,used+2);
}
static int status(client *c)
{
 char buf[256]; buzz_hub hub; snapshot(&hub); int n=buzz_status_json(&hub,buf,sizeof(buf));
 if(n<0 || !frame(c,1,buf,(size_t)n) || !lights(c,&hub)) return 0;
 c->generation=hub.generation; return 1;
}
static int http_reply(client *c,unsigned code,const char *type,const void *body,size_t size)
{
 char header[256]; int n=snprintf(header,sizeof(header),
  "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",
  code,code==200?"OK":"Error",type,(unsigned)size);
 c->closing=1; return n>0 && (size_t)n<sizeof(header) && queue(c,header,(size_t)n) && queue(c,body,size);
}
static int http_request(client *c,int ready)
{
 size_t i,header_len=0; char path[128],accept[29],reply[256]; int parsed;
 for(i=0;i+3<c->rxlen;i++) if(!memcmp(c->rx+i,"\r\n\r\n",4)) { header_len=i+4; break; }
 if(!header_len) return c->rxlen<RX_CAP-1;
 c->rx[c->rxlen]=0;
 if(memcmp(c->rx,"GET ",4))
  return http_reply(c,400,"text/plain","Bad request",11);
 for(i=4;i<header_len && c->rx[i]!=' ' && c->rx[i]!='\r';i++)
  if(i-4>=sizeof(path)-1) return http_reply(c,400,"text/plain","Bad request",11);
 if(i+11>header_len || memcmp(c->rx+i," HTTP/1.1\r\n",11))
  return http_reply(c,400,"text/plain","Bad request",11);
 memcpy(path,c->rx+4,i-4); path[i-4]=0;
 trace("request",c->fd,0,path);
 if(!strcmp(path,"/ws")) {
  size_t consumed=0;
  parsed=buzz_http_upgrade((const char *)c->rx,c->rxlen,accept,&consumed);
  if(parsed!=1) return http_reply(c,400,"text/plain","Bad WebSocket upgrade",21);
  parsed=snprintf(reply,sizeof(reply),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);
  if(parsed<0 || !queue(c,reply,(size_t)parsed)) return 0;
  c->ws=1; memmove(c->rx,c->rx+consumed,c->rxlen-consumed); c->rxlen-=consumed;
  return status(c);
 }
 if(!strcmp(path,"/")) return http_reply(c,200,"text/html; charset=utf-8",web_index,sizeof(web_index));
 if(!strcmp(path,"/static/app.js")) return http_reply(c,200,"application/javascript; charset=utf-8",web_app,sizeof(web_app));
 if(!strcmp(path,"/static/style.css")) return http_reply(c,200,"text/css; charset=utf-8",web_style,sizeof(web_style));
 if(!strcmp(path,"/health.json")) {
  uint8_t a[5],b[5]; char body[384]; buzz_hub hub; snapshot(&hub); buzz_report(&hub,0,a); buzz_report(&hub,1,b);
  parsed=snprintf(body,sizeof(body),"{\"service\":\"ps3buzz-phase1\",\"usb_backend_ready\":%s,\"reports\":[[%u,%u,%u,%u,%u],[%u,%u,%u,%u,%u]]}",
   ready?"true":"false",a[0],a[1],a[2],a[3],a[4],b[0],b[1],b[2],b[3],b[4]);
  return parsed>0 && (size_t)parsed<sizeof(body) && http_reply(c,200,"application/json",body,(size_t)parsed);
 }
 return http_reply(c,404,"text/plain","Not found",9);
}
static int ws_input(client *c,unsigned index)
{
 for(unsigned frames=0;frames<16 && c->rxlen && !(stop_requested && stop_requested());frames++) {
  buzz_ws_frame f; char reply[256]; int n=buzz_ws_decode(c->rx,c->rxlen,&f),bytes;
  if(!n) return 1;
  if(n<0) { const uint8_t code[]={3,234}; c->closing=1; c->rxlen=0; return frame(c,8,code,2); }
  memmove(c->rx,c->rx+n,c->rxlen-(size_t)n); c->rxlen-=(size_t)n;
  if(f.opcode==8) { c->closing=1; return frame(c,8,f.data,f.len); }
  if(f.opcode==9) { if(!frame(c,10,f.data,f.len)) return 0; }
  else if(f.opcode==1) {
   hub_lock(); bytes=buzz_phone_message(server.hub,index+1,(const char *)f.data,f.len,reply,sizeof(reply)); hub_unlock();
   if(bytes<0) { const char error[]="{\"type\":\"error\",\"message\":\"Invalid message or unclaimed slot\"}";
    if(!frame(c,1,error,sizeof(error)-1)) return 0;
   } else if(bytes>0 && !frame(c,1,reply,(size_t)bytes)) return 0;
  }
 }
 return 1;
}
int buzz_server_fail_step(void) { return server.fail_step; }
int buzz_server_fail_errno(void) { return server.fail_errno; }
int buzz_server_start(buzz_hub *hub,unsigned port,int loopback)
{
 struct sockaddr_in address; int yes=1;
 if(server.started) return -1;
#ifdef _WIN32
 { WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) return -1; }
#endif
 memset(&server,0,sizeof(server)); server.hub=hub;
 server.listener=socket(AF_INET,SOCK_STREAM,0);
 if(server.listener==BUZZ_BAD) { server.fail_step=1; server.fail_errno=socket_error(); goto failed; }
 setsockopt(server.listener,SOL_SOCKET,SO_REUSEADDR,(const char *)&yes,sizeof(yes));
 memset(&address,0,sizeof(address)); address.sin_family=AF_INET; address.sin_port=htons((uint16_t)port);
#ifdef BUZZ_PS3
 address.sin_len=sizeof(address);
#endif
 address.sin_addr.s_addr=htonl(loopback?INADDR_LOOPBACK:INADDR_ANY);
 if(bind(server.listener,(struct sockaddr *)&address,sizeof(address))) server.fail_step=2;
 else if(listen(server.listener,16)) server.fail_step=3;
 else if(nonblock(server.listener)) server.fail_step=4;
 if(server.fail_step) { server.fail_errno=socket_error(); close_fd(server.listener); goto failed; }
 server.started=1; return 0;
failed:
#ifdef _WIN32
 WSACleanup();
#endif
 return -1;
}
void buzz_server_tick(uint64_t now,int ready)
{
 unsigned i; buzz_socket incoming;
 if(!server.started) return;
 if(stop_requested && stop_requested()) return;
 /* At most one accept per tick keeps a connection flood from starving pads. */
 progress("poll-call");
 { int ready_to_accept=listener_ready(); static int reported_poll_error;
  progress("poll-return"); incoming=BUZZ_BAD;
  if(ready_to_accept<0 && !reported_poll_error) { trace("poll-error",server.listener,socket_error(),""); reported_poll_error=1; }
  if(ready_to_accept>0) {
   progress("accept-call"); incoming=accept(server.listener,NULL,NULL); progress("accept-return");
   if(incoming==BUZZ_BAD && !would_block()) trace("accept-error",server.listener,socket_error(),"");
  }
 }
 if(incoming!=BUZZ_BAD) {
  trace("accept",incoming,0,"");
  for(i=0;i<CLIENTS;i++) if(!server.peers[i].active) break;
  if(i==CLIENTS) close_fd(incoming);
  else if(nonblock(incoming)) { trace("nonblock-failed",incoming,socket_error(),""); close_fd(incoming); }
  else { client *c=&server.peers[i]; memset(c,0,sizeof(*c)); c->active=1; c->fd=incoming; c->seen_ms=c->ping_ms=now; }
 }
 for(i=0;i<CLIENTS;i++) {
  if(stop_requested && stop_requested()) break;
  client *c=&server.peers[i]; int n;
  if(!c->active) continue;
  if(now-c->seen_ms>(c->ws?server.hub->config.stale_ms:5000)) { drop(i); continue; }
  if(!c->closing) {
   progress("recv-call");
   n=recv(c->fd,(char *)c->rx+c->rxlen,(int)(RX_CAP-1-c->rxlen),MSG_DONTWAIT);
   progress("recv-return");
   if(n==0 || (n<0 && !would_block())) { trace("recv-close",c->fd,n<0?socket_error():0,""); drop(i); continue; }
   if(n>0) { c->rxlen+=(size_t)n; c->seen_ms=now; }
   if((!c->ws && !http_request(c,ready)) || (c->ws && !ws_input(c,i))) { drop(i); continue; }
   { unsigned ping_interval=server.hub->config.stale_ms/3;
    if(ping_interval>10000) ping_interval=10000;
    if(c->ws && now-c->ping_ms>=ping_interval) { if(!frame(c,9,"",0)) { drop(i); continue; } c->ping_ms=now; }
   }
   { uint32_t generation; hub_lock(); generation=server.hub->generation; hub_unlock();
    if(c->ws && c->generation!=generation && !status(c)) { drop(i); continue; }
   }
  }
  if(c->txlen>c->txoff) {
   progress("send-call");
   n=send(c->fd,(const char *)c->tx+c->txoff,(int)(c->txlen-c->txoff),MSG_NOSIGNAL|MSG_DONTWAIT);
   progress("send-return");
   if(n==0 || (n<0 && !would_block())) { trace("send-close",c->fd,n<0?socket_error():0,""); drop(i); continue; }
   if(n>0 && !c->ws) trace("send",c->fd,n,"");
   if(n>0) c->txoff+=(size_t)n;
  }
  if(c->txoff==c->txlen) { c->txoff=c->txlen=0; if(c->closing) drop(i); }
 }
 progress("net-idle");
}
void buzz_server_stop(void)
{
 unsigned i; if(!server.started) return;
 for(i=0;i<CLIENTS;i++) drop(i); close_fd(server.listener); server.started=0;
#ifdef _WIN32
 WSACleanup();
#endif
}
