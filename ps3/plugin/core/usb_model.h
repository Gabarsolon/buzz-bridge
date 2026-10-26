/* SPDX-License-Identifier: MIT. A USB contract model, NOT an LV2 ABI implementation. */
#ifndef BUZZ_USB_MODEL_H
#define BUZZ_USB_MODEL_H
#include "buzz.h"
#define BUZZ_USB_TRANSFERS 68
#define BUZZ_USB_EVENTS 80
typedef struct { uint8_t type, request; uint16_t value, index, length; } buzz_setup;
typedef struct { uint64_t type, arg2, arg3; } buzz_usb_event;
typedef struct {
 uint32_t id, pipe; uint64_t due_us;
 uint8_t used, done, data[256]; uint32_t count, result;
} buzz_usb_transfer;
typedef struct {
 buzz_hub *hub;
 unsigned attached, configured[2];
 uint32_t next_id;
 buzz_usb_transfer transfers[BUZZ_USB_TRANSFERS];
 buzz_usb_event events[BUZZ_USB_EVENTS]; unsigned head, count;
} buzz_usb;
extern const uint8_t buzz_device_descriptor[18];
extern const uint8_t buzz_config_descriptor[34];
extern const uint8_t buzz_hid_report_descriptor[78];
void buzz_usb_init(buzz_usb *u,buzz_hub *hub);
/* Attach is explicit and idempotent. A real adapter must first match LDD VID/PID. */
int buzz_usb_attach(buzz_usb *u);
void buzz_usb_reset(buzz_usb *u);
/* Pipe IDs below are MODEL IDs only: 1/2 = dongle0 control/IN, 3/4 = dongle1. */
int buzz_usb_submit(buzz_usb *u,unsigned pipe,const buzz_setup *setup,
                    const uint8_t *out,size_t capacity,uint64_t now_us);
void buzz_usb_tick(buzz_usb *u,uint64_t now_us);
int buzz_usb_pop(buzz_usb *u,buzz_usb_event *event);
const buzz_usb_transfer *buzz_usb_status(const buzz_usb *u,uint32_t id);
void buzz_usb_reap(buzz_usb *u,uint32_t id);
#endif
