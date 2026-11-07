#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t ds3_report_descriptor[148];
// Feature APIs use the full EP0 wire data, including each feature's own header.
// Genuine DS3 feature headers are not uniformly the requested report ID.
void ds3_identity(const uint8_t address[6]);
void ds3_session_reset(uint32_t now,bool auto_ps);
void ds3_session_configured(uint32_t now);
void ds3_enable_anchor(uint32_t now);
void ds3_disable_anchor(uint32_t now);
uint8_t ds3_wake_used(void);
bool ds3_wake_pending(void);
void ds3_activation_tick(uint32_t bits,uint32_t now);
void ds3_request_ps(uint32_t now);
bool ds3_streaming(void);
bool ds3_send_allowed(uint32_t now);
bool ds3_ps_value(uint32_t now);
void ds3_input_acked(bool ps);
void ds3_linux_usb_poke(void);
uint32_t ds3_ps_count(void);
uint32_t ds3_enable_count(void);
uint32_t ds3_disable_count(void);
void ds3_output_reset(void);
void ds3_input(uint32_t bits,uint8_t payload[48]);
uint16_t ds3_feature_get(uint8_t id,uint8_t *out,uint16_t capacity);
bool ds3_feature_set(uint8_t id,const uint8_t *data,uint16_t len);
// Callback data follows TinyUSB's report-ID removal convention.
bool ds3_output(uint8_t id,const uint8_t *data,uint16_t len,uint32_t now,
                uint8_t *small,uint8_t *large,uint8_t *mask,bool *commit);
