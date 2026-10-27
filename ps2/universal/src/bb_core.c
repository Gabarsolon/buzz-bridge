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

    for (h = 0; h < BB_HANDSETS; h++) {
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
    default:
        return -1;
    }
}

uint32_t bb_map(const bb_layout_t *lay, const uint16_t src_masks[BB_NSRC])
{
    uint32_t w = 0;
    unsigned h, c;

    for (h = 0; h < BB_HANDSETS; h++) {
        const bb_bind_t *b = &lay->h[h];
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

int bb_sio2_scan(const uint32_t regdata[16], const uint8_t *in, uint32_t in_size,
                 const uint8_t *out, uint32_t out_size, bb_padev_t ev[BB_MAX_PADEV])
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
                ev[n].valid = 1;
                ev[n].buttons = (uint16_t)(~((uint16_t)r5[3] | ((uint16_t)r5[4] << 8)));
                n++;
            } else if (id == 0xFF || id == 0x00) {
                /* nothing plugged in: the bus floats high or answers with zeros */
                ev[n].port = (uint8_t)port;
                ev[n].valid = 0;
                ev[n].buttons = 0;
                n++;
            }
            /* any other id (configuration mode, rumble set-up replies) is ignored on purpose */
        }
        ioff += ilen;
        ooff += olen;
    }
    return n;
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
