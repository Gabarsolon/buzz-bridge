/* SPDX-License-Identifier: MIT. Descriptor fields mirror the pinned RPCS3 source.
 * HID report bytes reconstructed from AJFaraday's decoded physical capture;
 * hardware byte-for-byte verification is still required. See DESIGN.md. */
#include "usb_model.h"
#include <string.h>
const uint8_t buzz_device_descriptor[18]={
 18,1,0x00,0x02,0,0,0,8,0x4c,0x05,0x02,0x00,0xa1,0x05,2,1,0,1
};
const uint8_t buzz_config_descriptor[34]={
 9,2,34,0,1,1,0,0x80,0x32,
 9,4,0,0,1,3,0,0,0,
 9,0x21,0x11,0x01,0x33,1,0x22,78,0,
 7,5,0x81,3,8,0,10
};
const uint8_t buzz_hid_report_descriptor[78]={
 0x05,0x01,0x09,0x04,0xa1,0x01,0xa1,0x02,0x75,0x08,0x95,0x02,
 0x15,0x00,0x26,0xff,0x00,0x35,0x00,0x46,0xff,0x00,0x09,0x30,
 0x09,0x31,0x81,0x02,0x75,0x01,0x95,0x14,0x25,0x01,0x45,0x01,
 0x05,0x09,0x19,0x01,0x29,0x14,0x81,0x02,0x06,0x00,0xff,0x75,
 0x01,0x95,0x04,0x25,0x01,0x45,0x01,0x09,0x01,0x81,0x02,0xc0,
 0xa1,0x02,0x75,0x08,0x95,0x07,0x26,0xff,0x00,0x46,0xff,0x00,
 0x09,0x02,0x91,0x02,0xc0,0xc0
};
static int event_add(buzz_usb *u,unsigned type,unsigned arg2)
{
 unsigned pos; if(u->count==BUZZ_USB_EVENTS) return 0;
 pos=(u->head+u->count)%BUZZ_USB_EVENTS;
 u->events[pos].type=type; u->events[pos].arg2=arg2; u->events[pos].arg3=0; ++u->count; return 1;
}
void buzz_usb_init(buzz_usb *u,buzz_hub *h) { memset(u,0,sizeof(*u)); u->hub=h; u->next_id=1; }
void buzz_usb_reset(buzz_usb *u)
{
 buzz_hub *h=u->hub; unsigned i; uint32_t next=u->next_id;
 memset(u,0,sizeof(*u)); u->hub=h; u->next_id=next;
 for(i=0;i<BUZZ_PLAYERS;i++) h->slots[i].light=0;
 ++h->generation;
}
int buzz_usb_attach(buzz_usb *u)
{
 unsigned i; if(u->attached) return 1;
 if(!u->hub->config.enabled) return 0;
 if(u->count+u->hub->config.dongles>BUZZ_USB_EVENTS) return 0;
 for(i=0;i<u->hub->config.dongles;i++) event_add(u,1,i+1);
 u->attached=1; return 1;
}
static size_t bounded_copy(uint8_t *out,const uint8_t *src,size_t n,size_t cap)
{ if(n>cap) n=cap; memcpy(out,src,n); return n; }
static int control(buzz_usb *u,unsigned dongle,const buzz_setup *s,const uint8_t *out,
                   size_t cap,buzz_usb_transfer *t)
{
 size_t n=0; uint8_t val[64]; const uint8_t *src=val; unsigned i;
 if(!s || s->length>cap) return 0; cap=s->length;
 if((s->type==0x80 || s->type==0x81) && s->request==6) {
  switch(s->value>>8) {
  case 1: if((s->value&255) || s->index) return 0; src=buzz_device_descriptor; n=18; break;
  case 2: if((s->value&255) || s->index) return 0; src=buzz_config_descriptor; n=34; break;
  case 0x21: src=buzz_config_descriptor+18; n=9; break;
  case 0x22: src=buzz_hid_report_descriptor; n=78; break;
  case 3: {
   const char *str=(s->value&255)==1?"Logitech Buzz(tm) Controller V1":(s->value&255)==2?"Logitech":NULL;
   if(!(s->value&255)) { val[0]=4; val[1]=3; val[2]=9; val[3]=4; n=4; }
   else if(str) { n=2+2*strlen(str); val[0]=(uint8_t)n; val[1]=3;
    for(i=0;str[i];i++) { val[2+2*i]=(uint8_t)str[i]; val[3+2*i]=0; }
   } else return 0;
   break;
  }
  default: return 0;
  }
 } else if(s->type==0x00 && s->request==9 && s->value<=1 && !s->index && !s->length) {
  u->configured[dongle]=s->value; return 1;
 } else if(s->type==0x80 && s->request==8) { val[0]=(uint8_t)u->configured[dongle]; n=1;
 } else if(s->type==0x80 && s->request==0) { val[0]=val[1]=0; n=2;
 } else if(s->type==0x01 && s->request==11 && !s->value && !s->index && !s->length) {
  /* BCES00347 1.00 @ 0xbb4d8 / callback 0x54e440: SET_INTERFACE(0,0). */
  return 1;
 } else if(s->type==0x21 && s->request==9 && s->value==0x0200 && !s->index) {
  /* Seven-byte output report, report type 2 / ID 0. RPCS3 also tolerates >=5. */
  if(!buzz_lights(u->hub,dongle,out,cap)) return 0; t->count=(uint32_t)cap; return 1;
 } else if(s->type==0xa1 && s->request==1 && s->value==0x0100 && !s->index) {
  buzz_report(u->hub,dongle,val); n=5;
 } else if(s->type==0x21 && s->request==0x0a && !s->index && !s->length) return 1;
 else return 0;
 t->count=(uint32_t)bounded_copy(t->data,src,n,cap); return 1;
}
int buzz_usb_submit(buzz_usb *u,unsigned pipe,const buzz_setup *s,const uint8_t *out,size_t cap,uint64_t now)
{
 unsigned i,dongle; buzz_usb_transfer next;
 if(!u->attached || !pipe || pipe>u->hub->config.dongles*2 || cap>256) return -1;
 for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(!u->transfers[i].used) break;
 if(i==BUZZ_USB_TRANSFERS) return -2;
 memset(&next,0,sizeof(next)); dongle=(pipe-1)/2;
 if(pipe%2) {
  if(!control(u,dongle,s,out,cap,&next)) return -3;
  next.due_us=now+100;
 } else {
  if(s || cap<5) return -1;
  buzz_report(u->hub,dongle,next.data); next.count=5; next.due_us=now+6000;
 }
 /* IDs are bounded for signed return; NEVER interpret these as native LV2 IDs. */
 if(u->next_id>=0x7fffffff) return -2;
 next.used=1; next.id=u->next_id++; next.pipe=pipe; u->transfers[i]=next; return (int)next.id;
}
void buzz_usb_tick(buzz_usb *u,uint64_t now)
{
 unsigned i; for(i=0;i<BUZZ_USB_TRANSFERS;i++) {
  buzz_usb_transfer *t=&u->transfers[i];
  if(t->used && !t->done && now>=t->due_us && event_add(u,3,t->id)) t->done=1;
 }
}
int buzz_usb_pop(buzz_usb *u,buzz_usb_event *e)
{
 if(!u->count) return 0; *e=u->events[u->head]; u->head=(u->head+1)%BUZZ_USB_EVENTS; --u->count; return 1;
}
const buzz_usb_transfer *buzz_usb_status(const buzz_usb *u,uint32_t id)
{
 unsigned i; for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(u->transfers[i].used && u->transfers[i].id==id && u->transfers[i].done) return &u->transfers[i];
 return NULL;
}
void buzz_usb_reap(buzz_usb *u,uint32_t id)
{
 unsigned i; for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(u->transfers[i].id==id && u->transfers[i].done) u->transfers[i].used=0;
}
