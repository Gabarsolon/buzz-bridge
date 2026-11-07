// USB wire-format reference: GP2040-CE PS3Descriptors.h/PS3Driver.cpp (MIT),
// Copyright 2021 Jason Skuby, 2024 OpenStickCommunity. See THIRD_PARTY_LAMPS.md.
// Input encoding and the PREPARE/COMMIT lamp protocol are original to this bridge.
#include <string.h>
#include "ds3.h"
const uint8_t ds3_report_descriptor[148]={
  0x05,0x01,0x09,0x04,0xa1,0x01,0xa1,0x02,0x85,0x01,0x75,0x08,0x95,0x01,
  0x15,0x00,0x26,0xff,0x00,0x81,0x03,0x75,0x01,0x95,0x13,0x15,0x00,0x25,
  0x01,0x35,0x00,0x45,0x01,0x05,0x09,0x19,0x01,0x29,0x13,0x81,0x02,0x75,
  0x01,0x95,0x0d,0x06,0x00,0xff,0x81,0x03,0x15,0x00,0x26,0xff,0x00,0x05,
  0x01,0x09,0x01,0xa1,0x00,0x75,0x08,0x95,0x04,0x35,0x00,0x46,0xff,0x00,
  0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,0x81,0x02,0xc0,0x05,0x01,0x75,
  0x08,0x95,0x27,0x09,0x01,0x81,0x02,0x75,0x08,0x95,0x30,0x09,0x01,0x91,
  0x02,0x75,0x08,0x95,0x30,0x09,0x01,0xb1,0x02,0xc0,0xa1,0x02,0x85,0x02,
  0x75,0x08,0x95,0x30,0x09,0x01,0xb1,0x02,0xc0,0xa1,0x02,0x85,0xee,0x75,
  0x08,0x95,0x30,0x09,0x01,0xb1,0x02,0xc0,0xa1,0x02,0x85,0xef,0x75,0x08,
  0x95,0x30,0x09,0x01,0xb1,0x02,0xc0,0xc0
};
static uint8_t host_mac[6],device_mac[6]={0x02,0x50,0x49,0x43,0x4f,0x32},ef_byte=0xa0;
static bool streaming,automatic_ps,configured,auto_done,ps_pending,red_tracking,red_latched;
static uint32_t auto_at,ps_until,red_since,ps_count,enable_count,disable_count;
static bool wake_anchor_needed,wake_pending,disabled_seen;
static uint8_t wake_used;
static uint32_t wake_at;
static const uint32_t wake_delay[3]={5000,10000,20000};
static uint8_t prepare_mask;
static bool prepared;
static uint32_t prepare_at;
void ds3_output_reset(void){prepared=false;}
void ds3_identity(const uint8_t address[6]){memcpy(device_mac,address,6);}
void ds3_session_reset(uint32_t now,bool auto_ps) {
  (void)now;streaming=false;automatic_ps=auto_ps;configured=false;auto_done=false;
  ps_pending=false;red_tracking=false;red_latched=false;ps_until=0;
  ps_count=enable_count=disable_count=0;prepared=false;
  wake_anchor_needed=wake_pending=disabled_seen=false;wake_used=0;wake_at=0;
}
void ds3_session_configured(uint32_t now){configured=true;auto_at=now+2000;auto_done=false;streaming=false;ps_pending=false;wake_anchor_needed=wake_pending=disabled_seen=false;wake_used=0;}
void ds3_enable_anchor(uint32_t now){if(!auto_done&&automatic_ps)auto_at=now+2000;}
void ds3_disable_anchor(uint32_t now) {
  if(!wake_anchor_needed)return;
  wake_anchor_needed=false;
  wake_pending=configured&&automatic_ps&&wake_used<3;
  if(wake_pending)wake_at=now+wake_delay[wake_used];
}
uint8_t ds3_wake_used(void){return wake_used;}
bool ds3_wake_pending(void){return wake_pending||wake_anchor_needed;}
static void ps_pulse(uint32_t now){ps_pending=true;ps_until=now+250;ps_count++;auto_done=true;}
void ds3_request_ps(uint32_t now) {
  // A deliberate manual PS request rearms the bounded wake budget. A host
  // repeatedly disabling the pad cannot refill it through F4 enable/disable.
  wake_used=0;wake_pending=false;wake_anchor_needed=false;ps_pulse(now);
  if(disabled_seen&&!streaming){wake_anchor_needed=true;ds3_disable_anchor(now);}
}
void ds3_activation_tick(uint32_t bits,uint32_t now) {
  if(wake_anchor_needed)ds3_disable_anchor(now);
  bool manual=false;
  if((bits&0x8421u)==0x8421u) {
    if(!red_tracking){red_tracking=true;red_since=now;}
    if(!red_latched&&now-red_since>=2000){red_latched=true;ds3_request_ps(now);manual=true;}
  } else {red_tracking=false;red_latched=false;}
  if(manual)return;
  if(configured&&automatic_ps&&!auto_done&&(int32_t)(now-auto_at)>=0)ps_pulse(now);
  if(wake_pending&&!streaming&&(int32_t)(now-wake_at)>=0) {
    ps_pulse(now);wake_used++;
    wake_pending=wake_used<3;
    if(wake_pending)wake_at=now+wake_delay[wake_used];
  }
}
bool ds3_streaming(void){return streaming;}
bool ds3_send_allowed(uint32_t now){(void)now;return streaming||ps_pending;}
bool ds3_ps_value(uint32_t now){return ps_pending||(streaming&&ps_count&&(int32_t)(now-ps_until)<0);}
void ds3_input_acked(bool ps){if(ps)ps_pending=false;}
void ds3_linux_usb_poke(void){streaming=true;wake_pending=wake_anchor_needed=false;}
uint32_t ds3_ps_count(void){return ps_count;}
uint32_t ds3_enable_count(void){return enable_count;}
uint32_t ds3_disable_count(void){return disable_count;}
void ds3_input(uint32_t bits,uint8_t p[48]) {
  memset(p,0,48);
  // p[] excludes report ID. Wire digital bits are report bytes 2 and 3.
  p[1]=(uint8_t)bits;p[2]=(uint8_t)(bits>>8);
  for(unsigned i=0;i<4;i++)p[5+i]=(bits&(1u<<(16+i)))?0xe0:0x80;
  for(unsigned i=0;i<12;i++)p[13+i]=(bits&(1u<<(4+i)))?255:0;
  p[28]=2;p[29]=5;p[30]=0x10;
  for(unsigned i=40;i<48;i+=2){p[i]=2;p[i+1]=0;}
}
uint16_t ds3_feature_get(uint8_t id,uint8_t *out,uint16_t cap) {
  // Wire layouts: GP2040-CE MIT templates plus published genuine DS3 USB
  // captures (DsHidMini PS3_USB_STARTUP.md / MOTION.md). No generic ID prefix.
  uint8_t b[64]={0};uint16_t n=64;
  switch(id) {
    case 0x01: {
      const uint8_t prefix[]={1,4,0,0x0b,0x0c,1,2,0x18,0x18,0x18,0x18,9,0x0a,0x10,0x11,0x12,
        0x13,0,0,0,0,4,0,2,2,2,2,0,0,0,4,4,4,4,0,0,4,0,1,2,7,0,0x17};
      memcpy(b+1,prefix,sizeof(prefix));break;
    }
    case 0xf2: {
      // Linux requests 17; PS3 requests 64. Tail is firmware information,
      // not the paired host MAC. The caller's wLength truncates naturally.
      const uint8_t firmware[]={0,3,0x55,0x43,0xc3,1,0x8a,0};
      b[0]=0xf2;b[1]=255;b[2]=255;memcpy(b+4,device_mac,6);
      memcpy(b+10,firmware,sizeof(firmware));break;
    }
    case 0xf5: b[0]=1;memcpy(b+2,host_mac,6);break;
    case 0xef:
      b[1]=0xef;b[2]=4;b[4]=0x0b;b[5]=3;b[6]=1;b[7]=ef_byte;
      if(ef_byte==0xa0) {
        // Coherent synthetic calibration: zero=512, one-G=399; static gyro.
        for(unsigned i=17;i<33;i+=4){b[i]=2;b[i+1]=0;b[i+2]=1;b[i+3]=0x8f;}
        b[31]=0;b[32]=0x7f; // Gyro calibration selector, not an accelerometer span.
      } else if(ef_byte==0xb0){b[17]=2;b[18]=0x6c;b[19]=2;b[20]=0x6e;}
      b[48]=6;break;
    case 0xf7: {
      const uint8_t prefix[]={1,0,8,2,0xdc,1,0xee,255,0,0,0};
      memcpy(b,prefix,sizeof(prefix));break;
    }
    case 0xf8:
      ds3_feature_get(0xef,b,sizeof(b));b[0]=0;b[1]=1;b[2]=0;b[3]=0;break;
    default:return 0;
  }
  if(n>cap)n=cap;
  memcpy(out,b,n);return n;
}
bool ds3_feature_set(uint8_t id,const uint8_t *p,uint16_t n) {
  // Windows HID APIs may prepend the selected ID. PS3 EP0 payloads do not.
  if(n>1&&p[0]==id){p++;n--;}
  if(id==0xf5&&n>=8){memcpy(host_mac,p+2,6);return true;}
  if(id==0xef&&n>=7&&p[4]==3&&p[5]==1){ef_byte=p[6];return true;}
  if(id==0xf4&&n>=4&&p[0]==0x42&&p[2]==0&&p[3]==0) {
    if(p[1]==0x0c){streaming=true;wake_pending=wake_anchor_needed=false;enable_count++;return true;}
    if(p[1]==0x0b){
      streaming=false;ps_pending=false;auto_done=true;disabled_seen=true;disable_count++;
      if(!wake_pending&&!wake_anchor_needed&&wake_used<3)wake_anchor_needed=true;
      return true;
    }
  }
  return false;
}
bool ds3_output(uint8_t id,const uint8_t *p,uint16_t n,uint32_t now,
                uint8_t *small,uint8_t *large,uint8_t *mask,bool *commit) {
  *commit=false;
  if(id==0) {if(!n||p[0]!=1)return false;id=p[0];p++;n--;}
  if(id!=1)return false;
  uint8_t restored[35];
  // Linux SKIP_OUTPUT_REPORT_ID + TinyUSB removal leaves 34 bytes; restore the
  // padding byte exactly as the referenced GP2040-CE implementation does.
  if(n==34){restored[0]=1;memcpy(restored+1,p,34);p=restored;n=35;}
  if(n!=35&&n!=48)return false;
  *small=p[2];*large=p[4];
  if(*small>1){prepared=false;return true;}
  unsigned level=(*large+8u)/17u;
  unsigned nominal=level*17u;
  unsigned error=nominal>*large?nominal-*large:*large-nominal;
  if(level>15||error>2){prepared=false;return true;}
  if(*small==0){prepare_mask=(uint8_t)level;prepare_at=now;prepared=true;return true;}
  if(prepared&&prepare_mask==level&&(uint32_t)(now-prepare_at)<=500) {
    *mask=(uint8_t)level;*commit=true;
  }
  prepared=false;return true;
}
