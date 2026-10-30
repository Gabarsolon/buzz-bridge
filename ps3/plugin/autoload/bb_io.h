/* SPDX-License-Identifier: MIT. Explicit 64-bit ISO I/O (PPU libc is ILP32). */
#ifndef BB_IO_H
#define BB_IO_H
#include "bb.h"
#ifndef __lv2ppu__
#include <stdio.h>
#endif
typedef struct {
#ifdef __lv2ppu__
 int fd;
#else
 FILE *f;
#endif
 uint64_t size;int64_t mtime;
} bb_io;
int bb_io_open(bb_io *f,const char *path,int writable);
int bb_io_read(bb_io *f,uint64_t off,void *p,size_t n);
int bb_io_write(bb_io *f,uint64_t off,const void *p,size_t n);
int bb_io_sync(bb_io *f);
int bb_io_close(bb_io *f);
#endif
