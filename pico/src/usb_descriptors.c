#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "bridge.h"
#include "captured_descriptors.h"
#include "ds3.h"

// Mode B retains the familiar X/Y + button layout, but only usages 1..12.
// Buttons 11 and 12 are always zero; four constant bits align to four bytes.
const uint8_t two_pad_report_descriptor[] = {
  0x05,0x01, 0x09,0x04, 0xa1,0x01,
  0x15,0x00, 0x26,0xff,0x00, 0x75,0x08, 0x95,0x02,
  0x09,0x30, 0x09,0x31, 0x81,0x02,
  0x05,0x09, 0x19,0x01, 0x29,0x0c, 0x15,0x00, 0x25,0x01,
  0x75,0x01, 0x95,0x0c, 0x81,0x02,
  0x75,0x01, 0x95,0x04, 0x81,0x03, 0xc0
};
#define A_CDC_LEN (34+TUD_CDC_DESC_LEN)
#define B_LEN (TUD_CONFIG_DESC_LEN+2*TUD_HID_DESC_LEN)
#define B_CDC_LEN (B_LEN+TUD_CDC_DESC_LEN)
static uint8_t device[18] __attribute__((aligned(4)));
static uint8_t a_debug[A_CDC_LEN];
static const uint8_t cdc_a[] = {
  TUD_CDC_DESCRIPTOR(1,4,0x83,8,0x04,0x84,64)
};
static const uint8_t b_clean[] = {
  TUD_CONFIG_DESCRIPTOR(1,2,0,B_LEN,0,100),
  TUD_HID_DESCRIPTOR(0,0,HID_ITF_PROTOCOL_NONE,sizeof(two_pad_report_descriptor),0x81,8,10),
  TUD_HID_DESCRIPTOR(1,0,HID_ITF_PROTOCOL_NONE,sizeof(two_pad_report_descriptor),0x82,8,10)
};
static const uint8_t b_debug[] = {
  TUD_CONFIG_DESCRIPTOR(1,4,0,B_CDC_LEN,0,100),
  TUD_HID_DESCRIPTOR(0,0,HID_ITF_PROTOCOL_NONE,sizeof(two_pad_report_descriptor),0x81,8,10),
  TUD_HID_DESCRIPTOR(1,0,HID_ITF_PROTOCOL_NONE,sizeof(two_pad_report_descriptor),0x82,8,10),
  TUD_CDC_DESCRIPTOR(2,4,0x83,8,0x04,0x84,64)
};
static const uint8_t c_clean[] = {
  TUD_CONFIG_DESCRIPTOR(1,1,0,41,0,500),
  TUD_HID_INOUT_DESCRIPTOR(0,0,HID_ITF_PROTOCOL_NONE,sizeof(ds3_report_descriptor),0x02,0x81,64,1)
};
// Windows rejects undeclared feature IDs before issuing any USB request. The
// diagnostic identity adds these declarations; clean C retains the genuine 148.
#define C_DEBUG_REPORT_LEN (148+5*13)
static uint8_t c_debug_report[C_DEBUG_REPORT_LEN];
static const uint8_t c_debug[] = {
  TUD_CONFIG_DESCRIPTOR(1,3,0,41+TUD_CDC_DESC_LEN,0,500),
  TUD_HID_INOUT_DESCRIPTOR(0,0,HID_ITF_PROTOCOL_NONE,C_DEBUG_REPORT_LEN,0x02,0x81,64,1),
  TUD_CDC_DESCRIPTOR(1,4,0x83,8,0x04,0x84,64)
};
static char serial[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1];
void bridge_descriptors_init(void) {
  memcpy(c_debug_report,ds3_report_descriptor,147);
  const uint8_t ids[]={0xf2,0xf4,0xf5,0xf7,0xf8};
  for(unsigned i=0;i<sizeof(ids);i++) {
    const uint8_t feature[]={0xa1,0x02,0x85,ids[i],0x75,0x08,0x95,0x3f,0x09,0x01,0xb1,0x02,0xc0};
    memcpy(c_debug_report+147+i*sizeof(feature),feature,sizeof(feature));
  }
  c_debug_report[C_DEBUG_REPORT_LEN-1]=0xc0;
  memcpy(device,captured_device,sizeof(device));
  memcpy(a_debug,captured_configuration,sizeof(captured_configuration));
  a_debug[2]=A_CDC_LEN;a_debug[3]=A_CDC_LEN>>8;a_debug[4]=3;
  memcpy(a_debug+34,cdc_a,sizeof(cdc_a));
  pico_get_unique_board_id_string(serial,sizeof(serial));
  if(bridge_mode==MODE_TWO_PADS) {
    device[2]=0x10;device[3]=0x01; // USB 1.10, two HID interfaces
    // 054c:0002 is also hard-coded as a Sony hub in Windows usb.inf. Retain
    // the other proven netemu Buzz-map identity, with a unique serial so this
    // composite configuration has a different Windows instance from the clone.
    device[10]=0x00;device[11]=0x10;
    device[12]=0;device[13]=1;device[16]=3;
  }
  if(bridge_mode==MODE_DS3) {
    device[2]=0;device[3]=2;device[7]=64;
    device[10]=0x68;device[11]=2;device[12]=0;device[13]=1;
  }
  if(bridge_debug) {
    device[2]=0;device[3]=2;
    device[4]=TUSB_CLASS_MISC;device[5]=MISC_SUBCLASS_COMMON;device[6]=MISC_PROTOCOL_IAD;
    device[8]=0xfe;device[9]=0xca;
    device[10]=(uint8_t)(1+bridge_mode);device[11]=0xb0;
    device[16]=3;
  }
}
const uint8_t *tud_descriptor_device_cb(void) {return device;}
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  if(index)return NULL;
  if(bridge_mode==MODE_CLONE)return bridge_debug?a_debug:captured_configuration;
  if(bridge_mode==MODE_DS3)return bridge_debug?c_debug:c_clean;
  return bridge_debug?b_debug:b_clean;
}
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  if(bridge_mode==MODE_CLONE)return instance?NULL:captured_hid_report;
  if(bridge_mode==MODE_DS3)return instance?NULL:(bridge_debug?c_debug_report:ds3_report_descriptor);
  return instance<2?two_pad_report_descriptor:NULL;
}
const uint16_t *tud_descriptor_string_cb(uint8_t index,uint16_t langid) {
  static uint16_t str[64];
  if(index==0)return (const uint16_t *)captured_string_0;
  if(langid!=0x0409)return NULL;
  if(bridge_mode==MODE_CLONE&&!bridge_debug) {
    if(index==1)return (const uint16_t *)captured_string_1;
    if(index==2)return (const uint16_t *)captured_string_2;
    return NULL;
  }
  const char *s=NULL;
  if(index==1)s=(bridge_mode==MODE_DS3&&!bridge_debug)?"Sony":"Buzz bridge";
  if(index==2)s=bridge_mode==MODE_DS3?(bridge_debug?"Buzz bridge C debug":"PLAYSTATION(R)3 Controller"):
    (bridge_mode==MODE_CLONE?"Buzz bridge A debug":"Buzz bridge two pads");
  if(index==3&&(bridge_debug||bridge_mode==MODE_TWO_PADS))s=serial;
  if(index==4&&bridge_debug)s="Buzz bridge debug";
  if(!s)return NULL;
  unsigned n=strlen(s);if(n>63)n=63;
  str[0]=(uint16_t)((TUSB_DESC_STRING<<8)|(2*n+2));
  for(unsigned i=0;i<n;i++)str[i+1]=(uint8_t)s[i];
  return str;
}
