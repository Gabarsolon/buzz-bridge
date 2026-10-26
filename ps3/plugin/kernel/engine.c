/* SPDX-License-Identifier: MIT. Host-tested engine; no firmware offsets. */
#include "engine.h"
#include <string.h>
void buzz_kernel_engine_init(buzz_kernel_engine *e)
{
 buzz_config c; memset(e,0,sizeof(*e)); buzz_defaults(&c);
 buzz_hub_init(&e->state,&c); buzz_usb_init(&e->usb,&e->state);
}
int buzz_kernel_exchange(buzz_kernel_engine *e,const uint8_t req[32],uint8_t reply[32],uint64_t now)
{
 unsigned old_dongles=e->state.config.dongles,old_enabled=e->state.config.enabled;
 if(!buzz_mailbox_decode_reports(&e->state,req)) return 0;
 if(old_dongles!=e->state.config.dongles || (old_enabled && !e->state.config.enabled)) buzz_usb_reset(&e->usb);
 e->heartbeat_us=now;
 buzz_mailbox_reply(&e->state,e->ready,e->state.generation,reply); return 1;
}
void buzz_kernel_watchdog(buzz_kernel_engine *e,uint64_t now)
{
 unsigned i;
 if(now-e->heartbeat_us>500000) {
  for(i=0;i<8;i++) e->state.slots[i].buttons=0;
 }
 buzz_usb_tick(&e->usb,now);
}
