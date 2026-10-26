/* SPDX-License-Identifier: MIT */
#include "buzz.h"
#include "usb_model.h"
#include "websocket.h"
#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 0; } } while(0)
static int report_tests(void)
{
 uint32_t v; uint8_t b[4],out[5],reference[5]; unsigned i,k;
 /* Independent per-button transcription of RPCS3 Buzz.cpp's switch expression.
    Compare all 2^20 simultaneous button combinations, not merely happy paths. */
 for(v=0;v<(1u<<20);v++) {
  reference[0]=reference[1]=0x7f; reference[2]=reference[3]=0; reference[4]=0xf0;
  for(i=0;i<4;i++) {
   b[i]=(uint8_t)((v>>(i*5))&31);
   for(k=0;k<5;k++) if(b[i]&(1u<<k)) reference[2+(k+5*i)/8]|=(uint8_t)(1u<<((k+5*i)%8));
  }
  buzz_encode(b,out); CHECK(!memcmp(out,reference,5));
 }
 memset(b,31,4); buzz_encode(b,out); CHECK(!memcmp(out,"\x7f\x7f\xff\xff\xff",5));
 puts("report: all 1,048,576 states agree with RPCS3 expression"); return 1;
}
static int hub_tests(void)
{
 buzz_hub h; buzz_config c; char reply[512]; uint8_t report[5],leds[]={0,255,0,0,255,0,0}; unsigned i;
 buzz_defaults(&c); CHECK(!c.enabled); buzz_hub_init(&h,&c);
 buzz_pad_update(&h,3,1,1,BP_R1|BP_CROSS);
 CHECK(h.slots[0].kind==BUZZ_PAD && h.slots[0].id==6 && h.slots[0].buttons==3);
 CHECK(buzz_claim(&h,BUZZ_PHONE,45,1)<0);
 CHECK(buzz_claim(&h,BUZZ_PHONE,45,0)==2);
 CHECK(buzz_claim(&h,BUZZ_PHONE,45,3)<0); /* one client cannot leak claims */
 CHECK(buzz_claim(&h,BUZZ_PHONE,45,2)==2);
 CHECK(buzz_press(&h,BUZZ_PHONE,45,BUZZ_BLUE,1));
 buzz_report(&h,0,report); CHECK(report[2]==3 && report[3]==2);
 buzz_pad_update(&h,3,1,0,0); CHECK(h.slots[0].buttons==3); /* len=0 means no change */
 CHECK(buzz_lights(&h,0,leds,7)); CHECK(buzz_pad_light(&h,3)==1);
 CHECK(!buzz_lights(&h,0,leds,4));
 buzz_release(&h,BUZZ_PHONE,45); CHECK(!h.slots[1].kind && !h.slots[1].buttons);
 buzz_pad_update(&h,3,0,0,0); CHECK(!h.slots[0].kind && !h.slots[0].buttons);
 for(i=0;i<8;i++) CHECK(buzz_claim(&h,BUZZ_PHONE,100+i,0)==(int)i+1);
 CHECK(buzz_claim(&h,BUZZ_PHONE,200,0)<0);
 CHECK(buzz_press(&h,BUZZ_PHONE,107,BUZZ_BLUE,1)); buzz_report(&h,1,report); CHECK(report[4]==0xf8);
 CHECK(buzz_status_json(&h,reply,sizeof(reply))>0 && strstr(reply,"\"8\":true"));
 CHECK(buzz_status_json(&h,reply,3)<0);
 c.players_per_pad=2; buzz_hub_init(&h,&c);
 buzz_pad_update(&h,0,1,1,BP_L1|BP_UP|BP_CROSS);
 CHECK(h.slots[0].buttons==2 && h.slots[1].buttons==17);
 CHECK(h.slots[0].id==0 && h.slots[1].id==1);
 buzz_pad_update(&h,0,0,0,0); CHECK(!h.slots[0].kind && !h.slots[1].kind);
 c.dongles=1; buzz_hub_init(&h,&c); CHECK(buzz_claim(&h,BUZZ_PHONE,9,5)<0);
 c.dongles=2; c.pad_slot[0][0]=8; c.pad_slot[0][1]=255; buzz_hub_init(&h,&c);
 CHECK(buzz_claim(&h,BUZZ_PHONE,45,8)==8);
 buzz_pad_update(&h,0,1,1,BP_R1); CHECK(h.slots[7].kind==BUZZ_PHONE);
 buzz_release(&h,BUZZ_PHONE,45); buzz_pad_update(&h,0,1,1,BP_R1);
 CHECK(h.slots[7].kind==BUZZ_PAD && h.slots[7].buttons==1 && !h.slots[0].kind);
 puts("hub: mixed ownership, exhaustion, disconnect, stable order, split pad, player 8, lights pass"); return 1;
}
static int config_json_tests(void)
{
 buzz_config c,copy; buzz_message msg; buzz_hub h; char reply[128]; const char *s;
 buzz_defaults(&c); copy=c;
 CHECK(buzz_config_parse(&c,"port=9000\nplayers_per_pad=2\nmap.right.red=cross|r1\n")==0);
 CHECK(c.port==9000 && c.players_per_pad==2 && c.map[0][0]==(BP_CROSS|BP_R1));
 copy=c; CHECK(buzz_config_parse(&c,"port=1234\ndongles=3\n")==2); CHECK(!memcmp(&copy,&c,sizeof(c)));
 CHECK(buzz_config_parse(&c,"port=-1")==1); CHECK(buzz_config_parse(&c,"port=65536")==1);
 CHECK(buzz_config_parse(&c,"map.left.red=x")==1); CHECK(buzz_config_parse(&c,"enabled=1 # yes\r\n")==0);
 CHECK(buzz_config_parse(&c,"pad.1.right=8\npad.1.left=off\n")==0 && c.pad_slot[0][0]==8 && c.pad_slot[0][1]==255);
 CHECK(buzz_config_parse(&c,"pad.0.right=1")==1 && buzz_config_parse(&c,"pad.1.right=9")==1);
 CHECK(buzz_config_parse(&c,"dongles=1")==1); c.pad_slot[0][0]=0;
 s="{\"type\":\"join\",\"slot\":8}";
 CHECK(buzz_message_parse(s,strlen(s),&msg) && msg.slot==8 &&
       !buzz_message_parse("\v{\"type\":\"join\"}",16,&msg));
 s="{\"button\":\"red\",\"type\":\"down\"}"; CHECK(buzz_message_parse(s,strlen(s),&msg) && msg.type==BUZZ_DOWN);
 { const char *bad[]={"{}","{\"type\":\"join\",}","{\"type\":\"join\",\"slot\":1,\"slot\":2}",
  "{\"type\":\"join\",\"slot\":-1}","{\"type\":\"join\",\"slot\":01}","{\"type\":\"join\",\"slot\":9}",
  "{\"type\":\"join\"}x","{\"type\":\"down\",\"button\":\"cross\"}","{\"type\":\"join\",\"a\":{}}"};
  unsigned i; for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++) CHECK(!buzz_message_parse(bad[i],strlen(bad[i]),&msg)); }
 buzz_hub_init(&h,&c); s="{\"type\":\"down\",\"button\":\"red\"}";
 CHECK(buzz_phone_message(&h,1,s,strlen(s),reply,sizeof(reply))<0);
 s="{\"type\":\"join\",\"slot\":0}"; CHECK(buzz_phone_message(&h,1,s,strlen(s),reply,sizeof(reply))>0);
 CHECK(strstr(reply,"\"joined\"") && strstr(reply,"\"slot\":1"));
 puts("config/JSON: strict parsing, transactional failures and protocol compatibility pass"); return 1;
}
static int websocket_tests(void)
{
 char accept[29]; uint8_t frame[600],plain[600]; buzz_ws_frame decoded; size_t n,used=0,i;
 const char *request="GET /ws HTTP/1.1\r\nHost: localhost:8420\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://localhost:8420\r\nSec-WebSocket-Extensions: permessage-deflate\r\n\r\n";
 CHECK(buzz_ws_accept("dGhlIHNhbXBsZSBub25jZQ==",accept)); CHECK(!strcmp(accept,"s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
 CHECK(!buzz_ws_accept("invalid",accept));
 for(i=0;i<strlen(request);i++) CHECK(buzz_http_upgrade(request,i,accept,&used)==0);
 CHECK(buzz_http_upgrade(request,strlen(request),accept,&used)==1 && used==strlen(request));
 CHECK(buzz_http_upgrade("GET /ws HTTP/1.1\r\n\r\n",20,accept,&used)<0);
 frame[0]=0x81; frame[1]=0x82; frame[2]=1; frame[3]=2; frame[4]=3; frame[5]=4; frame[6]='h'^1; frame[7]='i'^2;
 for(i=0;i<8;i++) CHECK(buzz_ws_decode(frame,i,&decoded)==0);
 CHECK(buzz_ws_decode(frame,8,&decoded)==8 && !strcmp((char *)decoded.data,"hi"));
 frame[1]=2; CHECK(buzz_ws_decode(frame,8,&decoded)<0); frame[1]=0x82;
 frame[0]=1; CHECK(buzz_ws_decode(frame,8,&decoded)<0); frame[0]=0x81;
 memset(plain,'a',sizeof(plain)); n=buzz_ws_encode(1,plain,512,frame,sizeof(frame)); CHECK(n==516 && frame[1]==126);
 CHECK(!buzz_ws_encode(9,plain,126,frame,sizeof(frame)));
 CHECK(!buzz_ws_encode(1,plain,513,frame,sizeof(frame)));
 puts("WebSocket: RFC6455 accept vector, split input, lengths, mask/control bounds pass"); return 1;
}
static int usb_tests(void)
{
 buzz_config c; buzz_hub h; buzz_usb u; buzz_usb_event event; const buzz_usb_transfer *t; int id,i;
 buzz_setup setup={0x80,6,0x0100,0,18}; uint8_t led[]={0,255,0,255,0,0,0};
 buzz_defaults(&c); buzz_hub_init(&h,&c); buzz_usb_init(&u,&h); CHECK(!buzz_usb_attach(&u));
 h.config.enabled=1; CHECK(buzz_usb_attach(&u)); CHECK(buzz_usb_attach(&u));
 CHECK(buzz_usb_pop(&u,&event) && event.type==1 && event.arg2==1);
 CHECK(buzz_usb_pop(&u,&event) && event.type==1 && event.arg2==2); CHECK(!buzz_usb_pop(&u,&event));
 CHECK(sizeof(buzz_config_descriptor)==34 && sizeof(buzz_hid_report_descriptor)==78);
 id=buzz_usb_submit(&u,1,&setup,NULL,18,1000); CHECK(id>0);
 buzz_usb_tick(&u,1099); CHECK(!buzz_usb_status(&u,(uint32_t)id));
 buzz_usb_tick(&u,1100); t=buzz_usb_status(&u,(uint32_t)id); CHECK(t && t->count==18 && t->data[8]==0x4c && t->data[10]==2);
 CHECK(buzz_usb_pop(&u,&event) && event.type==3 && event.arg2==(unsigned)id);
 buzz_usb_reap(&u,(uint32_t)id); CHECK(!buzz_usb_status(&u,(uint32_t)id));
 id=buzz_usb_submit(&u,4,NULL,NULL,8,2000); CHECK(id>0); buzz_usb_tick(&u,7999); CHECK(!buzz_usb_pop(&u,&event));
 buzz_usb_tick(&u,8000); t=buzz_usb_status(&u,(uint32_t)id); CHECK(t && t->count==5 && t->data[4]==0xf0);
 buzz_usb_reap(&u,(uint32_t)id); CHECK(buzz_usb_pop(&u,&event));
 setup.type=0x21; setup.request=9; setup.value=0x0200; setup.length=7;
 CHECK(buzz_usb_submit(&u,1,&setup,led,7,0)>0); CHECK(h.slots[0].light && !h.slots[1].light && h.slots[2].light);
 setup.value=0x0300; CHECK(buzz_usb_submit(&u,1,&setup,led,7,0)<0);
 buzz_usb_reset(&u); CHECK(!u.attached && !h.slots[0].light && !u.count);
 CHECK(buzz_usb_attach(&u)); for(i=0;i<BUZZ_USB_TRANSFERS;i++) CHECK(buzz_usb_submit(&u,2,NULL,NULL,8,0)>0);
 CHECK(buzz_usb_submit(&u,2,NULL,NULL,8,0)==-2);
 buzz_usb_tick(&u,6000); CHECK(u.count==BUZZ_USB_TRANSFERS+2);
 buzz_usb_reset(&u); CHECK(!buzz_usb_pop(&u,&event));
 puts("USB model: descriptors, async completion, 6ms/100us deadlines, LEDs, limits, reset pass"); return 1;
}
static int mailbox_tests(void)
{
 buzz_config c; buzz_hub h; buzz_kernel_engine engine; uint8_t req[32],reply[32],before[32];
 buzz_defaults(&c); c.enabled=1; buzz_hub_init(&h,&c); buzz_kernel_engine_init(&engine);
 CHECK(buzz_claim(&h,BUZZ_PHONE,1,8)==8); CHECK(buzz_press(&h,BUZZ_PHONE,1,BUZZ_BLUE,1));
 buzz_mailbox_request(&h,req); CHECK(buzz_kernel_exchange(&engine,req,reply,1000));
 CHECK(engine.state.slots[7].buttons==16); CHECK(!buzz_mailbox_apply_lights(&h,reply));
 engine.ready=1; engine.state.slots[7].light=1;
 CHECK(buzz_kernel_exchange(&engine,req,reply,2000)); CHECK(buzz_mailbox_apply_lights(&h,reply)); CHECK(h.slots[7].light);
 memcpy(before,req,32); req[31]=1; CHECK(!buzz_kernel_exchange(&engine,req,reply,2001));
 CHECK(engine.heartbeat_us==2000 && engine.state.slots[7].buttons==16); memcpy(req,before,32);
 buzz_kernel_watchdog(&engine,502000); CHECK(engine.state.slots[7].buttons==16);
 buzz_kernel_watchdog(&engine,502001); CHECK(!engine.state.slots[7].buttons);
 CHECK(buzz_kernel_exchange(&engine,req,reply,503000)); CHECK(engine.state.slots[7].buttons==16);
 reply[8]=2; CHECK(!buzz_mailbox_apply_lights(&h,reply));
 puts("mailbox/engine: byte ABI, validation, unsupported backend, LEDs and watchdog pass"); return 1;
}
int main(void)
{
 if(!report_tests() || !hub_tests() || !config_json_tests() || !websocket_tests() || !usb_tests() || !mailbox_tests()) return 1;
 printf("PASS: %u checks\n",checks); return 0;
}
