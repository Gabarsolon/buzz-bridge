/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_APP_UI_H
#define BUZZ_APP_UI_H
#ifdef BUZZ_APP_HOST_TEST
#include "app_mock.h"
#else
#include <cell/msg_dialog.h>
#endif
int app_ui_init(void);
int app_dialog(const char *text, int question);
/* Dialogs wrap into pages. Cross advances; Up/Down reviews pages. On the last
 * question page Left selects Yes, Right selects No (default), Cross confirms.
 * Circle returns to the preceding screen; it never authorizes an operation.
 * Interactive waits have no reading timeout; errors/exit never authorize. */
/* D-pad list, Cross chooses, Circle returns -1. No choice on timeout/error. */
int app_menu(const char *title,const char *const *items,int count,int selected);
void app_work(const char *stage);
int app_exiting(void);
void app_log(const char *fmt, ...);
void app_stage(const char *text);
void app_ui_exit(int result);
#endif
