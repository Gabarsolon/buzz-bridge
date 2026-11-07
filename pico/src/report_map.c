#include "bridge.h"
uint32_t buzz_buttons(const uint8_t raw[5]) {
  return (raw[2] | ((uint32_t)raw[3]<<8) | ((uint32_t)raw[4]<<16)) & 0xfffffu;
}
void buzz_pad_report(const uint8_t raw[5], unsigned pad, uint8_t out[4]) {
  uint16_t buttons=(uint16_t)((buzz_buttons(raw)>>(pad?10:0))&0x3ffu);
  out[0]=0x7f;out[1]=0x7f;out[2]=(uint8_t)buttons;out[3]=(uint8_t)(buttons>>8);
}
