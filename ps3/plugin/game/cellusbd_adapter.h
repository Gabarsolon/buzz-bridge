/* SPDX-License-Identifier: MIT. User-process cellUsbd adapter, no LV2 IDs. */
#ifndef BUZZ_CELLUSBD_ADAPTER_H
#define BUZZ_CELLUSBD_ADAPTER_H
#include "usb_model.h"
#define BZ_CELL_INVALID ((int32_t)0x80110004u)
#define BZ_CELL_DEVICE ((int32_t)0x8011000au)
#define BZ_CELL_PIPE ((int32_t)0x8011000du)
#define BZ_CELL_PIPE_BUSY ((int32_t)0x8011000cu)
#define BZ_CELL_BUSY ((int32_t)0x80110008u)
#define BZ_CELL_NOMEM ((int32_t)0x80110003u)
#define BZ_CELL_DEVICE_BASE 0x425a0000
#define BZ_CELL_PIPE_BASE 0x425b0000
typedef void (*buzz_cell_done)(int32_t result,int32_t count,void *arg);
typedef struct {
 const char *name;
 int32_t (*probe)(int32_t device);
 int32_t (*attach)(int32_t device);
 int32_t (*detach)(int32_t device);
} buzz_cell_ldd;
typedef struct {
 int32_t (*init)(void),(*end)(void);
 int32_t (*register_extra)(buzz_cell_ldd *,uint16_t,uint16_t);
 int32_t (*register_extra2)(buzz_cell_ldd *,uint16_t,uint16_t,uint16_t);
 int32_t (*unregister_extra)(buzz_cell_ldd *);
 void *(*scan)(int32_t,void *,uint8_t);
 int32_t (*open)(int32_t,void *),(*close)(int32_t);
 int32_t (*control)(int32_t,buzz_setup *,void *,buzz_cell_done,void *);
 int32_t (*interrupt)(int32_t,void *,int32_t,buzz_cell_done,void *);
 int32_t (*set_private)(int32_t,void *);
 void *(*get_private)(int32_t);
 int32_t (*location)(int32_t,uint8_t *);
} buzz_cell_real;
typedef struct {
 void (*lock)(void *),(*unlock)(void *); void *arg;
} buzz_cell_sync;
typedef struct { buzz_cell_ldd *ldd; uint16_t vid,lo,hi; unsigned attempted; } buzz_cell_registration;
typedef struct { int32_t id; unsigned model_pipe; } buzz_cell_pipe;
typedef struct {
 uint32_t transfer; int32_t pipe; void *buffer,*arg; buzz_cell_done done;
 unsigned input;
} buzz_cell_pending;
typedef struct {
 buzz_usb usb; buzz_cell_real real; buzz_cell_sync sync;
 buzz_cell_registration registrations[4];
 buzz_cell_ldd *owner[2]; void *private_data[2];
 buzz_cell_pipe pipes[4]; buzz_cell_pending pending[BUZZ_USB_TRANSFERS];
 uint8_t descriptors[52];
 uint64_t now_us,attach_after_us; unsigned callbacks,stopping;
 uint32_t next_pipe;
 int32_t real_reserved_pipes[BUZZ_USB_TRANSFERS];
 unsigned namespace_fault;
 unsigned input_started,input_completed,attached_mask,present_allowed;
 /* Diagnostic sink must not call the adapter or block; called under its lock. */
 void (*trace)(unsigned event,int32_t object,uint32_t result,uint32_t count);
 /* Single poll worker only; publish a literal state without blocking. */
 void (*progress)(const char *state);
 /* Lock-free safety latch, checked between external callback boundaries. */
 int (*paused)(void);
 /* Optional native nonblocking game-state/mutex preflight; outside our lock. */
 int (*may_attach)(void);
} buzz_cell;
enum { BZ_TRACE_LOOKUP=1,BZ_TRACE_PROBE,BZ_TRACE_ATTACH,BZ_TRACE_CONTROL,BZ_TRACE_INPUT,BZ_TRACE_DETACH,
 BZ_TRACE_PAD,BZ_TRACE_LOOKUP_CALL,BZ_TRACE_PROBE_CALL,BZ_TRACE_ATTACH_CALL,BZ_TRACE_INPUT_SUBMIT,
 BZ_TRACE_OPEN=15,BZ_TRACE_FIRST_INPUT,BZ_TRACE_CONTROL_SUBMIT,BZ_TRACE_LOCATION,BZ_TRACE_SCAN };
void buzz_cell_construct(buzz_cell *,buzz_hub *,const buzz_cell_real *,const buzz_cell_sync *);
int32_t buzz_cell_init(buzz_cell *);
int32_t buzz_cell_end(buzz_cell *);
/* kind=1 => RegisterExtraLdd; kind=2 => RegisterExtraLdd2. */
int32_t buzz_cell_register(buzz_cell *,buzz_cell_ldd *,uint16_t,uint16_t,uint16_t,unsigned kind);
/* Late injection: caller must validate that this exact LDD is already registered. */
int32_t buzz_cell_adopt(buzz_cell *,buzz_cell_ldd *,uint16_t,uint16_t,uint16_t);
int32_t buzz_cell_unregister(buzz_cell *,buzz_cell_ldd *);
void *buzz_cell_scan(buzz_cell *,int32_t,void *,uint8_t);
int32_t buzz_cell_open(buzz_cell *,int32_t,void *);
int32_t buzz_cell_close(buzz_cell *,int32_t);
int32_t buzz_cell_control(buzz_cell *,int32_t,buzz_setup *,void *,buzz_cell_done,void *);
int32_t buzz_cell_interrupt(buzz_cell *,int32_t,void *,int32_t,buzz_cell_done,void *);
int32_t buzz_cell_set_private(buzz_cell *,int32_t,void *);
void *buzz_cell_get_private(buzz_cell *,int32_t);
int32_t buzz_cell_location(buzz_cell *,int32_t,uint8_t *);
/* Single worker. Callbacks run without the adapter mutex, and may resubmit. */
void buzz_cell_poll(buzz_cell *,uint64_t now_us);
int buzz_cell_can_unload(buzz_cell *);
/* True after every requested dongle attached, configured and started IN. */
int buzz_cell_ready(buzz_cell *);
#endif
