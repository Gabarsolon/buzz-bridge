/* SPDX-License-Identifier: MIT. Buzz game-process SPRX, runtime 0.9.
 * Never put this module in VSH's boot_plugins.txt. No kernel component needed.
 * Native compiler/linking, loader OPDs, synchronization and callbacks require
 * a recoverable PS3 test; PC compilation checks syntax with declarations only.
 */
#ifdef BUZZ_HOST_CHECK
#include "host_ps3.h"
#else
#include <sys/prx_module.h>
#include <sys/thread.h>
#include <sys/timer.h>
#include <sys/sys_time.h>
#include <ppu-lv2.h>
#include <io/pad.h>
#include <sys/file.h>
#include <sys/memory.h>
#include <sys/mutex.h>
_Static_assert(sizeof(void *)==4,"Requires the 32-bit PPU user ABI");
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "format.h"
#include "cellusbd_adapter.h"
#include "import_hook.h"
#include "resolve_game.h"
#include "server.h"
#include "profile.h"
#include "presentation.h"
#ifndef BUZZ_HOST_CHECK
_Static_assert(sizeof(buzz_cell_ldd)==16,"Requires compact 32-bit PPU function descriptors");
_Static_assert(sizeof(buzz_setup)==8,"cellUsbd setup ABI");
#endif
static buzz_cell adapter;
static buzz_hub hub;
static buzz_import_plan imports;
static buzz_game_layout game;
static atomic_flag mutex=ATOMIC_FLAG_INIT;
static atomic_int running;
static atomic_int published;
static sys_ppu_thread_t worker_id;
static sys_ppu_thread_t network_id,diagnostic_id;
enum { WORKER_PRIORITY=1000, NETWORK_PRIORITY=1010, DIAGNOSTIC_PRIORITY=990, LOG_LIMIT=32768 }; /* Local pacing policy; does not guarantee recovery from a foreign/kernel wait. */
static atomic_int safety_stopped;
static atomic_uint cpu_guard_hits;
static atomic_uint worker_loops,network_loops,lock_waits,trace_dropped;
static atomic_uint game_pad_polls,attach_active,attach_started_ms,attach_fault,attach_success;
static buzz_presentation presentation;
static int presentation_started;
static uint64_t microseconds(void);
static void callback_watch(unsigned token,uint32_t now)
{
 if(token && !(token&0x80000000u) && now-atomic_load(&attach_started_ms)>=BUZZ_ATTACH_LIMIT_MS) {
  unsigned expected=token;
  if(atomic_compare_exchange_strong(&attach_active,&expected,token|0x80000000u))
   atomic_store(&attach_fault,token);
 }
}
static _Atomic(const char *) worker_state="not-started",network_state="not-started";
static void worker_progress(const char *state)
{
 if(!strcmp(state,"attach-return")) {
  callback_watch(atomic_load(&attach_active),(uint32_t)(microseconds()/1000));
  unsigned token=atomic_load(&attach_active);
  if(token&0x80000000u)atomic_store(&attach_fault,token&0x7fffffffu);
  else if(token && !atomic_compare_exchange_strong(&attach_active,&token,0) && (token&0x80000000u))
   atomic_store(&attach_fault,token&0x7fffffffu);
 }
 atomic_store(&worker_state,state);
}
static void network_progress(const char *state) { atomic_store(&network_state,state); }
static sys_ppu_thread_t bootstrap_id;
static atomic_int bootstrap_started;
static int installed,started;
static uint16_t pad_mask[BUZZ_PADS];
static unsigned pad_changed[BUZZ_PADS];
static unsigned pad_present[BUZZ_PADS];
static unsigned pad_observed[BUZZ_PADS];
static uint64_t pad_seen[BUZZ_PADS];
static int32_t (*original_pad_data)(uint32_t,padData *);
/* No process CRT, constructors, malloc, stdio streams or process-wide init.
 * libgcc may consult this before using its static unwind reserve. */
int __ps3dk_heap_ready;
int buzz_vsnprintf(char *,size_t,const char *,va_list);
static int log_fd=-1;
static unsigned log_bytes,event_log_full;
static atomic_flag log_lock=ATOMIC_FLAG_INIT;
static atomic_flag log_writer=ATOMIC_FLAG_INIT;
/* This fd and byte counter belong ONLY to diagnostics. No shared log locks. */
static int heartbeat_fd=-1;
static unsigned heartbeat_bytes;
typedef struct { unsigned event; int32_t object; uint32_t result,count; char detail[80]; } trace_event;
static trace_event trace_ring[64];
static unsigned trace_head,trace_count,trace_inputs;
static void log_line(const char *fmt,...)
{
 char line[256]; int n; va_list ap;
 va_start(ap,fmt); n=buzz_vsnprintf(line,sizeof(line)-1,fmt,ap); va_end(ap);
 if(n<0) return; if((size_t)n>=sizeof(line)-1) n=(int)sizeof(line)-2;
 line[n++]='\n'; line[n]=0;
 if(atomic_flag_test_and_set_explicit(&log_writer,memory_order_acquire)) { atomic_fetch_add(&trace_dropped,1); return; }
#ifndef BUZZ_HOST_CHECK
 if(log_fd>=0 && !event_log_full && log_bytes+(unsigned)n<=LOG_LIMIT-64) {
  uint64_t written=0; (void)sysLv2FsWrite(log_fd,line,(unsigned)n,&written);
  log_bytes+=(unsigned)written;
 } else if(log_fd>=0 && !event_log_full && log_bytes+64<=LOG_LIMIT) {
  static const char cap[]="event log capped; see ps3buzz.heartbeat.log\n"; uint64_t written=0;
  (void)sysLv2FsWrite(log_fd,cap,sizeof(cap)-1,&written); log_bytes+=(unsigned)written; event_log_full=1;
 }
#else
 (void)log_fd; (void)log_bytes; (void)event_log_full; buzz_host_log_line(line);
#endif
 atomic_flag_clear_explicit(&log_writer,memory_order_release);
}
static void trace(unsigned event,int32_t object,uint32_t result,uint32_t count)
{
 if(event==BZ_TRACE_ATTACH_CALL) {
  atomic_store(&attach_started_ms,(uint32_t)(microseconds()/1000));
  atomic_store(&attach_active,(unsigned)(object-BZ_CELL_DEVICE_BASE+1));
 }
 if(event==BZ_TRACE_ATTACH && !result && !atomic_load(&attach_fault))
  atomic_fetch_or(&attach_success,1u<<(unsigned)(object-BZ_CELL_DEVICE_BASE));
 /* Never wait or perform HDD I/O inside a game callback or adapter lock. */
 if(atomic_flag_test_and_set_explicit(&log_lock,memory_order_acquire)) { atomic_fetch_add(&trace_dropped,1); return; }
 if(event!=BZ_TRACE_INPUT || ++trace_inputs<=4 || !(trace_inputs%1024) || result) {
  if(trace_count<64) {
   trace_ring[(trace_head+trace_count)%64]=(trace_event){event,object,result,count,{0}}; ++trace_count;
  } else atomic_fetch_add(&trace_dropped,1);
 }
 atomic_flag_clear_explicit(&log_lock,memory_order_release);
}
static void network_trace(const char *event,int fd,int result,const char *detail)
{
 if(atomic_flag_test_and_set_explicit(&log_lock,memory_order_acquire)) { atomic_fetch_add(&trace_dropped,1); return; }
 if(trace_count<64) {
  trace_event *e=&trace_ring[(trace_head+trace_count)%64];
  *e=(trace_event){12,fd,(uint32_t)result,0,{0}};
  (void)snprintf(e->detail,sizeof(e->detail),"%s %s",event,detail); ++trace_count;
 } else atomic_fetch_add(&trace_dropped,1);
 atomic_flag_clear_explicit(&log_lock,memory_order_release);
}
static void flush_trace(void)
{
 static const char *const names[]={"?","lookup-return","probe-return","attach-return","control","input","detach","pad",
  "lookup-call","probe-call","attach-call","first-IN-submit"};
 for(unsigned i=0;i<16;i++) {
  trace_event e;
  if(atomic_flag_test_and_set_explicit(&log_lock,memory_order_acquire)) break;
  if(!trace_count) { atomic_flag_clear_explicit(&log_lock,memory_order_release); break; }
  e=trace_ring[trace_head]; trace_head=(trace_head+1)%64; --trace_count;
  atomic_flag_clear_explicit(&log_lock,memory_order_release);
  if(e.event==12) log_line("http %s fd=%d result=%d",e.detail,e.object,(int32_t)e.result);
  else if(e.event==13) log_line("game state count=%u presence=%02x,%02x (ff=absent)",(unsigned)e.object,(e.result>>8)&255,e.result&255);
  else if(e.event==14) log_line("adopt buzzer2 ldd=%08x ready=%u result=%08x",(unsigned)e.object,e.count,e.result);
  else if(e.event==BZ_TRACE_OPEN) log_line("open-pipe device=%08x pipe=%08x endpoint=%02x",(unsigned)e.object,e.result,e.count);
  else if(e.event==BZ_TRACE_FIRST_INPUT) log_line("first-IN-complete object=%08x result=%08x count=%u",(unsigned)e.object,e.result,e.count);
  else if(e.event==BZ_TRACE_CONTROL_SUBMIT) log_line("control-submit pipe=%08x type=%02x request=%02x value=%04x length=%u",(unsigned)e.object,e.result>>24,(e.result>>16)&255,e.result&65535,e.count);
  else if(e.event==BZ_TRACE_LOCATION) log_line("virtual-location device=%08x result=%08x",(unsigned)e.object,e.result);
  else if(e.event==BZ_TRACE_SCAN) log_line("virtual-scan device=%08x type=%u found=%u",(unsigned)e.object,e.count,e.result);
  else log_line("%s object=%08x result=%08x count=%u",e.event<12?names[e.event]:"?",(unsigned)e.object,e.result,e.count);
 }
}
static void enter(void *arg)
{ (void)arg; while(atomic_flag_test_and_set_explicit(&mutex,memory_order_acquire)) { sys_timer_usleep(2000); if(atomic_load(&safety_stopped)) sys_timer_usleep(100000); atomic_fetch_add(&lock_waits,1); } }
static void leave(void *arg)
{ (void)arg; atomic_flag_clear_explicit(&mutex,memory_order_release); }
static uint64_t microseconds(void)
{
#ifdef BUZZ_HOST_CHECK
 return buzz_host_time_us();
#else
 return (uint64_t)sys_time_get_system_time();
#endif
}
static void check_stop(void)
{
#ifdef BUZZ_HOST_CHECK
 if(buzz_host_stop_requested()) atomic_store(&safety_stopped,1);
#else
 sysFSStat st;
 if(!sysLv2FsStat("/dev_hdd0/tmp/ps3buzz.stop",&st)) atomic_store(&safety_stopped,1);
#endif
}
typedef struct { uint64_t epoch; unsigned count,reported; } loop_guard;
static void guard_loop(loop_guard *g,unsigned bit)
{
 uint64_t now=microseconds();
 if(now-g->epoch>=1000000) { g->epoch=now; g->count=0; }
 if(++g->count>1000) {
  if(!g->reported) { atomic_fetch_or(&cpu_guard_hits,bit); g->reported=1; }
  /* Repeat throttling even after the once-only report. */
  sys_timer_usleep(100000);
 }
}
static int device_index(int32_t id)
{ return id>=BZ_CELL_DEVICE_BASE && id<BZ_CELL_DEVICE_BASE+2 ? id-BZ_CELL_DEVICE_BASE : -1; }
static int own_pipe(int32_t id)
{ return id>=BZ_CELL_PIPE_BASE && id<BZ_CELL_PIPE_BASE+0x10000; }
static int is_stopped(void) { return atomic_load(&safety_stopped) || atomic_load(&attach_fault); }
/* A callback quarantine keeps the phone listener available for diagnostics. */
static int user_stopped(void) { return atomic_load(&safety_stopped); }
static int32_t hooked_init(void) { return buzz_cell_init(&adapter); }
static int32_t hooked_end(void) { return is_stopped()?adapter.real.end():buzz_cell_end(&adapter); }
static int32_t hooked_reg(buzz_cell_ldd *l,uint16_t v,uint16_t p)
{ return is_stopped()?adapter.real.register_extra(l,v,p):buzz_cell_register(&adapter,l,v,p,p,1); }
static int32_t hooked_reg2(buzz_cell_ldd *l,uint16_t v,uint16_t lo,uint16_t hi)
{ return is_stopped()?adapter.real.register_extra2(l,v,lo,hi):buzz_cell_register(&adapter,l,v,lo,hi,2); }
static int32_t hooked_unreg(buzz_cell_ldd *l) { return is_stopped()?adapter.real.unregister_extra(l):buzz_cell_unregister(&adapter,l); }
static void *hooked_scan(int32_t d,void *p,uint8_t t)
{ if(is_stopped()) return device_index(d)>=0?NULL:adapter.real.scan(d,p,t); return buzz_cell_scan(&adapter,d,p,t); }
static int32_t hooked_open(int32_t d,void *p)
{ if(is_stopped()) return device_index(d)>=0?BZ_CELL_DEVICE:adapter.real.open(d,p); return buzz_cell_open(&adapter,d,p); }
static int32_t hooked_close(int32_t p)
{ if(is_stopped()) return own_pipe(p)?BZ_CELL_DEVICE:adapter.real.close(p); return buzz_cell_close(&adapter,p); }
static int32_t hooked_control(int32_t p,buzz_setup *s,void *b,buzz_cell_done f,void *a)
{ if(is_stopped()) return own_pipe(p)?BZ_CELL_DEVICE:adapter.real.control(p,s,b,f,a); return buzz_cell_control(&adapter,p,s,b,f,a); }
static int32_t hooked_interrupt(int32_t p,void *b,int32_t n,buzz_cell_done f,void *a)
{ if(is_stopped()) return own_pipe(p)?BZ_CELL_DEVICE:adapter.real.interrupt(p,b,n,f,a); return buzz_cell_interrupt(&adapter,p,b,n,f,a); }
static int32_t hooked_set(int32_t d,void *p)
{ if(is_stopped()) return device_index(d)>=0?BZ_CELL_DEVICE:adapter.real.set_private(d,p); return buzz_cell_set_private(&adapter,d,p); }
static void *hooked_get(int32_t d)
{ if(is_stopped()) return device_index(d)>=0?NULL:adapter.real.get_private(d); return buzz_cell_get_private(&adapter,d); }
static int32_t hooked_location(int32_t d,uint8_t *p)
{ if(is_stopped()) return device_index(d)>=0?BZ_CELL_DEVICE:adapter.real.location(d,p); return buzz_cell_location(&adapter,d,p); }
static int32_t hooked_pad_data(uint32_t port,padData *data)
{
 int32_t result=original_pad_data(port,data);
 /* Count actual game calls, including NO_DEVICE and unchanged reports. No
  * callback dispatch, clock/syscall or adapter lock is needed for this signal. */
 atomic_fetch_add(&game_pad_polls,1);
 /* Tee successful game reads. An independent GetData poll in this process can
  * consume change notifications before the game's own pad reader sees them. */
 if(!is_stopped() && port<BUZZ_PADS) {
  enter(NULL);
  if(!result) {
   pad_present[port]=1; pad_observed[port]=1;
   if(data && data->len>0) {
    pad_mask[port]=(uint16_t)(data->button[PAD_BUTTON_OFFSET_DIGITAL1]|data->button[PAD_BUTTON_OFFSET_DIGITAL2]<<8);
    pad_changed[port]=1;
   }
  } else if((uint32_t)result==0x80121107u) { pad_present[port]=0; pad_mask[port]=0; pad_changed[port]=1; }
  leave(NULL);
 }
 return result;
}
static int read_memory(void *arg,uint32_t address,void *out,size_t n)
{
 (void)arg;
#ifdef BUZZ_HOST_CHECK
 (void)address; (void)out; (void)n; return 0;
#else
 sys_page_attr_t attr;
 if(!n || address<0x10000 || address>=0x40000000 || n>0x40000000-address) return 0;
 for(uint32_t p=address&~0xfffu;p<=((address+(uint32_t)n-1)&~0xfffu);p+=0x1000)
  if(sys_memory_get_page_attribute(p,&attr)) return 0;
 memcpy(out,(const void *)(uintptr_t)address,n); return 1;
#endif
}
static uint32_t load_be(uint32_t address)
{
 uint8_t b[4]; if(!read_memory(NULL,address,b,4)) return 0;
 return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];
}
static int replace_memory(void *arg,uint32_t address,uint32_t old,uint32_t next)
{
 (void)arg;
#ifdef BUZZ_HOST_CHECK
 (void)address; (void)old; (void)next; return 0;
#else
 int allowed=0;
 for(unsigned i=0;i<imports.count;i++)if(address==imports.patch[i].slot && address==game.slots[i])allowed=1;
 if((address&3) || !allowed) return 0;
 if(!atomic_compare_exchange_strong_explicit((_Atomic uint32_t *)(uintptr_t)address,&old,next,memory_order_seq_cst,memory_order_seq_cst)) return 0;
 atomic_store(&published,1); return 1;
#endif
}
static int valid_opd(void *arg,uint32_t opd)
{
 uint32_t code,toc; uint8_t instruction[4]; (void)arg;
 if((opd&3) || opd<0x10000 || opd>=0x40000000-8) return 0;
 code=load_be(opd); toc=load_be(opd+4);
 return !(code&3) && code>=0x10000 && code<0x40000000 && toc>=0x10000 && toc<0x40000000
        && read_memory(NULL,code,instruction,sizeof(instruction));
}
static const buzz_import_memory memory_api={NULL,read_memory,replace_memory,valid_opd};
/* NID -> compact OPD pointer, no copied game code. */
#define WANT(index,nid_value,fn) do { imports.patch[index].nid=nid_value; imports.patch[index].replacement=(uint32_t)(uintptr_t)(fn); } while(0)
static int prepare_imports(void)
{
 buzz_cell_real real; buzz_cell_sync sync={enter,leave,NULL};
 memset(&imports,0,sizeof(imports)); imports.count=14;
 WANT(0,0xd0e766fe,hooked_init); WANT(1,0x35f22ac3,hooked_end);
 WANT(2,0x7fe92c54,hooked_reg); WANT(3,0xbd554bcb,hooked_reg2);
 WANT(4,0x90460081,hooked_unreg); WANT(5,0x2fb08e1e,hooked_scan);
 WANT(6,0x254289ac,hooked_open); WANT(7,0x9763e962,hooked_close);
 WANT(8,0x97cf128e,hooked_control); WANT(9,0x0f411262,hooked_interrupt);
 WANT(10,0x63bfdb97,hooked_set); WANT(11,0x5de3af36,hooked_get);
 WANT(12,0xdb819e03,hooked_location); WANT(13,0x8b72cda1,hooked_pad_data);
 if(!buzz_import_discover(&imports,&memory_api,game.begin,game.end)) return 0;
 for(unsigned i=0;i<imports.count;i++) {
  if(imports.patch[i].slot!=game.slots[i]) return 0;
  log_line("hook plan nid=%08x slot=%08x original=%08x replacement=%08x",imports.patch[i].nid,
           imports.patch[i].slot,imports.patch[i].original,imports.patch[i].replacement);
 }
 /* The saved values ARE descriptors, not code entry addresses. */
 real.init=(int32_t (*)(void))(uintptr_t)imports.patch[0].original;
 real.end=(int32_t (*)(void))(uintptr_t)imports.patch[1].original;
 real.register_extra=(int32_t (*)(buzz_cell_ldd *,uint16_t,uint16_t))(uintptr_t)imports.patch[2].original;
 real.register_extra2=(int32_t (*)(buzz_cell_ldd *,uint16_t,uint16_t,uint16_t))(uintptr_t)imports.patch[3].original;
 real.unregister_extra=(int32_t (*)(buzz_cell_ldd *))(uintptr_t)imports.patch[4].original;
 real.scan=(void *(*)(int32_t,void *,uint8_t))(uintptr_t)imports.patch[5].original;
 real.open=(int32_t (*)(int32_t,void *))(uintptr_t)imports.patch[6].original;
 real.close=(int32_t (*)(int32_t))(uintptr_t)imports.patch[7].original;
 real.control=(int32_t (*)(int32_t,buzz_setup *,void *,buzz_cell_done,void *))(uintptr_t)imports.patch[8].original;
 real.interrupt=(int32_t (*)(int32_t,void *,int32_t,buzz_cell_done,void *))(uintptr_t)imports.patch[9].original;
 real.set_private=(int32_t (*)(int32_t,void *))(uintptr_t)imports.patch[10].original;
 real.get_private=(void *(*)(int32_t))(uintptr_t)imports.patch[11].original;
 real.location=(int32_t (*)(int32_t,uint8_t *))(uintptr_t)imports.patch[12].original;
 original_pad_data=(int32_t (*)(uint32_t,padData *))(uintptr_t)imports.patch[13].original;
 buzz_cell_construct(&adapter,&hub,&real,&sync); adapter.trace=trace; adapter.progress=worker_progress;
 adapter.present_allowed=0;
 adapter.now_us=microseconds(); return 1;
}
static int manager_preflight(void)
{
 uint8_t ready=0,constructed=0;uint32_t mutex_id=load_be(game.ready+4);
 if(!mutex_id || !read_memory(NULL,game.ready,&ready,1) || ready!=1 ||
    !read_memory(NULL,game.ready-0x9c,&constructed,1) || constructed!=1 ||
    (int32_t)load_be(game.ready+8)>0 || load_be(game.count)>=2)return 0;
#ifdef BUZZ_HOST_CHECK
 return buzz_host_manager_available();
#else
 /* Never hold the game's mutex while calling its attach, any game routine,
  * our adapter lock, logging, or a page query. Success is an observation. */
 if(sysMutexTryLock(mutex_id))return 0;
 return sysMutexUnlock(mutex_id)==0;
#endif
}
static int attach_preflight(void)
{
 if(is_stopped())return 0;
 /* Once both slots are owned there is no reason to sample the game lock. */
 if(atomic_load(&attach_success)==((1u<<hub.config.dongles)-1u))return 0;
 return manager_preflight();
}
static void poll_pad_snapshot(uint64_t now)
{
 static uint16_t old_mask[BUZZ_PADS];
 unsigned p;
 /* Consume the game's existing GetData results, including NO_DEVICE. No
  * second native pad query can block presentation or consume notifications. */
 enter(NULL);
 for(p=0;p<BUZZ_PADS;p++) {
  if(pad_observed[p]) { pad_seen[p]=now; pad_observed[p]=0; }
  /* The game may stop GetData after its own connection check. Release stale
   * buttons after 250 ms even when it never calls us with NO_DEVICE. */
  if(pad_present[p] && now-pad_seen[p]>250000) { pad_present[p]=0; pad_mask[p]=0; pad_changed[p]=1; }
  int connected=(int)pad_present[p];
  if(connected!=hub.pad_connected[p] || (pad_changed[p] && pad_mask[p]!=old_mask[p]))
   trace(7,(int32_t)p,(unsigned)connected,pad_mask[p]);
  old_mask[p]=pad_mask[p];
  buzz_pad_update(&hub,p,connected,(int)pad_changed[p],pad_mask[p]);
  pad_changed[p]=0; if(!connected) pad_mask[p]=0;
 }
 leave(NULL);
}
static void rumble(unsigned port,unsigned on)
{ padActParam act; memset(&act,0,sizeof(act)); act.motor[1]=(uint8_t)(on?48:0); (void)ioPadSetActDirect(port,&act); }
static void worker(void *arg)
{
 loop_guard guard={0};
 uint64_t until[BUZZ_PADS]={0}; unsigned old[BUZZ_PADS]={0}; uint64_t last_pads=0,last_state=0;
 uint32_t prior_state=UINT32_MAX; int adopted=0;
 uint64_t last_gate_log=0;unsigned gate_logged=0;
 (void)arg;
 worker_progress("entry");
 while(atomic_load(&running)) {
  sys_timer_usleep(2000);
  guard_loop(&guard,1);
  if(is_stopped()) { worker_progress("safety-stopped"); sys_timer_usleep(100000); continue; }
  atomic_fetch_add(&worker_loops,1); worker_progress("adopt-check");
  uint64_t now=microseconds(); unsigned p;
  if(!presentation_started) {
   buzz_presentation_init(&presentation,now,atomic_load(&game_pad_polls));presentation_started=1;
   log_line("presentation wait: game-pad-polls >=5 active 100ms buckets/s for 10s; minimum runtime age=10s; manager trylock required");
  }
  if(!adopted && now-presentation.sample>=100000) {
   int manager_ok=manager_preflight() && load_be(game.count)==0;
   uint8_t presence[2]={0,0};
   manager_ok=manager_ok && read_memory(NULL,game.presence[0],presence,1) && read_memory(NULL,game.presence[1],presence+1,1) && presence[0]==255 && presence[1]==255;
   (void)buzz_presentation_tick(&presentation,now,atomic_load(&game_pad_polls),manager_ok);
   if(!gate_logged || now-last_gate_log>=5000000 || presentation.allowed) {
    log_line("presentation signal age=%us game-pad-polls=%u active-windows=%u/10 manager-trylock=%u allowed=%u",
     (unsigned)((now-presentation.start)/1000000),atomic_load(&game_pad_polls),presentation.good,manager_ok,presentation.allowed);
    last_gate_log=now;gate_logged=1;
   }
  }
  /* The static LDD record and manager init flag were recovered from this build.
   * Adoption remembers the callbacks; subsequent polls present the devices.
   * No physical driver is unregistered/restarted to make injection "early". */
  if(!adopted && presentation.allowed) {
   uint8_t initialized=0;int callbacks_valid=1;
   for(unsigned k=0;k<3;k++)if(load_be(game.ldd+4+k*4)!=game.opds[k]||!valid_opd(NULL,game.opds[k]))callbacks_valid=0;
   if(callbacks_valid && read_memory(NULL,game.ready,&initialized,1) && initialized==1) {
    enter(NULL); adapter.now_us=now; leave(NULL);
    int32_t result=buzz_cell_adopt(&adapter,(buzz_cell_ldd *)(uintptr_t)game.ldd,0x54c,2,0x42);
    trace(14,(int32_t)game.ldd,(uint32_t)result,initialized);
    adopted=result?0:1;
    if(adopted) {enter(NULL);adapter.present_allowed=1;leave(NULL);log_line("presentation-ready signal=game-pad-polls+manager-trylock (operational input loop; no retail screen assertion)");}
   }
  }
  worker_progress("usb-poll");
  buzz_cell_poll(&adapter,now);
  if(atomic_load(&safety_stopped)) continue;
  worker_progress("pad-snapshot");
  if(now-last_pads>=8000) { poll_pad_snapshot(now); last_pads=now; }
  if(now-last_state>=1000000) {
   worker_progress("game-state-read");
   uint8_t presence[2]={255,255};
   (void)read_memory(NULL,game.presence[0],presence,1); (void)read_memory(NULL,game.presence[1],presence+1,1);
   uint32_t count=load_be(game.count),state=count<<16|(unsigned)presence[0]<<8|presence[1];
   if(state!=prior_state) {
    /* The observer drains this even if a later callback never returns. */
    trace(13,(int32_t)count,state,0); prior_state=state;
   }
   last_state=now;
  }
  unsigned lights[BUZZ_PADS]; worker_progress("light-snapshot"); enter(NULL);
  for(p=0;p<BUZZ_PADS;p++) lights[p]=buzz_pad_light(&hub,p);
  leave(NULL);
  for(p=0;p<BUZZ_PADS;p++) {
   unsigned light=lights[p];
   if(light && !old[p]) { until[p]=now+80000; worker_progress("rumble-call"); rumble(p,1); worker_progress("rumble-return"); }
   if(until[p] && now>=until[p]) { worker_progress("rumble-call"); rumble(p,0); worker_progress("rumble-return"); until[p]=0; }
   old[p]=light;
  }
  worker_progress("sleep");
 }
 for(unsigned p=0;p<BUZZ_PADS;p++) if(until[p]) rumble(p,0);
#ifndef BUZZ_HOST_CHECK
 sysThreadExit(0);
#endif
}
static void network(void *arg)
{
 loop_guard guard={0};
 (void)arg;
 while(atomic_load(&running)) {
  sys_timer_usleep(10000);
  guard_loop(&guard,2);
  if(atomic_load(&safety_stopped)) { network_progress("safety-stopped"); sys_timer_usleep(100000); continue; }
  int ready; atomic_fetch_add(&network_loops,1); network_progress("hub-ready-lock");
  ready=buzz_cell_ready(&adapter);
  if(atomic_load(&safety_stopped)) continue;
  buzz_server_tick(microseconds()/1000,ready);
  network_progress("sleep");
 }
 buzz_server_stop();
#ifndef BUZZ_HOST_CHECK
 lv2syscall1(41,0);
#endif
}
static void heartbeat_line(const char *fmt,...)
{
 char line[256]; va_list ap; int n;
 va_start(ap,fmt); n=buzz_vsnprintf(line,sizeof(line)-1,fmt,ap); va_end(ap);
 if(n<0) return; if((size_t)n>=sizeof(line)-1) n=(int)sizeof(line)-2;
 line[n++]='\n'; line[n]=0;
#ifdef BUZZ_HOST_CHECK
 (void)heartbeat_fd; (void)heartbeat_bytes; buzz_host_log_line(line);
#else
 if(heartbeat_fd>=0 && heartbeat_bytes+(unsigned)n<=LOG_LIMIT) {
  uint64_t written=0;
  (void)sysLv2FsWrite(heartbeat_fd,line,(unsigned)n,&written);
  heartbeat_bytes+=(unsigned)written;
 }
#endif
}
static void heartbeat(uint64_t start)
{
 heartbeat_line("heartbeat t=%u worker loops=%u last=%s; http loops=%u last=%s; lock-waits=%u dropped=%u stop=%u",
  (unsigned)((microseconds()-start)/1000000),atomic_load(&worker_loops),atomic_load(&worker_state),
  atomic_load(&network_loops),atomic_load(&network_state),atomic_load(&lock_waits),atomic_load(&trace_dropped),
  (unsigned)atomic_load(&safety_stopped));
}
static void diagnostics(void *arg)
{
 uint64_t start=microseconds(); loop_guard guard={0}; unsigned guards_reported=0,stop_reported=0,timeout_reported=0; (void)arg;
#ifndef BUZZ_HOST_CHECK
 (void)sysLv2FsOpen("/dev_hdd0/tmp/ps3buzz.heartbeat.log",SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&heartbeat_fd,0666,NULL,0);
#endif
 check_stop(); heartbeat(start);
 while(atomic_load(&running)) {
  /* Two seconds per iteration; the mid-wait check gives a 1 Hz stop poll. */
  sys_timer_usleep(1000000); check_stop();
  callback_watch(atomic_load(&attach_active),(uint32_t)(microseconds()/1000));
  sys_timer_usleep(1000000); check_stop();
  callback_watch(atomic_load(&attach_active),(uint32_t)(microseconds()/1000));
  guard_loop(&guard,4);
  /* Write BEFORE touching the optional event ring or general log. */
  heartbeat(start);
  unsigned fault=atomic_load(&attach_fault);
  if(fault && !timeout_reported) {
   heartbeat_line("attach-timeout device=%08x limit=3000ms; quarantined; no second dongle/no retry; quit game",
    (unsigned)(BZ_CELL_DEVICE_BASE+fault-1));timeout_reported=1;
  }
  unsigned guards=atomic_load(&cpu_guard_hits)&~guards_reported;
  if(guards) {
   heartbeat_line("CPU GUARD mask=%u (>1000 loops/s); sleeping 100 ms; reported once per thread",guards);
   guards_reported|=guards;
  }
  if(atomic_load(&safety_stopped)) {
   if(!stop_reported) { heartbeat_line("SAFETY STOP latched; services quiescent at next safe boundary; fresh game required"); stop_reported=1; }
   continue;
  }
  flush_trace();
 }
#ifndef BUZZ_HOST_CHECK
 lv2syscall1(41,0);
#endif
}
static const char *game_title(void)
{
 static char title[10]="UNKNOWN";
#ifndef BUZZ_HOST_CHECK
 static const char *const paths[]={"/dev_bdvd/PS3_GAME/PARAM.SFO","/app_home/PARAM.SFO"};
 /* Mounted metadata is a log label, never an identity/acceptance guard. */
 for(unsigned path=0;path<2;path++) {
  uint8_t b[4096];uint64_t n=0;int fd;
  if(sysLv2FsOpen(paths[path],SYS_O_RDONLY,&fd,0,NULL,0))continue;
  int result=sysLv2FsRead(fd,b,sizeof(b),&n);(void)sysLv2FsClose(fd);
  if(result||n<20||memcmp(b,"\0PSF",4))continue;
  uint32_t keys=(uint32_t)b[8]|(uint32_t)b[9]<<8|(uint32_t)b[10]<<16|(uint32_t)b[11]<<24;
  uint32_t values=(uint32_t)b[12]|(uint32_t)b[13]<<8|(uint32_t)b[14]<<16|(uint32_t)b[15]<<24;
  unsigned count=(unsigned)b[16]|(unsigned)b[17]<<8;
  if(count>128||20+count*16>n||keys>=n||values>=n)continue;
  for(unsigned i=0;i<count;i++) {
   unsigned at=20+i*16,ko=(unsigned)b[at]|(unsigned)b[at+1]<<8;
   uint32_t off=(uint32_t)b[at+12]|(uint32_t)b[at+13]<<8|(uint32_t)b[at+14]<<16|(uint32_t)b[at+15]<<24;
   if(ko>n-keys||n-keys-ko<9||memcmp(b+keys+ko,"TITLE_ID\0",9)||off>n-values||n-values-off<10)continue;
   int valid=1;for(unsigned j=0;j<9;j++)if(!((b[values+off+j]>='A'&&b[values+off+j]<='Z')||(b[values+off+j]>='0'&&b[values+off+j]<='9')))valid=0;
   if(valid && !b[values+off+9]) {memcpy(title,b+values+off,9);title[9]=0;return title;}
  }
 }
#endif
 return title;
}
static int startup(void)
{
 buzz_config cfg; char text[4096]; unsigned i;
 log_line("ps3buzz 0.9 bootstrap; stack=65536; no process CRT; log-cap=32768");
 buzz_defaults(&cfg);
#ifndef BUZZ_HOST_CHECK
 { int fd; int32_t result=sysLv2FsOpen("/dev_hdd0/plugins/ps3buzz/ps3buzz.cfg",SYS_O_RDONLY,&fd,0,NULL,0);
  if(result) { log_line("config unavailable result=%08x",(unsigned)result); goto refused; }
  uint64_t n=0; result=sysLv2FsRead(fd,text,sizeof(text)-1,&n); (void)sysLv2FsClose(fd);
  if(result || n>=sizeof(text)-1) { log_line("config read failed"); goto refused; }
  text[n]=0; if(buzz_config_parse(&cfg,text)) { log_line("config parse failed"); goto refused; }
 }
#else
 (void)text;
#endif
 log_line("config enabled=%u dongles=%u split=%u port=%u generic=%u",cfg.enabled,cfg.dongles,cfg.players_per_pad,cfg.port,cfg.generic);
 if(!cfg.enabled) { log_line("load REFUSED enabled=0"); goto refused; }
 const char *reason;
 if(!buzz_game_resolve(&memory_api,&game,&reason)) { log_line("load REFUSED %s",reason); goto refused; }
 if(game.profile<0 && !cfg.generic) { log_line("load REFUSED generic=0"); goto refused; }
 if(game.profile>=0)log_line("exact profile title=%s APP_VER=%s source-sha256=%s",buzz_profiles[game.profile].title,buzz_profiles[game.profile].version,buzz_profiles[game.profile].sha256);
 else log_line("UNVERIFIED title %s; structural family=%u",game_title(),game.family);
 log_line("resolved imports=%08x..%08x ldd=%08x ready=%08x",game.begin,game.end,game.ldd,game.ready);
 uint8_t ready=0,constructed=0;
 if(!read_memory(NULL,game.ready,&ready,1)||ready!=1||!read_memory(NULL,game.ready-0x9c,&constructed,1)||constructed!=1) {
  log_line("load REFUSED LDD manager not ready; fresh game/main-menu attempt required");goto refused;
 }
 buzz_hub_init(&hub,&cfg);
 if(!prepare_imports()) { log_line("import plan REFUSED"); goto refused; }
 adapter.paused=is_stopped;
 adapter.may_attach=attach_preflight;
 buzz_server_set_stop(user_stopped);
 check_stop();
 if(buzz_server_start(&hub,cfg.port,0)) { log_line("phone listener failed step=%d errno=%d (1 socket, 2 bind, 3 listen, 4 nonblocking)",buzz_server_fail_step(),buzz_server_fail_errno()); goto refused; }
 buzz_server_set_hooks(enter,leave,NULL,network_trace,network_progress);
 if(!buzz_import_install(&imports,&memory_api)) {
  buzz_server_stop(); for(i=0;i<imports.count;i++) if(imports.patch[i].installed) { installed=1; return SYS_PRX_RESIDENT; }
  /* An error after any published slot can leave an in-flight wrapper even if
   * rollback restored every slot. Stay resident until process exit. */
  log_line("hook install failed; published=%u",(unsigned)atomic_load(&published));
  if(atomic_load(&published)) return SYS_PRX_RESIDENT;
  goto refused;
 }
 installed=1; atomic_store(&running,1);
 int32_t thread_result=sysThreadCreate(&diagnostic_id,diagnostics,NULL,DIAGNOSTIC_PRIORITY,65536,0,"ps3buzz_diag");
 if(!thread_result) thread_result=sysThreadCreate(&network_id,network,NULL,NETWORK_PRIORITY,65536,0,"ps3buzz_http");
 if(!thread_result) thread_result=sysThreadCreate(&worker_id,worker,NULL,WORKER_PRIORITY,65536,THREAD_JOINABLE,"ps3buzz_usb");
 if(thread_result) {
  log_line("service create failed result=%08x; quit game before retry",(unsigned)thread_result);
  /* Published hooks and any service already created stay valid until exit.
   * Do not close sockets concurrently with a partially started network loop. */
  return SYS_PRX_RESIDENT;
 }
 started=1; log_line("14 hooks installed; usb priority=1000 sleep=2000us; http priority=1010 sleep=10000us; diag priority=990 period=2000000us");
 log_line("clock=sys_time_get_system_time; heartbeat=/dev_hdd0/tmp/ps3buzz.heartbeat.log; stop-file polled 1Hz, latched");
 return SYS_PRX_RESIDENT;
refused:
#ifndef BUZZ_HOST_CHECK
 if(log_fd>=0) { (void)sysLv2FsClose(log_fd); log_fd=-1; }
#endif
 return SYS_PRX_NO_RESIDENT;
}
static void bootstrap(void *arg)
{
 (void)arg; (void)startup();
#ifndef BUZZ_HOST_CHECK
 /* Cobra starts module_start directly as a PPU thread, with no user CRT.
  * The raw thread API does not need process TLS/allocator cleanup. */
 lv2syscall1(41,0);
#endif
}
int module_start(size_t args,void *argp)
{
 /* Keep this frame small: Cobra 4.92 requests only 0x1000 bytes. The very
  * first external operation is open/write/fsync, before guards, formatting,
  * imports, config, networking or hook planning. /dev_hdd0/tmp must exist. */
 (void)args; (void)argp;
#ifndef BUZZ_HOST_CHECK
 static const char first[]="ps3buzz 0.9 ENTRY; libent=1c; game APP; before initialization\n";
 uint64_t written=0;
 (void)sysLv2FsOpen("/dev_hdd0/tmp/ps3buzz.log",SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&log_fd,0666,NULL,0);
 if(log_fd>=0) {
  (void)sysLv2FsWrite(log_fd,first,sizeof(first)-1,&written);
  (void)sysLv2FsFsync(log_fd); log_bytes+=(unsigned)written;
 }
#endif
 if(!atomic_exchange(&bootstrap_started,1)) {
  int32_t result=sysThreadCreate(&bootstrap_id,bootstrap,NULL,1000,65536,0,"ps3buzz_init");
  if(result) log_line("bootstrap create failed result=%08x",(unsigned)result);
 }
#ifndef BUZZ_HOST_CHECK
 /* A return instruction is not a thread-exit protocol. Cobra joins this
  * thread and ignores its exit code; it passes zero to PRX start phase 2. */
 lv2syscall1(41,0);
#endif
 return SYS_PRX_RESIDENT;
}
int module_stop(size_t args,void *argp)
{
 (void)args; (void)argp;
 int busy=installed || started || atomic_load(&published) ? 1 : 0;
 /* Cobra joins a bare stop thread but ignores its exit code. This code
  * cannot veto that loader's live unload; process exit is the only supported
  * undo. A raw exit also permits cleanup if initial module start failed. */
#ifndef BUZZ_HOST_CHECK
 lv2syscall1(41,(uint64_t)busy);
#endif
 return busy;
}
