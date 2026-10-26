/* SPDX-License-Identifier: MIT */
#include "mailbox.h"
#include <string.h>
void buzz_mailbox_request(const buzz_hub *h,uint8_t out[32])
{
 memset(out,0,32); memcpy(out,"BZV1",4); out[4]=1; out[5]=(uint8_t)h->config.enabled;
 out[6]=(uint8_t)h->config.dongles; buzz_report(h,0,out+8); buzz_report(h,1,out+13);
}
int buzz_mailbox_decode_reports(buzz_hub *h,const uint8_t in[32])
{
 unsigned i,d; uint8_t buttons[8]={0};
 if(memcmp(in,"BZV1",4) || in[4]!=1 || in[5]>1 || in[6]<1 || in[6]>2 || in[7]) return 0;
 for(i=18;i<32;i++) if(in[i]) return 0;
 for(d=0;d<2;d++) {
  const uint8_t *r=in+8+d*5; uint32_t bits;
  if(r[0]!=127 || r[1]!=127 || (r[4]&0xf0)!=0xf0) return 0;
  bits=r[2]|((uint32_t)r[3]<<8)|((uint32_t)(r[4]&15)<<16);
  for(i=0;i<4;i++) buttons[d*4+i]=(uint8_t)((bits>>(5*i))&31);
 }
 h->config.enabled=in[5]; h->config.dongles=in[6];
 for(i=0;i<8;i++) h->slots[i].buttons=(uint8_t)(in[5] && i<in[6]*4 ? buttons[i] : 0);
 return 1;
}
void buzz_mailbox_reply(const buzz_hub *h,int ready,uint32_t gen,uint8_t out[32])
{
 unsigned i; memset(out,0,32); memcpy(out,"BZV1",4); out[4]=1; out[5]=(uint8_t)(ready!=0); out[6]=(uint8_t)h->config.dongles;
 for(i=0;i<8;i++) out[8+i]=h->slots[i].light;
 for(i=0;i<4;i++) out[16+i]=(uint8_t)(gen>>(24-8*i));
}
int buzz_mailbox_apply_lights(buzz_hub *h,const uint8_t in[32])
{
 unsigned i,changed=0;
 if(memcmp(in,"BZV1",4) || in[4]!=1 || in[5]!=1 || in[6]!=h->config.dongles || in[7]) return 0;
 for(i=8;i<16;i++) if(in[i]>1) return 0;
 for(i=20;i<32;i++) if(in[i]) return 0;
 for(i=0;i<8;i++) { changed|=h->slots[i].light!=in[8+i]; h->slots[i].light=in[8+i]; }
 if(changed) ++h->generation; return 1;
}
