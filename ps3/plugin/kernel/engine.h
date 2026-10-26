/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_KERNEL_ENGINE_H
#define BUZZ_KERNEL_ENGINE_H
#include "usb_model.h"
#include "mailbox.h"
typedef struct { buzz_hub state; buzz_usb usb; uint64_t heartbeat_us; int ready; } buzz_kernel_engine;
void buzz_kernel_engine_init(buzz_kernel_engine *e);
/* Caller serializes all engine accesses and verifies caller is the VSH process.
 * request/reply point to kernel-owned copies, never raw userspace pointers. */
int buzz_kernel_exchange(buzz_kernel_engine *e,const uint8_t request[32],uint8_t reply[32],uint64_t now);
void buzz_kernel_watchdog(buzz_kernel_engine *e,uint64_t now);
#endif
