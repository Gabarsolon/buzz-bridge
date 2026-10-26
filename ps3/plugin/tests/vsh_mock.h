/* SPDX-License-Identifier: MIT. Host-only declarations for real helper loop. */
#include <stdint.h>
#include <stddef.h>
#define THREAD_JOINABLE 1
#define SYS_O_WRONLY 1
#define SYS_O_CREAT 2
#define SYS_O_TRUNC 4
#define PAD_CTRL_R2 2
typedef uint64_t sys_ppu_thread_t;
typedef struct {int dummy;} sysFSStat;
typedef struct {int len;unsigned button[64];} padData;
static int sysLv2FsStat(const char *,sysFSStat *);
static int sysLv2FsWrite(int,const void *,size_t,uint64_t *);
static int sysLv2FsOpen(const char *,int,int *,int,void *,int);
static int sysLv2FsClose(int);
static int sysLv2FsFsync(int);
static int sysLv2FsMkdir(const char *,int);
static int sys_timer_usleep(uint64_t);
static uint64_t sys_time_get_system_time(void);
static uint32_t sysProcessGetPid(void);
static int ioPadGetData(unsigned,padData *);
static int sysThreadCreate(sys_ppu_thread_t *,void (*)(void *),void *,int,uint64_t,uint64_t,char *);
static int sysThreadJoin(sys_ppu_thread_t,uint64_t *);
static void buzz_exit_thread(void);
static uintptr_t buzz_ptr(const void *);
static int buzz_mapi(unsigned,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t resolve(const char *,uint32_t);
static int title_now(char *,uint32_t *);
static int modules(uint32_t,int *,int *);
static int fingerprints(uint32_t,const char *);
static int helper_unique(void);
