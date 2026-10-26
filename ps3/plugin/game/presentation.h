/* SPDX-License-Identifier: MIT. Local policy, not a measured firmware deadline.
 * Source evidence and limits: build/ATTACH_SAFETY_09.md. */
#ifndef BUZZ_PRESENTATION_H
#define BUZZ_PRESENTATION_H
#include <stdint.h>
#define BUZZ_PRESENT_MIN_US UINT64_C(10000000)
#define BUZZ_PRESENT_WINDOWS 10u
#define BUZZ_ATTACH_LIMIT_MS 3000u
typedef struct {
 uint64_t start,sample,window;
 unsigned polls,buckets,good,allowed,manager_bad;
} buzz_presentation;
static inline void buzz_presentation_init(buzz_presentation *g,uint64_t now,unsigned polls)
{ *g=(buzz_presentation){.start=now,.sample=now,.window=now,.polls=polls}; }
/* One observation per >=100 ms, never credit a burst as multiple frames.
 * A stalled worker cannot turn a long elapsed interval into valid windows. */
static inline int buzz_presentation_tick(buzz_presentation *g,uint64_t now,unsigned polls,int manager_ok)
{
 if(now<g->sample || now-g->sample>250000) {
  if(now<g->sample)g->start=now;
  g->sample=now;g->window=now;g->polls=polls;g->buckets=g->good=g->allowed=g->manager_bad=0;
  return 1;
 }
 if(now-g->sample<100000)return 0;
 if(!manager_ok)g->manager_bad=1;
 if(polls!=g->polls)g->buckets++;
 g->polls=polls;g->sample=now;
 if(now-g->window<1000000)return 0;
 if(!g->manager_bad && g->buckets>=5) {if(g->good<BUZZ_PRESENT_WINDOWS)g->good++;}
 else g->good=0;
 g->allowed=g->good>=BUZZ_PRESENT_WINDOWS && now-g->start>=BUZZ_PRESENT_MIN_US;
 g->buckets=g->manager_bad=0;g->window=now;return 1;
}
#endif
