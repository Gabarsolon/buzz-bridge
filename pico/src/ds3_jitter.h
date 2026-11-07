#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DS3_JITTER_INTERVAL_MS 40u
#define DS3_JITTER_LIMIT 3
// Main-core/task context only. Seed once per boot; no USB, heap or flash calls.
void ds3_jitter_seed(uint32_t seed,uint32_t now);
void ds3_jitter_enable(bool enabled);
bool ds3_jitter_enabled(void);
void ds3_jitter_tick(uint32_t now);
// Payload excludes ID: only bytes 40..47 (wire 41..48) can change.
void ds3_jitter_apply(uint8_t payload[48]);
