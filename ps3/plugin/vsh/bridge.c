/* SPDX-License-Identifier: MIT */
#include "bridge.h"
/* A firmware port must replace this implementation after review. No unassigned
 * syscall or arbitrary syscall8 sub-opcode is invoked by this Phase 1 build. */
int buzz_bridge_exchange(const uint8_t request[32],uint8_t reply[32])
{ (void)request; (void)reply; return -1; }
