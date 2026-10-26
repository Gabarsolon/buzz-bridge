/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_AUTOLOAD_LOGIC_H
#define BUZZ_AUTOLOAD_LOGIC_H
#include <stdint.h>
#include <stddef.h>
#define BUZZ_DIR "/dev_hdd0/plugins/ps3buzz"
#define BUZZ_HELPER BUZZ_DIR "/ps3buzz_vsh.sprx"
#define BUZZ_GAME BUZZ_DIR "/ps3buzz.sprx"
#define BUZZ_DISABLE BUZZ_DIR "/disable"
#define BUZZ_STOP "/dev_hdd0/tmp/ps3buzz.stop"
#define BUZZ_BOOT "/dev_hdd0/boot_plugins.txt"
#define BUZZ_SETTLE_US UINT64_C(15000000) /* 0.9 console log: runtime gates were met 10 s after load; the runtime readiness gate is the real protection. */
#define BUZZ_BOOT_MAX 16384
typedef struct { uint32_t pid; uint64_t ready_since; int waiting,attempted; } buzz_attempt;
int buzz_process_match(uint32_t pid,const char *name,const char *title);
int buzz_should_attempt(buzz_attempt *state,int ready,uint64_t now);
void buzz_mark_attempt(buzz_attempt *state);
uint32_t buzz_crc32(const unsigned char *data,size_t n);
/* Returns 1 changed, 0 unchanged, -1 invalid/capacity. Non-helper bytes survive. */
int buzz_boot_edit(const char *src,size_t n,int enable,char *dst,size_t cap,size_t *used);
int buzz_boot_has(const char *src,size_t n);
#endif
