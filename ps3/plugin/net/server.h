/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_SERVER_H
#define BUZZ_SERVER_H
#include "buzz.h"
/* One network owner; optional hub locks cover only in-memory hub operations.
 * No socket, formatting, observer or game callback runs under the hub lock. */
int buzz_server_start(buzz_hub *hub,unsigned port,int loopback_only);
/* After a failed start: 1 socket, 2 bind, 3 listen, 4 nonblocking; errno from the network library. */
int buzz_server_fail_step(void);
int buzz_server_fail_errno(void);
void buzz_server_set_hooks(void (*lock)(void *),void (*unlock)(void *),void *arg,
 void (*trace)(const char *event,int fd,int result,const char *detail),void (*progress)(const char *state));
void buzz_server_tick(uint64_t now_ms,int usb_backend_ready);
void buzz_server_stop(void);
void buzz_server_set_stop(int (*fn)(void));
#endif
