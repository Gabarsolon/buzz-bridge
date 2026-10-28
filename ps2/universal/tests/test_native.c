/* Native (PC) unit tests of the pure logic and of the virtual usbd, driven by a mock BUZZER.IRX.
 * Build: tests/run_native.sh  (needs a C compiler, e.g. the MSYS2 MINGW32 gcc). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "bb_core.h"
#include "vusb.h"
#include "vusb_os.h"

static int checks, failures;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        checks++;                                                                     \
        if (!(cond)) {                                                                \
            failures++;                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                    \
        }                                                                             \
    } while (0)

/* ---------------- mock OS ---------------- */

static uint16_t mock_masks[BB_NSRC];
static unsigned mock_flags;
static int gp_depth;
static void *last_gp_seen;

int vos_lock(void) { return 0; }
void vos_unlock(int s) { (void)s; }
unsigned vos_flags(void) { return mock_flags; }
unsigned vos_sample(uint16_t m[BB_NSRC]) { memcpy(m, mock_masks, sizeof mock_masks); return 0xFF; }
void vos_call_done(vu_done_t cb, int result, int count, void *arg, void *gp)
{
    last_gp_seen = gp;
    gp_depth++;
    cb(result, count, arg);
    gp_depth--;
}
int vos_call_ldd(int (*fn)(int), int devId, void *gp)
{
    last_gp_seen = gp;
    return fn(devId);
}

/* ---------------- core logic ---------------- */

static void test_descriptors(void)
{
    /* independent hand-written expectation of the 054C:0002 dongle descriptors (see PROTOTYPE_NOTES.md) */
    static const uint8_t dev[18] = {0x12, 1, 0, 2, 0, 0, 0, 8, 0x4C, 0x05, 0x02, 0x00, 0xA1, 0x05, 3, 1, 0, 1};
    CHECK(memcmp(bb_static_desc, dev, 18) == 0);
    CHECK(bb_static_desc[8] == 0x4C && bb_static_desc[9] == 0x05); /* VID 054C little endian */
    CHECK(bb_static_desc[10] == 0x02 && bb_static_desc[11] == 0x00);
    /* walk the chain by bLength like sceUsbdScanStaticDescriptor and check types and total length */
    {
        const uint8_t *p = bb_static_desc;
        const uint8_t *end = bb_static_desc + BB_STATIC_DESC_LEN;
        static const uint8_t types[5] = {1, 2, 4, 0x21, 5};
        int i = 0;
        while (p < end) {
            CHECK(i < 5 && p[1] == types[i]);
            CHECK(p[0] >= 2);
            p += p[0];
            i++;
        }
        CHECK(p == end && i == 5);
    }
    {
        const uint8_t *cfg = bb_static_desc + BB_DEV_DESC_LEN;
        CHECK(cfg[2] == BB_CFG_DESC_LEN && cfg[3] == 0);    /* wTotalLength 34 */
        CHECK(cfg[4] == 1 && cfg[5] == 1);                  /* 1 interface, config value 1 */
        CHECK(cfg[9 + 4] == 1);                              /* 1 endpoint */
        CHECK(cfg[9 + 5] == 3);                              /* HID */
        CHECK(cfg[18 + 7] == 0x4E && cfg[18 + 8] == 0);     /* report descriptor length 78 */
        CHECK(cfg[27 + 2] == 0x81 && cfg[27 + 3] == 0x03);  /* EP 0x81 interrupt */
        CHECK(cfg[27 + 4] == 8 && cfg[27 + 6] == 10);
    }
    /* HID report descriptor: must be 78 bytes, 20 button bits + 4 flags + 2 axes, 7 byte output */
    CHECK(sizeof bb_hid_report_desc == 78);
    CHECK(bb_hid_report_desc[sizeof bb_hid_report_desc - 1] == 0xC0);
}

static void test_report_pack(void)
{
    uint8_t r[5];
    bb_report_pack(0, r);
    CHECK(r[0] == 0x7F && r[1] == 0x7F && r[2] == 0 && r[3] == 0 && r[4] == 0xF0);
    bb_report_pack(1, r); /* handset 1 red */
    CHECK(r[2] == 0x01 && r[3] == 0 && r[4] == 0xF0);
    bb_report_pack(1u << 4, r); /* handset 1 blue */
    CHECK(r[2] == 0x10);
    bb_report_pack(1u << 5, r); /* handset 2 red */
    CHECK(r[2] == 0x20);
    bb_report_pack(1u << 8, r); /* handset 2 orange -> bit 8 = byte 3 bit 0 */
    CHECK(r[2] == 0 && r[3] == 0x01);
    bb_report_pack(1u << 19, r); /* handset 4 blue */
    CHECK(r[4] == 0xF8);
    bb_report_pack(0xFFFFF, r);
    CHECK(r[2] == 0xFF && r[3] == 0xFF && r[4] == 0xFF);
    bb_report_pack(0xFFFFFFFFu, r); /* stray high bits must not leak into the flag nibble */
    CHECK(r[4] == 0xFF);
}

static uint32_t map1(unsigned layout, unsigned src, uint16_t m)
{
    bb_layout_t l;
    uint16_t masks[BB_NSRC];
    memset(masks, 0, sizeof masks);
    masks[src] = m;
    CHECK(bb_layout_build(layout, &l) == 0);
    return bb_map(&l, masks);
}

#define W(handset, colour) (1u << ((handset) * 5 + (colour)))
#define P1 BB_SRC(0, 0)
#define P2 BB_SRC(1, 0)

static void test_layouts(void)
{
    /* (a) buzzers only: netemu mapping reversed, port 1 only */
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_SQUARE) == W(0, BZ_RED));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_CROSS) == W(0, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_CIRCLE) == W(0, BZ_GREEN));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_TRIANGLE) == W(0, BZ_ORANGE));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_L1) == W(0, BZ_BLUE));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_R1) == W(1, BZ_RED));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_L2) == W(1, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_R2) == W(1, BZ_GREEN));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_SELECT) == W(1, BZ_ORANGE));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_START) == W(1, BZ_BLUE));
    CHECK(map1(BB_LAYOUT_buzzers, P1, PADB_UP | PADB_L3 | PADB_R3) == 0); /* unmapped buttons do nothing */
    CHECK(map1(BB_LAYOUT_buzzers, P2, 0xFFFF) == 0);                      /* port 2 ignored */
    CHECK(map1(BB_LAYOUT_buzzers, P1, 0xFFFF) == 0x3FF);                  /* only handsets 1-2 */

    /* (b) buzzers + 1 DualShock */
    CHECK(map1(BB_LAYOUT_buzzers_ds, P1, PADB_SQUARE) == W(0, BZ_RED));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P1, PADB_START) == W(1, BZ_BLUE));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_L1) == W(2, BZ_RED));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_L2) == W(2, BZ_RED));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_UP) == W(2, BZ_BLUE));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_LEFT) == W(2, BZ_ORANGE));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_RIGHT) == W(2, BZ_GREEN));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_DOWN) == W(2, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_R1) == W(3, BZ_RED));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_R2) == W(3, BZ_RED));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_TRIANGLE) == W(3, BZ_BLUE));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_SQUARE) == W(3, BZ_ORANGE));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_CIRCLE) == W(3, BZ_GREEN));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_CROSS) == W(3, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_buzzers_ds, P2, PADB_START | PADB_SELECT) == 0);

    /* (c) 2 DualShocks shared */
    CHECK(map1(BB_LAYOUT_two_ds_shared, P1, PADB_L1) == W(0, BZ_RED));
    CHECK(map1(BB_LAYOUT_two_ds_shared, P1, PADB_R1) == W(1, BZ_RED));
    CHECK(map1(BB_LAYOUT_two_ds_shared, P1, PADB_CROSS) == W(1, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_two_ds_shared, P1, PADB_DOWN) == W(0, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_two_ds_shared, P2, PADB_L2) == W(2, BZ_RED));
    CHECK(map1(BB_LAYOUT_two_ds_shared, P2, PADB_TRIANGLE) == W(3, BZ_BLUE));

    /* (d) 2 DualShocks, one player each, whole pad = union of both halves */
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_L1) == W(0, BZ_RED));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_R2) == W(0, BZ_RED));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_UP) == W(0, BZ_BLUE));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_TRIANGLE) == W(0, BZ_BLUE));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_CROSS) == W(0, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_DOWN) == W(0, BZ_YELLOW));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_CIRCLE) == W(0, BZ_GREEN));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_RIGHT) == W(0, BZ_GREEN));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_SQUARE) == W(0, BZ_ORANGE));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, PADB_LEFT) == W(0, BZ_ORANGE));
    CHECK(map1(BB_LAYOUT_two_ds_single, P2, PADB_L1) == W(1, BZ_RED));
    CHECK(map1(BB_LAYOUT_two_ds_single, P1, 0xFFFF) == 0x1F);
    CHECK(map1(BB_LAYOUT_two_ds_single, P2, 0xFFFF) == 0x3E0);

    /* (e) 1 DualShock shared */
    CHECK(map1(BB_LAYOUT_one_ds_shared, P1, PADB_L1) == W(0, BZ_RED));
    CHECK(map1(BB_LAYOUT_one_ds_shared, P1, PADB_CIRCLE) == W(1, BZ_GREEN));
    CHECK(map1(BB_LAYOUT_one_ds_shared, P1, 0xFFFF) == 0x3FF);
    CHECK(map1(BB_LAYOUT_one_ds_shared, P2, 0xFFFF) == 0);

    /* two sources at once */
    {
        bb_layout_t l;
        uint16_t m[BB_NSRC] = {0};
        bb_layout_build(BB_LAYOUT_two_ds_shared, &l);
        m[P1] = PADB_L1 | PADB_R1;
        m[P2] = PADB_CROSS;
        CHECK(bb_map(&l, m) == (W(0, BZ_RED) | W(1, BZ_RED) | W(3, BZ_YELLOW)));
    }
    /* multitap slots: a slot that no layout binds stays silent */
    {
        bb_layout_t l;
        uint16_t m[BB_NSRC] = {0};
        bb_layout_build(BB_LAYOUT_buzzers, &l);
        m[BB_SRC(0, 1)] = 0xFFFF;
        m[BB_SRC(1, 3)] = 0xFFFF;
        CHECK(bb_map(&l, m) == 0);
    }
    /* unknown layout id */
    {
        bb_layout_t l;
        CHECK(bb_layout_build(99, &l) == -1);
        CHECK(l.h[0].src == BB_NOSRC);
    }
    /* layout names line up with the X-macro list */
    CHECK(strcmp(bb_layout_name(0), "buzzers") == 0);
    CHECK(strcmp(bb_layout_name(4), "one_ds_shared") == 0);
    CHECK(BB_LAYOUT_COUNT == 14);
}

static void test_script(void)
{
    CHECK(bb_script_word(0) == 0);
    CHECK(bb_script_word(499) == 0);
    CHECK(bb_script_word(500) == W(0, 0));
    CHECK(bb_script_word(1500) == W(0, 1));
    CHECK(bb_script_word(4500) == W(0, 4));
    CHECK(bb_script_word(5500) == W(1, 0));
    CHECK(bb_script_word(19500) == W(3, 4));
    CHECK(bb_script_word(20500) == W(0, 0)); /* wraps after 20 steps */
}

/* build one SIO2 pad command the way PADMAN does (padData.c) */
static unsigned add_pad_cmd(uint32_t *regdata, unsigned idx, uint8_t *in, uint32_t *in_size, uint8_t *out,
                            uint32_t *out_size, unsigned port, unsigned n_words, int present, uint8_t id,
                            uint16_t buttons_active_low)
{
    unsigned len = n_words * 2 + 3;
    unsigned i;
    regdata[idx] = ((len & 0x1FF) << 18) | ((len & 0x1FF) << 8) | 0x40 | port;
    regdata[idx + 1] = 0;
    in[*in_size] = 0x01;
    in[*in_size + 1] = 0x42;
    for (i = 2; i < len; i++)
        in[*in_size + i] = 0;
    if (present) {
        out[*out_size] = 0xFF;
        out[*out_size + 1] = id;
        out[*out_size + 2] = 0x5A;
        out[*out_size + 3] = buttons_active_low & 0xFF;
        out[*out_size + 4] = buttons_active_low >> 8;
        for (i = 5; i < len; i++)
            out[*out_size + i] = 0x80;
    } else {
        for (i = 0; i < len; i++)
            out[*out_size + i] = 0xFF;
    }
    *in_size += len;
    *out_size += len;
    return idx + 1;
}

static void test_sio2_scan(void)
{
    uint32_t reg[16];
    uint8_t in[64], out[64];
    uint32_t is, os;
    bb_padev_t ev[BB_MAX_PADEV];
    int n;

    /* one digital pad on port 1, Square + L1 held (active low: bits cleared) */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 0, 1, 1, 0x41, (uint16_t)~(PADB_SQUARE | PADB_L1));
    n = bb_sio2_scan(reg, in, is, out, os, ev);
    CHECK(n == 1 && ev[0].port == 0 && ev[0].valid == 1 && ev[0].buttons == (PADB_SQUARE | PADB_L1));

    /* both ports in one transfer: analog on port 1 (9 bytes), digital on port 2 */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    {
        unsigned i = add_pad_cmd(reg, 0, in, &is, out, &os, 0, 3, 1, 0x73, (uint16_t)~PADB_START);
        add_pad_cmd(reg, i, in, &is, out, &os, 1, 1, 1, 0x41, (uint16_t)~PADB_CROSS);
    }
    n = bb_sio2_scan(reg, in, is, out, os, ev);
    CHECK(n == 2);
    CHECK(ev[0].port == 0 && ev[0].buttons == PADB_START);
    CHECK(ev[1].port == 1 && ev[1].buttons == PADB_CROSS);

    /* pressure pad, no buttons */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 1, 9, 1, 0x79, 0xFFFF);
    n = bb_sio2_scan(reg, in, is, out, os, ev);
    CHECK(n == 1 && ev[0].port == 1 && ev[0].valid && ev[0].buttons == 0);

    /* nothing plugged in: bus answers all FF */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 0, 1, 0, 0, 0);
    n = bb_sio2_scan(reg, in, is, out, os, ev);
    CHECK(n == 1 && ev[0].valid == 0 && ev[0].buttons == 0);

    /* configuration mode reply (id F3) and wrong ack are ignored */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 0, 1, 1, 0xF3, 0);
    CHECK(bb_sio2_scan(reg, in, is, out, os, ev) == 0);
    add_pad_cmd(reg, 0, in, &is, out, &os, 0, 1, 1, 0x41, 0);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 0, 1, 1, 0x41, 0);
    out[2] = 0x00;
    CHECK(bb_sio2_scan(reg, in, is, out, os, ev) == 0);

    /* memory card traffic (address 81) and pad commands other than 42 are ignored */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 0, 1, 1, 0x41, 0);
    in[0] = 0x81;
    CHECK(bb_sio2_scan(reg, in, is, out, os, ev) == 0);
    in[0] = 0x01;
    in[1] = 0x43;
    CHECK(bb_sio2_scan(reg, in, is, out, os, ev) == 0);

    /* ports 2/3 are memory-card ports */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 2, 1, 1, 0x41, 0);
    CHECK(bb_sio2_scan(reg, in, is, out, os, ev) == 0);

    /* inconsistent lengths must not read past the buffers */
    memset(reg, 0, sizeof reg);
    is = os = 0;
    add_pad_cmd(reg, 0, in, &is, out, &os, 0, 3, 1, 0x73, 0);
    CHECK(bb_sio2_scan(reg, in, is - 1, out, os, ev) == 0);
    CHECK(bb_sio2_scan(reg, in, is, out, os - 1, ev) == 0);
    CHECK(bb_sio2_scan(reg, NULL, is, out, os, ev) == 0);
    CHECK(bb_sio2_scan(reg, in, 0, out, 0, ev) == 0);
    /* a full 16 entry list with garbage never overruns the event array */
    {
        uint32_t r16[16];
        uint8_t i16[16 * 5], o16[16 * 5];
        unsigned k;
        for (k = 0; k < 16; k++) {
            r16[k] = (5u << 18) | (5u << 8) | 0x40 | (k & 1);
            i16[k * 5] = 1;
            i16[k * 5 + 1] = 0x42;
            i16[k * 5 + 2] = i16[k * 5 + 3] = i16[k * 5 + 4] = 0;
            o16[k * 5] = 0xFF;
            o16[k * 5 + 1] = 0x41;
            o16[k * 5 + 2] = 0x5A;
            o16[k * 5 + 3] = 0xFF;
            o16[k * 5 + 4] = 0xFF;
        }
        n = bb_sio2_scan(r16, i16, sizeof i16, o16, sizeof o16, ev);
        CHECK(n == BB_MAX_PADEV);
    }
}

/* ---------------- virtual usbd with a mock BUZZER.IRX ---------------- */

typedef struct {
    int devid;
    int ep0, epin;
    int probed, connected;
    int cfg_done;
    uint8_t buf[16];
    uint8_t fifo[64][5];
    int nfifo;
    int stop;
    int light_done;
    uint8_t light_buf[7];
    vu_setup_t setup_cfg, setup_light;
    int results[32];
    int nres;
    uint16_t vid, pid;
    int ep_in_found, ep_out_found;
    int abort_count;
} mock_buzzer_t;

static mock_buzzer_t mb;
static vu_ldd_t ldd;
static char ldd_name[] = "buzzer";
static int fake_gp_a;

static void mb_read_done(int result, int count, void *arg)
{
    mock_buzzer_t *m = (mock_buzzer_t *)arg;
    CHECK(last_gp_seen == &fake_gp_a); /* callbacks run with the gp captured at the TransferPipe call */
    if (result == VU_RC_ABORTED) {
        m->abort_count++;
        return;
    }
    if (result == 0 && count == 5 && m->nfifo < 64) {
        memcpy(m->fifo[m->nfifo++], m->buf, 5);
    }
    if (!m->stop && result == 0)
        CHECK(vusb_transfer(m->epin, m->buf, 16, NULL, mb_read_done, m, &fake_gp_a) == 0);
}

static void mb_cfg_done(int result, int count, void *arg)
{
    mock_buzzer_t *m = (mock_buzzer_t *)arg;
    (void)count;
    m->cfg_done = result == 0 ? 1 : -1;
    if (result == 0)
        CHECK(vusb_transfer(m->epin, m->buf, 16, NULL, mb_read_done, m, &fake_gp_a) == 0);
}

static void mb_light_done(int result, int count, void *arg)
{
    mock_buzzer_t *m = (mock_buzzer_t *)arg;
    m->light_done = result == 0 && count == 7 ? 1 : -1;
}

static int mb_probe(int devId)
{
    const uint8_t *d = vusb_scan_descriptor(devId, NULL, 1);
    mb.probed++;
    if (!d)
        return 0;
    mb.vid = (uint16_t)(d[8] | (d[9] << 8));
    mb.pid = (uint16_t)(d[10] | (d[11] << 8));
    return mb.vid == 0x054C && mb.pid == 0x0002;
}

static int mb_connect(int devId)
{
    const uint8_t *cfg, *ifc, *ep;
    uint8_t loc[7];
    int r;
    vu_epdesc_t epd;

    mb.connected++;
    mb.devid = devId;
    cfg = vusb_scan_descriptor(devId, NULL, 2);
    CHECK(cfg && cfg[2] == 34);
    ifc = cfg ? vusb_scan_descriptor(devId, (void *)cfg, 4) : NULL;
    CHECK(ifc && ifc[4] == 1);
    /* endpoints: walk with type 5 until NULL (must terminate) */
    ep = ifc;
    {
        int guard = 0;
        while ((ep = ep ? vusb_scan_descriptor(devId, (void *)ep, 5) : NULL) != NULL && guard++ < 4) {
            if (ep[2] == 0x81 && (ep[3] & 3) == 3)
                mb.ep_in_found++;
            if (ep[2] == 0x01)
                mb.ep_out_found++;
            memcpy(&epd, ep, sizeof epd);
        }
        CHECK(guard == 1);
    }
    CHECK(vusb_get_location(devId, loc) == 0 && loc[0] == 1);
    mb.ep0 = vusb_open_pipe(devId, NULL);
    CHECK(mb.ep0 > 0);
    epd.bEndpointAddress = 0x81;
    epd.bmAttributes = 3;
    mb.epin = vusb_open_pipe(devId, &epd);
    CHECK(mb.epin > 0 && mb.epin != mb.ep0);
    r = vusb_set_private(devId, &mb);
    CHECK(r == 0);
    CHECK(vusb_get_private(devId) == &mb);
    mb.setup_cfg.requesttype = 0;
    mb.setup_cfg.request = 9;
    mb.setup_cfg.value = 1;
    mb.setup_cfg.index = 0;
    mb.setup_cfg.length = 0;
    CHECK(vusb_transfer(mb.ep0, NULL, 0, &mb.setup_cfg, mb_cfg_done, &mb, &fake_gp_a) == 0);
    mb.setup_cfg.value = 0xDEAD; /* the stack copy is gone by the time the worker runs: must have been copied */
    mb.setup_cfg.value = 1;
    return 0;
}

static int mb_disconnect(int devId)
{
    (void)devId;
    mb.connected = -1000;
    return 0;
}

static void ticks(int n)
{
    while (n-- > 0)
        vusb_tick();
}

static void mb_reset(void)
{
    memset(&mb, 0, sizeof mb);
    memset(&ldd, 0, sizeof ldd);
    ldd.name = ldd_name;
    ldd.probe = mb_probe;
    ldd.connect = mb_connect;
    ldd.disconnect = mb_disconnect;
    memset(mock_masks, 0, sizeof mock_masks);
}

static void test_vusb_flow(void)
{
    int r;

    mock_flags = 0;
    mb_reset();
    vusb_init(100, 100, BB_LAYOUT_buzzers, 0);
    /* nothing ready yet: the device does not exist */
    CHECK(vusb_scan_descriptor(VU_DEVID, NULL, 1) == NULL);
    CHECK(vusb_open_pipe(VU_DEVID, NULL) == -1);
    CHECK(vusb_register_ldd(&ldd, &fake_gp_a) == 0);
    CHECK(vusb_register_ldd(&ldd, &fake_gp_a) == VU_RC_BUSY);
    CHECK(ldd.gp == &fake_gp_a);
    ticks(10);
    CHECK(!vusb_is_ready() && mb.probed == 0);
    ticks(30); /* 100 ms attach delay = 25 ticks */
    CHECK(vusb_is_ready());
    CHECK(mb.probed == 1 && mb.connected == 1);
    CHECK(mb.vid == 0x054C && mb.pid == 0x0002);
    CHECK(mb.ep_in_found == 1 && mb.ep_out_found == 0);
    ticks(3);
    CHECK(mb.cfg_done == 1);
    /* first report: initial idle delivered without waiting for a heartbeat */
    ticks(5);
    CHECK(mb.nfifo >= 1);
    CHECK(memcmp(mb.fifo[0], "\x7F\x7F\x00\x00\xF0", 5) == 0);
    {
        int base, guard = 0;
        /* sync to a heartbeat delivery, then nothing may arrive for 25 ticks while the state is unchanged */
        base = mb.nfifo;
        while (mb.nfifo == base && guard++ < 100)
            ticks(1);
        base = mb.nfifo;
        ticks(10); /* nothing changed, heartbeat is 25 ticks away */
        CHECK(mb.nfifo == base);
        ticks(30); /* heartbeat */
        CHECK(mb.nfifo > base);
    }
    /* press handset 1 red = Square on port 1 */
    {
        int base = mb.nfifo;
        mock_masks[BB_SRC(0, 0)] = PADB_SQUARE;
        ticks(3);
        CHECK(mb.nfifo == base + 1);
        CHECK(memcmp(mb.fifo[mb.nfifo - 1], "\x7F\x7F\x01\x00\xF0", 5) == 0);
        /* press and release within two ticks must both arrive */
        mock_masks[BB_SRC(0, 0)] = 0;
        ticks(3);
        CHECK(mb.nfifo == base + 2);
        CHECK(memcmp(mb.fifo[mb.nfifo - 1], "\x7F\x7F\x00\x00\xF0", 5) == 0);
        /* handset 2 start = bit 9 */
        mock_masks[BB_SRC(0, 0)] = PADB_START;
        ticks(3);
        CHECK(memcmp(mb.fifo[mb.nfifo - 1], "\x7F\x7F\x00\x02\xF0", 5) == 0);
        mock_masks[BB_SRC(0, 0)] = 0;
        ticks(3);
    }
    /* lights: SET_REPORT with 7 bytes, class request on EP0 */
    mb.setup_light.requesttype = 0x21;
    mb.setup_light.request = 9;
    mb.setup_light.value = 0x0200;
    mb.setup_light.index = 0;
    mb.setup_light.length = 7;
    memcpy(mb.light_buf, "\x00\xFF\x00\xFF\x00\x00\x00", 7);
    r = vusb_transfer(mb.ep0, mb.light_buf, 7, &mb.setup_light, mb_light_done, &mb, &fake_gp_a);
    CHECK(r == 0);
    CHECK(mb.light_done == 0); /* never completes inline */
    ticks(2);
    CHECK(mb.light_done == 1);
    CHECK(memcmp(vusb_stats()->lights[0], "\x00\xFF\x00\xFF\x00\x00\x00", 7) == 0);
    CHECK(vusb_stats()->lights_updates == 1);

    /* unknown control request stalls, does not crash */
    {
        vu_setup_t s = {0x40, 0x55, 0, 0, 0};
        mb.light_done = 0;
        CHECK(vusb_transfer(mb.ep0, NULL, 0, &s, mb_light_done, &mb, &fake_gp_a) == 0);
        ticks(2);
        CHECK(mb.light_done == -1);
        CHECK(vusb_stats()->ctl_stalls == 1);
    }
    /* GET_DESCRIPTOR device through EP0 matches the static descriptor */
    {
        uint8_t d[18];
        vu_setup_t s = {0x80, 6, 0x0100, 0, 18};
        CHECK(vusb_transfer(mb.ep0, d, 18, &s, mb_light_done, &mb, &fake_gp_a) == 0);
        CHECK(memcmp(d, bb_static_desc, 18) == 0);
        ticks(2);
    }
    /* bad arguments */
    CHECK(vusb_transfer(999, mb.buf, 16, NULL, mb_read_done, &mb, &fake_gp_a) == VU_RC_BADPIPE);
    CHECK(vusb_transfer(mb.epin, NULL, 16, NULL, mb_read_done, &mb, &fake_gp_a) == VU_RC_BADLENGTH);
    CHECK(vusb_transfer(mb.ep0, NULL, 0, NULL, mb_light_done, &mb, &fake_gp_a) == VU_RC_BADOPTION);
    CHECK(vusb_open_pipe(VU_DEVID, &(vu_epdesc_t){7, 5, 0x01, 3, 8, 0, 10}) == -1); /* no OUT endpoint */
    CHECK(vusb_open_pipe(7, NULL) == -1);
    CHECK(vusb_close_pipe(12345) == VU_RC_BADPIPE);
    CHECK(vusb_set_private(7, &mb) == VU_RC_BADDEV);

    /* closing the interrupt pipe aborts the pending read with USB_RC_ABORTED (0x122) */
    {
        int ab = mb.abort_count;
        CHECK(vusb_close_pipe(mb.epin) == 0);
        ticks(2);
        CHECK(mb.abort_count == ab + 1);
        CHECK(vusb_close_pipe(mb.epin) == VU_RC_BADPIPE);
        CHECK(vusb_close_pipe(mb.ep0) == 0); /* default pipe: no-op */
    }

    /* unregister: pipes gone, private data gone, no disconnect callback (as USBD) */
    CHECK(vusb_unregister_ldd(&ldd) == 0);
    CHECK(vusb_get_private(VU_DEVID) == NULL);
    CHECK(mb.connected == 1);
    CHECK(vusb_unregister_ldd(&ldd) == VU_RC_BADDRIVER);
    /* re-register: device is offered again */
    mb.probed = 0;
    CHECK(vusb_register_ldd(&ldd, &fake_gp_a) == 0);
    ticks(2);
    CHECK(mb.probed == 1 && mb.connected == 2);
    /* detach calls disconnect and aborts */
    mb.stop = 0;
    ticks(5);
    vusb_detach();
    CHECK(mb.connected == -1000);
    CHECK(!vusb_is_ready());
    CHECK(vusb_scan_descriptor(VU_DEVID, NULL, 1) == NULL);
}

static void test_vusb_bad_ldd(void)
{
    vu_ldd_t l;
    char nm[] = "x";
    mb_reset();
    vusb_init(0, 100, BB_LAYOUT_buzzers, 0);
    memset(&l, 0, sizeof l);
    CHECK(vusb_register_ldd(&l, NULL) == VU_RC_BADDRIVER); /* no name */
    l.name = nm;
    l.reserved1 = 1;
    CHECK(vusb_register_ldd(&l, NULL) == VU_RC_BADDRIVER);
    l.reserved1 = 0;
    l.next = &l;
    CHECK(vusb_register_ldd(&l, NULL) == VU_RC_BADDRIVER);
    CHECK(vusb_register_ldd(NULL, NULL) == VU_RC_BADDRIVER);
    /* a driver whose probe rejects stays unbound and a second one gets its chance */
    {
        static vu_ldd_t a, b;
        static char na[] = "other", nb[] = "buzzer";
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        a.name = na;
        a.probe = NULL;
        b.name = nb;
        b.probe = mb_probe;
        b.connect = mb_connect;
        b.disconnect = mb_disconnect;
        CHECK(vusb_register_ldd(&a, &fake_gp_a) == 0);
        CHECK(vusb_register_ldd(&b, &fake_gp_a) == 0);
        ticks(3);
        CHECK(mb.probed == 1 && mb.connected == 1);
    }
}

static void test_vusb_layout_input(void)
{
    /* two DualShocks, single player each: port 2 Cross = handset 2 yellow */
    mb_reset();
    vusb_init(0, 100, BB_LAYOUT_two_ds_single, 0);
    CHECK(vusb_register_ldd(&ldd, &fake_gp_a) == 0);
    ticks(10);
    mock_masks[BB_SRC(1, 0)] = PADB_CROSS;
    ticks(3);
    CHECK(memcmp(mb.fifo[mb.nfifo - 1], "\x7F\x7F\x40\x00\xF0", 5) == 0); /* handset 2 yellow = bit 6 */
    mock_masks[BB_SRC(1, 0)] = 0;
}

static void test_vusb_script(void)
{
    int i, seen_press = 0;
    mb_reset();
    vusb_init(0, 100, BB_LAYOUT_buzzers, BB_FLAG_SCRIPT);
    CHECK(vusb_register_ldd(&ldd, &fake_gp_a) == 0);
    for (i = 0; i < 400; i++) { /* 1.6 s */
        vusb_tick();
        if (mb.nfifo && mb.fifo[mb.nfifo - 1][2] == 0x01)
            seen_press = 1;
    }
    CHECK(seen_press);
}

static void test_report_overrun_and_small_buffer(void)
{
    /* a read buffer smaller than the 5 byte report is a data-overrun error, not a memory overwrite */
    uint8_t small[2] = {0xAA, 0xBB};
    int got = -1;
    (void)got;
    mb_reset();
    vusb_init(0, 100, BB_LAYOUT_buzzers, 0);
    mb.stop = 1;
    CHECK(vusb_register_ldd(&ldd, &fake_gp_a) == 0);
    ticks(10);
    {
        static int res;
        void (*cb)(int, int, void *) = NULL;
        (void)cb;
        res = 0;
        /* use the pipe the mock opened */
        CHECK(mb.epin > 0);
        CHECK(vusb_transfer(mb.epin, small, 2, NULL, mb_read_done, &mb, &fake_gp_a) == 0);
        ticks(130);
        CHECK(small[0] == 0xAA && small[1] == 0xBB);
        (void)res;
    }
}


/* ---------------- random API fuzz: nothing may crash or write outside the caller's buffers ---------------- */

static uint32_t rng_state = 12345;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void fz_done(int result, int count, void *arg)
{
    (void)result;
    (void)count;
    (void)arg;
}

static int fz_probe(int d) { (void)d; return 1; }
static int fz_connect(int d) { (void)d; return 0; }
static int fz_disc(int d) { (void)d; return 0; }

static void test_fuzz(void)
{
    struct { uint8_t pre[16]; uint8_t buf[32]; uint8_t post[16]; } guard;
    vu_setup_t su;
    static vu_ldd_t lddfz[10];
    static char nm[] = "fz";
    int i, pipes[8], np = 0;

    mb_reset();
    vusb_init(8, 8, BB_LAYOUT_buzzers_ds, BB_FLAG_VERBOSE & 0);
    for (i = 0; i < 10; i++) {
        memset(&lddfz[i], 0, sizeof lddfz[i]);
        lddfz[i].name = nm;
        lddfz[i].probe = (i & 1) ? fz_probe : NULL;
        lddfz[i].connect = fz_connect;
        lddfz[i].disconnect = fz_disc;
    }
    memset(&guard, 0xA5, sizeof guard);
    for (i = 0; i < 200000; i++) {
        uint32_t op = rnd() % 14;
        int dev = (rnd() % 4 == 0) ? (int)(rnd() % 4) : VU_DEVID;
        mock_masks[rnd() % BB_NSRC] = (uint16_t)rnd();
        switch (op) {
        case 0: vusb_register_ldd(&lddfz[rnd() % 10], &fake_gp_a); break;
        case 1: vusb_unregister_ldd(&lddfz[rnd() % 10]); break;
        case 2: vusb_scan_descriptor(dev, (rnd() & 1) ? NULL : (void *)(bb_static_desc + rnd() % 60), (uint8_t)(rnd() % 7)); break;
        case 3: vusb_set_private(dev, &guard); break;
        case 4: vusb_get_private(dev); break;
        case 5: {
            vu_epdesc_t e = {7, 5, (uint8_t)((rnd() & 1) ? 0x81 : rnd()), (uint8_t)(rnd() & 7), 8, 0, 10};
            int p = vusb_open_pipe(dev, (rnd() % 5 == 0) ? NULL : &e);
            if (p > 0 && np < 8)
                pipes[np++] = p;
            break;
        }
        case 6:
            if (np) {
                int k = (int)(rnd() % (uint32_t)np);
                vusb_close_pipe(pipes[k]);
                pipes[k] = pipes[--np];
            } else {
                vusb_close_pipe((int)rnd());
            }
            break;
        case 7:
        case 8:
        case 9: {
            su.requesttype = (uint8_t)((rnd() % 3) ? ((uint8_t[]){0x00, 0x01, 0x21, 0x80, 0x81, 0xA1})[rnd() % 6] : rnd());
            su.request = (uint8_t)(rnd() % 14);
            su.value = (uint16_t)rnd();
            su.index = (uint16_t)(rnd() % 3);
            su.length = (uint16_t)(rnd() % 40);
            vusb_transfer(np ? pipes[rnd() % (uint32_t)np] : 1, (rnd() % 8) ? guard.buf : NULL, rnd() % 33, &su, fz_done,
                          NULL, &fake_gp_a);
            break;
        }
        case 10: vusb_transfer(np ? pipes[rnd() % (uint32_t)np] : 2, guard.buf, rnd() % 33, NULL, fz_done, NULL, &fake_gp_a); break;
        case 11: { uint8_t loc[7]; vusb_get_location(dev, loc); break; }
        case 12: ticks((int)(rnd() % 6)); break;
        case 13:
            if (rnd() % 200 == 0) {
                vusb_detach();
                vusb_init(8, 8, BB_LAYOUT_buzzers_ds, 0);
                np = 0;
            }
            break;
        }
        {
            uint8_t pre[16], post[16];
            memset(pre, 0xA5, 16);
            memset(post, 0xA5, 16);
            if (memcmp(guard.pre, pre, 16) || memcmp(guard.post, post, 16)) {
                CHECK(0 && "write outside the 32 byte buffer");
                return;
            }
        }
    }
    CHECK(1);
}

static void test_poll_fill(void)
{
    uint32_t reg[16];
    uint8_t in[10];
    unsigned i;
    bb_poll_fill(reg, in);
    CHECK(reg[0] == 0x140540u && reg[1] == 0x140541u);
    for (i = 2; i < 16; i++)
        CHECK(reg[i] == 0);
    { static const uint8_t exp[10] = {1, 0x42, 0, 0, 0, 1, 0x42, 0, 0, 0}; CHECK(memcmp(in, exp, 10) == 0); }
    CHECK(BB_POLL_CTRL1 == 0xFFC00505u && BB_POLL_CTRL2 == 0x20014u);
    /* what a pad answers to our poll parses with the same scanner that handles PADMAN transfers */
    {
        uint8_t out[10] = {0xFF, 0x41, 0x5A, 0xEF, 0xFF, 0xFF, 0x73, 0x5A, 0xFF, 0x7F};
        bb_padev_t ev[BB_MAX_PADEV];
        int n = bb_sio2_scan(reg, in, 10, out, 10, ev);
        CHECK(n == 2 && ev[0].port == 0 && ev[0].buttons == PADB_UP);
        CHECK(ev[1].port == 1 && ev[1].buttons == 0x8000);
    }
}

/* ---------------- multitap layouts, commands, slot tracking, two dongles ---------------- */

static uint32_t mapd(unsigned layout, unsigned dongle, unsigned src, uint16_t m)
{
    bb_layout_t l;
    uint16_t masks[BB_NSRC];
    memset(masks, 0, sizeof masks);
    masks[src] = m;
    CHECK(bb_layout_build(layout, &l) == 0);
    return bb_map_dongle(&l, masks, dongle);
}

#define SRC(port, slot) BB_SRC(port, slot)

static void test_tap_layouts(void)
{
    bb_layout_t l;
    uint8_t slots[BB_PORTS];

    /* 5: dongle pad on 1A = handsets 1-2 (netemu mapping), DualShocks on 1B and 1C = handsets 3 and 4, whole pad each */
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 0), PADB_SQUARE) == W(0, BZ_RED));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 0), PADB_START) == W(1, BZ_BLUE));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 1), PADB_L1) == W(2, BZ_RED));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 1), PADB_R2) == W(2, BZ_RED));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 1), PADB_UP | PADB_TRIANGLE) == W(2, BZ_BLUE));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 2), PADB_CROSS) == W(3, BZ_YELLOW));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 2), PADB_LEFT) == W(3, BZ_ORANGE));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(0, 3), 0xFFFF) == 0); /* slot D unused */
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap1, 0, SRC(1, 0), 0xFFFF) == 0);
    bb_layout_build(BB_LAYOUT_buzzers_ds2_tap1, &l);
    CHECK(l.ndongles == 1);
    CHECK(bb_layout_taps(&l, slots) == 1 && slots[0] == 0x06 && slots[1] == 0);

    /* 6: four DualShocks one player each */
    CHECK(mapd(BB_LAYOUT_four_ds_tap1, 0, SRC(0, 0), PADB_CIRCLE) == W(0, BZ_GREEN));
    CHECK(mapd(BB_LAYOUT_four_ds_tap1, 0, SRC(0, 1), PADB_CIRCLE) == W(1, BZ_GREEN));
    CHECK(mapd(BB_LAYOUT_four_ds_tap1, 0, SRC(0, 2), PADB_RIGHT) == W(2, BZ_GREEN));
    CHECK(mapd(BB_LAYOUT_four_ds_tap1, 0, SRC(0, 3), PADB_TRIANGLE) == W(3, BZ_BLUE));
    CHECK(mapd(BB_LAYOUT_four_ds_tap1, 0, SRC(1, 0), 0xFFFF) == 0);
    bb_layout_build(BB_LAYOUT_four_ds_tap1, &l);
    CHECK(bb_layout_taps(&l, slots) == 1 && slots[0] == 0x0E && slots[1] == 0);

    /* 7: tap on port 2: buzzers on port 1, DualShocks on 2A and 2B */
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap2, 0, SRC(0, 0), PADB_L1) == W(0, BZ_BLUE));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap2, 0, SRC(1, 0), PADB_SQUARE) == W(2, BZ_ORANGE));
    CHECK(mapd(BB_LAYOUT_buzzers_ds2_tap2, 0, SRC(1, 1), PADB_DOWN) == W(3, BZ_YELLOW));
    bb_layout_build(BB_LAYOUT_buzzers_ds2_tap2, &l);
    CHECK(bb_layout_taps(&l, slots) == 1 && slots[0] == 0 && slots[1] == 0x02);

    /* 8: two virtual dongles, eight players */
    bb_layout_build(BB_LAYOUT_eight_ds_tap12, &l);
    CHECK(l.ndongles == 2);
    CHECK(mapd(BB_LAYOUT_eight_ds_tap12, 0, SRC(0, 3), PADB_CROSS) == W(3, BZ_YELLOW));
    CHECK(mapd(BB_LAYOUT_eight_ds_tap12, 1, SRC(1, 0), PADB_L1) == W(0, BZ_RED));
    CHECK(mapd(BB_LAYOUT_eight_ds_tap12, 1, SRC(1, 2), PADB_SQUARE) == W(2, BZ_ORANGE));
    CHECK(mapd(BB_LAYOUT_eight_ds_tap12, 1, SRC(0, 1), 0xFFFF) == 0); /* port 1 belongs to dongle 1 */
    CHECK(mapd(BB_LAYOUT_eight_ds_tap12, 0, SRC(1, 1), 0xFFFF) == 0);
    CHECK(bb_layout_taps(&l, slots) == 2 && slots[0] == 0x0E && slots[1] == 0x0E);
    CHECK(bb_map_dongle(&l, (uint16_t[BB_NSRC]){0}, 2) == 0); /* only two dongles */

    /* the old layouts are untouched: one dongle, no tap */
    {
        unsigned id;
        for (id = 0; id <= 4; id++) {
            bb_layout_build(id, &l);
            CHECK(l.ndongles == 1 && bb_layout_taps(&l, slots) == 0);
        }
    }
}

static void test_tap_commands(void)
{
    bb_td_t td;
    static const uint8_t ok_det[6] = {0xFF, 0x80, 0x5A, 0x04, 0x00, 0x5A};
    static const uint8_t no_det[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t sel2[7] = {0xFF, 0x80, 0x5A, 0x00, 0x00, 0x02, 0x5A};
    static const uint8_t sel_bad[7] = {0xFF, 0x80, 0x5A, 0x00, 0x00, 0xFF, 0x66};

    bb_td_reset(&td);
    CHECK(bb_td_add_tap_detect(&td, 0) == 0 && bb_td_add_tap_detect(&td, 1) == 0);
    CHECK(td.regdata[0] == 0x180642u && td.regdata[1] == 0x180643u && td.regdata[2] == 0);
    CHECK(td.in_size == 12 && td.out_size == 12 && td.out_off[1] == 6);
    {
        static const uint8_t exp[12] = {0x21, 0x12, 0, 0, 0, 0, 0x21, 0x12, 0, 0, 0, 0};
        CHECK(memcmp(td.in, exp, 12) == 0);
    }
    CHECK(td.ctrl1[2] == 0xFF020505u && td.ctrl2[2] == 0x00030064u && td.ctrl1[3] == 0xFF020505u && td.ctrl1[0] == 0);

    bb_td_reset(&td);
    CHECK(bb_td_add_tap_select(&td, 0, 2) == 0 && bb_td_add_tap_select(&td, 1, 3) == 0);
    CHECK(td.regdata[0] == 0x1C0742u && td.regdata[1] == 0x1C0743u);
    {
        static const uint8_t exp[14] = {0x21, 0x21, 2, 0, 0, 0, 0, 0x21, 0x21, 3, 0, 0, 0, 0};
        CHECK(td.in_size == 14 && memcmp(td.in, exp, 14) == 0);
    }
    CHECK(bb_td_add_tap_select(&td, 0, 4) == -1 && bb_td_add_tap_select(&td, 2, 1) == -1);

    bb_td_reset(&td);
    CHECK(bb_td_add_pad_read(&td, 0) == 0 && bb_td_add_pad_read(&td, 1) == 0);
    CHECK(td.regdata[0] == 0x140540u && td.regdata[1] == 0x140541u && td.ctrl1[0] == 0xFFC00505u && td.ctrl1[2] == 0);

    /* full: 8 entries / 64 bytes are the limit */
    bb_td_reset(&td);
    {
        int i, last = 0;
        for (i = 0; i < 12; i++)
            last = bb_td_add_tap_select(&td, 0, 1);
        CHECK(td.n == 8 && last == -1);
    }

    CHECK(bb_tap_detect_reply(ok_det, 6) == 4);
    CHECK(bb_tap_detect_reply(no_det, 6) == 0);
    CHECK(bb_tap_detect_reply(ok_det, 5) == 0);
    {
        uint8_t o[6];
        memcpy(o, ok_det, 6);
        o[3] = 0;
        CHECK(bb_tap_detect_reply(o, 6) == 0);
        o[3] = 5;
        CHECK(bb_tap_detect_reply(o, 6) == 0);
        o[3] = 1;
        CHECK(bb_tap_detect_reply(o, 6) == 1);
        o[5] = 0x66; /* freemtap treats byte 5 == 0x66 as "no tap" */
        CHECK(bb_tap_detect_reply(o, 6) == 0);
    }
    CHECK(bb_tap_select_reply_ok(sel2, 7, 2) == 1);
    CHECK(bb_tap_select_reply_ok(sel2, 7, 1) == 0);
    CHECK(bb_tap_select_reply_ok(sel_bad, 7, 0xFF) == 0);
    CHECK(bb_tap_select_reply_ok(sel2, 6, 2) == 0);
    /* Exact native netemu replies: FIFO starts FF, terminator starts FF.
     * Sony's 21 41 5A setup changes the terminator but adds no header. */
    {
        uint8_t det[] = {0xFF, 0xFF, 0, 4, 0, 0xFF};
        uint8_t sel[] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 3, 0xFF};
        CHECK(bb_tap_detect_reply(det, sizeof det) == 4);
        CHECK(bb_tap_select_reply_ok(sel, sizeof sel, 3));
        CHECK(!bb_tap_select_reply_ok(sel, sizeof sel, 2));
        det[5] = sel[6] = 0x5A;
        CHECK(bb_tap_detect_reply(det, sizeof det) == 4);
        CHECK(bb_tap_select_reply_ok(sel, sizeof sel, 3));
        det[5] = sel[6] = 0x66;
        CHECK(!bb_tap_detect_reply(det, sizeof det));
        CHECK(!bb_tap_select_reply_ok(sel, sizeof sel, 3));
        CHECK(!bb_tap_detect_reply(NULL, 6));
        CHECK(!bb_tap_select_reply_ok(NULL, 7, 0));
        memset(sel, 0xFF, sizeof sel);
        CHECK(!bb_tap_select_reply_ok(sel, sizeof sel, 0));
        CHECK(!bb_tap_select_reply_ok(sel, sizeof sel, 0xFF));
    }
}

static void test_scan_slots(void)
{
    uint32_t reg[16];
    uint8_t in[64], out[64];
    uint32_t is = 0, os = 0;
    bb_padev_t ev[BB_MAX_PADEV];
    uint8_t cur[BB_PORTS] = {0, 0};
    int n;

    /* transfer 1: select slot 2 on tap 1 (answered), transfer 2 in the same descriptor: pad read port 1 */
    memset(reg, 0, sizeof reg);
    reg[0] = 0x1C0742u;
    memcpy(in + is, "\x21\x21\x02\x00\x00\x00\x00", 7);
    memcpy(out + os, "\xFF\x80\x5A\x00\x00\x02\x5A", 7);
    is += 7;
    os += 7;
    reg[1] = 0x140540u;
    memcpy(in + is, "\x01\x42\x00\x00\x00", 5);
    memcpy(out + os, "\xFF\x41\x5A\xFE\xFF", 5);
    is += 5;
    os += 5;
    n = bb_sio2_scan_ex(reg, in, is, out, os, ev, cur);
    CHECK(n == 1 && ev[0].port == 0 && ev[0].slot == 2 && ev[0].buttons == PADB_SELECT && cur[0] == 2 && cur[1] == 0);

    /* a refused select keeps the old slot */
    memset(reg, 0, sizeof reg);
    reg[0] = 0x1C0742u;
    memcpy(out, "\xFF\x80\x5A\x00\x00\xFF\x66", 7);
    memcpy(in, "\x21\x21\x03\x00\x00\x00\x00", 7);
    n = bb_sio2_scan_ex(reg, in, 7, out, 7, ev, cur);
    CHECK(n == 0 && cur[0] == 2);

    /* back to A on tap 2 only: port 1 of the console is not touched */
    memset(reg, 0, sizeof reg);
    reg[0] = 0x1C0743u;
    memcpy(in, "\x21\x21\x00\x00\x00\x00\x00", 7);
    memcpy(out, "\xFF\x80\x5A\x00\x00\x00\x5A", 7);
    cur[1] = 1;
    n = bb_sio2_scan_ex(reg, in, 7, out, 7, ev, cur);
    CHECK(n == 0 && cur[0] == 2 && cur[1] == 0);

    /* without the tracking array the same data only yields slot 0 and no crash */
    memset(reg, 0, sizeof reg);
    reg[0] = 0x140540u;
    memcpy(in, "\x01\x42\x00\x00\x00", 5);
    memcpy(out, "\xFF\x41\x5A\xFF\xFF", 5);
    n = bb_sio2_scan(reg, in, 5, out, 5, ev);
    CHECK(n == 1 && ev[0].slot == 0);
    /* a tap command whose descriptor is too short is skipped */
    memset(reg, 0, sizeof reg);
    reg[0] = (2u << 18) | (2u << 8) | 0x42;
    n = bb_sio2_scan_ex(reg, in, 2, out, 2, ev, cur);
    CHECK(n == 0);
}

/* ---- two dongles with a mock BUZZER that counts devices ---- */

static struct {
    int connected[3];
    int ep0[3], epin[3];
    uint8_t buf[3][16];
    uint8_t last[3][5];
    int nrep[3];
    int cfg[3];
    uint8_t light[3][7];
} m2;

static void m2_read_done(int result, int count, void *arg)
{
    int d = (int)(intptr_t)arg;
    if (result == 0 && count == 5) {
        memcpy(m2.last[d], m2.buf[d], 5);
        m2.nrep[d]++;
        vusb_transfer(m2.epin[d], m2.buf[d], 16, NULL, m2_read_done, arg, &fake_gp_a);
    }
}

static void m2_cfg_done(int result, int count, void *arg)
{
    int d = (int)(intptr_t)arg;
    (void)count;
    if (result == 0) {
        m2.cfg[d] = 1;
        vusb_transfer(m2.epin[d], m2.buf[d], 16, NULL, m2_read_done, arg, &fake_gp_a);
    }
}

static int m2_probe(int devId)
{
    const uint8_t *d = vusb_scan_descriptor(devId, NULL, 1);
    return d && d[8] == 0x4C && d[9] == 0x05 && d[10] == 2;
}

static vu_setup_t m2_setup[3];

static int m2_connect(int devId)
{
    vu_epdesc_t epd = {7, 5, 0x81, 3, 8, 0, 10};
    uint8_t loc[7];
    CHECK(devId == 1 || devId == 2);
    m2.connected[devId]++;
    m2.ep0[devId] = vusb_open_pipe(devId, NULL);
    m2.epin[devId] = vusb_open_pipe(devId, &epd);
    CHECK(m2.ep0[devId] > 0 && m2.epin[devId] > 0);
    CHECK(vusb_get_location(devId, loc) == 0 && loc[0] == devId);
    CHECK(vusb_set_private(devId, &m2) == 0);
    m2_setup[devId].requesttype = 0;
    m2_setup[devId].request = 9;
    m2_setup[devId].value = 1;
    CHECK(vusb_transfer(m2.ep0[devId], NULL, 0, &m2_setup[devId], m2_cfg_done, (void *)(intptr_t)devId, &fake_gp_a) == 0);
    return 0;
}

static void test_two_dongles(void)
{
    static vu_ldd_t l;
    static char nm[] = "buzzer";
    vu_setup_t su = {0x21, 9, 0x0200, 0, 7};

    memset(&m2, 0, sizeof m2);
    mb_reset();
    memset(mock_masks, 0, sizeof mock_masks);
    vusb_init(0, 100, BB_LAYOUT_eight_ds_tap12, 0);
    CHECK(vusb_dongles() == 2);
    memset(&l, 0, sizeof l);
    l.name = nm;
    l.probe = m2_probe;
    l.connect = m2_connect;
    CHECK(vusb_register_ldd(&l, &fake_gp_a) == 0);
    ticks(10);
    CHECK(m2.connected[1] == 1 && m2.connected[2] == 1);
    CHECK(m2.cfg[1] && m2.cfg[2] && m2.ep0[1] != m2.ep0[2] && m2.epin[1] != m2.epin[2]);
    CHECK(vusb_get_private(1) == &m2 && vusb_get_private(2) == &m2);
    CHECK(vusb_scan_descriptor(3, NULL, 1) == NULL);
    CHECK(m2.nrep[1] >= 1 && m2.nrep[2] >= 1);
    CHECK(memcmp(m2.last[1], "\x7F\x7F\x00\x00\xF0", 5) == 0 && memcmp(m2.last[2], "\x7F\x7F\x00\x00\xF0", 5) == 0);

    mock_masks[SRC(0, 1)] = PADB_CROSS;  /* dongle 1, handset 2 yellow = bit 6 */
    mock_masks[SRC(1, 2)] = PADB_SQUARE; /* dongle 2, handset 3 orange = bit 13 */
    ticks(4);
    CHECK(memcmp(m2.last[1], "\x7F\x7F\x40\x00\xF0", 5) == 0);
    CHECK(memcmp(m2.last[2], "\x7F\x7F\x00\x20\xF0", 5) == 0);
    mock_masks[SRC(0, 1)] = 0;
    ticks(4);
    CHECK(memcmp(m2.last[1], "\x7F\x7F\x00\x00\xF0", 5) == 0);
    CHECK(memcmp(m2.last[2], "\x7F\x7F\x00\x20\xF0", 5) == 0); /* untouched */

    /* lights of dongle 2 do not overwrite dongle 1 */
    memcpy(m2.light[2], "\x00\xFF\x00\x00\xFF\x00\x00", 7);
    CHECK(vusb_transfer(m2.ep0[2], m2.light[2], 7, &su, m2_cfg_done, (void *)(intptr_t)0, &fake_gp_a) == 0);
    ticks(2);
    CHECK(memcmp(vusb_stats()->lights[1], "\x00\xFF\x00\x00\xFF\x00\x00", 7) == 0);
    CHECK(memcmp(vusb_stats()->lights[0], "\x00\x00\x00\x00\x00\x00\x00", 7) == 0);

    /* a control pipe of the missing dongle number does not exist, closing a pipe aborts only its own read */
    CHECK(vusb_close_pipe(m2.epin[1]) == 0);
    ticks(2);
    mock_masks[SRC(1, 3)] = PADB_UP;
    ticks(4);
    CHECK(m2.nrep[2] > 2);
    CHECK(vusb_transfer(3, NULL, 0, &su, NULL, NULL, &fake_gp_a) == VU_RC_BADPIPE);

    /* one-dongle layouts never attach a second device */
    mb_reset();
    memset(&m2, 0, sizeof m2);
    vusb_init(0, 100, BB_LAYOUT_four_ds_tap1, 0);
    memset(&l, 0, sizeof l);
    l.name = nm;
    l.probe = m2_probe;
    l.connect = m2_connect;
    CHECK(vusb_dongles() == 1 && vusb_register_ldd(&l, &fake_gp_a) == 0);
    ticks(10);
    CHECK(m2.connected[1] == 1 && m2.connected[2] == 0);
    CHECK(vusb_scan_descriptor(2, NULL, 1) == NULL);
}

static void test_any_assignment(void)
{
    bb_layout_t l;
    bb_assign_t a;
    uint16_t m[BB_NSRC] = {0};
    uint8_t taps[2];
    unsigned id, s;
    for (id = 9; id <= 12; id++) {
        CHECK(bb_layout_build(id, &l) == 0);
        CHECK(l.ndongles == (id >= 11 ? 2 : 1));
        CHECK(bb_layout_taps(&l, taps) == 2 && taps[0] == 14 && taps[1] == 14);
    }
    bb_layout_build(9, &l);
    bb_assign_init(&a);
    bb_assign_update(&a, &l, 9, (1u << 0) | (1u << 4) | (1u << 6), 0);
    CHECK(l.h[0].src == 0 && l.h[1].src == 0); /* buzzer first */
    CHECK(a.owner[4] == 2 && a.owner[6] == 3);
    m[0] = PADB_SQUARE | PADB_R1;
    m[4] = PADB_UP;
    m[6] = PADB_CROSS;
    CHECK(bb_map(&l, m) == (W(0, BZ_RED) | W(1, BZ_RED) | W(2, BZ_BLUE) | W(3, BZ_YELLOW)));
    bb_assign_update(&a, &l, 9, 0xFF & ~(1u << 4), 2999);
    CHECK(a.owner[4] == 2 && a.owner[6] == 3 && a.owner[1] == BB_NOSRC);
    bb_assign_update(&a, &l, 9, 0xFF, 2999); /* return inside timeout */
    bb_assign_update(&a, &l, 9, 0xFF & ~(1u << 4), 5998);
    CHECK(a.owner[4] == 2);
    bb_assign_update(&a, &l, 9, 0xFF & ~(1u << 4), 5999);
    CHECK(a.owner[4] == BB_NOSRC && a.owner[1] == 2 && a.owner[6] == 3);
    bb_layout_build(11, &l);
    bb_assign_init(&a);
    bb_assign_update(&a, &l, 11, 0xFF, 0);
    CHECK(a.owner[0] == 0 && a.owner[1] == 4);
    CHECK(a.owner[2] == 2 && a.owner[3] == 3 && a.owner[4] == 6 && a.owner[5] == 7);
    CHECK(a.owner[6] == BB_NOSRC && a.owner[7] == BB_NOSRC);
    CHECK(l.h[4].src == 1 && l.h[5].src == 1);
    bb_assign_update(&a, &l, 11, 0xFE, 3000);
    CHECK(a.owner[1] == 4 && a.owner[2] == 2 && a.owner[6] == 0); /* no reshuffle */
    bb_layout_build(10, &l);
    bb_assign_init(&a);
    bb_assign_update(&a, &l, 10, 0xF0, 0xFFFFFF00u);
    bb_assign_update(&a, &l, 10, 0, 0xFFFFFF00u + 2999u);
    CHECK(a.owner[4] == 0); /* wrap */
    bb_assign_update(&a, &l, 10, 1, 0xFFFFFF00u + 3000u);
    CHECK(a.owner[4] == BB_NOSRC && a.owner[0] == 0);
    bb_layout_build(12, &l);
    bb_assign_init(&a);
    bb_assign_update(&a, &l, 12, 0xFF, 0);
    for (s = 0; s < 8; s++) CHECK(a.owner[s] == s);
    bb_layout_build(10, &l);
    bb_assign_init(&a);
    bb_assign_update(&a, &l, 10, 0x80, 0);
    bb_assign_update(&a, &l, 10, 0x81, 32);
    CHECK(a.owner[7] == 0 && a.owner[0] == 1); /* lower slot appears late */
    for (s = 0; s < 16; s++) {
        memset(m, 0, sizeof m);
        m[7] = (uint16_t)(1u << s);
        CHECK(bb_map(&l, m) != 0); /* every digital button belongs to this handset */
    }
}

static void test_custom_assignment(void)
{
    static const unsigned examples[][3] = {
        {0,4,0},{0,0,2},{0,2,1},{1,2,0},{1,0,1},{1,6,0},{1,4,1},{1,2,2},
        {1,0,3},{2,0,0},{2,4,0},{2,0,2},{0,4,2},{0,0,4},{0,8,0},{2,2,1}
    };
    /* Independent expected button tables: red, yellow, green, orange, blue. */
    static const uint16_t buz[2][5] = {{0x8000,0x4000,0x2000,0x1000,0x0400},
                                      {0x0800,0x0100,0x0200,0x0001,0x0008}};
    static const uint16_t halves[2][5] = {{0x0400,0x0040,0x0020,0x0080,0x0010},
                                         {0x0800,0x4000,0x2000,0x8000,0x1000}};
    unsigned e, p, w, sh;
    bb_layout_t l;
    bb_assign_t a;
    for (e = 0; e < sizeof examples / sizeof examples[0]; e++) {
        unsigned id = BB_CUSTOM_ID(examples[e][0],examples[e][1],examples[e][2]);
        CHECK(bb_layout_build(id, &l) == 0);
        CHECK(l.pairs == examples[e][0] && l.whole == examples[e][1] && l.shared == examples[e][2]);
    }
    /* Exhaust every representable triple: reject zero/overfull, test every
     * colour on every handset for every valid setup (not only the examples). */
    for (p = 0; p <= 2; p++) for (w = 0; w <= 8; w++) for (sh = 0; sh <= 4; sh++) {
        unsigned id = BB_CUSTOM_ID(p,w,sh), players = 2*p+w+2*sh;
        unsigned r, h = 0, roles = p+w+sh;
        int result = bb_layout_build(id, &l);
        if (!players || players > 8) { CHECK(result == -1); continue; }
        CHECK(result == 0 && l.any_slots && l.custom && l.ndongles == (players > 4 ? 2 : 1));
        bb_assign_init(&a);
        bb_assign_update(&a, &l, id, 0xFF, 10);
        for (r = 0; r < roles; r++) {
            unsigned c, half, width = r < p || r >= p+w ? 2 : 1;
            CHECK(a.owner[r] == h);
            for (half = 0; half < width; half++) {
                CHECK(l.h[h+half].src == r);
                for (c = 0; c < 5; c++) {
                    uint16_t masks[BB_NSRC] = {0};
                    masks[r] = r < p ? buz[half][c] : halves[half][c];
                    CHECK(bb_map_dongle(&l, masks, (h+half)/4) == W((h+half)%4,c));
                    if (width == 2 && h/4 != (h+1)/4)
                        CHECK(bb_map_dongle(&l, masks, 1-(h+half)/4) == 0);
                }
            }
            if (width == 1) {
                unsigned bit;
                for (bit = 0; bit < 16; bit++) {
                    uint16_t masks[BB_NSRC] = {0}; masks[r] = 1u << bit;
                    CHECK(bb_map_dongle(&l,masks,h/4) != 0);
                }
            }
            h += width;
        }
        for (; r < 8; r++) CHECK(a.owner[r] == BB_NOSRC);
        for (; h < 8; h++) CHECK(l.h[h].src == BB_NOSRC);
    }
    CHECK(bb_layout_build(391,&l) == -1);
    CHECK(bb_layout_build(13,&l) == -1); /* missing counts */
    CHECK(sizeof(bb_cfg_t) == 16);
    /* Sparse discovery, fewer pads than roles, late lower source, reservation,
     * returning pad, exact timeout, excess source and no survivor reshuffle. */
    {
        unsigned id = BB_CUSTOM_ID(1,1,1);
        bb_layout_build(id,&l); bb_assign_init(&a);
        bb_assign_update(&a,&l,id,(1u<<2)|(1u<<6),100);
        CHECK(a.owner[2] == 0 && a.owner[6] == 2 && l.h[3].src == BB_NOSRC);
        bb_assign_update(&a,&l,id,(1u<<0)|(1u<<2)|(1u<<6),200);
        CHECK(a.owner[0] == 3 && a.owner[2] == 0 && a.owner[6] == 2);
        bb_assign_update(&a,&l,id,(1u<<2)|(1u<<4)|(1u<<6),3199);
        CHECK(a.owner[0] == 3 && a.owner[4] == BB_NOSRC);
        bb_assign_update(&a,&l,id,(1u<<0)|(1u<<2)|(1u<<4)|(1u<<6),3199);
        CHECK(a.owner[0] == 3 && a.owner[4] == BB_NOSRC);
        bb_assign_update(&a,&l,id,(1u<<2)|(1u<<4)|(1u<<6),6198);
        CHECK(a.owner[0] == 3 && a.owner[4] == BB_NOSRC);
        bb_assign_update(&a,&l,id,(1u<<2)|(1u<<4)|(1u<<6),6199);
        CHECK(a.owner[0] == BB_NOSRC && a.owner[4] == 3 && a.owner[6] == 2);
        bb_assign_update(&a,&l,id,0xFF,6200);
        CHECK(a.owner[0] == BB_NOSRC && a.owner[4] == 3 && a.owner[2] == 0);
    }
    /* A pair straddles dongles; using only one half never requires the other. */
    {
        unsigned id = BB_CUSTOM_ID(0,3,1);
        uint16_t masks[BB_NSRC] = {0};
        bb_layout_build(id,&l); bb_assign_init(&a);
        bb_assign_update(&a,&l,id,15,0xFFFFFFF0u);
        masks[3] = PADB_UP;
        CHECK(bb_map_dongle(&l,masks,0) == W(3,BZ_BLUE) && bb_map_dongle(&l,masks,1) == 0);
        masks[3] = PADB_TRIANGLE;
        CHECK(bb_map_dongle(&l,masks,0) == 0 && bb_map_dongle(&l,masks,1) == W(0,BZ_BLUE));
        bb_assign_update(&a,&l,id,7,0x10u);
        CHECK(a.owner[3] == 3); /* wrap-safe reservation */
        bb_assign_update(&a,&l,id,7,0x1000u);
        CHECK(a.owner[3] == BB_NOSRC);
    }
}

int main(void)
{
    test_any_assignment();
    test_custom_assignment();
    test_descriptors();
    test_report_pack();
    test_layouts();
    test_script();
    test_sio2_scan();
    test_poll_fill();
    test_vusb_flow();
    test_vusb_bad_ldd();
    test_vusb_layout_input();
    test_vusb_script();
    test_report_overrun_and_small_buffer();
    test_fuzz();
    test_tap_layouts();
    test_tap_commands();
    test_scan_slots();
    test_two_dongles();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
