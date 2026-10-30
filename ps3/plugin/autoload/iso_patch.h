/* SPDX-License-Identifier: MIT */
#ifndef BB_ISO_PATCH_H
#define BB_ISO_PATCH_H
#include "bb.h"
typedef struct {
 uint64_t image_size,offset;uint32_t length;int64_t mtime;
 char volume[33],title_id[20],sha1[41];int path_index,known,patched;
} bb_iso_info;
typedef struct {unsigned layout,flags,pairs,whole,shared,attach_ms,heartbeat_ms;} bb_iso_options;
int bb_iso_inspect(const char *path,bb_iso_info *info,char *error,size_t cap);
int bb_iso_patch(const char *path,const char *module,const bb_iso_options *options,int check_only,char *error,size_t cap);
int bb_iso_restore(const char *path,int check_only,char *error,size_t cap);
int bb_iso_config(unsigned char *module,size_t n,const bb_iso_options *options);
extern const char *const bb_setup_names[7];
extern const char *const bb_layout_names[14];
#endif
