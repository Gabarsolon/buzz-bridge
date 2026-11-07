#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BRIDGE_SETTINGS_MAGIC 0x42555a31u
#define BRIDGE_SETTINGS_PAGE_BYTES 256u
#define BRIDGE_SETTINGS_USED_BYTES 36u
typedef struct {uint32_t magic,version,mode_debug,local_flags,hold_ms,crc;} settings_t;
uint32_t bridge_settings_crc32(const void *buffer,size_t n);
bool bridge_settings_valid(const settings_t *s);
// Optional extension after the unchanged 24-byte v3 record. Old firmware can
// still read the base. Missing/corrupt extension defaults jitter ON.
bool bridge_settings_jitter(const uint8_t page[BRIDGE_SETTINGS_PAGE_BYTES]);
void bridge_settings_page(uint8_t page[BRIDGE_SETTINGS_PAGE_BYTES],const settings_t *s,bool jitter);
