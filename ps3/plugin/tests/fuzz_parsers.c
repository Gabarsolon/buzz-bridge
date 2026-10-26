/* SPDX-License-Identifier: MIT. Deterministic malformed-network/config stress test. */
#include "buzz.h"
#include "websocket.h"
#include "mailbox.h"
#include <stdio.h>
#include <string.h>
static uint32_t rng=0x928f0123;
static uint32_t next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
int main(void)
{
 unsigned i,j; char data[4097],accept[29]; buzz_ws_frame frame;
 const char *seeds[]={"{\"type\":\"join\",\"slot\":8}","{\"type\":\"down\",\"button\":\"red\"}",
  "port=8420\nmap.left.red=l1|l2\n", "GET /ws HTTP/1.1\r\nHost: local\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n"};
 for(i=0;i<200000;i++) {
  size_t len,used=0; buzz_config c; buzz_hub hub; buzz_message msg;
  buzz_defaults(&c); buzz_hub_init(&hub,&c);
  if(i%2) {
   const char *seed=seeds[next()%4]; len=strlen(seed); memcpy(data,seed,len);
   for(j=0;j<1+next()%8;j++) data[next()%len]=(char)next();
  } else { len=next()%4096; for(j=0;j<len;j++) data[j]=(char)next(); }
  data[len]=0;
  (void)buzz_message_parse(data,len,&msg);
  (void)buzz_config_parse(&c,data);
  (void)buzz_ws_decode((const uint8_t *)data,len,&frame);
  (void)buzz_http_upgrade(data,len,accept,&used);
  if(len>=32) { (void)buzz_mailbox_decode_reports(&hub,(const uint8_t *)data); (void)buzz_mailbox_apply_lights(&hub,(const uint8_t *)data); }
 }
 puts("PASS: 200,000 deterministic mutated/random parser inputs"); return 0;
}
