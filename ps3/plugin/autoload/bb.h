/* SPDX-License-Identifier: MIT */
#ifndef BB_COMMON_H
#define BB_COMMON_H
#include <stdint.h>
#include <stddef.h>
#define BB_MAX_FILE (32u*1024*1024)
uint32_t bb_be32(const void *p);
void bb_put32(void *p,uint32_t v);
void bb_put64(void *p,uint64_t v);
void bb_sha1(const void *p,size_t n,unsigned char out[20]);
void bb_sha256(const void *p,size_t n,unsigned char out[32]);
void bb_hex(const void *p,size_t n,char *out);
int bb_hash_is(const void *p,size_t n,const char *hex);
int bb_read(const char *path,unsigned char **data,size_t *n,size_t max);
int bb_save(const char *path,const void *data,size_t n,int exclusive);
int bb_file_hash(const char *path,char out[65]);
int bb_error(char *error,size_t cap,const char *text);
/* Called between bounded sections; renderer/watchdog belongs to the caller. */
typedef void (*bb_progress)(const char *stage);
extern bb_progress bb_tick;
#endif
