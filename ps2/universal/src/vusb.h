/* ps2sdk public USB ABI declarations: Copyright ps2dev - http://www.ps2dev.org
 * Licensed under Academic Free License version 2.0; see ../licenses/AFL-2.0.txt.
 * Virtual-device declarations and changes Copyright (c) 2026 Gabarsolon, MIT (../../../LICENSE).
 */
/* Virtual wired Buzz! dongle behind the usbd 1.2 API (public ABI of USBD.IRX). Kernel-independent:
 * everything it needs from the IOP comes through vusb_os.h, so the same file runs in the unit tests. */
#ifndef VUSB_H
#define VUSB_H

#include <stdint.h>
#include "bb_core.h"

/* layouts below must equal ps2sdk usbd.h (checked with _Static_assert in main.c) */
typedef struct {
    uint8_t requesttype;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} vu_setup_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType; /* ps2sdk calls it bDescritorLength */
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint8_t wMaxPacketSizeLB;
    uint8_t wMaxPacketSizeHB;
    uint8_t bInterval;
} vu_epdesc_t;

typedef void (*vu_done_t)(int result, int count, void *arg);

typedef struct vu_ldd {
    struct vu_ldd *next, *prev;
    char *name;
    int (*probe)(int devId);
    int (*connect)(int devId);
    int (*disconnect)(int devId);
    uint32_t reserved1, reserved2, reserved3, reserved4, reserved5;
    void *gp;
} vu_ldd_t;

/* result codes (usbd.h) */
#define VU_RC_OK 0x000
#define VU_RC_STALL 0x004
#define VU_RC_DATAOVER 0x008
#define VU_RC_BADDEV 0x101
#define VU_RC_BADPIPE 0x102
#define VU_RC_BADLENGTH 0x103
#define VU_RC_BADDRIVER 0x104
#define VU_RC_BADCONTEXT 0x105
#define VU_RC_IOREQ 0x112
#define VU_RC_BADOPTION 0x113
#define VU_RC_BUSY 0x121
#define VU_RC_ABORTED 0x122

#define VU_DEVID 1
#define VU_DEVID2 2 /* second virtual dongle (layouts with two dongles) */
#define VU_MAXDEV BB_DONGLES
#define VU_TICK_MS 4

typedef struct {
    uint32_t ctl_xfers, ctl_stalls, in_delivered, aborted, lights_updates, bad_args;
    uint8_t lights[VU_MAXDEV][BB_OUT_REPORT_LEN]; /* last SET_REPORT payload of each dongle */
    uint8_t last_report[VU_MAXDEV][BB_REPORT_LEN];
} vusb_stats_t;

/* configuration is copied here: attach delay and heartbeat in ms, layout for the pad mapping */
void vusb_init(unsigned attach_delay_ms, unsigned heartbeat_ms, unsigned layout, unsigned flags);

/* thread-context entry points behind the exported usbd functions. gp = caller's $gp. */
int vusb_register_ldd(vu_ldd_t *drv, void *gp);
int vusb_unregister_ldd(vu_ldd_t *drv);
void *vusb_scan_descriptor(int devId, void *data, uint8_t type);
int vusb_set_private(int devId, void *data);
void *vusb_get_private(int devId);
int vusb_open_pipe(int devId, const vu_epdesc_t *desc);
int vusb_close_pipe(int id);
int vusb_transfer(int id, void *data, uint32_t len, void *option, vu_done_t cb, void *arg, void *gp);
int vusb_get_location(int devId, uint8_t *path);

/* called by the worker thread every VU_TICK_MS */
void vusb_tick(void);

/* test / policy hooks */
void vusb_detach(void);
const vusb_stats_t *vusb_stats(void);
int vusb_is_ready(void);          /* first dongle */
int vusb_dongles(void);           /* how many virtual dongles this layout presents */

#endif
