// Two CRC-protected flash sectors; only main-loop/explicit detached writes.
#include <stdio.h>
#include <string.h>
#ifdef BUZZ_JOURNAL_NATIVE_TEST
#include "journal_test_platform.h"
#else
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#endif
#include "bridge.h"
#include "ds3_trace.h"
#include "ds3_journal.h"
#define LOG_OFFSET (PICO_FLASH_SIZE_BYTES-4*FLASH_SECTOR_SIZE)
#define PROGRAM_BYTES ((sizeof(ds3_trace_record_t)+FLASH_PAGE_SIZE-1)/FLASH_PAGE_SIZE*FLASH_PAGE_SIZE)
static uint8_t page[PROGRAM_BYTES] __attribute__((aligned(4)));
static ds3_trace_record_t current_dump;
static ds3_trace_entry_t tail_dump[DS3_TRACE_RING];
static uint32_t sequence=1,slot,write_offset,writes,last_save_ms,last_controls,last_out,last_acks,tail_count;
static bool dumping,startup_saved;
static unsigned dump_source,dump_line;
static const ds3_trace_record_t *bank(unsigned b){return (const ds3_trace_record_t *)(XIP_BASE+LOG_OFFSET+b*FLASH_SECTOR_SIZE);}
void ds3_journal_init(void) {
  bool a=ds3_trace_record_valid(bank(0)),b=ds3_trace_record_valid(bank(1));
  if(a||b) {
    unsigned latest=!a?1:(!b?0:((int32_t)(bank(1)->sequence-bank(0)->sequence)>0?1:0));
    sequence=bank(latest)->sequence+1;slot=latest^1u;
  }
}
static void __not_in_flash_func(write_record)(void *unused) {
  (void)unused;flash_range_erase(write_offset,FLASH_SECTOR_SIZE);flash_range_program(write_offset,page,sizeof(page));
}
bool ds3_journal_save(uint32_t reason) {
  if(bridge_mode!=MODE_DS3||!ds3_trace_counts()->controls)return true;
  // On the first ever boot there is no previous session bank. Establish a
  // second valid bank before ever erasing the sole valid startup checkpoint.
  if(ds3_trace_record_valid(bank(slot))&&!ds3_trace_record_valid(bank(slot^1u))) {slot^=1u;sequence++;}
  static ds3_trace_record_t record;ds3_trace_pack(&record,sequence,reason,bridge_debug);
  memset(page,0xff,sizeof(page));memcpy(page,&record,sizeof(record));write_offset=LOG_OFFSET+slot*FLASH_SECTOR_SIZE;
  int rc=flash_safe_execute(write_record,NULL,1000);
  if(rc!=PICO_OK||!ds3_trace_record_valid(bank(slot))||memcmp(bank(slot),&record,sizeof(record)))return false;
  writes++;last_save_ms=to_ms_since_boot(get_absolute_time());
  last_controls=record.snapshot.counts.controls;last_out=record.snapshot.counts.out_reports;last_acks=record.snapshot.counts.in_acked;
  if(reason==DS3_SAVE_STARTUP)startup_saved=true;
  return true;
}
void ds3_journal_task(bool ready) {
  if(bridge_mode!=MODE_DS3||bridge_debug||!ready||dumping||writes>=3)return;
  const ds3_trace_counts_t *c=ds3_trace_counts();uint32_t now=to_ms_since_boot(get_absolute_time());
  if(!c->controls||usbd_edpt_busy(0,0)||usbd_edpt_busy(0,0x80)||!tud_hid_n_ready(0))return;
  // Capture startup before the first automatic PS report, while waiting/NAKing.
  bool startup=!startup_saved&&!c->in_queued&&now>=1800&&now-c->last_traffic_ms>=500;
  bool quiet=now-c->last_traffic_ms>=5000&&(!writes||now-last_save_ms>=30000);
  if(!startup&&!quiet)return;
  if(last_controls==c->controls&&last_out==c->out_reports&&last_acks==c->in_acked)return;
  // Failure is bounded too; leave the other bank intact, no retry loop/flash wear.
  if(!ds3_journal_save(startup?DS3_SAVE_STARTUP:DS3_SAVE_QUIET)){writes=3;startup_saved=true;}
}
void ds3_journal_dump_start(void) {
  ds3_trace_pack(&current_dump,sequence,0,bridge_debug);tail_count=ds3_trace_tail_count();
  for(uint32_t i=0;i<tail_count;i++)ds3_trace_tail(i,&tail_dump[i]);
  dump_source=0;dump_line=0;dumping=true;
}
bool ds3_journal_dump_active(void){return dumping;}
static void entry_line(char *out,size_t cap,const char *group,const ds3_trace_entry_t *e) {
  int n=snprintf(out,cap,"DS3LOG_%s seq=%lu ms=%lu kind=%s bm=%02X req=%02X value=%04X index=%04X length=%u actual=%u flags=%02X data=",
    group,(unsigned long)e->seq,(unsigned long)e->ms,e->kind==DS3_T_CONTROL?"CTRL":"OUT",e->bm,e->request,e->value,e->index,e->length,e->actual,e->flags);
  for(unsigned i=0;i<e->actual&&i<16&&(size_t)n+4<cap;i++)n+=snprintf(out+n,cap-(size_t)n,"%02X",e->data[i]);
  if((size_t)n+3<cap)snprintf(out+n,cap-(size_t)n,"\r\n");
}
bool ds3_journal_dump_line(char *out,size_t cap) {
  if(!dumping)return false;
  const ds3_trace_record_t *r=dump_source<2?bank(dump_source):&current_dump;
  if(dump_source==3){snprintf(out,cap,"DS3LOG_END\r\n");dumping=false;return true;}
  if(dump_source<2&&!ds3_trace_record_valid(r)) {
    snprintf(out,cap,"DS3LOG_BANK slot=%u empty_or_invalid=1\r\n",dump_source);dump_source++;dump_line=0;return true;
  }
  const ds3_trace_snapshot_t *s=&r->snapshot;
  if(!dump_line++) {
    snprintf(out,cap,"DS3LOG_BANK source=%s slot=%u sequence=%lu reason=%lu debug=%lu crc=valid first=%lu outs=%lu\r\n",
      dump_source<2?"FLASH":"RAM",dump_source,(unsigned long)r->sequence,(unsigned long)r->reason,(unsigned long)r->debug,(unsigned long)s->first_count,(unsigned long)s->out_count);return true;
  }
  if(dump_line==2) {
    const ds3_trace_counts_t *c=&s->counts;
    snprintf(out,cap,"DS3LOG_SUMMARY controls=%lu acks=%lu set_config=%lu first_in_ms=%lu in_queued=%lu in_acked=%lu in_failed=%lu out=%lu\r\n",
      (unsigned long)c->controls,(unsigned long)c->control_acks,(unsigned long)c->set_configurations,(unsigned long)c->first_in_ms,
      (unsigned long)c->in_queued,(unsigned long)c->in_acked,(unsigned long)c->in_failed,(unsigned long)c->out_reports);return true;
  }
  if(dump_line==3) {
    const ds3_trace_counts_t *c=&s->counts;
    snprintf(out,cap,"DS3LOG_SUMMARY feature_gets=%lu set_reports=%lu resets=%lu stalls=%lu skipped=%lu ps=%lu enable=%lu disable=%lu overwritten=%lu last_traffic_ms=%lu last_ms=%lu\r\n",
      (unsigned long)c->feature_gets,(unsigned long)c->set_reports,
      (unsigned long)c->bus_resets,(unsigned long)c->stalls,(unsigned long)c->skipped,(unsigned long)c->ps_pulses,
      (unsigned long)c->f4_enables,(unsigned long)c->f4_disables,(unsigned long)c->ring_overwritten,
      (unsigned long)c->last_traffic_ms,(unsigned long)c->last_ms);return true;
  }
  unsigned i=dump_line-4;
  if(i<s->first_count){entry_line(out,cap,"FIRST",&s->first[i]);return true;}
  i-=s->first_count;
  if(i<s->out_count){entry_line(out,cap,"OUT",&s->outs[i]);return true;}
  i-=s->out_count;
  if(dump_source==2&&i<tail_count){entry_line(out,cap,"TAIL",&tail_dump[i]);return true;}
  dump_source++;dump_line=0;return ds3_journal_dump_line(out,cap);
}
