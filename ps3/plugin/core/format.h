/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_FORMAT_H
#define BUZZ_FORMAT_H
#include <stddef.h>
int buzz_snprintf(char *,size_t,const char *,...);
#if defined(BUZZ_PS3) || defined(BUZZ_NATIVE_FORMAT)
#define snprintf buzz_snprintf
#endif
#endif
