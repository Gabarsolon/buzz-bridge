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
int app_exiting(void);
void app_log(const char *fmt, ...);
void app_stage(const char *text);
void app_ui_exit(int result);
#endif
