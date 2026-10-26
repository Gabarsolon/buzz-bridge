/* SPDX-License-Identifier: MIT. Local transport harness; no emulation or PS3 access. */
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned lock_depth;
static void check(int ok) { if(!ok) { fputs("server lock/observer contract failed\n",stderr); exit(2); } }
static void enter(void *arg) { (void)arg; check(!lock_depth); lock_depth=1; }
static void leave(void *arg) { (void)arg; check(lock_depth==1); lock_depth=0; }
static void progress(const char *state) { check(!lock_depth && state && *state); }
static void trace(const char *event,int fd,int result,const char *detail)
{
 (void)fd; (void)result; check(!lock_depth && event && detail);
 if(!strcmp(event,"request")) { printf("request %s\n",detail); fflush(stdout); }
}
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static uint64_t now_ms(void) { return GetTickCount64(); }
static void pause_tick(void) { Sleep(2); }
#else
#include <time.h>
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000; }
static void pause_tick(void) { struct timespec t={0,2000000}; nanosleep(&t,NULL); }
#endif
int main(int argc,char **argv)
{
 buzz_config config; buzz_hub hub; uint64_t start; unsigned port=argc>1?(unsigned)atoi(argv[1]):18420;
 buzz_defaults(&config); config.port=port; buzz_hub_init(&hub,&config);
 if(buzz_server_start(&hub,port,1)) { fputs("listen failed\n",stderr); return 1; }
 buzz_server_set_hooks(enter,leave,NULL,trace,progress);
 printf("PC harness: loopback:%u, USB backend unavailable\n",port); fflush(stdout); start=now_ms();
 while(now_ms()-start<30000) { buzz_server_tick(now_ms(),0); pause_tick(); }
 buzz_server_stop(); return 0;
}
