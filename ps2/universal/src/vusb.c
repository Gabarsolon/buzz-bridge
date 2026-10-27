/* ps2sdk-derived descriptor walk and LDD/API behavior:
 * Copyright ps2dev - http://www.ps2dev.org
 * Licensed under the Academic Free License version 2.0; see ../licenses/AFL-2.0.txt.
 * Modified for a virtual device, fixed endpoints and queued worker completions.
 * New code/changes Copyright (c) 2026 Gabarsolon, MIT; see ../../../LICENSE.
 * Provenance and license scope: ../THIRD_PARTY.md.
 */
#include "vusb.h"
#include "vusb_os.h"

#define MAXPIPE 4   /* slot 0 = default control pipe */
#define MAXPEND 4   /* pending interrupt-IN requests */
#define MAXCOMPL 16 /* completions waiting for the worker */
#define MAXLDD 8

enum { ST_OFF, ST_ATTACHING, ST_READY };
enum { PT_NONE, PT_CTL, PT_INT_IN };

typedef struct { int id; int type; } pipe_t;
typedef struct { int used; int pipe; uint8_t *buf; uint32_t len; vu_done_t cb; void *arg; void *gp; } pend_t;
typedef struct { vu_done_t cb; void *arg; void *gp; int res; int count; } compl_t;

static struct {
    int state;
    uint32_t tick, attach_at, hb_ticks, attach_ticks;
    unsigned flags;
    bb_layout_t layout;
    vu_ldd_t *head, *tail;
    vu_ldd_t *driver; /* LDD the device is bound to */
    void *priv;
    vu_ldd_t *offered[MAXLDD];
    int noffered;
    int configured;
    int pipe_gen;
    pipe_t pipes[MAXPIPE];
    pend_t pend[MAXPEND];
    compl_t compl[MAXCOMPL];
    int c_head, c_count;
    int sent_valid;
    uint32_t sent_tick;
    uint8_t sent_report[BB_REPORT_LEN];
    vusb_stats_t st;
} vu;

#define CTL_PIPE_ID 1

static void mem_zero(void *p, unsigned n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

static void mem_copy(void *d, const void *s, unsigned n)
{
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    while (n--)
        *dd++ = *ss++;
}

static int mem_diff(const void *a, const void *b, unsigned n)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    while (n--)
        if (*x++ != *y++)
            return 1;
    return 0;
}

/* ---- init ---- */

void vusb_init(unsigned attach_delay_ms, unsigned heartbeat_ms, unsigned layout, unsigned flags)
{
    mem_zero(&vu, sizeof vu);
    vu.flags = flags;
    vu.attach_ticks = attach_delay_ms / VU_TICK_MS;
    vu.hb_ticks = heartbeat_ms / VU_TICK_MS;
    if (vu.hb_ticks == 0)
        vu.hb_ticks = 1;
    if (bb_layout_build(layout, &vu.layout) != 0)
        bb_layout_build(BB_LAYOUT_buzzers, &vu.layout);
    vu.pipes[0].id = CTL_PIPE_ID;
    vu.pipes[0].type = PT_CTL;
    vu.pipe_gen = 1;
    vu.state = ST_ATTACHING;
    vu.attach_at = vu.attach_ticks;
}

const vusb_stats_t *vusb_stats(void) { return &vu.st; }
int vusb_is_ready(void) { return vu.state == ST_READY; }

/* ---- helpers (call with the lock held) ---- */

static int dev_ok(int devId) { return devId == VU_DEVID && vu.state == ST_READY; }

static int push_compl(vu_done_t cb, void *arg, void *gp, int res, int count)
{
    int idx;
    if (vu.c_count >= MAXCOMPL)
        return -1;
    idx = (vu.c_head + vu.c_count) % MAXCOMPL;
    vu.compl[idx].cb = cb;
    vu.compl[idx].arg = arg;
    vu.compl[idx].gp = gp;
    vu.compl[idx].res = res;
    vu.compl[idx].count = count;
    vu.c_count++;
    return 0;
}

/* pipe ids: 1 = default control pipe, otherwise 16 * generation + slot + 1 */
static pipe_t *find_pipe(int id)
{
    int slot;
    if (id == CTL_PIPE_ID)
        return &vu.pipes[0];
    slot = (id & 15) - 1;
    if (slot <= 0 || slot >= MAXPIPE)
        return 0;
    return vu.pipes[slot].id == id ? &vu.pipes[slot] : 0;
}

/* abort everything pending on pipe id (0 = all pipes); queues completions with USB_RC_ABORTED.
 * A full completion queue drops the abort callback rather than blocking. */
static void abort_pending(int id)
{
    int i;
    for (i = 0; i < MAXPEND; i++) {
        pend_t *p = &vu.pend[i];
        if (p->used && (id == 0 || p->pipe == id)) {
            push_compl(p->cb, p->arg, p->gp, VU_RC_ABORTED, 0);
            vu.st.aborted++;
            p->used = 0;
        }
    }
}

static void drop_pipes_locked(void)
{
    int k;
    for (k = 1; k < MAXPIPE; k++) {
        vu.pipes[k].id = 0;
        vu.pipes[k].type = PT_NONE;
    }
}

/* ---- LDD registration ---- */

int vusb_register_ldd(vu_ldd_t *drv, void *gp)
{
    int s, n = 0;
    vu_ldd_t *p;

    if (!drv)
        return VU_RC_BADDRIVER;
    s = vos_lock();
    if (drv->next || drv->prev || !drv->name || drv->reserved1 || drv->reserved2) {
        vos_unlock(s);
        return VU_RC_BADDRIVER;
    }
    if (vu.head == drv) {
        vos_unlock(s);
        return VU_RC_BUSY;
    }
    for (p = vu.head; p; p = p->next)
        n++;
    if (n >= MAXLDD) {
        vos_unlock(s);
        return VU_RC_BADDRIVER;
    }
    drv->gp = gp;
    drv->prev = vu.tail;
    if (vu.tail)
        vu.tail->next = drv;
    else
        vu.head = drv;
    drv->next = 0;
    vu.tail = drv;
    vos_unlock(s);
    if (vu.flags & BB_FLAG_VERBOSE)
        vos_log("RegisterLdd %s probe=%08x connect=%08x gp=%08x\n", drv->name,
                (unsigned)(uintptr_t)drv->probe, (unsigned)(uintptr_t)drv->connect, (unsigned)(uintptr_t)gp);
    return VU_RC_OK;
}

int vusb_unregister_ldd(vu_ldd_t *drv)
{
    int s, i;
    vu_ldd_t *p;

    s = vos_lock();
    for (p = vu.head; p && p != drv; p = p->next)
        ;
    if (!p) {
        vos_unlock(s);
        return VU_RC_BADDRIVER;
    }
    if (drv->next)
        drv->next->prev = drv->prev;
    else
        vu.tail = drv->prev;
    if (drv->prev)
        drv->prev->next = drv->next;
    else
        vu.head = drv->next;
    for (i = 0; i < vu.noffered; i++)
        if (vu.offered[i] == drv)
            vu.offered[i] = 0;
    if (vu.driver == drv) {
        /* like USBD: endpoints go, the driver and its private data are cleared, no disconnect call */
        abort_pending(0);
        drop_pipes_locked();
        vu.driver = 0;
        vu.priv = 0;
        vu.configured = 0;
        vu.sent_valid = 0;
    }
    vos_unlock(s);
    if (vu.flags & BB_FLAG_VERBOSE)
        vos_log("UnregisterLdd\n");
    return VU_RC_OK;
}

/* ---- descriptors, private data, location ---- */

void *vusb_scan_descriptor(int devId, void *data, uint8_t type)
{
    const uint8_t *base = bb_static_desc, *end = bb_static_desc + BB_STATIC_DESC_LEN, *d;
    int s, ok;

    s = vos_lock();
    ok = dev_ok(devId);
    vos_unlock(s);
    if (!ok)
        return 0;
    if (data) {
        d = (const uint8_t *)data;
        if (d < base || d >= end)
            return 0; /* not one of our descriptors */
        d += d[0];
    } else {
        d = base;
    }
    for (; d + 2 <= end && d[0] >= 2; d += d[0])
        if (!type || d[1] == type)
            return (void *)d;
    return 0;
}

int vusb_set_private(int devId, void *data)
{
    int s = vos_lock();
    int r = VU_RC_BADDEV;
    if (dev_ok(devId)) {
        vu.priv = data;
        r = VU_RC_OK;
    }
    vos_unlock(s);
    return r;
}

void *vusb_get_private(int devId)
{
    int s = vos_lock();
    void *r = dev_ok(devId) ? vu.priv : 0;
    vos_unlock(s);
    return r;
}

int vusb_get_location(int devId, uint8_t *path)
{
    int s, i, ok;
    s = vos_lock();
    ok = dev_ok(devId);
    vos_unlock(s);
    if (!ok)
        return VU_RC_BADDEV;
    if (!path)
        return VU_RC_BADOPTION;
    path[0] = 1; /* root port 1, no hub */
    for (i = 1; i < 7; i++)
        path[i] = 0;
    return VU_RC_OK;
}

/* ---- pipes ---- */

int vusb_open_pipe(int devId, const vu_epdesc_t *desc)
{
    int s, i, id = -1;

    s = vos_lock();
    if (!dev_ok(devId)) {
        vos_unlock(s);
        return -1;
    }
    if (!desc) { /* default control pipe: the device already has it open */
        vos_unlock(s);
        return CTL_PIPE_ID;
    }
    /* only the one interrupt-IN endpoint that the descriptors advertise exists */
    if (desc->bEndpointAddress == 0x81 && (desc->bmAttributes & 3) == 3) {
        for (i = 1; i < MAXPIPE; i++) {
            if (vu.pipes[i].id == 0) {
                id = 16 * (++vu.pipe_gen & 0xFFF) + i + 1;
                vu.pipes[i].id = id;
                vu.pipes[i].type = PT_INT_IN;
                break;
            }
        }
    }
    vos_unlock(s);
    if (vu.flags & BB_FLAG_VERBOSE)
        vos_log("OpenPipe ep=%02x attr=%02x -> %d\n", desc->bEndpointAddress, desc->bmAttributes, id);
    return id;
}

int vusb_close_pipe(int id)
{
    int s, r = VU_RC_BADPIPE;
    pipe_t *p;

    s = vos_lock();
    p = find_pipe(id);
    if (p) {
        r = VU_RC_OK;
        if (p->type != PT_CTL) { /* the default control pipe is never really closed */
            abort_pending(id);
            p->id = 0;
            p->type = PT_NONE;
        }
    }
    vos_unlock(s);
    if (vu.flags & BB_FLAG_VERBOSE)
        vos_log("ClosePipe %d -> %d\n", id, r);
    return r;
}

/* ---- control transfers ---- */

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* Executes a control request now. Returns a result code; *count = bytes transferred. Lock held. */
static int control_request(const vu_setup_t *su, uint8_t *data, uint32_t len, uint32_t *count)
{
    uint8_t rt = su->requesttype, rq = su->request;
    uint32_t n;

    *count = 0;
    switch (rt) {
    case 0x00: /* standard, device, host to device */
        if (rq == 0x09) { /* SET_CONFIGURATION */
            if (su->value > 1)
                return VU_RC_STALL;
            vu.configured = su->value;
            return VU_RC_OK;
        }
        break;
    case 0x01: /* standard, interface, host to device */
        if (rq == 0x0B) /* SET_INTERFACE: only alternate setting 0 of interface 0 exists */
            return (su->index == 0 && su->value == 0) ? VU_RC_OK : VU_RC_STALL;
        break;
    case 0x21: /* class, interface, host to device (HID) */
        if (rq == 0x09) { /* SET_REPORT: lights */
            uint32_t i;
            uint8_t tmp[BB_OUT_REPORT_LEN];
            n = min_u32(len, su->length);
            mem_zero(tmp, sizeof tmp);
            if (data)
                for (i = 0; i < n && i < sizeof tmp; i++)
                    tmp[i] = data[i];
            if (mem_diff(tmp, vu.st.lights, sizeof tmp))
                vu.st.lights_updates++;
            mem_copy(vu.st.lights, tmp, sizeof tmp);
            *count = n;
            return VU_RC_OK;
        }
        if (rq == 0x0A || rq == 0x0B) /* SET_IDLE, SET_PROTOCOL */
            return VU_RC_OK;
        break;
    case 0x80: /* standard, device, device to host */
        if (rq == 0x06) { /* GET_DESCRIPTOR */
            uint8_t type = (uint8_t)(su->value >> 8);
            if (type == 1) {
                n = min_u32(min_u32(len, su->length), BB_DEV_DESC_LEN);
                if (data)
                    mem_copy(data, bb_static_desc, n);
                *count = n;
                return VU_RC_OK;
            }
            if (type == 2) {
                n = min_u32(min_u32(len, su->length), BB_CFG_DESC_LEN);
                if (data)
                    mem_copy(data, bb_static_desc + BB_DEV_DESC_LEN, n);
                *count = n;
                return VU_RC_OK;
            }
            return VU_RC_STALL; /* no string descriptors */
        }
        if (rq == 0x00) { /* GET_STATUS */
            n = min_u32(min_u32(len, su->length), 2);
            if (data)
                mem_zero(data, n);
            *count = n;
            return VU_RC_OK;
        }
        if (rq == 0x08) { /* GET_CONFIGURATION */
            n = min_u32(min_u32(len, su->length), 1);
            if (data && n)
                data[0] = (uint8_t)vu.configured;
            *count = n;
            return VU_RC_OK;
        }
        break;
    case 0x81: /* standard, interface, device to host: HID report descriptor */
        if (rq == 0x06 && (su->value >> 8) == 0x22) {
            n = min_u32(min_u32(len, su->length), BB_HID_REPORT_DESC_LEN);
            if (data)
                mem_copy(data, bb_hid_report_desc, n);
            *count = n;
            return VU_RC_OK;
        }
        break;
    case 0xA1: /* class, interface, device to host: GET_REPORT (input) */
        if (rq == 0x01) {
            n = min_u32(min_u32(len, su->length), BB_REPORT_LEN);
            if (data)
                mem_copy(data, vu.st.last_report, n);
            *count = n;
            return VU_RC_OK;
        }
        break;
    default:
        break;
    }
    return VU_RC_STALL;
}

int vusb_transfer(int id, void *data, uint32_t len, void *option, vu_done_t cb, void *arg, void *gp)
{
    int s, r = VU_RC_OK, i;
    pipe_t *p;
    vu_setup_t su;
    int is_ctl = 0, res = 0;
    uint32_t cnt = 0;

    s = vos_lock();
    if (vu.state != ST_READY) {
        vos_unlock(s);
        return VU_RC_BADCONTEXT;
    }
    p = find_pipe(id);
    if (!p) {
        vos_unlock(s);
        return VU_RC_BADPIPE;
    }
    if (p->type == PT_CTL) {
        if (!option) {
            vu.st.bad_args++;
            vos_unlock(s);
            return VU_RC_BADOPTION;
        }
        if (vu.c_count >= MAXCOMPL) {
            vos_unlock(s);
            return VU_RC_IOREQ;
        }
        /* copy the setup packet now: it lives on the caller's stack */
        mem_copy(&su, option, sizeof su);
        res = control_request(&su, (uint8_t *)data, len, &cnt);
        vu.st.ctl_xfers++;
        if (res != VU_RC_OK)
            vu.st.ctl_stalls++;
        push_compl(cb, arg, gp, res, (int)cnt);
        is_ctl = 1;
    } else { /* interrupt IN */
        if (!data || len == 0) {
            vu.st.bad_args++;
            vos_unlock(s);
            return VU_RC_BADLENGTH;
        }
        r = VU_RC_IOREQ;
        for (i = 0; i < MAXPEND; i++) {
            if (!vu.pend[i].used) {
                vu.pend[i].used = 1;
                vu.pend[i].pipe = id;
                vu.pend[i].buf = (uint8_t *)data;
                vu.pend[i].len = len;
                vu.pend[i].cb = cb;
                vu.pend[i].arg = arg;
                vu.pend[i].gp = gp;
                r = VU_RC_OK;
                break;
            }
        }
    }
    vos_unlock(s);
    if (is_ctl && (vu.flags & BB_FLAG_VERBOSE))
        vos_log("ctl %02x %02x v=%04x i=%04x l=%d len=%d -> res=%d cnt=%d\n", su.requesttype, su.request,
                su.value, su.index, su.length, (int)len, res, (int)cnt);
    return r;
}

/* ---- worker side ---- */

static void drain_completions(void)
{
    int guard;
    for (guard = 0; guard < MAXCOMPL * 2; guard++) {
        compl_t c;
        int s = vos_lock();
        if (vu.c_count == 0) {
            vos_unlock(s);
            return;
        }
        c = vu.compl[vu.c_head];
        vu.c_head = (vu.c_head + 1) % MAXCOMPL;
        vu.c_count--;
        vos_unlock(s);
        if (c.cb)
            vos_call_done(c.cb, c.res, c.count, c.arg, c.gp);
    }
}

static void offer_device(void)
{
    for (;;) {
        vu_ldd_t *cand = 0, *p;
        int s, i, bound = 0, probed;

        s = vos_lock();
        if (vu.state == ST_READY && !vu.driver) {
            for (p = vu.head; p && !cand; p = p->next) {
                int seen = 0;
                for (i = 0; i < vu.noffered; i++)
                    if (vu.offered[i] == p)
                        seen = 1;
                if (!seen && vu.noffered < MAXLDD) {
                    vu.offered[vu.noffered++] = p;
                    cand = p;
                }
            }
            if (cand)
                vu.priv = 0; /* USBD clears private data before probing */
        }
        vos_unlock(s);
        if (!cand)
            return;
        probed = cand->probe ? vos_call_ldd(cand->probe, VU_DEVID, cand->gp) : 0;
        if (vu.flags & BB_FLAG_VERBOSE)
            vos_log("probe %s -> %d\n", cand->name, probed);
        if (!probed)
            continue;
        s = vos_lock();
        if (vu.state == ST_READY && !vu.driver) {
            vu.driver = cand;
            bound = 1;
        }
        vos_unlock(s);
        if (bound && cand->connect) {
            int rc = vos_call_ldd(cand->connect, VU_DEVID, cand->gp);
            if (vu.flags & BB_FLAG_VERBOSE)
                vos_log("connect %s -> %d\n", cand->name, rc);
        }
    }
}

static void deliver_input(void)
{
    uint16_t masks[BB_NSRC];
    uint32_t word;
    uint8_t rep[BB_REPORT_LEN];
    int s, i, dn = 0;

    for (i = 0; i < BB_NSRC; i++)
        masks[i] = 0;
    if (vu.flags & BB_FLAG_SCRIPT) {
        word = bb_script_word(vu.tick * VU_TICK_MS);
    } else {
        vos_sample(masks);
        word = bb_map(&vu.layout, masks);
    }
    bb_report_pack(word, rep);

    s = vos_lock();
    mem_copy(vu.st.last_report, rep, BB_REPORT_LEN);
    if (vu.state == ST_READY) {
        int due = !vu.sent_valid || mem_diff(rep, vu.sent_report, BB_REPORT_LEN) ||
                  (vu.tick - vu.sent_tick) >= vu.hb_ticks;
        if (due) {
            for (i = 0; i < MAXPEND; i++) {
                pend_t *p = &vu.pend[i];
                pipe_t *pp = p->used ? find_pipe(p->pipe) : 0;
                if (pp && pp->type == PT_INT_IN) {
                    if (vu.c_count >= MAXCOMPL)
                        break;
                    if (p->len < BB_REPORT_LEN) {
                        push_compl(p->cb, p->arg, p->gp, VU_RC_DATAOVER, 0);
                    } else {
                        mem_copy(p->buf, rep, BB_REPORT_LEN);
                        push_compl(p->cb, p->arg, p->gp, VU_RC_OK, BB_REPORT_LEN);
                    }
                    p->used = 0;
                    mem_copy(vu.sent_report, rep, BB_REPORT_LEN);
                    vu.sent_valid = 1;
                    vu.sent_tick = vu.tick;
                    vu.st.in_delivered++;
                    dn = (int)vu.st.in_delivered;
                    break; /* one request per tick */
                }
            }
        }
    }
    vos_unlock(s);
    if (dn && (vu.flags & BB_FLAG_VERBOSE) && (dn <= 8 || (dn % 256) == 0))
        vos_log("report #%d %02x %02x %02x %02x %02x\n", dn, rep[0], rep[1], rep[2], rep[3], rep[4]);
}

void vusb_tick(void)
{
    int s, became_ready = 0;

    s = vos_lock();
    vu.tick++;
    if (vu.state == ST_ATTACHING && vu.tick >= vu.attach_at) {
        vu.state = ST_READY;
        vu.noffered = 0;
        became_ready = 1;
    }
    vos_unlock(s);
    if (became_ready && (vu.flags & BB_FLAG_VERBOSE))
        vos_log("virtual Buzz! dongle 054C:0002 ready\n");
    offer_device();
    deliver_input();
    drain_completions();
}

void vusb_detach(void)
{
    vu_ldd_t *drv;
    int s;

    s = vos_lock();
    drv = vu.driver;
    abort_pending(0);
    drop_pipes_locked();
    vu.driver = 0;
    vu.priv = 0;
    vu.configured = 0;
    vu.sent_valid = 0;
    vu.state = ST_OFF;
    vos_unlock(s);
    if (drv && drv->disconnect)
        vos_call_ldd(drv->disconnect, VU_DEVID, drv->gp);
    drain_completions();
}
