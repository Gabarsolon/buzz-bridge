/* SPDX-License-Identifier: MIT. Bounded read-only executable discovery. */
#ifndef BUZZ_RESOLVE_GAME_H
#define BUZZ_RESOLVE_GAME_H
#include "import_hook.h"
typedef struct {
 uint32_t begin,end,slots[14],stubs[14],ldd,ready,count,presence[2],opds[3];
 int profile; unsigned family;
} buzz_game_layout;
/* All addresses come from this process's ELF/imports/LDD/code shape. No writes,
 * callbacks or native USB calls. -1 profile = structurally checked generic. */
int buzz_game_resolve(const buzz_import_memory *,buzz_game_layout *,const char **reason);
/* Revalidate immutable exact-profile fingerprints and callback descriptors. */
int buzz_game_exact(const buzz_import_memory *,unsigned profile);
#endif
