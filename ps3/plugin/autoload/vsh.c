/* SPDX-License-Identifier: MIT. VSH resident detector; no game writes except 0x44. */
#ifdef BUZZ_VSH_HOST_CHECK
#include "../tests/vsh_mock.h"
#else
#include <sys/prx_module.h>
#include <sys/thread.h>
#include <sys/timer.h>
#include <sys/sys_time.h>
#include <sys/file.h>
#include <sys/process.h>
#include <io/pad.h>
#include "ps3_api.h"
#endif
#include <string.h>
#include <stdatomic.h>
#include <stdarg.h>
#include "logic.h"
#include "../core/format.h"
#include "../game/profile.h"
int buzz_vsnprintf(char *,size_t,const char *,va_list);
int __ps3dk_heap_ready;
static atomic_int running,started;
static sys_ppu_thread_t thread;
static int log_fd=-1;static unsigned log_bytes;
static uint32_t vsh_pid;
static buzz_attempt attempts[256];static unsigned attempt_count;
static uint32_t (*view_find)(const char *);
static uint32_t (*get_interface)(uint32_t,int);
static int (*running_mode)(void);
static uint32_t (*game_pid)(void);
static int exists(const char *path) {sysFSStat st;return sysLv2FsStat(path,&st)==0;}
static void log_line(const char *fmt,...)
{
 char line[256];va_list ap;va_start(ap,fmt);int n=buzz_vsnprintf(line,sizeof(line)-2,fmt,ap);va_end(ap);
 if(n<0)return;if(n>(int)sizeof(line)-3)n=sizeof(line)-3;line[n++]='\n';
 if(log_fd>=0 && log_bytes+(unsigned)n<=65536) {uint64_t wr=0;(void)sysLv2FsWrite(log_fd,line,n,&wr);log_bytes+=(unsigned)wr;}
}
#ifndef BUZZ_VSH_HOST_CHECK
static int safe_read(uint32_t addr,void *out,size_t n)
{
 if(addr<0x10000 || n>16384 || (uint64_t)addr+n>0x80000000u)return -1;
 return buzz_read(vsh_pid,addr,out,n);
}
static int valid_opd(uint32_t addr)
{
 uint32_t opd[2],instruction;
 return !(addr&3) && !safe_read(addr,opd,sizeof(opd)) && opd[0]>=0x10000 && !(opd[0]&3) && opd[1]>=0x10000 && !safe_read(opd[0],&instruction,4);
}
/* Same export ABI as webMAN. All pointer reads are bounded kernel-mediated copies. */
static uint32_t resolve(const char *lib,uint32_t nid)
{
 uint32_t base=0;
 if(safe_read(0x1008c,&base,4) || base>0x7ffff000u)return 0;
 for(unsigned t=0;t<512;t++) {
  uint32_t ptr;unsigned char h[28];char name[64]={0};
  if(safe_read(base+0x984+t*4,&ptr,4) || !ptr)return 0;
  if(safe_read(ptr,h,sizeof(h)))return 0;
  uint16_t count;uint32_t np,ids,fns;
  memcpy(&count,h+6,2);memcpy(&np,h+16,4);memcpy(&ids,h+20,4);memcpy(&fns,h+24,4);
  if(count>4096 || safe_read(np,name,sizeof(name)-1))continue;
  if(strcmp(name,lib))continue;
  /* At most 4096 fixed entries; batches avoid thousands of syscalls. */
  for(unsigned i=0;i<count;i+=64) {
   uint32_t batch[64];unsigned num=count-i<64?count-i:64;
   if(safe_read(ids+i*4,batch,num*4))return 0;
   for(unsigned j=0;j<num;j++) if(batch[j]==nid) {
    uint32_t opd;if(safe_read(fns+(i+j)*4,&opd,4) || !valid_opd(opd))return 0;return opd;
   }
  }
  return 0;
 }
 return 0;
}
static int title_now(char title[16],uint32_t *pid)
{
 unsigned char info[0x120]={0};uint32_t function=0;
 title[0]=0;*pid=0;
 if(!running_mode || !game_pid || !view_find || !get_interface || running_mode()==0)return 0;
 uint32_t view=view_find("game_plugin");if(!view)return 0;
 uint32_t iface=get_interface(view,1);if(!iface || safe_read(iface+8*4,&function,4) || !valid_opd(function))return 0;
 int (*game_info)(void *)=(void *)(uintptr_t)function;
 (void)game_info(info);memcpy(title,info+4,9);title[9]=0;*pid=game_pid();return *pid!=0;
}
static int modules(uint32_t pid,int *usb,int *loaded)
{
 int32_t ids[128]={0};*usb=*loaded=0;
 if(buzz_mapi(0x41,pid,buzz_ptr(ids),0,0))return 0;
 for(unsigned i=0;i<128;i++) if(ids[i]>0) {
  char name[64]={0},path[512]={0};
  if(buzz_mapi(0x42,pid,(uint32_t)ids[i],buzz_ptr(name),0) || buzz_mapi(0x43,pid,(uint32_t)ids[i],buzz_ptr(path),0))return 0;
  const char *leaf=strrchr(path,'/');leaf=leaf?leaf+1:path;
  if(!strcmp(name,"cellUsbd") || !strcmp(name,"libusbd") || !strcmp(leaf,"libusbd.sprx"))*usb=1;
  if(!strcmp(name,"ps3buzz") || !strcmp(leaf,"ps3buzz.sprx"))*loaded=1;
 }
 return 1;
}
static int fingerprints(uint32_t pid,const char *title)
{
 unsigned char buf[1024];
 const buzz_exact_profile *p=NULL;
 for(unsigned i=0;i<BUZZ_PROFILE_COUNT;i++)if(!strcmp(title,buzz_profiles[i].title))p=&buzz_profiles[i];
 if(!p)return 0;
 for(unsigned i=0;i<p->check_count;i++) {
  const buzz_fingerprint *f=&p->checks[i];
  if(f->size>sizeof(buf) || buzz_read(pid,f->address,buf,f->size) || buzz_crc32(buf,f->size)!=f->crc)return 0;
 }
 /* Same static record adopted by unmodified 0.7. Presence of libusbd is insufficient. */
 uint8_t ready=0;uint32_t record[4]={0};
 if(buzz_read(pid,p->ready,&ready,1) || ready!=1 || buzz_read(pid,p->ldd,record,sizeof(record)))return 0;
 return record[1]==p->opds[0] && record[2]==p->opds[1] && record[3]==p->opds[2];
}
static int helper_unique(void)
{
 /* Refuse injection if someone bypassed the app and loaded duplicate helpers. */
 unsigned count=0;int32_t ids[128]={0};
 if(buzz_mapi(0x41,vsh_pid,buzz_ptr(ids),0,0))return 0;
 for(unsigned i=0;i<128;i++)if(ids[i]>0) {
  char name[64]={0};if(buzz_mapi(0x42,vsh_pid,(uint32_t)ids[i],buzz_ptr(name),0))return 0;
  if(!strcmp(name,"ps3buzz_vsh"))count++;
 }
 return count==1;
}
#endif
static buzz_attempt *state_for(uint32_t pid)
{
 for(unsigned i=0;i<attempt_count;i++)if(attempts[i].pid==pid)return &attempts[i];
 if(attempt_count==sizeof(attempts)/sizeof(attempts[0]))return NULL;
 buzz_attempt *s=&attempts[attempt_count++];s->pid=pid;return s;
}
static void clear_settle(void)
{
 for(unsigned i=0;i<attempt_count;i++)attempts[i].waiting=0;
}
static int skip_pad(void)
{
 for(unsigned p=0;p<7;p++) {padData data;memset(&data,0,sizeof(data));if(!ioPadGetData(p,&data) && data.len>0 && (data.button[3]&PAD_CTRL_R2))return 1;}
 return 0;
}
static void worker(void *arg)
{
 (void)arg;int stopped=0;
 (void)sysLv2FsMkdir("/dev_hdd0/tmp",0777);
 (void)sysLv2FsOpen("/dev_hdd0/tmp/ps3buzz_vsh.log",SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&log_fd,0666,NULL,0);
 log_line("ps3buzz_vsh 0.3 startup; runtime=0.9; priority=1100 sleep=1s; R2 skip window=10s");
 /* Get our actual process ID; PS3MAPI enumeration below confirms the game PID. */
 vsh_pid=(uint32_t)sysProcessGetPid();
 if(!vsh_pid) {log_line("VSH PID unavailable; inert until reboot");stopped=1;}
 for(unsigned tick=0;tick<10 && atomic_load(&running);tick++) {
  sys_timer_usleep(1000000);
  if(skip_pad() || exists(BUZZ_STOP))stopped=1;
 }
 if(stopped)log_line("startup safety stop latched");
 if(!stopped) {
  view_find=(void *)(uintptr_t)resolve("paf",0xf21655f3);get_interface=(void *)(uintptr_t)resolve("paf",0x23afb290);
  running_mode=(void *)(uintptr_t)resolve("vshmain",0xeb757101);game_pid=(void *)(uintptr_t)resolve("vshmain",0x0624d3ae);
  log_line("VSH APIs find=%u interface=%u mode=%u pid=%u; notification disabled",!!view_find,!!get_interface,!!running_mode,!!game_pid);
 }
 uint32_t last_pid=0;int disabled_before=-1,post_load_log_only=0;unsigned passive_ticks=0;
 uint64_t last_wait_log=0;
 while(atomic_load(&running)) {
  sys_timer_usleep(1000000); /* Mandatory pacing, including all continue paths. */
  /* Terminal for this helper lifetime. In particular, do NOT query even the
   * title/PID after success: old code reached those before attempted checks.
   * No PS3MAPI, VSH interface, pad, network, notification or sentinel calls. */
  if(post_load_log_only) {
   if(++passive_ticks==5) {log_line("post-load passive pid=%08x; log only; reboot/helper restart for another game",last_pid);passive_ticks=0;}
   continue;
  }
  if(!stopped && exists(BUZZ_STOP)) {stopped=1;log_line("stop file latched; reboot to reset");}
  int disabled=exists(BUZZ_DISABLE)||stopped;
  if(disabled!=disabled_before) {log_line("injection disabled=%u",disabled);disabled_before=disabled;}
  char title[16]={0};uint32_t pid=0;
  int identified=!disabled && title_now(title,&pid);
  if(last_pid && (!identified || pid!=last_pid)) {
   buzz_attempt *old=state_for(last_pid);if(old)(void)buzz_should_attempt(old,0,0);last_pid=0;
  }
  if(!identified){clear_settle();continue;}
  uint32_t pids[16]={0};int present=0;
  if(buzz_mapi(0x21,buzz_ptr(pids),0,0,0)){clear_settle();continue;}
  for(unsigned i=0;i<16;i++)if(pids[i]==pid)present=1;
  if(!present || pid==vsh_pid){clear_settle();continue;}
  char name[64]={0};
  if(buzz_mapi(0x22,pid,buzz_ptr(name),0,0) || !buzz_process_match(pid,name,title)){clear_settle();continue;}
  buzz_attempt *s=state_for(pid);if(!s || s->attempted)continue;
  if(last_pid!=pid) {log_line("candidate pid=%08x name=%s title=%s profile=01.00",pid,name,title);last_pid=pid;last_wait_log=0;}
  int usb=0,loaded=0;int ready=modules(pid,&usb,&loaded);
  if(loaded) {buzz_mark_attempt(s);post_load_log_only=1;log_line("pid=%08x already loaded; no attempt; post-load passive entered",pid);continue;}
  ready=ready && usb && fingerprints(pid,title) && helper_unique();
  int was_waiting=s->waiting;
  uint64_t now=sys_time_get_system_time();
  int fire=buzz_should_attempt(s,ready,now);
  if(!was_waiting && s->waiting)log_line("pid=%08x libusbd + exact profile + LDD ready; settle=15s (guard band, not menu proof)",pid);
  if(was_waiting && !s->waiting)log_line("pid=%08x readiness lost; settle reset",pid);
  if(!last_wait_log || now-last_wait_log>=5000000) {
   log_line("waiting pid=%08x ready=%u stable=%us/15; runtime will independently gate presentation",pid,ready,
    s->waiting?(unsigned)((now-s->ready_since)/1000000):0);last_wait_log=now;
  }
  if(!fire)continue;
  /* Revalidate title/PID/files/module residency immediately before syscall. */
  char title2[16]={0};uint32_t pid2=0;
  if(exists(BUZZ_DISABLE)||exists(BUZZ_STOP)||!title_now(title2,&pid2)||pid2!=pid||strcmp(title,title2)||!modules(pid,&usb,&loaded)||!usb||loaded||!fingerprints(pid,title)||!helper_unique()) { (void)buzz_should_attempt(s,0,now);continue; }
  if(!exists(BUZZ_GAME)||!exists(BUZZ_DIR "/ps3buzz.cfg")) {log_line("plugin/config missing; no load");(void)buzz_should_attempt(s,0,now);continue;}
  buzz_mark_attempt(s);log_line("ATTEMPT pid=%08x path=" BUZZ_GAME " (never retry this pid)",pid);
  (void)sysLv2FsFsync(log_fd);
  int rc=buzz_mapi(0x44,pid,buzz_ptr(BUZZ_GAME),0,0);
  /* A failed start may leave ambiguous residency too. Park on every result. */
  post_load_log_only=1;
  log_line("LOAD result=%08x pid=%08x",(unsigned)rc,pid);
  if(!rc)log_line("post-load passive entered; no further process/VSH/network/UI calls; loader acceptance only");
  else log_line("load failed; passive entered; no further process/VSH/network/UI calls; preserve logs and reboot");
 }
 log_line("helper thread stopped");if(log_fd>=0){(void)sysLv2FsClose(log_fd);log_fd=-1;}
 buzz_exit_thread();
}
int module_start(size_t args,void *argp)
{
 (void)args;(void)argp;
 if(!atomic_exchange(&started,1)) {atomic_store(&running,1);if(sysThreadCreate(&thread,worker,NULL,1100,65536,THREAD_JOINABLE,"ps3buzz_vsh"))atomic_store(&running,0);}
 buzz_exit_thread();return 0;
}
int module_stop(size_t args,void *argp)
{
 (void)args;(void)argp;int active=atomic_exchange(&running,0);uint64_t status=0;
 if(active)(void)sysThreadJoin(thread,&status);buzz_exit_thread();return 0;
}
