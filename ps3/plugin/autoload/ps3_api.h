/* SPDX-License-Identifier: MIT. Binary Cobra API, no network transport. */
#ifndef BUZZ_PS3_API_H
#define BUZZ_PS3_API_H
#include <stdint.h>
#include <stddef.h>
#include <ppu-lv2.h>
static inline int buzz_mapi(unsigned op,uint64_t a,uint64_t b,uint64_t c,uint64_t d)
{
 lv2syscall6(8,0x7777,op,a,b,c,d);return_to_user_prog(int);
}
static inline uint64_t buzz_ptr(const void *p) {return (uint64_t)(uintptr_t)p;}
static inline int buzz_is_hen(void)
{
 lv2syscall1(8,0x1337);return register_passing_1(uint64_t)==0x1337;
}
static inline int buzz_read(uint32_t pid,uint32_t addr,void *dst,size_t n)
{ return buzz_mapi(0x31,pid,addr,buzz_ptr(dst),n); }
static inline int buzz_load_vsh(unsigned slot,const char *path)
{
 lv2syscall5(8,0x1ee7,slot,buzz_ptr(path),0,0);return_to_user_prog(int);
}
static inline void buzz_exit_thread(void) {lv2syscall1(41,0);__builtin_unreachable();}
#endif
