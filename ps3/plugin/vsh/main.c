/* SPDX-License-Identifier: MIT.
 * PS3DK/PSL1GHT-style VSH source. NOT compiled or run on PS3 in Phase 1.
 * VSH already owns pad and network initialization; never ioPadEnd/netDeinitialize.
 */
#include <sys/prx_module.h>
#include <sys/thread.h>
#include <sys/timer.h>
#include <ppu-lv2.h>
#include <io/pad.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "buzz.h"
#include "mailbox.h"
#include "server.h"
#include "bridge.h"
static buzz_hub hub;
static sys_ppu_thread_t worker_id;
static atomic_int running;
static int started;
static uint64_t milliseconds(void)
{
 lv2syscall0(145); /* sys_time_get_system_time: microseconds, no input args. */
 return (uint64_t)p1/1000; /* p1 is the return register in PSL1GHT's syscall macro. */
}
static void poll_pads(void)
{
 padInfo2 info; unsigned port;
 memset(&info,0,sizeof(info));
 if(ioPadGetInfo2(&info)!=0) {
  /* A failed snapshot must release stale presses. Next successful poll reassigns. */
  for(port=0;port<BUZZ_PADS;port++) buzz_pad_update(&hub,port,0,0,0);
  return;
 }
 for(port=0;port<BUZZ_PADS;port++) {
  padData data; int result;
  if(!(info.port_status[port]&1)) { buzz_pad_update(&hub,port,0,0,0); continue; }
  memset(&data,0,sizeof(data)); result=ioPadGetData(port,&data);
  if(result) { buzz_pad_update(&hub,port,0,0,0); continue; }
  buzz_pad_update(&hub,port,1,data.len>0,
   (uint16_t)(data.button[PAD_BUTTON_OFFSET_DIGITAL1]|(data.button[PAD_BUTTON_OFFSET_DIGITAL2]<<8)));
 }
}
static void rumble(unsigned port,unsigned on)
{
 padActParam act; memset(&act,0,sizeof(act)); act.motor[1]=(uint8_t)(on?48:0);
 (void)ioPadSetActDirect(port,&act);
}
static void worker(void *arg)
{
 uint64_t lamp_until[BUZZ_PADS]={0}; unsigned old_light[BUZZ_PADS]={0},port;
 (void)arg;
 while(atomic_load(&running)) {
  uint8_t request[32],reply[32]; uint64_t now=milliseconds(); int ready=0;
  poll_pads(); buzz_mailbox_request(&hub,request);
  if(buzz_bridge_exchange(request,reply)==0) ready=buzz_mailbox_apply_lights(&hub,reply);
  if(!ready) for(port=0;port<BUZZ_PLAYERS;port++) if(hub.slots[port].light) { hub.slots[port].light=0; ++hub.generation; }
  for(port=0;port<BUZZ_PADS;port++) {
   unsigned light=buzz_pad_light(&hub,port);
   if(light && !old_light[port]) { lamp_until[port]=now+80; rumble(port,1); }
   if(lamp_until[port] && now>=lamp_until[port]) { rumble(port,0); lamp_until[port]=0; }
   old_light[port]=light;
  }
  buzz_server_tick(now,ready); sys_timer_usleep(8000);
 }
 buzz_server_stop(); for(port=0;port<BUZZ_PADS;port++) if(lamp_until[port]) rumble(port,0);
}
int module_start(size_t args,void *argp)
{
 buzz_config config; FILE *f; char text[4096]; size_t n;
 (void)args; (void)argp; if(started) return SYS_PRX_RESIDENT;
 buzz_defaults(&config);
 f=fopen("/dev_hdd0/plugins/ps3buzz/ps3buzz.cfg","rb");
 if(f) {
  n=fread(text,1,sizeof(text)-1,f);
  if(ferror(f) || n==sizeof(text)-1) { fclose(f); return SYS_PRX_NO_RESIDENT; }
  fclose(f); text[n]=0; if(buzz_config_parse(&config,text)) return SYS_PRX_NO_RESIDENT;
 }
 buzz_hub_init(&hub,&config);
 if(buzz_server_start(&hub,config.port,0)) return SYS_PRX_NO_RESIDENT;
 atomic_store(&running,1);
 if(sysThreadCreate(&worker_id,worker,NULL,1500,65536,THREAD_JOINABLE,"ps3buzz")) {
  atomic_store(&running,0); buzz_server_stop(); return SYS_PRX_NO_RESIDENT;
 }
 started=1; return SYS_PRX_RESIDENT;
}
int module_stop(size_t args,void *argp)
{
 uint64_t result; (void)args; (void)argp;
 if(started) { atomic_store(&running,0); sysThreadJoin(worker_id,&result); started=0; }
 return SYS_PRX_RESIDENT;
}
