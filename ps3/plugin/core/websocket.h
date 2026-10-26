/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_WEBSOCKET_H
#define BUZZ_WEBSOCKET_H
#include <stddef.h>
#include <stdint.h>
#define BUZZ_WS_MAX 512
typedef struct { uint8_t opcode; size_t len; uint8_t data[BUZZ_WS_MAX+1]; } buzz_ws_frame;
/* Client frames must be masked; server output is unmasked. 0=incomplete, -1=invalid. */
int buzz_ws_decode(const uint8_t *in,size_t len,buzz_ws_frame *frame);
size_t buzz_ws_encode(uint8_t opcode,const void *data,size_t len,uint8_t *out,size_t cap);
int buzz_ws_accept(const char *key,char out[29]);
/* 1=valid upgrade; 0=incomplete; -1=bad. Header bytes returned in consumed.
 * Same-origin browser upgrade only. Non-browser clients may omit Origin. */
int buzz_http_upgrade(const char *header,size_t len,char accept[29],size_t *consumed);
#endif
