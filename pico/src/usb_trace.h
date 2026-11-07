#pragma once
#include "tusb.h"
#include "ds3_trace.h"
// Called only from TinyUSB task context, never DCD IRQ context.
void modec_usb_setup(const tusb_control_request_t *request);
void modec_usb_data(const uint8_t *data,uint16_t len,uint16_t offset);
void modec_usb_flags(uint8_t flags);
void modec_usb_reset(void);
bool modec_hid_feature_request(const tusb_control_request_t *request);
bool modec_hid_feature_control(uint8_t rhport,uint8_t stage,const tusb_control_request_t *request);
void bridge_ds3_bus_reset(void);
