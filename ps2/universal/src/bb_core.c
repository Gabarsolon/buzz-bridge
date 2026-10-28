#include "bb_core.h"

/* Device descriptor, then the configuration chain exactly as USBD would cache it:
 * config, interface, HID class descriptor, endpoint. sceUsbdScanStaticDescriptor walks this. */
const uint8_t bb_static_desc[BB_STATIC_DESC_LEN] = {
    /* device */
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08,
    0x4C, 0x05,       /* idVendor 054C */
    0x02, 0x00,       /* idProduct 0002 */
    0xA1, 0x05,       /* bcdDevice */
    0x03, 0x01, 0x00, /* iManufacturer, iProduct, iSerialNumber */
    0x01,             /* bNumConfigurations */
    /* configuration */
    0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    /* interface 0, alt 0, 1 endpoint, HID */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    /* HID class descriptor */
    0x09, 0x21, 0x11, 0x01, 0x33, 0x01, 0x22, 0x4E, 0x00,
    /* endpoint 0x81, interrupt, 8 bytes, 10 ms */
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
};

const uint8_t bb_hid_report_desc[BB_HID_REPORT_DESC_LEN] = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0xA1, 0x02,
    0x75, 0x08, 0x95, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x35, 0x00, 0x46, 0xFF, 0x00,
    0x09, 0x30, 0x09, 0x31, 0x81, 0x02,
    0x75, 0x01, 0x95, 0x14, 0x25, 0x01, 0x45, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x14, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x75, 0x01, 0x95, 0x04, 0x25, 0x01, 0x45, 0x01, 0x09, 0x01, 0x81, 0x02,
    0xC0,
    0xA1, 0x02, 0x75, 0x08, 0x95, 0x07, 0x26, 0xFF, 0x00, 0x46, 0xFF, 0x00, 0x09, 0x02, 0x91, 0x02,
    0xC0, 0xC0,
};

/* ---- layouts ---- */

static const char *const layout_names[] = {
#define BB_LAYOUT(id, name, desc) #name,
#include "bb_layouts.h"
#undef BB_LAYOUT
};

const char *bb_layout_name(unsigned id)
{
    return id < BB_LAYOUT_COUNT ? layout_names[id] : "?";
}

/* netemu turns the Buzz! dongle into one DualShock; this is its (hardware confirmed) mapping. */
static const uint16_t netemu_h1[BZ_COUNT] = {PADB_SQUARE, PADB_CROSS, PADB_CIRCLE, PADB_TRIANGLE, PADB_L1};
static const uint16_t netemu_h2[BZ_COUNT] = {PADB_R1, PADB_L2, PADB_R2, PADB_SELECT, PADB_START};
/* one DualShock split into a left and a right player */
static const uint16_t half_left[BZ_COUNT] = {PADB_L1 | PADB_L2, PADB_DOWN, PADB_RIGHT, PADB_LEFT, PADB_UP};
static const uint16_t half_right[BZ_COUNT] = {PADB_R1 | PADB_R2, PADB_CROSS, PADB_CIRCLE, PADB_SQUARE, PADB_TRIANGLE};

static void bind(bb_bind_t *b, unsigned src, const uint16_t *m1, const uint16_t *m2)
{
    unsigned i;
    b->src = (uint8_t)src;
    for (i = 0; i < BZ_COUNT; i++)
        b->mask[i] = (uint16_t)(m1[i] | (m2 ? m2[i] : 0));
}

int bb_layout_build(unsigned id, bb_layout_t *out)
{
    unsigned h, i;
    const unsigned P1 = BB_SRC(0, 0), P2 = BB_SRC(1, 0);

    out->ndongles = 1;
    for (h = 0; h < BB_ALL_HANDSETS; h++) {
        out->h[h].src = BB_NOSRC;
        for (i = 0; i < BZ_COUNT; i++)
            out->h[h].mask[i] = 0;
    }
    switch (id) {
    case BB_LAYOUT_buzzers:
        bind(&out->h[0], P1, netemu_h1, 0);
        bind(&out->h[1], P1, netemu_h2, 0);
        return 0;
    case BB_LAYOUT_buzzers_ds:
        bind(&out->h[0], P1, netemu_h1, 0);
        bind(&out->h[1], P1, netemu_h2, 0);
        bind(&out->h[2], P2, half_left, 0);
        bind(&out->h[3], P2, half_right, 0);
        return 0;
    case BB_LAYOUT_two_ds_shared:
        bind(&out->h[0], P1, half_left, 0);
        bind(&out->h[1], P1, half_right, 0);
        bind(&out->h[2], P2, half_left, 0);
        bind(&out->h[3], P2, half_right, 0);
        return 0;
    case BB_LAYOUT_two_ds_single:
        /* whole pad = union of both halves */
        bind(&out->h[0], P1, half_left, half_right);
        bind(&out->h[1], P2, half_left, half_right);
        return 0;
    case BB_LAYOUT_one_ds_shared:
        bind(&out->h[0], P1, half_left, 0);
        bind(&out->h[1], P1, half_right, 0);
        return 0;
    case BB_LAYOUT_buzzers_ds2_tap1:
        bind(&out->h[0], P1, netemu_h1, 0);
        bind(&out->h[1], P1, netemu_h2, 0);
        bind(&out->h[2], BB_SRC(0, 1), half_left, half_right);
        bind(&out->h[3], BB_SRC(0, 2), half_left, half_right);
        return 0;
    case BB_LAYOUT_four_ds_tap1:
        for (h = 0; h < 4; h++)
            bind(&out->h[h], BB_SRC(0, h), half_left, half_right);
        return 0;
    case BB_LAYOUT_buzzers_ds2_tap2:
        bind(&out->h[0], P1, netemu_h1, 0);
        bind(&out->h[1], P1, netemu_h2, 0);
        bind(&out->h[2], P2, half_left, half_right);
        bind(&out->h[3], BB_SRC(1, 1), half_left, half_right);
        return 0;
    case BB_LAYOUT_eight_ds_tap12:
        out->ndongles = 2;
        for (h = 0; h < 4; h++) {
            bind(&out->h[h], BB_SRC(0, h), half_left, half_right);
            bind(&out->h[4 + h], BB_SRC(1, h), half_left, half_right);
        }
        return 0;
    default:
        return -1;
    }
}

uint32_t bb_map_dongle(const bb_layout_t *lay, const uint16_t src_masks[BB_NSRC], unsigned dongle)
{
    uint32_t w = 0;
    unsigned h, c;

    if (dongle >= BB_DONGLES)
        return 0;
    for (h = 0; h < BB_HANDSETS; h++) {
        const bb_bind_t *b = &lay->h[dongle * BB_HANDSETS + h];
        uint16_t pad;
        if (b->src >= BB_NSRC)
            continue;
        pad = src_masks[b->src];
        for (c = 0; c < BZ_COUNT; c++)
            if (pad & b->mask[c])
                w |= (uint32_t)1 << (h * BZ_COUNT + c);
    }
    return w;
}

unsigned bb_layout_taps(const bb_layout_t *lay, uint8_t slots[BB_PORTS])
{
    unsigned h, p, n = 0;

    for (p = 0; p < BB_PORTS; p++)
        slots[p] = 0;
    for (h = 0; h < BB_ALL_HANDSETS; h++) {
        unsigned src = lay->h[h].src;
        if (src < BB_NSRC && (src % BB_SLOTS) != 0)
            slots[src / BB_SLOTS] |= (uint8_t)(1u << (src % BB_SLOTS));
    }
    for (p = 0; p < BB_PORTS; p++)
        if (slots[p])
            n++;
    return n;
}

void bb_report_pack(uint32_t word20, uint8_t out[BB_REPORT_LEN])
{
    out[0] = 0x7F;
    out[1] = 0x7F;
    out[2] = (uint8_t)(word20 & 0xFF);
    out[3] = (uint8_t)((word20 >> 8) & 0xFF);
    out[4] = (uint8_t)(((word20 >> 16) & 0x0F) | 0xF0);
}

uint32_t bb_script_word(uint32_t ms)
{
    uint32_t n = ms / 1000;
    uint32_t pressed = (ms % 1000) >= 500;
    uint32_t handset = (n / BZ_COUNT) % BB_HANDSETS;
    uint32_t colour = n % BZ_COUNT;
    return pressed ? ((uint32_t)1 << (handset * BZ_COUNT + colour)) : 0;
}

/* ---- SIO2 pad transfer parsing ---- */

int bb_sio2_scan_ex(const uint32_t regdata[16], const uint8_t *in, uint32_t in_size,
                    const uint8_t *out, uint32_t out_size, bb_padev_t ev[BB_MAX_PADEV], uint8_t cur_slot[BB_PORTS])
{
    uint32_t ioff = 0, ooff = 0;
    int n = 0;
    unsigned i;

    if (!in || !out)
        return 0;
    for (i = 0; i < 16 && regdata[i] != 0; i++) {
        uint32_t r = regdata[i];
        uint32_t port = r & 3;
        uint32_t ilen = (r >> 8) & 0x1FF;
        uint32_t olen = (r >> 18) & 0x1FF;

        if (ioff + ilen > in_size || ooff + olen > out_size)
            break; /* inconsistent descriptor: stop, never read past the buffers */
        /* pad read: address 01, command 42, a response of at least FF id 5A b1 b2 */
        if (port < BB_PORTS && ilen >= 3 && olen >= 5 && in[ioff] == 0x01 && in[ioff + 1] == 0x42 &&
            n < BB_MAX_PADEV) {
            const uint8_t *r5 = out + ooff;
            uint8_t id = r5[1];
            if (r5[2] == 0x5A && (id == 0x41 || id == 0x53 || id == 0x73 || id == 0x79)) {
                ev[n].port = (uint8_t)port;
                ev[n].slot = cur_slot ? cur_slot[port] : 0;
                ev[n].valid = 1;
                ev[n].buttons = (uint16_t)(~((uint16_t)r5[3] | ((uint16_t)r5[4] << 8)));
                n++;
            } else if (id == 0xFF || id == 0x00) {
                /* nothing plugged in: the bus floats high or answers with zeros */
                ev[n].port = (uint8_t)port;
                ev[n].slot = cur_slot ? cur_slot[port] : 0;
                ev[n].valid = 0;
                ev[n].buttons = 0;
                n++;
            }
            /* any other id (configuration mode, rumble set-up replies) is ignored on purpose */
        } else if (cur_slot && (port == 2 || port == 3) && ilen >= 3 && olen >= 7 && in[ioff] == 0x21 &&
                   in[ioff + 1] == 0x21) {
            /* multitap pad-slot select: remember the slot when the tap confirms it */
            if (bb_tap_select_reply_ok(out + ooff, olen, in[ioff + 2]))
                cur_slot[port & 1] = in[ioff + 2];
        }
        ioff += ilen;
        ooff += olen;
    }
    return n;
}

int bb_sio2_scan(const uint32_t regdata[16], const uint8_t *in, uint32_t in_size,
                 const uint8_t *out, uint32_t out_size, bb_padev_t ev[BB_MAX_PADEV])
{
    return bb_sio2_scan_ex(regdata, in, in_size, out, out_size, ev, 0);
}

/* ---- multitap commands ---- */

unsigned bb_tap_detect_reply(const uint8_t *out, uint32_t len)
{
    int pcsx2, netemu;
    if (!out || len < 6 || out[3] < 1 || out[3] > 4)
        return 0;
    pcsx2 = out[1] == 0x80 && out[2] == 0x5A && out[5] == 0x5A;
    /* netemu 0x1261a8 writes only bytes 2..5 into an FF-filled FIFO.
     * Its programmable terminator starts at FF (21 41 can change it to 5A).
     * Do not mistake a floating bus or the 66 failure reply for a tap. */
    netemu = out[0] == 0xFF && out[1] == 0xFF && out[2] == 0 && out[4] == 0 &&
             (out[5] == 0xFF || out[5] == 0x5A);
    return pcsx2 || netemu ? out[3] : 0;
}

int bb_tap_select_reply_ok(const uint8_t *out, uint32_t len, unsigned slot)
{
    int pcsx2, netemu;
    if (!out || len < 7 || slot >= BB_SLOTS || out[5] != slot)
        return 0;
    pcsx2 = out[1] == 0x80 && out[2] == 0x5A && out[6] == 0x5A;
    netemu = out[0] == 0xFF && out[1] == 0xFF && out[2] == 0xFF && out[3] == 0xFF &&
             out[4] == 0 && (out[6] == 0xFF || out[6] == 0x5A);
    return pcsx2 || netemu;
}

void bb_td_reset(bb_td_t *td)
{
    unsigned i;
    for (i = 0; i < 16; i++)
        td->regdata[i] = 0;
    for (i = 0; i < BB_TD_MAXIN; i++)
        td->in[i] = 0;
    for (i = 0; i < 4; i++)
        td->ctrl1[i] = td->ctrl2[i] = 0;
    for (i = 0; i < 8; i++)
        td->kind[i] = td->port[i] = td->slot[i] = td->out_off[i] = 0;
    td->in_size = td->out_size = 0;
    td->n = 0;
}

static int td_room(const bb_td_t *td, unsigned len)
{
    return td->n < 8 && td->in_size + len <= BB_TD_MAXIN;
}

static void td_entry(bb_td_t *td, unsigned sio_port, unsigned len, unsigned kind, unsigned port, unsigned slot)
{
    td->regdata[td->n] = ((uint32_t)len << 18) | ((uint32_t)len << 8) | 0x40u | sio_port;
    td->kind[td->n] = (uint8_t)kind;
    td->port[td->n] = (uint8_t)port;
    td->slot[td->n] = (uint8_t)slot;
    td->out_off[td->n] = (uint8_t)td->out_size;
    td->n++;
    td->regdata[td->n] = 0;
    td->in_size += len;
    td->out_size += len;
}

int bb_td_add_pad_read(bb_td_t *td, unsigned port)
{
    uint8_t *b;
    if (port >= BB_PORTS || !td_room(td, BB_POLL_CMD_LEN))
        return -1;
    b = td->in + td->in_size;
    b[0] = 0x01;
    b[1] = 0x42;
    b[2] = b[3] = b[4] = 0;
    td->ctrl1[port] = BB_POLL_CTRL1;
    td->ctrl2[port] = BB_POLL_CTRL2;
    td_entry(td, port, BB_POLL_CMD_LEN, BB_TDK_PAD, port, 0);
    return 0;
}

int bb_td_add_tap_select(bb_td_t *td, unsigned port, unsigned slot)
{
    uint8_t *b;
    unsigned i;
    if (port >= BB_PORTS || slot >= BB_SLOTS || !td_room(td, 7))
        return -1;
    b = td->in + td->in_size;
    b[0] = 0x21;
    b[1] = 0x21;
    b[2] = (uint8_t)slot;
    for (i = 3; i < 7; i++)
        b[i] = 0;
    td->ctrl1[2 + port] = BB_TAP_CTRL1;
    td->ctrl2[2 + port] = BB_TAP_CTRL2;
    td_entry(td, 2 + port, 7, BB_TDK_TAPSEL, port, slot);
    return 0;
}

int bb_td_add_tap_detect(bb_td_t *td, unsigned port)
{
    uint8_t *b;
    unsigned i;
    if (port >= BB_PORTS || !td_room(td, 6))
        return -1;
    b = td->in + td->in_size;
    b[0] = 0x21;
    b[1] = 0x12;
    for (i = 2; i < 6; i++)
        b[i] = 0;
    td->ctrl1[2 + port] = BB_TAP_CTRL1;
    td->ctrl2[2 + port] = BB_TAP_CTRL2;
    td_entry(td, 2 + port, 6, BB_TDK_TAPDET, port, 0);
    return 0;
}

void bb_poll_fill(uint32_t regdata[16], uint8_t in[BB_PORTS * BB_POLL_CMD_LEN])
{
    unsigned p, i;
    for (i = 0; i < 16; i++)
        regdata[i] = 0;
    for (p = 0; p < BB_PORTS; p++) {
        /* out length bits 18.., in length bits 8.., 0x40 = same flag PADMAN sets, port in bits 0-1 */
        regdata[p] = ((uint32_t)BB_POLL_CMD_LEN << 18) | ((uint32_t)BB_POLL_CMD_LEN << 8) | 0x40u | p;
        in[p * BB_POLL_CMD_LEN + 0] = 0x01;
        in[p * BB_POLL_CMD_LEN + 1] = 0x42;
        for (i = 2; i < BB_POLL_CMD_LEN; i++)
            in[p * BB_POLL_CMD_LEN + i] = 0;
    }
}
