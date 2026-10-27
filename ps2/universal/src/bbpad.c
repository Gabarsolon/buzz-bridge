/* ps2sdk-derived library lookup, export hook and import-stub relinking:
 * Copyright ps2dev - http://www.ps2dev.org (ioplib.c credits maximus32).
 * Licensed under the Academic Free License version 2.0; see ../licenses/AFL-2.0.txt.
 * OPL SIO2 hook reference: Copyright 2006-2008 Romz; Copyright 2010 Polo;
 * licensed under Academic Free License version 3.0; see ../licenses/AFL-3.0.txt.
 * Modified: bounded entries 25/51, passive snapshots and optional session-gated polling.
 * New code/changes Copyright (c) 2026 Gabarsolon, MIT; see ../../../LICENSE.
 * Provenance and license scope: ../THIRD_PARTY.md.
 */
/* Observes completed SIO2 pad transfers and optionally polls idle ports.
 * Pattern: ps2sdk mx4sio_bd ioplib.c (library lookup, hook entries and their aliases, re-link the
 * import stubs of modules that are already linked) and OPL pademu (wrapper calls the saved original,
 * then works on the sio2_transfer_data_t). IOP only. */
#include <types.h>
#include <irx.h>
#include <loadcore.h>
#include <intrman.h>
#include <sio2man.h>
#include <thbase.h>
#include <stdio.h>

#include "bb_core.h"
#include "bbpad.h"

#define SIO2_EXP_TRANSFER_A 25 /* used by MCMAN and old PADMAN */
#define SIO2_EXP_TRANSFER_B 51 /* used by PADMAN and SIO2D */
#define SIO2_EXP_PAD_BEGIN 46  /* sio2_pad_transfer_init: PADMAN's "begin pad session" */
#define SIO2_EXP_END 52        /* sio2_transfer_reset: PADMAN's "end session" */
#define AGE_LIMIT_TICKS (1000 / 4) /* a port nobody has polled for 1 s counts as released */

static volatile u32 snap[BB_PORTS]; /* bit 31 = a controller answered, bits 0..15 = buttons */
static volatile u32 age[BB_PORTS];
static volatile int state; /* 0 not hooked, 1 hooked, -1 gave up (unsupported SIO2MAN) */
static unsigned hook_calls, pad_events;
static volatile unsigned passive_age = 0xFFFFFFF0u; /* worker ticks since a pad read was seen from another module */
static volatile unsigned installed_ticks;           /* worker ticks since the hook went in */
static unsigned poll_count;
static volatile int polling;
static void (*real_begin)(void);
static void (*real_end)(void);

typedef int (*sio2_xfer_fn)(sio2_transfer_data_t *td);
static sio2_xfer_fn real_a, real_b;

static void observe(sio2_transfer_data_t *td)
{
    bb_padev_t ev[BB_MAX_PADEV];
    int n, i;

    hook_calls++;
    if (!td || !td->in || !td->out)
        return;
    if ((td->stat6c >> 13) & 1) /* transfer error flag, PADMAN discards the data too */
        return;
    n = bb_sio2_scan(td->regdata, td->in, td->in_size, td->out, td->out_size, ev);
    for (i = 0; i < n; i++) {
        snap[ev[i].port] = ev[i].valid ? (0x80000000u | ev[i].buttons) : 0;
        age[ev[i].port] = 0;
        pad_events++;
    }
    if (n > 0)
        passive_age = 0;
}

static int hook_a(sio2_transfer_data_t *td)
{
    int r = real_a(td);
    observe(td);
    return r;
}

static int hook_b(sio2_transfer_data_t *td)
{
    int r = real_b(td);
    observe(td);
    return r;
}

static iop_library_t *find_lib(const char *name)
{
    iop_library_t *lib = GetLoadcoreInternalData()->let_next;
    while (lib) {
        int i;
        for (i = 0; i < 8; i++)
            if (lib->name[i] != name[i])
                break;
        if (i == 8)
            return lib;
        lib = lib->prev;
    }
    return NULL;
}

#define J_FOR(addr) (0x08000000u | ((((u32)(addr)) << 4) >> 6))

/* Rewrites every import stub that currently jumps to oldf so it jumps to newf. */
static void relink_stubs(iop_library_t *lib, void *oldf, void *newf)
{
    struct irx_import_table *t;
    struct irx_import_stub *s;
    int st;

    CpuSuspendIntr(&st);
    for (t = lib->caller; t != NULL; t = t->next)
        for (s = (struct irx_import_stub *)t->stubs; s->jump != 0; s++)
            if (s->jump == J_FOR(oldf))
                s->jump = J_FOR(newf);
    CpuResumeIntr(st);
}

/* Replaces one export entry. Only entries 25 and 51 are touched: the real SIO2MAN tables (2.4 and 2.7)
 * contain empty slots, so the table length cannot be found by scanning for a NULL, and in those tables
 * 25 and 51 are the only two entries that point at the transfer function. */
static void hook_entry(iop_library_t *lib, unsigned idx, void *newf)
{
    int st;
    CpuSuspendIntr(&st);
    lib->exports[idx] = newf;
    CpuResumeIntr(st);
}

int bbpad_install(void)
{
    iop_library_t *lib;
    void *oa, *ob;

    if (state != 0)
        return state;
    lib = find_lib("sio2man");
    if (!lib)
        return 0; /* not loaded yet, caller retries */
    if ((lib->version >> 8) != 2) {
        state = -1; /* only the 2.x table (25/51 = transfer) is understood */
        return state;
    }
    oa = lib->exports[SIO2_EXP_TRANSFER_A];
    ob = lib->exports[SIO2_EXP_TRANSFER_B];
    if (!oa || !ob) {
        state = -1;
        return state;
    }
    real_a = (sio2_xfer_fn)oa;
    real_b = (sio2_xfer_fn)ob;
    /* PADMAN's session entry points, used only by our own poll (not hooked) */
    real_begin = (void (*)(void))lib->exports[SIO2_EXP_PAD_BEGIN];
    real_end = (void (*)(void))lib->exports[SIO2_EXP_END];
    /* modules that are already linked first (their stubs jump straight to the old function), then the
     * export table for modules that link later */
    relink_stubs(lib, oa, (void *)hook_a);
    if (ob != oa)
        relink_stubs(lib, ob, (void *)hook_b);
    hook_entry(lib, SIO2_EXP_TRANSFER_A, (void *)hook_a);
    hook_entry(lib, SIO2_EXP_TRANSFER_B, (ob == oa) ? (void *)hook_a : (void *)hook_b);
    FlushIcache();
    state = 1;
    return state;
}

void bbpad_tick(void)
{
    int p;
    if (passive_age < 0xFFFFFFF0u)
        passive_age++;
    if (state == 1 && installed_ticks < 0xFFFFFFF0u)
        installed_ticks++;
    for (p = 0; p < BB_PORTS; p++)
        if (age[p] < 0xFFFFFFF0u)
            age[p]++;
}

void bbpad_sample(uint16_t masks[BB_NSRC])
{
    int p;
    for (p = 0; p < BB_PORTS; p++) {
        u32 v = snap[p];
        if ((v & 0x80000000u) && age[p] < AGE_LIMIT_TICKS)
            masks[BB_SRC(p, 0)] = (uint16_t)(v & 0xFFFF);
    }
}

int bbpad_state(void) { return state; }
unsigned bbpad_hook_calls(void) { return hook_calls; }
unsigned bbpad_pad_events(void) { return pad_events; }

/* ---- own polling ---- */

#define POLL_PERIOD_US 16000   /* 60 Hz like PADMAN */
#define POLL_GRACE_TICKS 250   /* 1 s after the hook went in: let the game finish booting */
#define POLL_IDLE_TICKS 125    /* nobody else read a pad for 500 ms */

static sio2_transfer_data_t ptd;
static u8 pin[BB_PORTS * BB_POLL_CMD_LEN];
static u8 pout[BB_PORTS * BB_POLL_CMD_LEN];

/* One session exactly like PADMAN's: begin (46, SIO2MAN's own arbitration with MCMAN/PADMAN), one transfer
 * through the ORIGINAL export 51 (so our hook does not see our own read), end (52). */
static void poll_once(void)
{
    bb_padev_t ev[BB_MAX_PADEV];
    int n, i;
    unsigned k;

    ptd.stat6c = 0;
    for (k = 0; k < 4; k++) {
        ptd.port_ctrl1[k] = k < BB_PORTS ? BB_POLL_CTRL1 : 0;
        ptd.port_ctrl2[k] = k < BB_PORTS ? BB_POLL_CTRL2 : 0;
    }
    bb_poll_fill(ptd.regdata, pin);
    for (k = 0; k < sizeof pout; k++)
        pout[k] = 0;
    ptd.in_size = sizeof pin;
    ptd.out_size = sizeof pout;
    ptd.in = pin;
    ptd.out = pout;
    ptd.in_dma.addr = 0;
    ptd.out_dma.addr = 0;

    real_begin();
    real_b(&ptd);
    real_end();
    poll_count++;

    if ((ptd.stat6c >> 13) & 1)
        return;
    n = bb_sio2_scan(ptd.regdata, ptd.in, ptd.in_size, ptd.out, ptd.out_size, ev);
    for (i = 0; i < n; i++) {
        snap[ev[i].port] = ev[i].valid ? (0x80000000u | ev[i].buttons) : 0;
        age[ev[i].port] = 0;
    }
}

void bbpad_poll_loop(unsigned flags)
{
    int mode_always = (flags & BB_FLAG_POLL_ALWAYS) != 0;
    for (;;) {
        DelayThread(POLL_PERIOD_US);
        if (state != 1 || !real_begin || !real_end || installed_ticks < POLL_GRACE_TICKS) {
            polling = 0;
            continue;
        }
        if (mode_always || passive_age >= POLL_IDLE_TICKS) {
            if (!polling && flags & BB_FLAG_VERBOSE)
                printf("BBUSB: no pad traffic from the game: reading the pads ourselves%s\n", mode_always ? " (always)" : "");
            polling = 1;
            poll_once();
        } else {
            if (polling && flags & BB_FLAG_VERBOSE)
                printf("BBUSB: the game reads the pads again: own pad reads stopped\n");
            polling = 0;
        }
    }
}

unsigned bbpad_poll_count(void) { return poll_count; }
int bbpad_polling(void) { return polling; }
