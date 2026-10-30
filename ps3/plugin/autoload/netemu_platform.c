/* SPDX-License-Identifier: MIT. Same webMAN dev_blind endpoint as the proven
 * PowerShell route. App uses local filesystem in place of FTP upload.
 * No speculative firmware-sensitive mount syscalls. Never connects to LAN. */
#include "netemu_platform.h"
#include "ps3_api.h"
#include "app_health.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/net.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
static int mount_local(int enable,void *unused){
 (void)unused;int owned=netInitialize()==0;int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0){if(owned)netDeinitialize();return -1;}
 atomic_fetch_add(&app_sockets,1);
 struct timeval tv={5,0};(void)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));(void)setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
 struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(80);addr.sin_addr.s_addr=htonl(0x7f000001u);
 int rc=-1;if(connect(fd,(struct sockaddr*)&addr,sizeof(addr)))goto finish;
 char req[128],reply[256];int n=snprintf(req,sizeof(req),"GET /dev_blind?%d HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",enable);
 if(send(fd,req,(size_t)n,0)!=n)goto finish;
 n=recv(fd,reply,sizeof(reply)-1,0);if(n<12)goto finish;reply[n]=0;
 if(!strncmp(reply,"HTTP/1.0 200",12)||!strncmp(reply,"HTTP/1.1 200",12))rc=0;
finish:close(fd);atomic_fetch_sub(&app_sockets,1);if(owned)netDeinitialize();return rc;
}
const bb_netemu_paths bb_console_netemu_paths={"/dev_flash/ps2emu/ps2_netemu.self","/dev_blind/ps2emu/ps2_netemu.self","/dev_hdd0/buzzbridge/netemu_backup"};
const bb_netemu_ops bb_console_netemu_ops={buzz_is_hen,mount_local,NULL};
