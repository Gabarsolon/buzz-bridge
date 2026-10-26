/* SPDX-License-Identifier: MIT. PS3 import-slot plan, independent of host ABI. */
#ifndef BUZZ_IMPORT_HOOK_H
#define BUZZ_IMPORT_HOOK_H
#include <stddef.h>
#include <stdint.h>
#define BUZZ_IMPORT_MAX 14
typedef struct {
 void *arg;
 int (*read)(void *,uint32_t,void *,size_t);
 /* Compare-and-swap one aligned BE PPU word, with publication barriers. */
 int (*replace)(void *,uint32_t,uint32_t,uint32_t);
 int (*valid_opd)(void *,uint32_t);
} buzz_import_memory;
typedef struct { uint32_t nid,replacement,slot,original; unsigned installed; } buzz_import_patch;
typedef struct { buzz_import_patch patch[BUZZ_IMPORT_MAX]; unsigned count; } buzz_import_plan;
/* Caller supplies a validated mapped .lib.stub interval, and desired NIDs/OPDs.
 * The plan accepts 0x2c-byte function-only import headers and refuses duplicates,
 * missing imports, invalid descriptors and truncated tables. No writes here. */
int buzz_import_discover(buzz_import_plan *,const buzz_import_memory *,uint32_t begin,uint32_t end);
/* Failed install rolls back its owned slots. A failed rollback stays installed
 * in the plan, so the caller must remain resident. Never overwrite another hook. */
int buzz_import_install(buzz_import_plan *,const buzz_import_memory *);
int buzz_import_restore(buzz_import_plan *,const buzz_import_memory *);
#endif
