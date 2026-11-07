/* ps2sdk-derived library lookup, export hook and import-stub relinking:
 * Copyright ps2dev - http://www.ps2dev.org (ioplib.c credits maximus32).
 * Licensed under the Academic Free License version 2.0; see ../licenses/AFL-2.0.txt.
 * OPL SIO2 hook reference: Copyright 2006-2008 Romz; Copyright 2010 Polo;
 * licensed under Academic Free License version 3.0; see ../licenses/AFL-3.0.txt.
 * Modified: bounded entries 25/51, passive snapshots and optional session-gated polling.
 * New code/changes Copyright (c) 2026 Gabarsolon, MIT; see ../../../LICENSE.
 * Provenance and license scope: ../THIRD_PARTY.md.
 */
/* Pad input for the virtual dongles.
 *
 * Passive: observes completed SIO2 pad transfers of other modules (PADMAN, ...) through a hook of SIO2MAN's
 * transfer export and keeps the latest buttons per (port, slot).
 * Active (optional, own thread): reads the pads itself when nobody else does, and reads multitap slots B-D,
 * always inside one complete SIO2 session so that the game's own PADMAN never sees a tap that is not on slot A.
 *
 * Pattern: ps2sdk mx4sio_bd ioplib.c (library lookup, re-link the import stubs of modules that are already
 * linked) and OPL pademu (wrapper calls the saved original, then works on the sio2_transfer_data_t);
 * PADMAN's own call order 46 / 51 / 52; freemtap's multitap commands (research/MULTITAP_NOTES.md). IOP only. */
#ifdef BBPAD_HOST_TEST
#include "../tests/iop_mock.h"
#else
#include <types.h>
#include <irx.h>
#include <loadcore.h>
#include <intrman.h>
#include <sio2man.h>
#include <thbase.h>
#include <stdio.h>
#endif

#include "bb_core.h"
#include "bbpad.h"

#define SIO2_EXP_TRANSFER_A 25 /* used by MCMAN and old PADMAN */
#define SIO2_EXP_TRANSFER_B 51 /* used by PADMAN and SIO2D */
#define SIO2_EXP_PAD_BEGIN 46  /* sio2_pad_transfer_init: PADMAN's "begin pad session" */
#define SIO2_EXP_END 52        /* sio2_transfer_reset: PADMAN's "end session" */
#define AGE_LIMIT_TICKS (1000 / 4) /* a source nobody has polled for 1 s counts as released */

static volatile u32 snap[BB_NSRC]; /* bit 31 = a controller answered, bits 0..15 = buttons */
static volatile u32 age[BB_NSRC];
static volatile int state; /* 0 not hooked, 1 hooked, -1 gave up (unsupported SIO2MAN) */
static unsigned hook_calls, pad_events;
static volatile unsigned passive_age = 0xFFFFFFF0u; /* worker ticks since a pad read was seen from another module */
static volatile unsigned installed_ticks;           /* worker ticks since the hook went in */
static unsigned poll_count, tap_cycles;
static volatile int polling;
static void (*real_begin)(void);
static void (*real_end)(void);
static u8 passive_slot[BB_PORTS]; /* slot a multitap-aware game selected last, per port (0 = A) */

/* layout-derived: which slots 1..3 of which console port are wanted */
static u8 tap_wanted[BB_PORTS];
static u8 tap_slots[BB_PORTS]; /* slot count a detected tap reported, 0 = none found */
static volatile u8 tap_stage[BB_NSRC]; /* 0 no detect, 1 select refused, 2 empty/error, 3 valid read */
static u8 diag_slot[BB_PORTS];
static unsigned diag_tap;
static unsigned any_slots;
static unsigned custom_slots, restore_blocked; /* custom only: isolate a port with an unconfirmed restore */
static volatile u32 scan_snap[BB_NSRC]; /* publish one complete ordered scan, never a partial A/B batch */
static unsigned tap_selected_mask; /* include attempted selections whose acknowledgement may be lost */

/* Dedicated Mode C source: port 1/A only. USB writes merely publish desired
 * lamps. The poll thread owns setup/sequencing; hooks only overlay motor bytes. */
static unsigned modec;
static volatile u32 modec_snap, modec_age;
static volatile unsigned modec_desired, modec_ready, modec_motors;
static volatile u32 modec_gamecfg_ms;
static volatile unsigned modec_gamecfg_seen;
static bb_modec_lamps_t modec_lamps;

static u32 modec_now_ms(void)
{
    iop_sys_clock_t t;
    u32 sec, usec;
    GetSystemTime(&t);
    SysClock2USec(&t, &sec, &usec);
    return sec * 1000u + usec / 1000u;
}

static void modec_take(sio2_transfer_data_t *td, unsigned slot)
{
    u32 word;
    int r = bb_modec_scan(td->regdata, td->in, td->in_size, td->out, td->out_size,
                         td->stat6c, slot, &word);
    if (r >= 0) {
        modec_snap = r ? 0x80000000u | word : 0;
        modec_age = 0;
    }
}

typedef struct { unsigned off; u8 small, large; } modec_saved_t;

/* Called inside the caller's existing SIO2 session. Never changes descriptors,
 * reply bytes or command sizes. Restore the caller's request buffer afterwards.
 * Configuration belongs to the game until it has been quiet for 500 ms. */
static unsigned modec_overlay(sio2_transfer_data_t *td, modec_saved_t saved[16])
{
    unsigned i, a = 0, n = 0, slot = passive_slot[0];
    u8 *in = td ? td->in : NULL;
    if (!in) return 0;
    for (i = 0; i < 16 && td->regdata[i]; i++) {
        unsigned reg = td->regdata[i], p = reg & 3, len = (reg >> 8) & 511;
        if (a + len > td->in_size) break;
        /* Conservatively follow attempted selections before their reply. */
        if (p == 2 && len >= 3 && in[a] == 0x21 && in[a + 1] == 0x21)
            slot = in[a + 2];
        if (p == 0 && !slot && len >= 3 && in[a] == 1) {
            if (in[a + 1] != 0x42) {
                modec_ready = 0;
                modec_gamecfg_ms = modec_now_ms();
                modec_gamecfg_seen = 1;
            } else if (len >= 5 && modec_ready) {
                unsigned motors = modec_motors;
                saved[n].off = a;
                saved[n].small = in[a + 3]; saved[n].large = in[a + 4]; n++;
                in[a + 3] = (u8)motors; in[a + 4] = (u8)(motors >> 8);
            }
        }
        a += len;
    }
    return n;
}

typedef int (*sio2_xfer_fn)(sio2_transfer_data_t *td);
static sio2_xfer_fn real_a, real_b;

static void set_src(unsigned port, unsigned slot, int valid, unsigned buttons)
{
    unsigned i = BB_SRC(port, slot);
    snap[i] = valid ? (0x80000000u | buttons) : 0;
    age[i] = 0;
}

static void observe(sio2_transfer_data_t *td)
{
    bb_padev_t ev[BB_MAX_PADEV];
    int n, i;

    hook_calls++;
    if (!td || !td->in || !td->out)
        return;
    if (modec)
        modec_take(td, passive_slot[0]);
    if ((td->stat6c >> 13) & 1) /* transfer error flag, PADMAN discards the data too */
        return;
    n = bb_sio2_scan_ex(td->regdata, td->in, td->in_size, td->out, td->out_size, ev, passive_slot);
    for (i = 0; i < n; i++) {
        set_src(ev[i].port, ev[i].slot, ev[i].valid, ev[i].buttons);
        pad_events++;
    }
    if (n > 0)
        passive_age = 0;
}

static int hooked_transfer(sio2_xfer_fn real, sio2_transfer_data_t *td)
{
    modec_saved_t saved[16];
    unsigned i, n = modec ? modec_overlay(td, saved) : 0;
    int r = real(td);
    for (i = 0; i < n; i++) {
        u8 *in = td->in;
        in[saved[i].off + 3] = saved[i].small;
        in[saved[i].off + 4] = saved[i].large;
    }
    observe(td);
    return r;
}

static int hook_a(sio2_transfer_data_t *td) { return hooked_transfer(real_a, td); }

static int hook_b(sio2_transfer_data_t *td)
{
    return hooked_transfer(real_b, td);
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

#define J_FOR(addr) (0x08000000u | ((((u32)(uintptr_t)(addr)) << 4) >> 6))

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
    unsigned i;
    if (modec_age < 0xFFFFFFF0u) modec_age++;
    if (passive_age < 0xFFFFFFF0u)
        passive_age++;
    if (state == 1 && installed_ticks < 0xFFFFFFF0u)
        installed_ticks++;
    for (i = 0; i < BB_NSRC; i++)
        if (age[i] < 0xFFFFFFF0u)
            age[i]++;
}

unsigned bbpad_sample(uint16_t masks[BB_NSRC])
{
    unsigned i, present = 0;
    for (i = 0; i < BB_NSRC; i++) {
        u32 v = any_slots ? scan_snap[i] : snap[i];
        if ((v & 0x80000000u) && age[i] < AGE_LIMIT_TICKS) {
            masks[i] = (uint16_t)(v & 0xFFFF);
            present |= 1u << i;
        }
    }
    /* Deliberately user-gated: no game-screen detection is possible here.
     * Select is unbound on a whole-pad slot A. Select+Start can join the
     * diagnostic handset; Select alone pulses its status, 252 ms / 2 s.
     * The last used non-A slot of each tapped port is the diagnostic target. */
    if (diag_tap) {
        static const u16 colour[] = {PADB_UP, PADB_LEFT, PADB_DOWN, PADB_RIGHT};
        for (i = 0; i < BB_PORTS; i++) {
            unsigned src, st;
            u16 trigger = masks[BB_SRC(i, 0)];
            if (!diag_slot[i] || !(trigger & PADB_SELECT))
                continue;
            src = BB_SRC(i, diag_slot[i]);
            st = tap_stage[src];
            if (st == 3 && (!(snap[src] & 0x80000000u) || age[src] >= AGE_LIMIT_TICKS))
                st = 2;
            masks[src] = (trigger & PADB_START) ? PADB_L1 :
                         ((installed_ticks % 500) < 63 ? colour[st] : 0);
        }
    }
    return present;
}

uint32_t bbpad_modec_sample(void)
{
    return modec && (modec_snap & 0x80000000u) && modec_age < AGE_LIMIT_TICKS ?
           modec_snap & 0xFFFFFu : 0;
}
void bbpad_modec_lamps(unsigned mask) { if (modec) modec_desired = mask & 15; }
int bbpad_modec_enabled(void) { return modec; }

int bbpad_state(void) { return state; }
unsigned bbpad_hook_calls(void) { return hook_calls; }
unsigned bbpad_pad_events(void) { return pad_events; }

/* ---- own polling ---- */

#define POLL_PERIOD_US 16000   /* 60 Hz like PADMAN */
#define POLL_GRACE_TICKS 250   /* 1 s after the hook went in: let the game finish booting */
#define POLL_IDLE_TICKS 125    /* nobody else read a pad for 500 ms */
#define TAP_DETECT_RETRY 250   /* poll periods (4 s) between multitap detections while none answered */
#define TAP_PERIOD 2           /* slots B-D are read every second poll period (30 Hz) */

static sio2_transfer_data_t ptd;
static u8 pin[BB_TD_MAXIN];
static u8 pout[BB_TD_MAXIN];
static bb_td_t btd;

/* One transfer through the ORIGINAL export 51 (so our hook does not see our own traffic). Must be called
 * between real_begin() and real_end(). Returns stat6c. */
static u32 send_td(void)
{
    unsigned k;

    ptd.stat6c = 0;
    for (k = 0; k < 4; k++) {
        ptd.port_ctrl1[k] = btd.ctrl1[k];
        ptd.port_ctrl2[k] = btd.ctrl2[k];
    }
    for (k = 0; k < 16; k++)
        ptd.regdata[k] = btd.regdata[k];
    for (k = 0; k < BB_TD_MAXIN; k++) {
        pin[k] = btd.in[k];
        pout[k] = 0;
    }
    ptd.in_size = btd.in_size;
    ptd.out_size = btd.out_size;
    ptd.in = pin;
    ptd.out = pout;
    ptd.in_dma.addr = 0;
    ptd.out_dma.addr = 0;
    real_b(&ptd);
    return ptd.stat6c;
}

static unsigned modec_send(unsigned step, u8 small, u8 large)
{
    bb_td_reset(&btd);
    bb_td_add_modec(&btd, step, small, large);
    return send_td();
}

static int modec_config_reply(unsigned status)
{
    return !(status & (1u << 13)) && pout[0] == 0xFF && pout[1] == 0xF3 && pout[2] == 0x5A;
}

static void modec_session(unsigned flags)
{
    static unsigned attempted;
    static u32 last_setup;
    unsigned status, ok, need_analog;
    u32 now, word;
    u8 small, large;

    real_begin(); /* may wait for PADMAN; never called on the USB worker */
    now = modec_now_ms();
    if (passive_slot[0] || (modec_gamecfg_seen && (u32)(now - modec_gamecfg_ms) < 500)) {
        real_end();
        return;
    }
    if (!modec_ready) {
        if (attempted && (u32)(now - last_setup) < 1000) {
            real_end();
            return;
        }
        attempted = 1; last_setup = now;
        /* Complete bounded setup in ONE session so PADMAN never sees F3.
         * Always attempt exit even when an earlier command fails. No sleeps. */
        status = modec_send(0, 0, 0);
        need_analog = pout[1] != 0x73 && pout[1] != 0x79;
        ok = !(status & (1u << 13)) && pout[0] == 0xFF && pout[2] == 0x5A &&
             (pout[1] == 0x41 || pout[1] == 0x73 || pout[1] == 0x79 || pout[1] == 0xF3);
        /* Preserve an already analog/pressure pad's 4F pressure settings.
         * 44 is necessary only for a digital pad (or abandoned config mode). */
        if (ok && need_analog) ok = modec_config_reply(modec_send(1, 0, 0));
        if (ok) ok = modec_config_reply(modec_send(2, 0, 0));
        if (!modec_config_reply(modec_send(3, 0, 0))) ok = 0;
        modec_lamps.started = 0;
        if (!ok) {
            modec_snap = 0;
            real_end();
            return;
        }
        modec_ready = 1;
        if (flags & BB_FLAG_VERBOSE)
            printf("BBUSB: Mode C port 1: analog mode and motor map configured\n");
    }
    /* Get time AFTER session acquisition/setup, not before a blocking begin. */
    bb_modec_lamp_next(&modec_lamps, modec_desired, modec_now_ms(), &small, &large);
    modec_motors = (unsigned)small | ((unsigned)large << 8);
    status = modec_send(4, small, large);
    ok = bb_modec_decode(pout, 9, status, &word);
    modec_snap = ok ? 0x80000000u | word : 0;
    modec_age = 0;
    if (ok) bb_modec_lamp_sent(&modec_lamps);
    else modec_ready = 0; /* disconnect, mode changed, or transport failure */
    poll_count++;
    real_end();
}

/* pad read entries of the transfer just made -> snapshots of (port, slot) */
static void take_reads(unsigned slot)
{
    bb_padev_t ev[BB_MAX_PADEV];
    u8 cs[BB_PORTS];
    int n, i;

    cs[0] = cs[1] = (u8)slot;
    n = bb_sio2_scan_ex(ptd.regdata, ptd.in, ptd.in_size, ptd.out, ptd.out_size, ev, cs);
    for (i = 0; i < n; i++) {
        set_src(ev[i].port, ev[i].slot, ev[i].valid, ev[i].buttons);
        if (slot)
            tap_stage[BB_SRC(ev[i].port, slot)] = ev[i].valid ? 3 : 2;
    }
}

static void log_tap(const char *what, unsigned port, unsigned n)
{
    printf("BBUSB: multitap port %u: %s %u\n", port + 1, what, n);
}

static void tap_detect(unsigned flags)
{
    unsigned i, p;

    bb_td_reset(&btd);
    for (p = 0; p < BB_PORTS; p++)
        if (tap_wanted[p] && !tap_slots[p] && !(restore_blocked & (1u << p)))
            bb_td_add_tap_detect(&btd, p);
    if (!btd.n)
        return;
    if ((send_td() >> 13) & 1)
        return;
    for (i = 0; i < btd.n; i++) {
        unsigned n = bb_tap_detect_reply(pout + btd.out_off[i], 6);
        p = btd.port[i];
        if (n && !tap_slots[p]) {
            unsigned s;
            tap_slots[p] = (u8)n;
            for (s = 1; s < BB_SLOTS; s++)
                tap_stage[BB_SRC(p, s)] = 1;
            if (flags & BB_FLAG_VERBOSE)
                log_tap("tap found, slots", p, n);
        }
    }
}

/* Selects slot s on every tapped port that wants it. Returns a bit mask of the ports whose tap confirmed it. */
static unsigned tap_select(unsigned slot)
{
    unsigned p, ok = 0, i;

    bb_td_reset(&btd);
    for (p = 0; p < BB_PORTS; p++)
        if (tap_slots[p] && (tap_wanted[p] & (1u << slot)) && slot < tap_slots[p] &&
            !(restore_blocked & (1u << p))) {
            bb_td_add_tap_select(&btd, p, slot);
            tap_selected_mask |= 1u << p;
        }
    if (!btd.n)
        return 0;
    p = send_td();
    for (i = 0; i < btd.n; i++) {
        if (!((p >> 13) & 1) && bb_tap_select_reply_ok(pout + btd.out_off[i], 7, slot))
            ok |= 1u << btd.port[i];
        else
            tap_stage[BB_SRC(btd.port[i], slot)] = 1;
    }
    return ok;
}

/* Puts every tap we moved back on slot A (three tries). The game's PADMAN reads whatever slot is selected. */
static void tap_restore(unsigned flags)
{
    unsigned tries, p, i, need = tap_selected_mask;

    for (tries = 0; tries < 3 && need; tries++) {
        bb_td_reset(&btd);
        for (p = 0; p < BB_PORTS; p++)
            if (need & (1u << p))
                bb_td_add_tap_select(&btd, p, 0);
        if (!btd.n)
            break;
        if ((send_td() >> 13) & 1)
            continue;
        for (i = 0; i < btd.n; i++)
            if (bb_tap_select_reply_ok(pout + btd.out_off[i], 7, 0))
                need &= ~(1u << btd.port[i]);
    }
    tap_selected_mask = need;
    if (need && (flags & BB_FLAG_VERBOSE))
        printf("BBUSB: multitap: could not put the tap back on slot A (ports %u), will retry\n", need);
}

/* Reads slots B..D of every tap that has a wanted controller, inside the caller's session. */
static void tap_cycle(unsigned flags)
{
    unsigned slot, p, ok, status, any = 0;

    for (slot = 1; slot < BB_SLOTS; slot++) {
        ok = tap_select(slot);
        if (!ok)
            continue;
        tap_selected_mask |= ok;
        any = 1;
        bb_td_reset(&btd);
        for (p = 0; p < BB_PORTS; p++)
            if (ok & (1u << p)) {
                bb_td_add_pad_read(&btd, p);
            }
        status = send_td();
        for (p = 0; p < BB_PORTS; p++)
            if (ok & (1u << p))
                tap_stage[BB_SRC(p, slot)] = 2;
        if (!((status >> 13) & 1))
            take_reads(slot);
    }
    if (any || tap_selected_mask)
        tap_restore(flags);
    tap_cycles++;
}

static void poll_session(unsigned flags, int need_a, int need_tap_cycle, int need_detect)
{
    unsigned p;

    real_begin();
    if (tap_selected_mask) /* a previous restore failed: never label a non-A read as A */
        tap_restore(flags);
    restore_blocked = custom_slots ? tap_selected_mask : 0;
    if (tap_selected_mask && !custom_slots) {
        real_end();
        return;
    }
    /* A netemu port can stop acknowledging restores after its last B-D pad
     * disappears. Keep retrying that port without blocking scans of the other
     * port or stale-snapshot publication. Never read the blocked port as A.
     * Legacy layouts retain their original session/restore behavior. */
    if (need_detect && any_slots)
        tap_detect(flags);
    if (need_a || (any_slots && need_tap_cycle)) {
        bb_td_reset(&btd);
        for (p = 0; p < BB_PORTS; p++)
            /* A PADMAN read can supply A, but traffic on port 1 must never
             * suppress discovering an idle/new pad on port 2. */
            if (!(restore_blocked & (1u << p)) &&
                (need_a || age[BB_SRC(p, 0)] >= 8 || !(snap[BB_SRC(p, 0)] & 0x80000000u)))
                bb_td_add_pad_read(&btd, p);
        if (btd.n && !((send_td() >> 13) & 1))
            take_reads(0);
        poll_count++;
    }
    if (need_detect && !any_slots)
        tap_detect(flags);
    if (need_tap_cycle)
        tap_cycle(flags);
    if (any_slots && need_tap_cycle) {
        int st;
        CpuSuspendIntr(&st);
        for (p = 0; p < BB_NSRC; p++)
            scan_snap[p] = age[p] < 16 ? snap[p] : 0;
        CpuResumeIntr(st);
    }
    real_end();
}

void bbpad_configure(unsigned layout_id, unsigned flags)
{
    bb_layout_t lay;
    unsigned p;

    modec = layout_id == BB_LAYOUT_mode_c && !(flags & (BB_FLAG_NOPAD | BB_FLAG_SCRIPT));
    modec_snap = modec_ready = modec_desired = modec_motors = 0;
    modec_age = AGE_LIMIT_TICKS;

    if (bb_layout_build(layout_id, &lay) != 0)
        bb_layout_build(BB_LAYOUT_buzzers, &lay);
    bb_layout_taps(&lay, tap_wanted);
    any_slots = lay.any_slots;
    custom_slots = lay.custom;
    restore_blocked = 0;
    /* The CLI refuses this combination; never synthesize onto unknown bindings. */
    diag_tap = any_slots ? 0 : flags & BB_FLAG_DIAG_TAP;
    for (p = 0; p < BB_PORTS; p++) {
        unsigned s;
        tap_slots[p] = 0;
        diag_slot[p] = 0;
        for (s = 1; s < BB_SLOTS; s++) {
            tap_stage[BB_SRC(p, s)] = 0;
            if (tap_wanted[p] & (1u << s))
                diag_slot[p] = (u8)s;
        }
    }
}

int bbpad_taps_wanted(void) { return (tap_wanted[0] | tap_wanted[1]) != 0; }

void bbpad_poll_loop(unsigned flags)
{
    int mode_always = (flags & BB_FLAG_POLL_ALWAYS) != 0;
    int taps = bbpad_taps_wanted();
    /* a multitap layout needs slot A as well (for example the dongle pad on 1-A when the game never polls) */
    int poll_a_enabled = (flags & (BB_FLAG_POLL_AUTO | BB_FLAG_POLL_ALWAYS)) != 0 || taps;
    unsigned iter = 0, detect_wait = 0;

    for (;;) {
        int need_a, need_tap = 0, need_detect = 0;

        DelayThread(POLL_PERIOD_US);
        iter++;
        if (state != 1 || !real_begin || !real_end || installed_ticks < POLL_GRACE_TICKS) {
            polling = 0;
            continue;
        }
        if (modec) {
            /* Mode C always needs its dedicated service, even --poll off or
             * active game reads: analog axes and the lamp lease require it. */
            polling = 1;
            modec_session(flags);
            continue;
        }
        need_a = poll_a_enabled && (mode_always || passive_age >= POLL_IDLE_TICKS);
        if (need_a) {
            if (!polling && flags & BB_FLAG_VERBOSE)
                printf("BBUSB: no pad traffic from the game: reading the pads ourselves%s\n", mode_always ? " (always)" : "");
            polling = 1;
        } else {
            if (polling && flags & BB_FLAG_VERBOSE)
                printf("BBUSB: the game reads the pads again: own pad reads stopped\n");
            polling = 0;
        }
        if (taps) {
            unsigned p;
            int missing = 0;
            for (p = 0; p < BB_PORTS; p++)
                if (tap_wanted[p] && !tap_slots[p])
                    missing = 1;
            if (missing) {
                if (detect_wait == 0) {
                    need_detect = 1;
                    detect_wait = TAP_DETECT_RETRY;
                } else {
                    detect_wait--;
                }
            }
            need_tap = ((iter % TAP_PERIOD) == 0);
        }
        if (need_a || need_tap || need_detect || tap_selected_mask)
            poll_session(flags, need_a, need_tap, need_detect);
    }
}

unsigned bbpad_poll_count(void) { return poll_count; }
unsigned bbpad_tap_cycles(void) { return tap_cycles; }
int bbpad_polling(void) { return polling; }
unsigned bbpad_tap_mask(void) { return (tap_slots[0] ? 1u : 0u) | (tap_slots[1] ? 2u : 0u); }
