/* ps2sdk-derived module entry, registration and callback patterns:
 * Copyright ps2dev - http://www.ps2dev.org
 * Licensed under the Academic Free License version 2.0; see ../licenses/AFL-2.0.txt.
 * Modified for a virtual Buzz! device, worker and callback GP handling.
 * New code/changes Copyright (c) 2026 Gabarsolon, MIT; see ../../../LICENSE.
 * Provenance and license scope: ../THIRD_PARTY.md.
 */
/* Buzz! Bridge universal USBD: replacement USBD.IRX that presents one virtual wired Buzz! dongle
 * (054C:0002) to the game's own BUZZER.IRX. IOP glue: module entry, usbd 1.2 exports, worker thread. */
#include <types.h>
#include <defs.h>
#include <irx.h>
#include <loadcore.h>
#include <intrman.h>
#include <thbase.h>
#include <stdio.h>
#include <usbd.h>

#include "bb_core.h"
#include "vusb.h"
#include "vusb_os.h"
#include "bbpad.h"

IRX_ID("USB_driver", 2, 4);

extern struct irx_export_table _exp_usbd;

/* The patcher finds this block by its magic and rewrites layout/flags/timings in the IRX file.
 * volatile + external linkage so the compiler cannot fold the defaults into the code. */
volatile bb_cfg_t bb_cfg __attribute__((used, aligned(4))) = {
    BB_CFG_MAGIC, 1, BB_LAYOUT_buzzers, BB_FLAG_VERBOSE, 0, 100, 100};

/* our mirror structs must match the public usbd.h layout the game was compiled against */
_Static_assert(sizeof(vu_ldd_t) == sizeof(sceUsbdLddOps), "LDD layout");
_Static_assert(sizeof(vu_setup_t) == sizeof(UsbDeviceRequest), "setup layout");
_Static_assert(sizeof(vu_epdesc_t) == sizeof(UsbEndpointDescriptor), "endpoint descriptor layout");

#define WORKER_PRIO 0x24 /* same as the real USBD callback thread */
#define WORKER_STACK 0x4000
#define POLL_PRIO 0x28     /* below the USB worker (0x24): never delays a report */
#define POLL_STACK 0x1000
#define SIO2_RETRY_TICKS 25      /* 100 ms */
#define SIO2_GIVEUP_TICKS 7500   /* 30 s */
#define STATUS_TICKS 1250        /* 5 s */

/* ---- vusb_os.h implementation ---- */

int vos_lock(void)
{
    int st;
    CpuSuspendIntr(&st);
    return st;
}

void vos_unlock(int st) { CpuResumeIntr(st); }

unsigned vos_flags(void) { return bb_cfg.flags; }

void vos_sample(uint16_t masks[BB_NSRC]) { bbpad_sample(masks); }

/* gpcall.S: runs fn(a, b, c) with the client's $gp and puts our own back afterwards */
extern int bb_call3(void *fn, void *gp, int a, int b, int c);

void vos_call_done(vu_done_t cb, int result, int count, void *arg, void *gp)
{
    bb_call3((void *)cb, gp, result, count, (int)arg);
}

int vos_call_ldd(int (*fn)(int), int devId, void *gp)
{
    return bb_call3((void *)fn, gp, devId, 0, 0);
}

/* ---- usbd 1.2 exports ---- */

void _retonly(void) {}

void usbdReboot(int ac) { (void)ac; }

int sceUsbdRegisterLdd(sceUsbdLddOps *drv) { return vusb_register_ldd((vu_ldd_t *)drv, GetGP()); }

int sceUsbdUnregisterLdd(sceUsbdLddOps *drv) { return vusb_unregister_ldd((vu_ldd_t *)drv); }

void *sceUsbdScanStaticDescriptor(int devId, void *data, u8 type) { return vusb_scan_descriptor(devId, data, type); }

int sceUsbdSetPrivateData(int devId, void *data) { return vusb_set_private(devId, data); }

void *sceUsbdGetPrivateData(int devId) { return vusb_get_private(devId); }

int sceUsbdOpenPipe(int devId, UsbEndpointDescriptor *desc) { return vusb_open_pipe(devId, (const vu_epdesc_t *)desc); }

int sceUsbdOpenPipeAligned(int devId, UsbEndpointDescriptor *desc) { return vusb_open_pipe(devId, (const vu_epdesc_t *)desc); }

int sceUsbdClosePipe(int id) { return vusb_close_pipe(id); }

int sceUsbdTransferPipe(int id, void *data, u32 len, void *option, sceUsbdDoneCallback callback, void *cbArg)
{
    return vusb_transfer(id, data, len, option, (vu_done_t)callback, cbArg, GetGP());
}

int sceUsbdGetDeviceLocation(int devId, u8 *path) { return vusb_get_location(devId, path); }

/* Not used by BUZZER.IRX (imports 4-11 and 13 only). Harmless answers instead of a crash. */
int sceUsbdRegisterAutoloader(sceUsbdLddOps *drv)
{
    (void)drv;
    return USB_RC_NOSUPPORT;
}

int sceUsbdUnregisterAutoloader(void) { return USB_RC_OK; }

int sceUsbdChangeThreadPriority(int prio1, int prio2)
{
    (void)prio1;
    (void)prio2;
    return USB_RC_OK;
}

int sceUsbdGetReportDescriptor(int devId, int cfgNum, int ifNum, void **desc, u32 *len)
{
    (void)devId;
    (void)cfgNum;
    (void)ifNum;
    (void)desc;
    (void)len;
    return USB_RC_NOSUPPORT;
}

int sceUsbdMultiIsochronousTransfer(int pipeId, sceUsbdMultiIsochronousRequest *request,
                                    sceUsbdMultiIsochronousDoneCallback callback, void *cbArg)
{
    (void)pipeId;
    (void)request;
    (void)callback;
    (void)cbArg;
    return USB_RC_NOSUPPORT;
}

/* ---- worker ---- */

static void poll_thread(void *arg)
{
    (void)arg;
    bbpad_poll_loop(bb_cfg.flags);
}

static void worker(void *arg)
{
    unsigned tick = 0, last_in = 0, last_calls = 0, last_polls = 0;
    int verbose = (bb_cfg.flags & BB_FLAG_VERBOSE) != 0;
    int hook_pad = (bb_cfg.flags & (BB_FLAG_NOPAD | BB_FLAG_SCRIPT)) == 0;

    (void)arg;
    for (;;) {
        if (hook_pad && bbpad_state() == 0 && (tick % SIO2_RETRY_TICKS) == 0 && tick < SIO2_GIVEUP_TICKS) {
            int r = bbpad_install();
            if (verbose && r != 0)
                printf("BBUSB: SIO2MAN hook %s (tick %u)\n", r > 0 ? "installed" : "NOT installed (unsupported SIO2MAN)", tick);
        }
        bbpad_tick();
        vusb_tick();
        tick++;
        if (verbose && (tick % STATUS_TICKS) == 0) {
            const vusb_stats_t *st = vusb_stats();
            if (st->in_delivered != last_in || bbpad_hook_calls() != last_calls || bbpad_poll_count() != last_polls) {
                last_in = st->in_delivered;
                last_calls = bbpad_hook_calls();
                last_polls = bbpad_poll_count();
                printf("BBUSB: status sio2=%d calls=%u padev=%u polls=%u%s reports=%u ctl=%u stall=%u lights=%02x%02x%02x%02x rep=%02x%02x%02x%02x%02x\n",
                       bbpad_state(), bbpad_hook_calls(), bbpad_pad_events(), bbpad_poll_count(),
                       bbpad_polling() ? "(on)" : "", st->in_delivered, st->ctl_xfers,
                       st->ctl_stalls, st->lights[1], st->lights[2], st->lights[3], st->lights[4],
                       st->last_report[0], st->last_report[1], st->last_report[2], st->last_report[3],
                       st->last_report[4]);
            }
        }
        DelayThread(VU_TICK_MS * 1000);
    }
}

int _start(int ac, char *av[], void *startaddr, ModuleInfo_t *mi)
{
    iop_thread_t th;
    int tid;
    unsigned flags;

    (void)av;
    (void)startaddr;
    if (ac < 0)
        return MODULE_RESIDENT_END; /* hooks cannot be removed safely: refuse to unload */

    flags = bb_cfg.flags;
    vusb_init(bb_cfg.attach_delay_ms, bb_cfg.heartbeat_ms, bb_cfg.layout, flags);

    th.attr = TH_C;
    th.thread = worker;
    th.stacksize = WORKER_STACK;
    th.option = 0;
    th.priority = WORKER_PRIO;
    tid = CreateThread(&th);
    if (tid < 0)
        return MODULE_NO_RESIDENT_END;

    if (RegisterLibraryEntries(&_exp_usbd) != 0) {
        DeleteThread(tid);
        return MODULE_NO_RESIDENT_END;
    }
    if (StartThread(tid, NULL) != 0) {
        ReleaseLibraryEntries(&_exp_usbd);
        DeleteThread(tid);
        return MODULE_NO_RESIDENT_END;
    }
    if ((flags & (BB_FLAG_POLL_AUTO | BB_FLAG_POLL_ALWAYS)) && !(flags & (BB_FLAG_NOPAD | BB_FLAG_SCRIPT))) {
        /* the poll thread blocks inside SIO2MAN's session handshake, so it must not be the USB worker */
        th.attr = TH_C;
        th.thread = poll_thread;
        th.stacksize = POLL_STACK;
        th.option = 0;
        th.priority = POLL_PRIO;
        tid = CreateThread(&th);
        if (tid < 0 || StartThread(tid, NULL) != 0)
            printf("BBUSB: could not start the pad poll thread, passive input only\n");
    }
    if (flags & BB_FLAG_VERBOSE)
        printf("BBUSB: virtual Buzz! dongle USBD loaded, layout %u (%s) flags %02x\n", bb_cfg.layout,
               bb_layout_name(bb_cfg.layout), flags);
    if (mi && ((mi->newflags & 2) != 0))
        mi->newflags |= 0x10; /* same as ps2sdk usbd: mark the resident module */
    return MODULE_RESIDENT_END;
}
