/* SPDX-License-Identifier: MIT. Declarations ONLY for PC syntax checking.
 * These do not implement, link or test any PS3 service. */
#ifndef BUZZ_HOST_PS3_H
#define BUZZ_HOST_PS3_H
#include <stddef.h>
#include <stdint.h>
#define SYS_PRX_RESIDENT 0
#define SYS_PRX_NO_RESIDENT 1
#define THREAD_JOINABLE 1
#define PAD_BUTTON_OFFSET_DIGITAL1 2
#define PAD_BUTTON_OFFSET_DIGITAL2 3
typedef uint64_t sys_ppu_thread_t;
typedef struct { int32_t len; uint16_t button[64]; } padData;
typedef struct { uint32_t max_connect,now_connect,system_info,port_status[7],port_setting[7],device_capability[7],device_type[7]; } padInfo2;
typedef struct { uint8_t motor[2],reserved[6]; } padActParam;
int ioPadGetInfo2(padInfo2 *);
int ioPadSetActDirect(unsigned,padActParam *);
int sysThreadCreate(sys_ppu_thread_t *,void (*)(void *),void *,int,uint64_t,uint64_t,char *);
int sysThreadJoin(sys_ppu_thread_t,uint64_t *);
int sys_timer_usleep(uint64_t);
uint64_t buzz_host_time_us(void);
void buzz_host_log_line(const char *);
int buzz_host_stop_requested(void);
int buzz_host_manager_available(void);
#endif
