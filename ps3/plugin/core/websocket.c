/* SPDX-License-Identifier: MIT */
#include "websocket.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "format.h"

int buzz_ws_decode(const uint8_t *in,size_t len,buzz_ws_frame *f)
{
 size_t n,pos=2,i; unsigned op;
 if(len<2) return 0; op=in[0]&15; n=in[1]&127;
 if((in[0]&0x70) || !(in[0]&0x80) || !(in[1]&0x80) || (op!=1 && op!=8 && op!=9 && op!=10)) return -1;
 if(n==127) return -1;
 if(n==126) { if(len<4) return 0; n=(size_t)in[2]*256+in[3]; pos=4; if(n<126) return -1; }
 if(n>BUZZ_WS_MAX || (op>=8 && n>125) || (op==8 && n==1)) return -1;
 if(len<pos+4+n) return 0;
 for(i=0;i<n;i++) f->data[i]=in[pos+4+i]^in[pos+i%4];
 /* Protocol is ASCII JSON. Close/ping/pong payloads remain binary. */
 if(op==1) for(i=0;i<n;i++) if(f->data[i]<32 || f->data[i]>=127) return -1;
 f->data[n]=0; f->len=n; f->opcode=(uint8_t)op; return (int)(pos+4+n);
}
size_t buzz_ws_encode(uint8_t op,const void *data,size_t n,uint8_t *out,size_t cap)
{
 size_t pos=n<126?2:4;
 if(n>BUZZ_WS_MAX || cap<pos+n || (op>=8 && n>125)) return 0;
 out[0]=(uint8_t)(0x80|op); out[1]=(uint8_t)(n<126?n:126);
 if(pos==4) { out[2]=(uint8_t)(n>>8); out[3]=(uint8_t)n; }
 if(n) memcpy(out+pos,data,n); return pos+n;
}
static uint32_t rol(uint32_t v,unsigned n) { return (v<<n)|(v>>(32-n)); }
/* SHA-1 is used only for RFC6455's public handshake, not authentication. */
static void sha1(const uint8_t *in,size_t n,uint8_t digest[20])
{
 uint8_t blocks[128]={0}; uint32_t h[5]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
 size_t len=((n+9+63)/64)*64,off; unsigned i;
 memcpy(blocks,in,n); blocks[n]=0x80;
 for(i=0;i<8;i++) blocks[len-1-i]=(uint8_t)(((uint64_t)n*8)>>(8*i));
 for(off=0;off<len;off+=64) {
  uint32_t w[80],a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
  for(i=0;i<16;i++) w[i]=((uint32_t)blocks[off+4*i]<<24)|((uint32_t)blocks[off+4*i+1]<<16)|((uint32_t)blocks[off+4*i+2]<<8)|blocks[off+4*i+3];
  for(i=16;i<80;i++) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
  for(i=0;i<80;i++) {
   uint32_t f,k,temp;
   if(i<20) { f=(b&c)|(~b&d); k=0x5a827999; }
   else if(i<40) { f=b^c^d; k=0x6ed9eba1; }
   else if(i<60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdc; }
   else { f=b^c^d; k=0xca62c1d6; }
   temp=rol(a,5)+f+e+k+w[i]; e=d; d=c; c=rol(b,30); b=a; a=temp;
  }
  h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
 }
 for(i=0;i<20;i++) digest[i]=(uint8_t)(h[i/4]>>(24-8*(i%4)));
}
int buzz_ws_accept(const char *key,char out[29])
{
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 char input[61]; uint8_t hash[20]; unsigned i,j=0;
 if(strlen(key)!=24 || key[22]!='=' || key[23]!='=') return 0;
 for(i=0;i<22;i++) if(!strchr(alphabet,key[i])) return 0;
 /* A 16-byte nonce has only two useful bits in base64 character 22. */
 if((strchr(alphabet,key[21])-alphabet)%16) return 0;
 snprintf(input,sizeof(input),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
 sha1((const uint8_t *)input,60,hash);
 for(i=0;i<20;i+=3) {
  uint32_t v=(uint32_t)hash[i]<<16;
  if(i+1<20) v|=(uint32_t)hash[i+1]<<8; if(i+2<20) v|=hash[i+2];
  out[j++]=alphabet[v>>18]; out[j++]=alphabet[(v>>12)&63];
  out[j++]=i+1<20?alphabet[(v>>6)&63]:'='; out[j++]=i+2<20?alphabet[v&63]:'=';
 } out[j]=0; return 1;
}
static int same(const char *a,const char *b)
{ while(*a && *b && tolower((unsigned char)*a)==tolower((unsigned char)*b)) { ++a; ++b; } return !*a && !*b; }
static char *strip(char *p)
{ char *end; while(*p==' '||*p=='\t') ++p; end=p+strlen(p); while(end>p && (end[-1]==' '||end[-1]=='\t')) --end; *end=0; return p; }
static int token(char *p,const char *wanted)
{
 while(*p) { char part[128]; size_t n=0; while(*p && *p!=',') { if(n+1>=sizeof(part)) return 0; part[n++]=*p++; }
  part[n]=0; if(same(strip(part),wanted)) return 1; if(*p) ++p;
 } return 0;
}
int buzz_http_upgrade(const char *in,size_t len,char accept[29],size_t *consumed)
{
 char buf[4096],host[128]="",origin[160]="",key[32]=""; char *line,*end; unsigned seen=0;
 size_t n; if(len>=sizeof(buf)) return -1;
 /* All parsing stays within the supplied length; no strlen on network input. */
 for(n=0;n+3<len;n++) if(!memcmp(in+n,"\r\n\r\n",4)) break;
 if(n+3>=len) return 0; *consumed=n+4; memcpy(buf,in,*consumed); buf[*consumed]=0;
 line=strstr(buf,"\r\n"); if(!line) return -1; *line=0;
 if(strcmp(buf,"GET /ws HTTP/1.1")) return -1; line+=2;
 while(*line) {
  char *colon,*name,*value; unsigned bit=0; end=strstr(line,"\r\n"); if(!end) return -1; *end=0;
  if(!*line) break; colon=strchr(line,':'); if(!colon || colon==line) return -1; *colon=0;
  name=line; value=strip(colon+1);
  if(same(name,"Host")) { bit=1; if(strlen(value)>=sizeof(host)) return -1; memcpy(host,value,strlen(value)+1); }
  else if(same(name,"Origin")) { bit=2; if(strlen(value)>=sizeof(origin)) return -1; memcpy(origin,value,strlen(value)+1); }
  else if(same(name,"Sec-WebSocket-Key")) { bit=4; if(strlen(value)>=sizeof(key)) return -1; memcpy(key,value,strlen(value)+1); }
  else if(same(name,"Upgrade")) { bit=8; if(!same(value,"websocket")) return -1; }
  else if(same(name,"Connection")) { bit=16; if(!token(value,"upgrade")) return -1; }
  else if(same(name,"Sec-WebSocket-Version")) { bit=32; if(strcmp(value,"13")) return -1; }
  else if(same(name,"Content-Length") && strcmp(value,"0")) return -1;
  else if(same(name,"Transfer-Encoding")) return -1;
  /* Browsers offer permessage-deflate. Omit it from the reply and use plain frames. */
  if(bit && (seen&bit)) return -1; seen|=bit; line=end+2;
 }
 if((seen&61)!=61 || !*host) return -1;
 if(*origin) { char expected[160]; snprintf(expected,sizeof(expected),"http://%s",host); if(!same(expected,origin)) return -1; }
 return buzz_ws_accept(key,accept)?1:-1;
}
