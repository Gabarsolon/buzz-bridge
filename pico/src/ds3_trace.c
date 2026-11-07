// Bounded, task-context collector. No flash or USB operations in this module.
#include <string.h>
#include "ds3_trace.h"
_Static_assert(sizeof(ds3_trace_entry_t)==36,"trace entry format changed");
_Static_assert(sizeof(ds3_trace_record_t)<=4096,"journal exceeds one sector");
static ds3_trace_snapshot_t state;
static ds3_trace_entry_t ring[DS3_TRACE_RING];
static uint32_t sequence,ring_count,active_seq,active_first;
static uint32_t previous_ps,previous_enables,previous_disables;
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t crc32(const void *buffer,size_t n) {
  const uint8_t *p=buffer;uint32_t crc=0xffffffffu;
  while(n--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
  return ~crc;
}
void ds3_trace_init(void){memset(&state,0,sizeof(state));memset(ring,0,sizeof(ring));sequence=ring_count=active_seq=active_first=0;previous_ps=previous_enables=previous_disables=0;}
static ds3_trace_entry_t *add(uint32_t now) {
  sequence++;ds3_trace_entry_t *e=&ring[(sequence-1)%DS3_TRACE_RING];memset(e,0,sizeof(*e));
  e->seq=sequence;e->ms=now;
  if(ring_count<DS3_TRACE_RING)ring_count++;else state.counts.ring_overwritten++;
  state.counts.last_traffic_ms=state.counts.last_ms=now;return e;
}
void ds3_trace_setup(const uint8_t p[8],uint32_t now) {
  ds3_trace_entry_t *e=add(now);e->kind=DS3_T_CONTROL;e->flags=DS3_T_SETUP;
  e->bm=p[0];e->request=p[1];e->value=le16(p+2);e->index=le16(p+4);e->length=le16(p+6);
  state.counts.controls++;active_seq=e->seq;active_first=state.counts.controls-1;
  if(e->bm==0&&e->request==9)state.counts.set_configurations++;
  if((e->bm&0x60)==0x20&&e->request==1&&(e->value>>8)==3)state.counts.feature_gets++;
  if((e->bm&0x60)==0x20&&e->request==9)state.counts.set_reports++;
  if(active_first<DS3_TRACE_FIRST){state.first[active_first]=*e;state.first_count=active_first+1;}
}
static ds3_trace_entry_t *active(void) {
  if(!active_seq)return NULL;
  ds3_trace_entry_t *e=&ring[(active_seq-1)%DS3_TRACE_RING];
  return e->seq==active_seq?e:NULL;
}
static void sync_first(ds3_trace_entry_t *e){if(active_first<DS3_TRACE_FIRST)state.first[active_first]=*e;}
void ds3_trace_data(const uint8_t *p,uint16_t n,uint16_t offset,uint32_t now) {
  ds3_trace_entry_t *e=active();if(!e)return;
  if(offset<16&&p){unsigned copy=n;if(copy>16u-offset)copy=16u-offset;memcpy(e->data+offset,p,copy);}
  uint32_t total=(uint32_t)offset+n;e->actual=(uint16_t)(total>65535?65535:total);e->flags|=DS3_T_DATA;
  state.counts.last_traffic_ms=state.counts.last_ms=now;sync_first(e);
}
void ds3_trace_flags(uint8_t flags,uint32_t now) {
  ds3_trace_entry_t *e=active();if(!e)return;
  if((flags&DS3_T_ACK)&&!(e->flags&DS3_T_ACK))state.counts.control_acks++;
  if((flags&DS3_T_STALL)&&!(e->flags&DS3_T_STALL))state.counts.stalls++;
  if((flags&DS3_T_SKIPPED)&&!(e->flags&DS3_T_SKIPPED))state.counts.skipped++;
  e->flags|=flags;state.counts.last_ms=now;sync_first(e);
}
void ds3_trace_out(const uint8_t *p,uint16_t n,uint32_t now) {
  ds3_trace_entry_t *e=add(now);e->kind=DS3_T_OUT;e->flags=DS3_T_DATA|DS3_T_ACK;e->index=2;e->length=e->actual=n;
  memcpy(e->data,p,n<16?n:16);state.counts.out_reports++;
  if(state.out_count==DS3_TRACE_OUT)memmove(state.outs,state.outs+1,(DS3_TRACE_OUT-1)*sizeof(*e));else state.out_count++;
  state.outs[state.out_count-1]=*e;
}
void ds3_trace_control_output(void){state.counts.out_reports++;}
void ds3_trace_in(bool queued,bool success,uint32_t now) {
  if(queued){if(!state.counts.in_queued)state.counts.first_in_ms=now;state.counts.in_queued++;}
  else if(success)state.counts.in_acked++;else state.counts.in_failed++;
  state.counts.last_ms=now;
}
void ds3_trace_reset_bus(uint32_t now){state.counts.bus_resets++;state.counts.last_traffic_ms=state.counts.last_ms=now;active_seq=0;}
void ds3_trace_state(uint32_t ps,uint32_t enables,uint32_t disables,uint32_t now) {
  state.counts.ps_pulses+=ps>=previous_ps?ps-previous_ps:ps;
  state.counts.f4_enables+=enables>=previous_enables?enables-previous_enables:enables;
  state.counts.f4_disables+=disables>=previous_disables?disables-previous_disables:disables;
  previous_ps=ps;previous_enables=enables;previous_disables=disables;state.counts.last_ms=now;
}
void ds3_trace_snapshot(ds3_trace_snapshot_t *out){*out=state;}
const ds3_trace_counts_t *ds3_trace_counts(void){return &state.counts;}
uint32_t ds3_trace_tail_count(void){return ring_count;}
bool ds3_trace_tail(uint32_t i,ds3_trace_entry_t *out) {
  if(i>=ring_count)return false;
  *out=ring[(sequence-ring_count+i)%DS3_TRACE_RING];return true;
}
void ds3_trace_pack(ds3_trace_record_t *r,uint32_t seq,uint32_t reason,bool debug) {
  memset(r,0,sizeof(*r));r->magic=0x44334c47;r->version=1;r->sequence=seq;r->reason=reason;r->debug=debug;
  r->bytes=sizeof(*r);r->snapshot=state;r->crc=crc32(r,offsetof(ds3_trace_record_t,crc));
}
bool ds3_trace_record_valid(const ds3_trace_record_t *r) {
  return r->magic==0x44334c47&&r->version==1&&r->bytes==sizeof(*r)&&
    r->snapshot.first_count<=DS3_TRACE_FIRST&&r->snapshot.out_count<=DS3_TRACE_OUT&&
    r->crc==crc32(r,offsetof(ds3_trace_record_t,crc));
}
