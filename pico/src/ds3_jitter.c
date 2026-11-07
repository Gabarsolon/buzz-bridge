#include <string.h>
#include "ds3_jitter.h"
static uint32_t random_state=0x6d2b79f5u,last_step;
static int8_t walk[4];
static bool enabled=true;
void ds3_jitter_seed(uint32_t seed,uint32_t now) {
  random_state=seed?seed:0x6d2b79f5u;last_step=now;memset(walk,0,sizeof(walk));
}
void ds3_jitter_enable(bool value){enabled=value;}
bool ds3_jitter_enabled(void){return enabled;}
static uint32_t random_next(void) {
  uint32_t x=random_state;x^=x<<13;x^=x>>17;x^=x<<5;random_state=x;return x;
}
void ds3_jitter_tick(uint32_t now) {
  if(!enabled||(uint32_t)(now-last_step)<DS3_JITTER_INTERVAL_MS)return;
  // A late call advances once, never performs a catch-up loop after a pause.
  last_step=now;
  for(unsigned i=0;i<4;i++) {
    int next=walk[i]+(int)(random_next()%3u)-1;
    if(next>DS3_JITTER_LIMIT)next=DS3_JITTER_LIMIT-1;
    if(next<-DS3_JITTER_LIMIT)next=-DS3_JITTER_LIMIT+1;
    walk[i]=(int8_t)next;
  }
}
void ds3_jitter_apply(uint8_t p[48]) {
  if(!enabled)return;
  // Preserve the existing nominal 512, including gyro/calibration assumptions.
  // This is small synthetic noise, not measured motion or a flat-pad pose model.
  for(unsigned i=0;i<4;i++) {
    uint16_t value=(uint16_t)(512+walk[i]);
    p[40+2*i]=(uint8_t)(value>>8);p[41+2*i]=(uint8_t)value;
  }
}
