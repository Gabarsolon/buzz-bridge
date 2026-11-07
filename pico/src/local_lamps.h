#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
  uint32_t expires[4], hold_ms;
  uint8_t previous_reds, mask;
  bool enabled, first, remote_owned;
} local_lamps_t;
void local_lamps_init(local_lamps_t *s,bool enabled,bool first,uint32_t hold_ms);
void local_lamps_remote(local_lamps_t *s);
uint8_t local_lamps_tick(local_lamps_t *s,uint32_t buttons,uint32_t now_ms);
