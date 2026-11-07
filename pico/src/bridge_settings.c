#include <string.h>
#include "bridge_settings.h"
#define JITTER_MAGIC 0x4a495431u
_Static_assert(sizeof(settings_t)==24,"v3 base record layout changed");
uint32_t bridge_settings_crc32(const void *buffer,size_t n) {
  const uint8_t *p=buffer;uint32_t crc=0xffffffffu;
  while(n--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
  return ~crc;
}
bool bridge_settings_valid(const settings_t *s) {
  return s->magic==BRIDGE_SETTINGS_MAGIC&&(s->version==2||s->version==3)&&(s->mode_debug&0xff)<=2&&
    !(s->mode_debug&~0x103u)&&s->local_flags<=(s->version==3?15u:7u)&&s->hold_ms>=100&&s->hold_ms<=10000&&
    s->crc==bridge_settings_crc32(s,20);
}
bool bridge_settings_jitter(const uint8_t page[BRIDGE_SETTINGS_PAGE_BYTES]) {
  uint32_t ext[3];memcpy(ext,page+24,sizeof(ext));
  return ext[0]!=JITTER_MAGIC||ext[1]>1||ext[2]!=bridge_settings_crc32(page,32)||ext[1]!=0;
}
void bridge_settings_page(uint8_t page[BRIDGE_SETTINGS_PAGE_BYTES],const settings_t *s,bool jitter) {
  memset(page,255,BRIDGE_SETTINGS_PAGE_BYTES);memcpy(page,s,sizeof(*s));
  uint32_t ext[3]={JITTER_MAGIC,jitter?1u:0u,0};memcpy(page+24,ext,8);
  ext[2]=bridge_settings_crc32(page,32);memcpy(page+32,&ext[2],4);
}
