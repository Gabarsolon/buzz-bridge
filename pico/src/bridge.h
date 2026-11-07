#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { MODE_CLONE=0, MODE_TWO_PADS=1, MODE_DS3=2 };
extern uint8_t bridge_mode;
extern bool bridge_debug;
void bridge_descriptors_init(void);
uint32_t buzz_buttons(const uint8_t raw[5]);
void buzz_pad_report(const uint8_t raw[5], unsigned pad, uint8_t out[4]);
