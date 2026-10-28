/* Buzz! Bridge universal USBD: pure logic (no kernel calls), compiled for the IOP
 * and for the PC unit tests. */
#ifndef BB_CORE_H
#define BB_CORE_H

#include <stdint.h>

/* ---- virtual dongle identity and descriptors ---- */
#define BB_VID 0x054C
#define BB_PID 0x0002

#define BB_DEV_DESC_LEN 18
#define BB_CFG_DESC_LEN 34 /* config 9 + interface 9 + HID 9 + endpoint 7 */
#define BB_STATIC_DESC_LEN (BB_DEV_DESC_LEN + BB_CFG_DESC_LEN)
#define BB_HID_REPORT_DESC_LEN 78
#define BB_REPORT_LEN 5     /* bytes the game consumes from each interrupt-IN report */
#define BB_OUT_REPORT_LEN 7 /* lights report (byte 0 zero, bytes 1-4 handset lights) */

extern const uint8_t bb_static_desc[BB_STATIC_DESC_LEN]; /* device descriptor, then configuration chain */
extern const uint8_t bb_hid_report_desc[BB_HID_REPORT_DESC_LEN];

/* ---- buzzer buttons: wire bit order inside a handset ---- */
enum { BZ_RED = 0, BZ_YELLOW = 1, BZ_GREEN = 2, BZ_ORANGE = 3, BZ_BLUE = 4, BZ_COUNT = 5 };
#define BB_HANDSETS 4          /* handsets per virtual dongle */
#define BB_DONGLES 2           /* BUZZER.IRX supports two USB devices (RPC status: maximum 2) */
#define BB_ALL_HANDSETS (BB_HANDSETS * BB_DONGLES)

/* ---- DualShock buttons as an active-high 16 bit mask (PS2SDK libpad numbering) ---- */
#define PADB_SELECT   0x0001
#define PADB_L3       0x0002
#define PADB_R3       0x0004
#define PADB_START    0x0008
#define PADB_UP       0x0010
#define PADB_RIGHT    0x0020
#define PADB_DOWN     0x0040
#define PADB_LEFT     0x0080
#define PADB_L2       0x0100
#define PADB_R2       0x0200
#define PADB_L1       0x0400
#define PADB_R1       0x0800
#define PADB_TRIANGLE 0x1000
#define PADB_CIRCLE   0x2000
#define PADB_CROSS    0x4000
#define PADB_SQUARE   0x8000

/* ---- input sources: (port, slot). Slots are for a later multitap phase, v1 uses slot 0 ---- */
#define BB_PORTS 2
#define BB_SLOTS 4
#define BB_NSRC (BB_PORTS * BB_SLOTS)
#define BB_SRC(port, slot) ((port) * BB_SLOTS + (slot))
#define BB_NOSRC 0xFF

typedef struct {
    uint8_t src;          /* source index or BB_NOSRC (handset unbound) */
    uint16_t mask[BZ_COUNT]; /* any of these pad buttons presses the buzzer button */
} bb_bind_t;

typedef struct {
    bb_bind_t h[BB_ALL_HANDSETS]; /* handsets 0-3 = dongle 1, 4-7 = dongle 2 */
    uint8_t ndongles;             /* 1 or 2 virtual dongles */
    uint8_t any_slots;            /* scan both ports, even before any bindings exist */
    uint8_t custom, pairs, whole, shared; /* only used by layout 13 */
} bb_layout_t;

enum {
#define BB_LAYOUT(id, name, desc) BB_LAYOUT_##name = id,
#include "bb_layouts.h"
#undef BB_LAYOUT
    BB_LAYOUT_COUNT
};

/* Fills out for layout id. Returns 0, or -1 for an unknown id (out = all unbound). */
/* Runtime-only IDs tag the spare config byte with bit 8. Never stored in
 * the on-disk layout byte. A bare custom ID has no counts and is rejected. */
#define BB_CUSTOM_ID(p, w, s) (256u + (p) + 3u * (w) + 27u * (s))
int bb_layout_build(unsigned id, bb_layout_t *out);
const char *bb_layout_name(unsigned id);

/* Sticky ownership, measured in milliseconds with unsigned wrap-safe subtraction.
 * No wire discriminator exists: allocate buzzer roles first, then whole pads, then shared pads (custom).
 * Call once per complete scan, in port-major source order. */
#define BB_ASSIGN_TIMEOUT_MS 3000u
typedef struct {
    uint8_t owner[BB_NSRC]; /* first handset, or BB_NOSRC */
    uint32_t seen[BB_NSRC];
} bb_assign_t;
void bb_assign_init(bb_assign_t *a);
void bb_assign_update(bb_assign_t *a, bb_layout_t *lay, unsigned id,
                      unsigned present, uint32_t ms);

/* 20-bit buzzer word (bit = handset*5 + colour) of one dongle from the per-source pad masks. */
uint32_t bb_map_dongle(const bb_layout_t *lay, const uint16_t src_masks[BB_NSRC], unsigned dongle);
#define bb_map(lay, masks) bb_map_dongle((lay), (masks), 0)

/* Multitap slots (1..3) of each console port that the layout reads: bit s of slots[port] = slot s is used.
 * Returns the number of console ports with at least one such slot. */
unsigned bb_layout_taps(const bb_layout_t *lay, uint8_t slots[BB_PORTS]);

/* The 5 byte interrupt-IN report: 7F 7F b0 b1 (b2 | F0). Idle = 7F 7F 00 00 F0. */
void bb_report_pack(uint32_t word20, uint8_t out[BB_REPORT_LEN]);

/* Demo generator for the diagnostic mode: handset (n/5)%4 colour n%5 is held 500 ms of every 1000 ms. */
uint32_t bb_script_word(uint32_t ms);

/* ---- SIO2 pad transfer parsing ---- */
typedef struct {
    uint8_t port;     /* 0 or 1 */
    uint8_t slot;     /* multitap slot the read was addressed to (0 = A) */
    uint8_t valid;    /* 1 = a controller answered, 0 = port answered "nothing" */
    uint16_t buttons; /* active-high PADB_* mask, 0 when !valid */
} bb_padev_t;

#define BB_MAX_PADEV 8

/* ---- our own pad poll (only used when nobody else polls): the same 5 byte digital read PADMAN sends ----
 * "01 42 00 00 00" for port 1 and port 2 in one transfer, with PADMAN's default port timing constants
 * (ps2sdk padman sio2Cmds.c: 0xFFC00505 / 0x20014, motors off). */
#define BB_POLL_CMD_LEN 5
#define BB_POLL_CTRL1 0xFFC00505u
#define BB_POLL_CTRL2 0x00020014u
void bb_poll_fill(uint32_t regdata[16], uint8_t in[BB_PORTS * BB_POLL_CMD_LEN]);

/* Walks the regdata entries of a completed sio2_transfer_data_t (arrays are the in/out FIFO
 * buffers) and returns the number of pad read results found (0..BB_MAX_PADEV). Never reads
 * outside in[in_size] / out[out_size]. */
int bb_sio2_scan(const uint32_t regdata[16], const uint8_t *in, uint32_t in_size,
                 const uint8_t *out, uint32_t out_size, bb_padev_t ev[BB_MAX_PADEV]);

/* Same, but also follows confirmed multitap slot selections ("21 21 <slot>" on SIO2 port 2/3,
 * with PCSX2 or netemu reply framing; see bb_tap_select_reply_ok)
 * and tags the pad reads that follow with the selected slot. cur_slot[port] is kept between calls (power-up = 0). */
int bb_sio2_scan_ex(const uint32_t regdata[16], const uint8_t *in, uint32_t in_size,
                    const uint8_t *out, uint32_t out_size, bb_padev_t ev[BB_MAX_PADEV], uint8_t cur_slot[BB_PORTS]);

/* ---- multitap (SCPH-10090) commands, see research/MULTITAP_NOTES.md ---- */
#define BB_TAP_CTRL1 0xFF020505u
#define BB_TAP_CTRL2 0x00030064u
#define BB_TD_MAXIN 64

/* a plain transfer descriptor under construction (the IOP fills a sio2_transfer_data_t from it) */
typedef struct {
    uint32_t regdata[16];
    uint8_t in[BB_TD_MAXIN];
    uint32_t in_size, out_size;
    uint32_t ctrl1[4], ctrl2[4];
    uint8_t n;               /* entries */
    uint8_t kind[8];         /* what entry i is: BB_TDK_* */
    uint8_t port[8];         /* console port (0/1) of entry i */
    uint8_t slot[8];         /* slot for BB_TDK_TAPSEL */
    uint8_t out_off[8];      /* where entry i's reply starts in out[] */
} bb_td_t;
enum { BB_TDK_PAD = 1, BB_TDK_TAPSEL = 2, BB_TDK_TAPDET = 3 };

void bb_td_reset(bb_td_t *td);
int bb_td_add_pad_read(bb_td_t *td, unsigned port);              /* 01 42 00 00 00 on SIO2 port 0/1 */
int bb_td_add_tap_select(bb_td_t *td, unsigned port, unsigned slot); /* 21 21 slot 00 00 00 00 on SIO2 port 2/3 */
int bb_td_add_tap_detect(bb_td_t *td, unsigned port);            /* 21 12 00 00 00 00 on SIO2 port 2/3 */
/* Reply checks accept PCSX2's 80/5A header and netemu's FF-filled header with
 * terminator FF (reset default) or 5A. Detect returns 1..4 or 0 = no tap. */
unsigned bb_tap_detect_reply(const uint8_t *out, uint32_t len);
int bb_tap_select_reply_ok(const uint8_t *out, uint32_t len, unsigned slot);

/* ---- patch-time configuration, found by the patcher through its magic ---- */
#define BB_CFG_MAGIC "BBZCFG01"
#define BB_FLAG_VERBOSE 0x01  /* log to the IOP console */
#define BB_FLAG_NOPAD   0x02  /* do not hook SIO2MAN (virtual dongle only) */
#define BB_FLAG_SCRIPT  0x04  /* diagnostic: generate scripted presses instead of reading pads */
#define BB_FLAG_DIAG_TAP 0x08 /* hold Select on a tapped port's A pad: status colour on its last used slot */
#define BB_FLAG_POLL_AUTO   0x10 /* read the pads ourselves when nobody else has polled for 500 ms */
#define BB_FLAG_POLL_ALWAYS 0x20 /* read the pads ourselves all the time (diagnostic) */
/* A layout that uses multitap slots starts the poll thread by itself, whatever the poll flags say. */

typedef struct {
    char magic[8];
    uint8_t version;       /* 1 */
    uint8_t layout;        /* BB_LAYOUT_* */
    uint8_t flags;         /* BB_FLAG_* */
    uint8_t reserved;      /* layout 13 only: P + 3*W + 27*S, P<=2 W<=8 S<=4;
                           * require 1 <= 2*P+W+2*S <= 8. Ignored for IDs 0-12.
                           * Version, magic, timings and 16-byte size unchanged. */
    uint16_t attach_delay_ms;
    uint16_t heartbeat_ms;
} bb_cfg_t;

#endif
