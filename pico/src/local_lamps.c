// Original local policy, not an emulation of any game's question/round state.
#include <string.h>
#include "local_lamps.h"
void local_lamps_init(local_lamps_t *s,bool enabled,bool first,uint32_t hold_ms) {
  memset(s,0,sizeof(*s));s->enabled=enabled;s->first=first;s->hold_ms=hold_ms;
}
void local_lamps_remote(local_lamps_t *s) {
  s->remote_owned=true;s->mask=0;memset(s->expires,0,sizeof(s->expires));
}
uint8_t local_lamps_tick(local_lamps_t *s,uint32_t buttons,uint32_t now) {
  uint8_t reds=0;
  for(unsigned i=0;i<4;i++)if(buttons&(1u<<(5*i)))reds|=(uint8_t)(1u<<i);
  uint8_t rising=reds&(uint8_t)~s->previous_reds;s->previous_reds=reds;
  if(!s->enabled||s->remote_owned){s->mask=0;return 0;}
  if(s->first) {
    if(s->mask) {
      unsigned winner=0;while(!(s->mask&(1u<<winner)))winner++;
      if((int32_t)(now-s->expires[winner])>=0)s->mask=0;
    }
    // Lowest handset number wins ties in the same USB report. New edges during
    // lock-out are discarded, not queued; a held button cannot re-trigger.
    if(!s->mask&&rising)for(unsigned i=0;i<4;i++)if(rising&(1u<<i)) {
      s->mask=(uint8_t)(1u<<i);s->expires[i]=now+s->hold_ms;break;
    }
  } else {
    for(unsigned i=0;i<4;i++) {
      if(reds&(1u<<i)){s->mask|=(uint8_t)(1u<<i);s->expires[i]=now+s->hold_ms;}
      else if((s->mask&(1u<<i))&&(int32_t)(now-s->expires[i])>=0)s->mask&=(uint8_t)~(1u<<i);
    }
  }
  return s->mask;
}
