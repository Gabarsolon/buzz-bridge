/* SPDX-License-Identifier: MIT. Execute the actual helper worker, no PS3 APIs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BUZZ_VSH_HOST_CHECK 1
#include "../autoload/vsh.c"
static uint64_t clock_us,loaded_at;
static unsigned forbidden,loads,wait_logs,passive_logs,resets;
static int load_result,already_installed;
static void api(void) {if(loaded_at)forbidden++;}
static int sysLv2FsStat(const char *p,sysFSStat *st)
{(void)st;api();return strcmp(p,BUZZ_GAME) && strcmp(p,BUZZ_DIR "/ps3buzz.cfg")?-1:0;}
static int sysLv2FsWrite(int fd,const void *p,size_t n,uint64_t *wr)
{
 (void)fd;*wr=n;fwrite(p,1,n,stdout);
 if(n>=8 && !memcmp(p,"waiting ",8))wait_logs++;
 if(n>=17 && !memcmp(p,"post-load passive",17))passive_logs++;
 return 0;
}
static int sysLv2FsOpen(const char *p,int flags,int *fd,int mode,void *arg,int size)
{(void)p;(void)flags;(void)mode;(void)arg;(void)size;api();*fd=1;return 0;}
static int sysLv2FsClose(int fd) {(void)fd;return 0;}
static int sysLv2FsFsync(int fd) {(void)fd;api();return 0;}
static int sysLv2FsMkdir(const char *p,int mode) {(void)p;(void)mode;api();return 0;}
static int sys_timer_usleep(uint64_t n)
{clock_us+=n;if(loaded_at && clock_us-loaded_at>=16000000)atomic_store(&running,0);return 0;}
static uint64_t sys_time_get_system_time(void) {api();return clock_us;}
static uint32_t sysProcessGetPid(void) {api();return 0x1000300;}
static int ioPadGetData(unsigned port,padData *data) {(void)port;api();data->len=0;return -1;}
static int sysThreadCreate(sys_ppu_thread_t *id,void (*fn)(void *),void *arg,int prio,uint64_t stack,uint64_t flags,char *name)
{(void)id;(void)fn;(void)arg;(void)prio;(void)stack;(void)flags;(void)name;abort();}
static int sysThreadJoin(sys_ppu_thread_t id,uint64_t *status) {(void)id;(void)status;abort();}
static void buzz_exit_thread(void) {}
static uintptr_t buzz_ptr(const void *p) {return (uintptr_t)p;}
static int buzz_mapi(unsigned op,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
 (void)c;(void)d;api();
 if(op==0x21) {((uint32_t *)a)[0]=0x01050200;return 0;}
 if(op==0x22) {strcpy((char *)b,"01050200_main_EBOOT.BIN");return 0;}
 if(op==0x44) {loads++;loaded_at=clock_us;return load_result;}
 abort();
}
static uintptr_t resolve(const char *lib,uint32_t nid) {(void)lib;(void)nid;api();return 1;}
static int title_now(char *title,uint32_t *pid) {api();strcpy(title,"BCES00303");*pid=0x01050200;return 1;}
static int modules(uint32_t pid,int *usb,int *loaded)
{(void)pid;api();*usb=1;*loaded=already_installed;if(*loaded)loaded_at=clock_us;return 1;}
static int fingerprints(uint32_t pid,const char *title)
{(void)pid;(void)title;api();if(clock_us>=15000000 && !resets) {resets++;return 0;}return 1;}
static int helper_unique(void) {api();return 1;}
int main(int argc,char **argv)
{
 if(argc==2 && !strcmp(argv[1],"failure"))load_result=1;
 if(argc==2 && !strcmp(argv[1],"resident"))already_installed=1;
 atomic_store(&running,1);worker(NULL);
 if(forbidden || passive_logs<3)return 1;
 if(already_installed) {if(loads || wait_logs) return 2;}
 else if(loads!=1 || !resets || loaded_at<30000000 || wait_logs<5)return 3;
 puts("PASS: actual VSH worker: readiness reset/15s/progress; one attempt or resident reuse; 16s passive after success/failure/residency with ZERO process/VSH/pad/network/sentinel/clock API calls");
 return 0;
}
