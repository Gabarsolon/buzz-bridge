/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_AUTOLOAD_FILES_H
#define BUZZ_AUTOLOAD_FILES_H
#include <stddef.h>
/* -1 errors, 0 no change, 1 changed. Error description is for the app UI. */
int buzz_edit_boot_file(const char *path,int enable,char *error,size_t error_cap);
int buzz_file_read(const char *path,char *data,size_t cap,size_t *n,int *present);
int buzz_install(const char *source,const char *dest,char *error,size_t cap);
#endif
