/* ps2sdk-derived descriptor walk and LDD/API behavior:
 * Copyright ps2dev - http://www.ps2dev.org
 * Licensed under the Academic Free License version 2.0; see ../licenses/AFL-2.0.txt.
 * Modified for a virtual device, fixed endpoints and queued worker completions.
 * New code/changes Copyright (c) 2026 Gabarsolon, MIT; see ../../../LICENSE.
 * Provenance and license scope: ../THIRD_PARTY.md.
 */
#include "vusb.h"
#include "vusb_os.h"

#define MAXPIPE 8   /* slots 0..VU_MAXDEV-1 = default control pipes, the rest interrupt-IN pipes */
#define MAXPEND 8   /* pending interrupt-IN requests */
#define MAXCOMPL 16 /* completions waiting for the worker */
#define MAXLDD 8

enum { ST_OFF, ST_ATTACHING, ST_READY };
enum { PT_NONE, PT_CTL, PT_INT_IN };

typedef struct { int id; int type; int dev; } pipe_t;
typedef struct { int used; int pipe; uint8_t *buf; uint32_t len; vu_done_t cb; void *arg; void *gp; } pend_t;
typedef struct { vu_done_t cb; void *arg; void *gp; int res; int count; } compl_t;

typedef struct {
    int state;
    vu_ldd_t *driver; /* LDD the device is bound to */
    void *priv;
    vu_ldd_t *offered[MAXLDD];
    int noffered;
    int configured;
    int sent_valid;
    uint32_t sent_tick;
    uint8_t sent_report[BB_REPORT_LEN];
} dev_t_;

static struct {
    int ndev;
    uint32_t tick, attach_at, hb_ticks, attach_ticks;
    unsigned flags;
    bb_layout_t layout;
    bb_assign_t assignment;
    unsigned layout_id;
    vu_ldd_t *head, *tail;
    dev_t_ dev[VU_MAXDEV];
    int pipe_gen;
    pipe_t pipes[MAXPIPE];
    pend_t pend[MAXPEND];
    compl_t compl[MAXCOMPL];
    int c_head, c_count;
    vusb_stats_t st;
} vu;

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
    int d;

    mem_zero(&vu, sizeof vu);
    vu.flags = flags;
    vu.layout_id = layout;
    bb_assign_init(&vu.assignment);
    vu.attach_ticks = attach_delay_ms / VU_TICK_MS;
    vu.hb_ticks = heartbeat_ms / VU_TICK_MS;
    if (vu.hb_ticks == 0)
        vu.hb_ticks = 1;
    if (bb_layout_build(layout, &vu.layout) != 0)
        bb_layout_build(BB_LAYOUT_buzzers, &vu.layout);
    vu.ndev = vu.layout.ndongles;
    if (vu.ndev < 1 || vu.ndev > VU_MAXDEV)
        vu.ndev = 1;
    for (d = 0; d < vu.ndev; d++) {
        vu.pipes[d].id = d + 1; /* control pipe of dongle d+1 has the id d+1 */
        vu.pipes[d].type = PT_CTL;
        vu.pipes[d].dev = d;
        vu.dev[d].state = ST_ATTACHING;
    }
    vu.pipe_gen = 1;
    vu.attach_at = vu.attach_ticks;
}

const vusb_stats_t *vusb_stats(void) { return &vu.st; }
int vusb_is_ready(void) { return vu.dev[0].state == ST_READY; }
int vusb_dongles(void) { return vu.ndev; }

/* ---- helpers (call with the lock held) ---- */

static dev_t_ *dev_of(int devId)
{
    if (devId < VU_DEVID || devId > vu.ndev || vu.dev[devId - 1].state != ST_READY)
        return 0;
    return &vu.dev[devId - 1];
}

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

/* pipe ids: 1..VU_MAXDEV = default control pipes, otherwise 16 * generation + slot + 1 */
static pipe_t *find_pipe(int id)
{
    int slot;
    if (id >= 1 && id <= VU_MAXDEV)
        return (id <= vu.ndev) ? &vu.pipes[id - 1] : 0;
    slot = (id & 15) - 1;
    if (slot < VU_MAXDEV || slot >= MAXPIPE)
        return 0;
    return vu.pipes[slot].id == id ? &vu.pipes[slot] : 0;
}

/* abort everything pending on pipe id (0 = all pipes of device dev, or every device if dev < 0); queues
 * completions with USB_RC_ABORTED. A full completion queue drops the abort callback rather than blocking. */
static void abort_pending(int id, int dev)
{
    int i;
    for (i = 0; i < MAXPEND; i++) {
        pend_t *p = &vu.pend[i];
        pipe_t *pp;
        if (!p->used)
            continue;
        pp = find_pipe(p->pipe);
        if (id != 0 ? p->pipe == id : (dev < 0 || (pp && pp->dev == dev))) {
            push_compl(p->cb, p->arg, p->gp, VU_RC_ABORTED, 0);
            vu.st.aborted++;
            p->used = 0;
        }
    }
}

/* the device loses its driver: endpoints go, private data goes */
static void drop_device_locked(int d)
{
    int k;
    abort_pending(0, d);
    for (k = VU_MAXDEV; k < MAXPIPE; k++) {
        if (vu.pipes[k].dev == d && vu.pipes[k].type != PT_NONE) {
            vu.pipes[k].id = 0;
            vu.pipes[k].type = PT_NONE;
        }
    }
    vu.dev[d].driver = 0;
    vu.dev[d].priv = 0;
    vu.dev[d].configured = 0;
    vu.dev[d].sent_valid = 0;
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
    int s, i, d;
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
    for (d = 0; d < vu.ndev; d++) {
        for (i = 0; i < vu.dev[d].noffered; i++)
            if (vu.dev[d].offered[i] == drv)
                vu.dev[d].offered[i] = 0;
        /* like USBD: endpoints go, the driver and its private data are cleared, no disconnect call */
        if (vu.dev[d].driver == drv)
            drop_device_locked(d);
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
    ok = dev_of(devId) != 0;
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
    dev_t_ *d = dev_of(devId);
    int r = VU_RC_BADDEV;
    if (d) {
        d->priv = data;
        r = VU_RC_OK;
    }
    vos_unlock(s);
    return r;
}

void *vusb_get_private(int devId)
{
    int s = vos_lock();
    dev_t_ *d = dev_of(devId);
    void *r = d ? d->priv : 0;
    vos_unlock(s);
    return r;
}

int vusb_get_location(int devId, uint8_t *path)
{
    int s, i, ok;
    s = vos_lock();
    ok = dev_of(devId) != 0;
    vos_unlock(s);
    if (!ok)
        return VU_RC_BADDEV;
    if (!path)
        return VU_RC_BADOPTION;
    path[0] = (uint8_t)devId; /* root port 1 / 2, no hub: the two dongles have different locations */
    for (i = 1; i < 7; i++)
        path[i] = 0;
    return VU_RC_OK;
}

/* ---- pipes ---- */

int vusb_open_pipe(int devId, const vu_epdesc_t *desc)
{
    int s, i, id = -1;

    s = vos_lock();
    if (!dev_of(devId)) {
        vos_unlock(s);
        return -1;
    }
    if (!desc) { /* default control pipe: the device already has it open */
        vos_unlock(s);
        return devId;
    }
    /* only the one interrupt-IN endpoint that the descriptors advertise exists */
    if (desc->bEndpointAddress == 0x81 && (desc->bmAttributes & 3) == 3) {
        for (i = VU_MAXDEV; i < MAXPIPE; i++) {
            if (vu.pipes[i].id == 0) {
                id = 16 * (++vu.pipe_gen & 0xFFF) + i + 1;
                vu.pipes[i].id = id;
                vu.pipes[i].type = PT_INT_IN;
                vu.pipes[i].dev = devId - 1;
                break;
            }
        }
    }
    vos_unlock(s);
    if (vu.flags & BB_FLAG_VERBOSE)
        vos_log("OpenPipe dev=%d ep=%02x attr=%02x -> %d\n", devId, desc->bEndpointAddress, desc->bmAttributes, id);
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
            abort_pending(id, 0);
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
static int control_request(int d, const vu_setup_t *su, uint8_t *data, uint32_t len, uint32_t *count)
{
    uint8_t rt = su->requesttype, rq = su->request;
    uint32_t n;

    *count = 0;
    switch (rt) {
    case 0x00: /* standard, device, host to device */
        if (rq == 0x09) { /* SET_CONFIGURATION */
            if (su->value > 1)
                return VU_RC_STALL;
            vu.dev[d].configured = su->value;
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
            if (mem_diff(tmp, vu.st.lights[d], sizeof tmp))
                vu.st.lights_updates++;
            mem_copy(vu.st.lights[d], tmp, sizeof tmp);
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
                data[0] = (uint8_t)vu.dev[d].configured;
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
                mem_copy(data, vu.st.last_report[d], n);
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
    int is_ctl = 0, res = 0, d = 0;
    uint32_t cnt = 0;

    s = vos_lock();
    p = find_pipe(id);
    if (!p || vu.dev[p->dev].state != ST_READY) {
        int bad = p ? VU_RC_BADCONTEXT : VU_RC_BADPIPE;
        vos_unlock(s);
        return bad;
    }
    d = p->dev;
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
        res = control_request(d, &su, (uint8_t *)data, len, &cnt);
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
        vos_log("ctl d%d %02x %02x v=%04x i=%04x l=%d len=%d -> res=%d cnt=%d\n", d + 1, su.requesttype, su.request,
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
    int d;
    for (d = 0; d < vu.ndev; d++) {
        for (;;) {
            vu_ldd_t *cand = 0, *p;
            int s, i, bound = 0, probed;
            dev_t_ *dv = &vu.dev[d];

            s = vos_lock();
            if (dv->state == ST_READY && !dv->driver) {
                for (p = vu.head; p && !cand; p = p->next) {
                    int seen = 0;
                    for (i = 0; i < dv->noffered; i++)
                        if (dv->offered[i] == p)
                            seen = 1;
                    if (!seen && dv->noffered < MAXLDD) {
                        dv->offered[dv->noffered++] = p;
                        cand = p;
                    }
                }
                if (cand)
                    dv->priv = 0; /* USBD clears private data before probing */
            }
            vos_unlock(s);
            if (!cand)
                break;
            probed = cand->probe ? vos_call_ldd(cand->probe, d + 1, cand->gp) : 0;
            if (vu.flags & BB_FLAG_VERBOSE)
                vos_log("probe %s dev %d -> %d\n", cand->name, d + 1, probed);
            if (!probed)
                continue;
            s = vos_lock();
            if (dv->state == ST_READY && !dv->driver) {
                dv->driver = cand;
                bound = 1;
            }
            vos_unlock(s);
            if (bound && cand->connect) {
                int rc = vos_call_ldd(cand->connect, d + 1, cand->gp);
                if (vu.flags & BB_FLAG_VERBOSE)
                    vos_log("connect %s dev %d -> %d\n", cand->name, d + 1, rc);
            }
        }
    }
}

static void deliver_input(void)
{
    uint16_t masks[BB_NSRC];
    uint32_t word[VU_MAXDEV];
    uint8_t rep[VU_MAXDEV][BB_REPORT_LEN];
    int s, i, d, dn = 0;

    for (i = 0; i < BB_NSRC; i++)
        masks[i] = 0;
    if (vu.flags & BB_FLAG_SCRIPT) {
        word[0] = bb_script_word(vu.tick * VU_TICK_MS);
        for (d = 1; d < VU_MAXDEV; d++)
            word[d] = 0;
    } else {
        unsigned present = vos_sample(masks);
        bb_assign_update(&vu.assignment, &vu.layout, vu.layout_id, present, vu.tick * VU_TICK_MS);
        for (d = 0; d < VU_MAXDEV; d++)
            word[d] = bb_map_dongle(&vu.layout, masks, (unsigned)d);
    }
    for (d = 0; d < VU_MAXDEV; d++)
        bb_report_pack(word[d], rep[d]);

    s = vos_lock();
    for (d = 0; d < vu.ndev; d++) {
        dev_t_ *dv = &vu.dev[d];
        mem_copy(vu.st.last_report[d], rep[d], BB_REPORT_LEN);
        if (dv->state == ST_READY) {
            int due = !dv->sent_valid || mem_diff(rep[d], dv->sent_report, BB_REPORT_LEN) ||
                      (vu.tick - dv->sent_tick) >= vu.hb_ticks;
            if (due) {
                for (i = 0; i < MAXPEND; i++) {
                    pend_t *p = &vu.pend[i];
                    pipe_t *pp = p->used ? find_pipe(p->pipe) : 0;
                    if (pp && pp->type == PT_INT_IN && pp->dev == d) {
                        if (vu.c_count >= MAXCOMPL)
                            break;
                        if (p->len < BB_REPORT_LEN) {
                            push_compl(p->cb, p->arg, p->gp, VU_RC_DATAOVER, 0);
                        } else {
                            mem_copy(p->buf, rep[d], BB_REPORT_LEN);
                            push_compl(p->cb, p->arg, p->gp, VU_RC_OK, BB_REPORT_LEN);
                        }
                        p->used = 0;
                        mem_copy(dv->sent_report, rep[d], BB_REPORT_LEN);
                        dv->sent_valid = 1;
                        dv->sent_tick = vu.tick;
                        vu.st.in_delivered++;
                        if (d == 0)
                            dn = (int)vu.st.in_delivered;
                        break; /* one request per dongle per tick */
                    }
                }
            }
        }
    }
    vos_unlock(s);
    if (dn && (vu.flags & BB_FLAG_VERBOSE) && (dn <= 8 || (dn % 256) == 0))
        vos_log("report #%d %02x %02x %02x %02x %02x\n", dn, rep[0][0], rep[0][1], rep[0][2], rep[0][3], rep[0][4]);
}

void vusb_tick(void)
{
    int s, d, became_ready = 0;

    s = vos_lock();
    vu.tick++;
    for (d = 0; d < vu.ndev; d++) {
        if (vu.dev[d].state == ST_ATTACHING && vu.tick >= vu.attach_at) {
            vu.dev[d].state = ST_READY;
            vu.dev[d].noffered = 0;
            became_ready++;
        }
    }
    vos_unlock(s);
    if (became_ready && (vu.flags & BB_FLAG_VERBOSE))
        vos_log("%d virtual Buzz! dongle(s) 054C:0002 ready\n", became_ready);
    offer_device();
    deliver_input();
    drain_completions();
}

void vusb_detach(void)
{
    vu_ldd_t *drv[VU_MAXDEV];
    int s, d;

    s = vos_lock();
    for (d = 0; d < vu.ndev; d++) {
        drv[d] = vu.dev[d].driver;
        drop_device_locked(d);
        vu.dev[d].state = ST_OFF;
    }
    vos_unlock(s);
    for (d = 0; d < vu.ndev; d++)
        if (drv[d] && drv[d]->disconnect)
            vos_call_ldd(drv[d]->disconnect, d + 1, drv[d]->gp);
    drain_completions();
}
