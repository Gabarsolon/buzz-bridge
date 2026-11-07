#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DS3_TRACE_RING 256
#define DS3_TRACE_FIRST 64
#define DS3_TRACE_OUT 16
enum {DS3_T_CONTROL=1,DS3_T_OUT=2};
enum {DS3_T_SETUP=1,DS3_T_DATA=2,DS3_T_ACK=4,DS3_T_STALL=8,DS3_T_SKIPPED=16,DS3_T_ERROR=32};
typedef struct {
  uint32_t seq,ms;
  uint8_t bm,request;
  uint16_t value,index,length,actual;
  uint8_t kind,flags;
  uint8_t data[16];
} ds3_trace_entry_t;
typedef struct {
  uint32_t controls,control_acks,set_configurations,feature_gets,set_reports;
  uint32_t out_reports,in_queued,in_acked,in_failed,first_in_ms;
  uint32_t bus_resets,stalls,skipped,ps_pulses,f4_enables,f4_disables;
  uint32_t last_traffic_ms,last_ms,ring_overwritten;
} ds3_trace_counts_t;
typedef struct {
  ds3_trace_counts_t counts;
  uint32_t first_count,out_count;
  ds3_trace_entry_t first[DS3_TRACE_FIRST],outs[DS3_TRACE_OUT];
} ds3_trace_snapshot_t;
typedef struct {
  uint32_t magic,version,sequence,reason,debug,bytes;
  ds3_trace_snapshot_t snapshot;
  uint32_t crc;
} ds3_trace_record_t;
enum {DS3_SAVE_QUIET=1,DS3_SAVE_COMMAND=2,DS3_SAVE_RECOVERY=3,DS3_SAVE_STARTUP=4};
void ds3_trace_init(void);
void ds3_trace_setup(const uint8_t setup[8],uint32_t now);
void ds3_trace_data(const uint8_t *data,uint16_t n,uint16_t offset,uint32_t now);
void ds3_trace_flags(uint8_t flags,uint32_t now);
void ds3_trace_out(const uint8_t *data,uint16_t n,uint32_t now);
void ds3_trace_control_output(void);
void ds3_trace_in(bool queued,bool success,uint32_t now);
void ds3_trace_reset_bus(uint32_t now);
void ds3_trace_state(uint32_t ps,uint32_t enables,uint32_t disables,uint32_t now);
void ds3_trace_snapshot(ds3_trace_snapshot_t *out);
const ds3_trace_counts_t *ds3_trace_counts(void);
uint32_t ds3_trace_tail_count(void);
bool ds3_trace_tail(uint32_t i,ds3_trace_entry_t *out);
void ds3_trace_pack(ds3_trace_record_t *out,uint32_t sequence,uint32_t reason,bool debug);
bool ds3_trace_record_valid(const ds3_trace_record_t *r);
