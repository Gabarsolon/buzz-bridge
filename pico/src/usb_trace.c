#include "pico/stdlib.h"
#include "bridge.h"
#include "ds3.h"
#include "usb_trace.h"
static uint32_t now_ms(void){return to_ms_since_boot(get_absolute_time());}
void modec_usb_setup(const tusb_control_request_t *r){if(bridge_mode==MODE_DS3)ds3_trace_setup((const uint8_t *)r,now_ms());}
void modec_usb_data(const uint8_t *p,uint16_t n,uint16_t offset){if(bridge_mode==MODE_DS3)ds3_trace_data(p,n,offset,now_ms());}
void modec_usb_flags(uint8_t flags){if(bridge_mode==MODE_DS3)ds3_trace_flags(flags,now_ms());}
void modec_usb_reset(void){if(bridge_mode==MODE_DS3){ds3_trace_reset_bus(now_ms());bridge_ds3_bus_reset();}}
bool modec_hid_feature_request(const tusb_control_request_t *r) {
  return bridge_mode==MODE_DS3&&r->wIndex==0&&(r->bmRequestType==0xa1||r->bmRequestType==0x21)&&
    (r->wValue>>8)==HID_REPORT_TYPE_FEATURE&&(r->bRequest==HID_REQ_CONTROL_GET_REPORT||r->bRequest==HID_REQ_CONTROL_SET_REPORT);
}
bool modec_hid_feature_control(uint8_t rhport,uint8_t stage,const tusb_control_request_t *r) {
  static uint8_t wire[64];
  uint8_t id=(uint8_t)r->wValue;
  if(r->bRequest==HID_REQ_CONTROL_GET_REPORT) {
    if(r->bmRequestType!=0xa1)return false;
    if(stage==CONTROL_STAGE_SETUP) {
      uint16_t n=ds3_feature_get(id,wire,tu_min16(r->wLength,sizeof(wire)));
      if(!n)return false;
      return tud_control_xfer(rhport,r,wire,n);
    }
  } else {
    if(r->bmRequestType!=0x21||r->wLength>sizeof(wire))return false;
    if(stage==CONTROL_STAGE_SETUP)return tud_control_xfer(rhport,r,wire,r->wLength);
    if(stage==CONTROL_STAGE_ACK)tud_hid_set_report_cb(0,id,HID_REPORT_TYPE_FEATURE,wire,r->wLength);
  }
  return true;
}
