/* SPDX-License-Identifier: MIT. Run the real service loops with PC-only mocks.
 * No PPU instruction execution, game code, console access or GUI operations. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include "../game/native.c"
static HANDLE paused,resume_worker;
static const char *blocked_step;
static atomic_int mock_stop;
static atomic_int locks_intentionally_held;
static atomic_uint heartbeats_while_locked;
static uint64_t fake_time;
uint64_t buzz_host_time_us(void) { return fake_time?fake_time:GetTickCount64()*1000; }
int buzz_host_stop_requested(void) { return atomic_load(&mock_stop); }
int buzz_host_manager_available(void) { return 1; }
void buzz_host_log_line(const char *line)
{
 if(!strncmp(line,"heartbeat ",10) && atomic_load(&locks_intentionally_held)) atomic_fetch_add(&heartbeats_while_locked,1);
 fputs(line,stdout); fflush(stdout);
}
int sys_timer_usleep(uint64_t us) { Sleep((DWORD)((us+999)/1000)); return 0; }
int ioPadGetInfo2(padInfo2 *p) { (void)p; abort(); }
int ioPadSetActDirect(unsigned port,padActParam *p) { (void)port; (void)p; return 0; }
int sysThreadCreate(sys_ppu_thread_t *id,void (*fn)(void *),void *arg,int prio,uint64_t stack,uint64_t flags,char *name)
{ (void)id; (void)fn; (void)arg; (void)prio; (void)stack; (void)flags; (void)name; return -1; }
static void block_at(const char *step)
{
 if(!strcmp(blocked_step,step) || (!strcmp(blocked_step,"attach-timeout") && !strcmp(step,"attach-call"))) {
  SetEvent(paused); (void)WaitForSingleObject(resume_worker,10000);
 }
}
static int32_t location(int32_t dev,uint8_t *p)
{ (void)dev; (void)p; abort(); } /* Virtual IDs must never reach real libusbd. */
static void test_progress(const char *step)
{
 worker_progress(step);
 /* Local lookup no longer has a foreign export. Stall its instrumentation
  * boundary to retain the independent-service liveness test. */
 if(!strcmp(step,"lookup-call")) block_at(step);
}
typedef struct { int32_t dev,ep0,in; uint8_t product[128],input[8]; unsigned stage; } mock_device;
static mock_device devices[2];
static atomic_uint mock_ready;
static void replay_input(int32_t result,int32_t count,void *arg)
{
 mock_device *g=arg;
 if(result || count!=5 || g->input[0]!=127 || g->input[1]!=127 || g->input[4]!=240) abort();
 if(hooked_interrupt(g->in,g->input,8,replay_input,g)) abort();
}
static void replay_setup(int32_t result,int32_t count,void *arg)
{
 mock_device *g=arg; buzz_setup s={0,9,1,0,0};
 if(result || (g->stage==0 ? count!=64 : count!=0)) abort();
 if(g->stage++==0) { if(hooked_control(g->ep0,&s,NULL,replay_setup,g)) abort(); }
 else if(g->stage==2) { s.type=1; s.request=11; s.value=0; if(hooked_control(g->ep0,&s,NULL,replay_setup,g)) abort(); }
 else {
  if(hooked_interrupt(g->in,g->input,8,replay_input,g)) abort();
  atomic_fetch_add(&mock_ready,1);
 }
}
static int32_t probe(int32_t dev)
{
 block_at("probe-call");
 if(!strcmp(blocked_step,"console-replay")) {
  uint8_t *desc=hooked_scan(dev,NULL,1);
  if(!desc || desc[8]!=0x4c || desc[9]!=5 || desc[10]!=2) abort();
 }
 return 0;
}
static int32_t attach(int32_t dev)
{
 block_at("attach-call");
 if(!strcmp(blocked_step,"console-replay")) {
  mock_device *g=&devices[dev-BZ_CELL_DEVICE_BASE]; uint8_t loc[7];
  buzz_setup s={0x80,6,0x301,0x409,128}; g->dev=dev;
  if(hooked_location(dev,loc)) abort();
  g->ep0=hooked_open(dev,NULL); g->in=hooked_open(dev,hooked_scan(dev,NULL,5));
  if(g->ep0<0 || g->in<0 || hooked_set(dev,g) || hooked_get(dev)!=g) abort();
  if(hooked_control(g->ep0,&s,g->product,replay_setup,g)) abort();
 }
 return 0;
}
static int32_t detach(int32_t dev) { (void)dev; return 0; }
static DWORD WINAPI usb_entry(void *arg) { worker(arg); return 0; }
static DWORD WINAPI net_entry(void *arg) { network(arg); return 0; }
static DWORD WINAPI diag_entry(void *arg) { diagnostics(arg); return 0; }
static int32_t pad_result;
static int32_t pad_data(uint32_t port,padData *data) { (void)port; (void)data; return pad_result; }
static void test_pad_tee(void)
{
 uint64_t now=microseconds(); padData data={0}; data.len=4; data.button[3]=BP_R1>>8;
 original_pad_data=pad_data;
 if(hooked_pad_data(0,&data)) abort();
 poll_pad_snapshot(now);
 if(!hub.pad_connected[0] || hub.slots[0].kind!=BUZZ_PAD || hub.slots[0].buttons!=1) abort();
 data.len=0; if(hooked_pad_data(0,&data)) abort(); poll_pad_snapshot(now+10000);
 if(hub.slots[0].buttons!=1) abort(); /* no change preserves held state */
 poll_pad_snapshot(now+260001);
 if(hub.pad_connected[0] || hub.slots[0].kind!=BUZZ_FREE) abort();
 pad_result=0; data.len=4; data.button[3]=0x80; /* console square mask 32768 */
 if(hooked_pad_data(0,&data)) abort(); poll_pad_snapshot(now+290000);
 uint8_t report[5]; buzz_report(&hub,0,report);
 if(report[2]!=8) abort();
 data.button[3]=0x10; /* console triangle mask 4096 */
 if(hooked_pad_data(0,&data)) abort(); poll_pad_snapshot(now+300000); buzz_report(&hub,0,report);
 if(report[2]!=16) abort();
 data.button[3]=0; if(hooked_pad_data(0,&data)) abort(); poll_pad_snapshot(now+310000);
 buzz_report(&hub,0,report); if(report[2]) abort();
 puts("PC pad tee: console square/triangle/release -> virtual report bytes passed");
 data.len=4; if(hooked_pad_data(0,&data)) abort(); poll_pad_snapshot(now+320000);
 pad_result=(int32_t)0x80121107u;
 if(hooked_pad_data(0,&data)!=pad_result) abort(); poll_pad_snapshot(now+330000);
 if(hub.pad_connected[0] || hub.slots[0].kind!=BUZZ_FREE) abort();
 puts("PC pad tee: held/no-change, stale-read release and NO_DEVICE passed");
}
static void test_guard(void)
{
 loop_guard g={0}; fake_time=1000000;
 for(unsigned i=0;i<1000;i++) guard_loop(&g,1);
 if(atomic_load(&cpu_guard_hits)) abort();
 uint64_t before=GetTickCount64(); guard_loop(&g,1); guard_loop(&g,1);
 if(GetTickCount64()-before<190 || atomic_load(&cpu_guard_hits)!=1 || !g.reported) abort();
 fake_time+=1000000; guard_loop(&g,1);
 if(g.count!=1 || !g.reported) abort();
 fake_time=0; atomic_store(&cpu_guard_hits,0);
 puts("PC CPU guard: >1000/s throttles repeatedly, once-only report bit, window reset passed");
}
static void test_presentation_policy(void)
{
 buzz_presentation g;uint64_t now=1000000;unsigned polls=0;
 buzz_presentation_init(&g,now,0);
 for(unsigned i=0;i<400;i++) {now+=100000; (void)buzz_presentation_tick(&g,now,0,1);}
 if(g.allowed || g.good)abort(); /* elapsed time alone never presents. */
 buzz_presentation_init(&g,now,0);
 for(unsigned i=0;i<99;i++) {now+=100000;polls++;(void)buzz_presentation_tick(&g,now,polls,1);if(g.allowed)abort();}
 now+=100000;polls++;(void)buzz_presentation_tick(&g,now,polls,1);
 if(!g.allowed || g.good!=10)abort();
 /* One unavailable mutex sample invalidates the whole window. */
 now+=100000;polls++;(void)buzz_presentation_tick(&g,now,polls,0);
 for(unsigned i=0;i<9;i++) {now+=100000;polls++;(void)buzz_presentation_tick(&g,now,polls,1);}
 if(g.allowed || g.good)abort();
 for(unsigned i=0;i<100;i++) {now+=100000;polls++;(void)buzz_presentation_tick(&g,now,polls,1);}
 if(!g.allowed)abort();
 now+=2000000;(void)buzz_presentation_tick(&g,now,polls+1000,1);
 if(g.allowed || g.good)abort(); /* a stall/burst earns no frames. */
 (void)buzz_presentation_tick(&g,now-1,polls,1);if(g.allowed)abort();
 buzz_presentation_init(&g,now,0);polls=0;
 for(unsigned i=0;i<400;i++) {now+=100000;if(i%10==0)polls+=1000;(void)buzz_presentation_tick(&g,now,polls,1);}
 if(g.allowed || g.good)abort(); /* huge one-bucket bursts are insufficient. */
 /* A callback returning after the deadline must quarantine without waiting
  * for diagnostics, including a uint32 millisecond clock rollover. */
 fake_time=(uint64_t)(UINT32_MAX-1000u)*1000;
 trace(BZ_TRACE_ATTACH_CALL,BZ_CELL_DEVICE_BASE,0,0);
 fake_time+=3000000;worker_progress("attach-return");
 if(atomic_load(&attach_fault)!=1)abort();
 atomic_store(&attach_fault,0);atomic_store(&attach_active,0);fake_time=0;
 /* Remove that isolated watchdog trace before the service tests. */
 trace_head=trace_count=0;
 puts("PC presentation: no polls/bursts/stalls refuse; sustained frames+minimum age; failed mutex resets; late return/clock rollover quarantines");
}
int main(int argc,char **argv)
{
 buzz_config cfg; buzz_cell_real real={0}; buzz_cell_sync sync={enter,leave,NULL};
 buzz_cell_ldd ldd={"PC-only",probe,attach,detach}; HANDLE threads[3];
 if(argc!=3) return 2;
 test_guard();
 test_presentation_policy();
 if(!strcmp(argv[2],"historical-replay")) {
  unsigned t,usb,http,last_usb=0,last_http=0,rows=0,noticed=0;uint64_t first=0;
  buzz_presentation g={0};
  while(scanf("%u %u %u",&t,&usb,&http)==3) {
   fake_time=(uint64_t)t*1000000;
   if(!rows) {first=fake_time;trace(BZ_TRACE_ATTACH_CALL,BZ_CELL_DEVICE_BASE,0,0);buzz_presentation_init(&g,0,0);}
   else if(usb!=last_usb || http<=last_http)abort();
   callback_watch(atomic_load(&attach_active),(uint32_t)(fake_time/1000));
   /* No polling measurement exists in 0.8 logs. The new policy cannot credit
    * an unobserved signal or gaps between these two-second samples. */
   (void)buzz_presentation_tick(&g,fake_time,0,1);
   if(g.allowed)abort();
   if(atomic_load(&attach_fault) && !noticed) {
    printf("historical replay: attach-timeout once at observation t=%u; no second dongle\n",t);noticed=1;
   }
   last_usb=usb;last_http=http;rows++;
  }
  if(rows<50 || !noticed || atomic_load(&attach_fault)!=1 || fake_time-first<100000000)return 15;
  puts("PASS: supplied stuck-worker/advancing-HTTP observations replayed through actual watchdog; unknown polling signal earns no presentation");
  return 0;
 }
 blocked_step=argv[2]; paused=CreateEvent(NULL,TRUE,FALSE,NULL); resume_worker=CreateEvent(NULL,TRUE,FALSE,NULL);
 buzz_defaults(&cfg); cfg.enabled=1; buzz_hub_init(&hub,&cfg); real.location=location;
 test_pad_tee();
 buzz_cell_construct(&adapter,&hub,&real,&sync); adapter.trace=trace; adapter.progress=test_progress;
 adapter.paused=is_stopped;
 adapter.now_us=microseconds();
 if(buzz_cell_adopt(&adapter,&ldd,0x54c,2,0x42) || buzz_server_start(&hub,(unsigned)atoi(argv[1]),1)) return 3;
 buzz_server_set_hooks(enter,leave,NULL,network_trace,network_progress); atomic_store(&running,1);
 buzz_server_set_stop(user_stopped);
 if(!strcmp(blocked_step,"safety-start")) { atomic_store(&mock_stop,1); check_stop(); }
 if(!strcmp(blocked_step,"locks-held")) {
  (void)atomic_flag_test_and_set(&mutex); (void)atomic_flag_test_and_set(&log_lock); (void)atomic_flag_test_and_set(&log_writer);
  atomic_store(&locks_intentionally_held,1);
 }
 threads[0]=CreateThread(NULL,0,usb_entry,NULL,0,NULL);
 threads[1]=CreateThread(NULL,0,net_entry,NULL,0,NULL);
 threads[2]=CreateThread(NULL,0,diag_entry,NULL,0,NULL);
 if(!strcmp(blocked_step,"console-replay")) {
  Sleep(350);
  if(atomic_load(&mock_ready)!=2 || !buzz_cell_ready(&adapter)) return 12;
  puts("PC console replay attached: actual hook wrappers -> deferred callbacks -> two dongles ready"); fflush(stdout);
  Sleep(2400);
 } else if(!strcmp(blocked_step,"locks-held")) {
  puts("PC all shared plugin locks deliberately held"); fflush(stdout); Sleep(2400);
  if(atomic_load(&heartbeats_while_locked)<2) return 11;
  atomic_store(&locks_intentionally_held,0);
  atomic_flag_clear(&mutex); atomic_flag_clear(&log_lock); atomic_flag_clear(&log_writer);
 } else if(!strcmp(blocked_step,"safety-start")) {
  Sleep(2400);
  if(!atomic_load(&safety_stopped) || atomic_load(&worker_loops) || atomic_load(&network_loops) || adapter.callbacks) return 10;
  puts("PC safety start: existing sentinel prevents USB and HTTP work from first iteration"); fflush(stdout);
 } else if(!strcmp(blocked_step,"safety-stop")) {
  Sleep(300); atomic_store(&mock_stop,1); Sleep(1400);
  if(!atomic_load(&safety_stopped) || strcmp(atomic_load(&worker_state),"safety-stopped") ||
     strcmp(atomic_load(&network_state),"safety-stopped")) return 7;
  unsigned usb=atomic_load(&worker_loops),net=atomic_load(&network_loops);
  atomic_store(&mock_stop,0); Sleep(1100);
  if(!atomic_load(&safety_stopped) || usb!=atomic_load(&worker_loops) || net!=atomic_load(&network_loops)) return 8;
  padData data={0}; data.len=4; unsigned observed=pad_observed[0]; pad_result=0;
  if(hooked_pad_data(0,&data) || pad_observed[0]!=observed || hooked_open(BZ_CELL_DEVICE_BASE,NULL)!=BZ_CELL_DEVICE ||
     hooked_interrupt(BZ_CELL_PIPE_BASE,NULL,5,NULL,NULL)!=BZ_CELL_DEVICE) return 9;
  puts("PC safety stop: observed within 1.4s, work counters frozen, hooks bypassed, removal stays latched"); fflush(stdout);
 } else {
  if(WaitForSingleObject(paused,3000)!=WAIT_OBJECT_0) return 4;
  printf("PC worker deliberately blocked at %s\n",blocked_step); fflush(stdout);
  if(!strcmp(blocked_step,"attach-timeout")) {
   Sleep(4400);
   if(atomic_load(&attach_fault)!=1 || adapter.owner[1] || !atomic_load(&network_loops))return 13;
   SetEvent(resume_worker);Sleep(250);
   if(adapter.callbacks || adapter.attached_mask || adapter.owner[1] || buzz_cell_ready(&adapter))return 14;
   puts("PC attach-timeout quarantined; late return suppressed; second dongle absent; HTTP continues");fflush(stdout);Sleep(500);
  } else {
   Sleep(2400);
   if(strcmp(atomic_load(&worker_state),blocked_step) || !atomic_load(&network_loops)) return 5;
  }
 }
 atomic_store(&running,0); SetEvent(resume_worker);
 if(WaitForMultipleObjects(3,threads,TRUE,3000)!=WAIT_OBJECT_0) return 6;
 /* Service diagnostics drains only 16 entries / 2 s. Finish the captured
  * startup trace after joining; no live service or game lock is involved. */
 if(!strcmp(blocked_step,"console-replay") || !strcmp(blocked_step,"attach-timeout")) for(unsigned i=0;i<4;i++) flush_trace();
 for(unsigned i=0;i<3;i++) CloseHandle(threads[i]);
 CloseHandle(paused); CloseHandle(resume_worker); return 0;
}
