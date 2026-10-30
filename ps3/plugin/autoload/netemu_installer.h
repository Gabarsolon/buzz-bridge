/* SPDX-License-Identifier: MIT */
#ifndef BB_NETEMU_INSTALLER_H
#define BB_NETEMU_INSTALLER_H
#include "bb.h"
#define BB_STOCK_SELF "bad3e45c9a81f25726e414b146ea5a112f28216ab4bdd2fd9f5c81b33d04c80a"
#define BB_MODIFIED_SELF "74e890bf7f77edfd92d4c0a57f45e73cf7fe39aa3c21cd7587948fb3c81ade40"
typedef struct {const char *current,*writable,*directory;} bb_netemu_paths;
typedef struct {char path[1024],name[256],hash[65];int valid;} bb_netemu_copy;
typedef struct {int (*is_hen)(void);int (*mount)(int writable,void *context);void *context;} bb_netemu_ops;
int bb_netemu_known(const char *hash);
/* ISO route: 0 verified stock, 1 known modified build, 2 unrecognized. */
int bb_netemu_classify(const char *hash);
const char *bb_netemu_name(const char *hash);
/* Page is enumeration order; oldest selection uses the timestamp in the name.
 * Every candidate is rehashed, including corrupt/incomplete backups. */
int bb_netemu_copies(const bb_netemu_paths *,size_t offset,bb_netemu_copy *,size_t count,size_t *total,bb_netemu_copy *oldest);
int bb_netemu_status(const bb_netemu_paths *paths,char *message,size_t cap);
/* Caller must obtain explicit destructive confirmation. The current hash must
 * be the one displayed at that confirmation. Unknown hash needs a SECOND yes. */
int bb_netemu_replace(const bb_netemu_paths *paths,const bb_netemu_ops *ops,const char *source,const char *expected_source,const char *confirmed_current,int unknown_confirmed,char *message,size_t cap);
#endif
