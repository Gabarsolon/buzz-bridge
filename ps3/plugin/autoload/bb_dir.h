/* SPDX-License-Identifier: MIT. LV2 directory metadata never narrows file sizes. */
#ifndef BB_DIR_H
#define BB_DIR_H
#include <stdint.h>
typedef struct {int fd;char root[1024];} bb_dir;
typedef struct {
 char name[256],path[1024];
 uint64_t size;
 int directory; /* -1 if neither dirent nor stat could classify it */
 int details_known,path_known;
} bb_dir_entry;
int bb_dir_open(bb_dir *,const char *);
/* 1 entry, 0 EOF, -1 directory error (already logged with path and LV2 code). */
int bb_dir_read(bb_dir *,bb_dir_entry *);
void bb_dir_close(bb_dir *);
int bb_path_exists(const char *);
int bb_iso_name(const char *);
/* Unknown entries remain visible for diagnosis/navigation, including failed stat. */
int bb_dir_visible(const bb_dir_entry *);
void bb_dir_label(const bb_dir_entry *,char *,unsigned);
#endif
