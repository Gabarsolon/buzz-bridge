/* SPDX-License-Identifier: MIT */
#ifndef PS3BUZZ_CORE_H
#define PS3BUZZ_CORE_H
#include <stddef.h>
#include <stdint.h>
#define BUZZ_PLAYERS 8
#define BUZZ_PADS 7
enum buzz_button { BUZZ_RED, BUZZ_YELLOW, BUZZ_GREEN, BUZZ_ORANGE, BUZZ_BLUE };
/* Normalized pad mask: digital1 | digital2 << 8, matching cellPad. */
enum buzz_pad_button {
 BP_SELECT=1, BP_L3=2, BP_R3=4, BP_START=8, BP_UP=16, BP_RIGHT=32,
 BP_DOWN=64, BP_LEFT=128, BP_L2=256, BP_R2=512, BP_L1=1024,
 BP_R1=2048, BP_TRIANGLE=4096, BP_CIRCLE=8192, BP_CROSS=16384, BP_SQUARE=32768
};
enum buzz_owner { BUZZ_FREE, BUZZ_PAD, BUZZ_PHONE };
typedef struct { uint8_t kind, buttons, light; uint16_t id; } buzz_slot;
typedef struct {
 unsigned enabled, dongles, players_per_pad, port, stale_ms, generic;
 uint16_t map[2][5];
 uint8_t pad_slot[BUZZ_PADS][2]; /* 0: first free; 1..8: target; 255: off */
} buzz_config;
typedef struct {
 buzz_config config;
 buzz_slot slots[BUZZ_PLAYERS];
 uint8_t pad_connected[BUZZ_PADS];
 uint32_t generation;
} buzz_hub;
void buzz_defaults(buzz_config *c);
/* Transactional, strict parser. Returns 0 or bad line number; destination unchanged on error. */
unsigned buzz_config_parse(buzz_config *c, const char *text);
void buzz_hub_init(buzz_hub *h, const buzz_config *c);
unsigned buzz_capacity(const buzz_hub *h);
int buzz_claim(buzz_hub *h, unsigned kind, unsigned id, unsigned requested_slot);
void buzz_release(buzz_hub *h, unsigned kind, unsigned id);
int buzz_press(buzz_hub *h, unsigned kind, unsigned id, unsigned button, int down);
/* connected=0 releases both halves. new_data=0 preserves last buttons. */
void buzz_pad_update(buzz_hub *h, unsigned port, int connected, int new_data, uint16_t mask);
void buzz_report(const buzz_hub *h, unsigned dongle, uint8_t out[5]);
void buzz_encode(const uint8_t buttons[4], uint8_t out[5]);
int buzz_lights(buzz_hub *h, unsigned dongle, const uint8_t *payload, size_t len);
unsigned buzz_pad_light(const buzz_hub *h, unsigned port);
int buzz_button_index(const char *name);
/* Compatible flat JSON input: join/slot, down|up/button. Unknown fields rejected. */
typedef struct { unsigned type, slot, button; } buzz_message;
enum { BUZZ_JOIN=1, BUZZ_DOWN, BUZZ_UP };
int buzz_message_parse(const char *data, size_t len, buzz_message *msg);
/* JSON reply; joined uses one-based slots. */
int buzz_phone_message(buzz_hub *h, unsigned client, const char *data, size_t len,
                       char *reply, size_t cap);
int buzz_status_json(const buzz_hub *h, char *out, size_t cap);
#endif
