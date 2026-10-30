/* SPDX-License-Identifier: MIT */
#ifndef APP_HEALTH_H
#define APP_HEALTH_H
#include <stdatomic.h>
#include <stdint.h>
#define APP_TICK_US 40000u                      /* idle loop period: 25 Hz */
#define APP_HEARTBEAT_US 5000000u               /* black-box line every 5 s */
#define APP_IDLE_EXIT_US UINT64_C(900000000)    /* 15 min without input at a menu or dialog */
#define APP_LOG_LIMIT (4u*1024u*1024u)          /* cap for each log file */
/* App-owned resources, counted explicitly where they are opened/closed. */
extern atomic_int app_files,app_dirs,app_sockets;
/* Logs counters, memory and flip statistics as one line in the main log. */
void app_health_snapshot(const char *label);
#endif
