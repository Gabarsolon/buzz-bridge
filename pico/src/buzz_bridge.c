// Buzz host -> native USB HID bridge for Pico 2. USB stacks never cross cores.
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "pio_usb.h"
#include "tusb.h"
#include "bridge.h"
#include "captured_descriptors.h"
#include "local_lamps.h"
#include "ds3.h"
#include "ds3_trace.h"
#include "ds3_journal.h"
#include "ds3_jitter.h"
#include "bridge_settings.h"
#include "pico/unique_id.h"

uint8_t bridge_mode=MODE_CLONE;
bool bridge_debug=true;
// Do not use the final sector: RP2350 UF2 E10 mitigation can erase it.
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES-2*FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC BRIDGE_SETTINGS_MAGIC
typedef struct {uint8_t raw[5];} sample_t;
typedef struct {char text[384];} log_t;
typedef struct {uint8_t data[7];bool local;uint32_t epoch;} lamp_t;
static queue_t reports, policy_reports, logs, lamps;
static volatile uint32_t host_mounted, host_count, seen_buttons, input_drops;
static volatile uint32_t lamp_ok, lamp_fail, log_drops, bad_input;
static volatile uint32_t host_speed=255;
static volatile bool host_core_ready;
static volatile uint32_t lamp_epoch;
static local_lamps_t local_policy;
static bool local_enabled,local_first=true,remote_desired_valid,rumble_probe;
static bool ds3_auto_ps=true,debug_chord_tracking;
static uint32_t debug_chord_since;
static uint32_t local_hold_ms=2000;
static uint8_t local_last_mask=255;
static uint32_t local_buttons;
static uint32_t ds3_lease_until;
static bool ds3_lease_active;
static uint32_t now_ms(void);
static void ds3_packet(uint32_t bits,uint8_t payload[48]) {
  ds3_input(ds3_streaming()?bits:0,payload);
  ds3_jitter_apply(payload);
  if(ds3_ps_value(now_ms()))payload[3]=1;
}
// Everything below except queues/counters is used by exactly one core.
static uint8_t host_addr,host_instance,host_input[5];
static bool get_pending,get_busy,lamp_busy,lamp_pending;
static uint8_t get_attempts,lamp_attempts;
static uint32_t next_get,next_lamp;
static uint8_t previous_input[5];
static bool previous_input_valid;
static lamp_t lamp_work;
static uint8_t live_raw[5]={0,0,0,0,0xf0}, current_raw[5]={0,0,0,0,0xf0};
static uint8_t desired_lamps[7],inject_raw[5];
static bool injected,inject_dirty,initial_send=true;
static uint8_t idle_rate[2];
static uint32_t last_sent[2];
enum {ACTION_NONE,ACTION_BOOTSEL,ACTION_REBOOT,ACTION_SAVE,ACTION_LOG_SAVE};
static unsigned action;
static uint32_t action_at;
static settings_t pending_settings;
// A CDC-only bench command can briefly enumerate a clean mode, then return to
// the saved configuration without a physical BOOTSEL press. Not used normally.
#define TRIAL_MAGIC 0x425a5432u
static uint32_t trial_deadline;
static uint8_t settings_page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));

static uint32_t now_ms(void){return to_ms_since_boot(get_absolute_time());}
static bool due(uint32_t now,uint32_t when){return (int32_t)(now-when)>=0;}
static void bridge_log(const char *fmt,...) {
  if(!bridge_debug)return;
  log_t l;va_list ap;va_start(ap,fmt);vsnprintf(l.text,sizeof(l.text),fmt,ap);va_end(ap);
  if(!queue_try_add(&logs,&l))__atomic_fetch_add(&log_drops,1,__ATOMIC_RELAXED);
}
static void settings_load(void) {
  settings_t s;memcpy(&s,(const void *)(XIP_BASE+SETTINGS_OFFSET),sizeof(s));
  if(bridge_settings_valid(&s)) {
    bridge_mode=s.mode_debug&0xff;bridge_debug=(s.mode_debug&0x100)!=0;
    local_enabled=(s.local_flags&1)!=0;local_first=(s.local_flags&2)!=0;rumble_probe=(s.local_flags&4)!=0;local_hold_ms=s.hold_ms;
    if(s.version==3)ds3_auto_ps=(s.local_flags&8)!=0;
    ds3_jitter_enable(bridge_settings_jitter((const uint8_t *)(XIP_BASE+SETTINGS_OFFSET)));
  } else if(s.magic==SETTINGS_MAGIC&&s.version==1&&s.mode_debug<=3&&s.local_flags==bridge_settings_crc32(&s,12)) {
    bridge_mode=s.mode_debug&1;bridge_debug=(s.mode_debug&2)!=0;
  }
#if BUZZ_RECOVERY
  bridge_debug=true;
#endif
}
static void __not_in_flash_func(settings_write)(void *unused) {
  (void)unused;
  flash_range_erase(SETTINGS_OFFSET,FLASH_SECTOR_SIZE);
  flash_range_program(SETTINGS_OFFSET,settings_page,sizeof(settings_page));
}
static void schedule_save(uint8_t mode,bool debug) {
  if(!host_core_ready){bridge_log("FAIL host core not ready; retry command\r\n");return;}
  pending_settings=(settings_t){SETTINGS_MAGIC,3,mode|(debug?0x100u:0u),
    (local_enabled?1u:0u)|(local_first?2u:0u)|(rumble_probe?4u:0u)|(ds3_auto_ps?8u:0u),local_hold_ms,0};
  pending_settings.crc=bridge_settings_crc32(&pending_settings,20);
  action=ACTION_SAVE;action_at=now_ms()+300;
  bridge_log("CONFIG_SAVING mode=%c debug=%u; rebooting\r\n",'A'+mode,debug);
}
static void publish_input(const uint8_t *p,uint16_t n) {
  if(n!=5){bad_input++;bridge_log("FAIL host input length=%u expected=5\r\n",n);return;}
  host_count++;seen_buttons|=buzz_buttons(p);
  if(previous_input_valid&&!memcmp(previous_input,p,5))return;
  sample_t s;memcpy(s.raw,p,5);memcpy(previous_input,p,5);previous_input_valid=true;
  if(!queue_try_add(&policy_reports,&s)) {
    sample_t old;queue_try_remove(&policy_reports,&old);queue_try_add(&policy_reports,&s);input_drops++;
  }
  if(!queue_try_add(&reports,&s)) {
    // Bounded FIFO: on overflow retain the newest state and expose the loss.
    sample_t old;queue_try_remove(&reports,&old);queue_try_add(&reports,&s);input_drops++;
  }
  bridge_log("INPUT 5 %02X %02X %02X %02X %02X bits=%05lX\r\n",
    p[0],p[1],p[2],p[3],p[4],(unsigned long)buzz_buttons(p));
}
void tuh_hid_mount_cb(uint8_t addr,uint8_t instance,const uint8_t *desc,uint16_t n) {
  uint16_t vid,pid;tuh_vid_pid_get(addr,&vid,&pid);
  if(vid!=0x054c||pid!=0x1000||instance!=0||host_addr)return;
  if(n!=sizeof(captured_hid_report)||!desc||memcmp(desc,captured_hid_report,n)) {
    bridge_log("FAIL Buzz report descriptor differs from captured unit\r\n");return;
  }
  host_addr=addr;host_instance=instance;
  host_speed=tuh_speed_get(addr);
  get_pending=true;get_busy=false;get_attempts=0;next_get=0;
  lamp_busy=false;lamp_pending=false;
  // New physical attachment starts with lamps off. Subsequent PC outputs follow.
  lamp_t off={0};queue_try_add(&lamps,&off);host_mounted=1;
  bridge_log("HOST_MOUNT VID=054C PID=1000 speed=%u report_descriptor=match\r\n",tuh_speed_get(addr));
  if(!tuh_hid_receive_report(addr,instance))bridge_log("FAIL initial interrupt receive; retrying\r\n");
}
void tuh_hid_umount_cb(uint8_t addr,uint8_t instance) {
  (void)instance;if(addr!=host_addr)return;
  host_addr=0;host_mounted=0;lamp_busy=false;lamp_pending=false;get_pending=false;get_busy=false;
  lamp_t l;while(queue_try_remove(&lamps,&l)){}
  sample_t s;while(queue_try_remove(&reports,&s)){}
  while(queue_try_remove(&policy_reports,&s)){}
  previous_input_valid=false;
  const sample_t neutral={{0,0,0,0,0xf0}};queue_try_add(&reports,&neutral);queue_try_add(&policy_reports,&neutral);
  bridge_log("HOST_UNMOUNT; buttons released\r\n");
}
void tuh_hid_report_received_cb(uint8_t addr,uint8_t instance,const uint8_t *p,uint16_t n) {
  if(addr!=host_addr||instance!=host_instance)return;
  publish_input(p,n);
  tuh_hid_receive_report(addr,instance);
}
void tuh_hid_get_report_complete_cb(uint8_t addr,uint8_t instance,uint8_t id,uint8_t type,uint16_t n) {
  (void)instance;(void)id;(void)type;if(addr!=host_addr)return;
  get_busy=false;
  if(n==5){publish_input(host_input,n);get_pending=false;}
  else {bridge_log("FAIL initial GET_REPORT bytes=%u\r\n",n);next_get=now_ms()+100;}
}
void tuh_hid_set_report_complete_cb(uint8_t addr,uint8_t instance,uint8_t id,uint8_t type,uint16_t n) {
  (void)instance;(void)id;(void)type;if(addr!=host_addr)return;
  lamp_busy=false;
  if(n==7) {
    lamp_ok++;lamp_pending=false;
    bridge_log("LAMP_OK %02X %02X %02X %02X %02X %02X %02X\r\n",
      lamp_work.data[0],lamp_work.data[1],lamp_work.data[2],lamp_work.data[3],
      lamp_work.data[4],lamp_work.data[5],lamp_work.data[6]);
  } else {
    lamp_fail++;next_lamp=now_ms()+100;
    bridge_log("FAIL lamp completion bytes=%u expected=7 attempt=%u\r\n",n,lamp_attempts);
  }
}
static void core1_main(void) {
  flash_safe_execute_core_init();
  pio_usb_configuration_t cfg=PIO_USB_DEFAULT_CONFIG;cfg.pin_dp=16;
  tuh_configure(1,TUH_CFGID_RPI_PIO_USB_CONFIGURATION,&cfg);tuh_init(1);
  host_core_ready=true;
  while(true) {
    tuh_task();uint32_t now=now_ms();
    if(!host_addr)continue;
    if(tuh_hid_receive_ready(host_addr,host_instance))tuh_hid_receive_report(host_addr,host_instance);
    if(!get_busy&&!lamp_busy&&get_pending&&due(now,next_get)) {
      if(get_attempts>=3){get_pending=false;bridge_log("FAIL initial GET_REPORT exhausted; interrupt input remains active\r\n");}
      else {
        get_attempts++;get_busy=true;
        if(!tuh_hid_get_report(host_addr,host_instance,0,HID_REPORT_TYPE_INPUT,host_input,5)) {
          get_busy=false;next_get=now+100;
          bridge_log("FAIL GET_REPORT submit attempt=%u\r\n",get_attempts);
        }
      }
    }
    if(!get_busy&&!get_pending&&!lamp_busy) {
      if(!lamp_pending&&queue_try_remove(&lamps,&lamp_work)){lamp_pending=true;lamp_attempts=0;next_lamp=0;}
      if(lamp_pending&&lamp_work.local&&lamp_work.epoch!=lamp_epoch)lamp_pending=false;
      if(lamp_pending&&due(now,next_lamp)) {
        if(lamp_attempts>=3){lamp_pending=false;lamp_fail++;bridge_log("FAIL lamp request exhausted\r\n");}
        else {
          // Buffer is never changed until completion (or unplug).
          lamp_busy=true;lamp_attempts++;
          if(!tuh_hid_set_report(host_addr,host_instance,0,HID_REPORT_TYPE_OUTPUT,lamp_work.data,7)) {
            lamp_busy=false;next_lamp=now+100;
            bridge_log("FAIL lamp submit attempt=%u; retrying\r\n",lamp_attempts);
          }
        }
      }
    }
  }
}
static bool enqueue_lamps(const uint8_t data[7],bool local) {
  lamp_t l={0};memcpy(l.data,data,7);l.local=local;l.epoch=lamp_epoch;
  if(!host_mounted)return false;
  if(!queue_try_add(&lamps,&l)){__atomic_fetch_add(&lamp_fail,1,__ATOMIC_RELAXED);bridge_log("FAIL lamp queue full\r\n");return false;}
  memcpy(desired_lamps,data,7);
  return true;
}
static void queue_lamps(const uint8_t data[7]) {
  // Latch host authority even for an all-off packet or an absent dongle. Only
  // explicit rearm, device disconnect, or reboot permits local output again.
  if(!local_policy.remote_owned) {
    __atomic_fetch_add(&lamp_epoch,1,__ATOMIC_RELAXED);
    lamp_t stale;while(queue_try_remove(&lamps,&stale)){}
    local_lamps_remote(&local_policy);
    bridge_log("LOCAL_YIELD host output owns lamps until disconnect/rearm\r\n");
  }
  remote_desired_valid=true;memcpy(desired_lamps,data,7);
  enqueue_lamps(data,false);
}
static void local_task(void) {
  static bool was_host;
  bool present=host_mounted!=0;
  if(present!=was_host) {
    was_host=present;local_last_mask=255;
    bool owned=local_policy.remote_owned;
    local_lamps_init(&local_policy,local_enabled,local_first,local_hold_ms);
    if(owned)local_lamps_remote(&local_policy);
    if(present&&owned&&remote_desired_valid)enqueue_lamps(desired_lamps,false);
  }
  // This FIFO is independent of USB device endpoint readiness. A PS3 that is
  // not reading HID input cannot stall the physical button-to-lamp policy.
  sample_t sample;
  while(queue_try_remove(&policy_reports,&sample)) {
    local_buttons=buzz_buttons(sample.raw);
    if(!injected)local_lamps_tick(&local_policy,local_buttons,now_ms());
  }
  if(bridge_mode!=MODE_CLONE||!present)return;
  uint8_t mask=local_lamps_tick(&local_policy,injected?buzz_buttons(current_raw):local_buttons,now_ms());
  if(!local_enabled||local_policy.remote_owned)return;
  if(mask!=local_last_mask) {
    uint8_t data[7]={0};for(unsigned j=0;j<4;j++)data[j+1]=(mask&(1u<<j))?255:0;
    if(enqueue_lamps(data,true)) {
      local_last_mask=mask;bridge_log("LOCAL mask=%X policy=%s source=%s\r\n",mask,local_first?"first":"follow",injected?"SIMULATED":"physical");
    }
  }
}
static void ds3_activation_task(void) {
  if(bridge_mode!=MODE_DS3)return;
  uint32_t bits=injected?buzz_buttons(current_raw):local_buttons;
  uint32_t before=ds3_ps_count();ds3_activation_tick(bits,now_ms());
  if(ds3_ps_count()!=before)bridge_log("DS3_PS requested startup_manual_or_wake count=%lu wake_used=%u\r\n",(unsigned long)ds3_ps_count(),ds3_wake_used());
  ds3_trace_state(ds3_ps_count(),ds3_enable_count(),ds3_disable_count(),now_ms());
  // P1 red + blue for 3 seconds is a deliberate recovery chord. Save debug-on
  // so unplugging/powering off after the switch still exposes CDC on the PC.
  if(!bridge_debug&&(bits&0x11u)==0x11u) {
    if(!debug_chord_tracking){debug_chord_tracking=true;debug_chord_since=now_ms();}
    if(!action&&now_ms()-debug_chord_since>=3000)schedule_save(MODE_DS3,true);
  } else debug_chord_tracking=false;
}
void bridge_ds3_bus_reset(void){ds3_session_reset(now_ms(),ds3_auto_ps);ds3_trace_state(0,0,0,now_ms());debug_chord_tracking=false;}
uint16_t tud_hid_get_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t *buf,uint16_t size) {
  if(bridge_mode==MODE_DS3) {
    if(instance)return 0;
    if(type==HID_REPORT_TYPE_FEATURE) {
      uint16_t n=ds3_feature_get(id,buf,size);bridge_log("DS3_GET_FEATURE id=%02X bytes=%u\r\n",id,n);return n;
    }
    if(id==1&&type==HID_REPORT_TYPE_INPUT) {
      uint8_t data[48];ds3_packet(buzz_buttons(current_raw),data);uint16_t n=tu_min16(size,48);memcpy(buf,data,n);return n;
    }
    return 0;
  }
  if(id||instance>=(bridge_mode==MODE_CLONE?1:2))return 0;
  uint8_t data[7];unsigned n=0;
  if(type==HID_REPORT_TYPE_INPUT) {
    if(bridge_mode==MODE_CLONE){memcpy(data,current_raw,5);n=5;}
    else {buzz_pad_report(current_raw,instance,data);n=4;}
  } else if(type==HID_REPORT_TYPE_OUTPUT&&bridge_mode==MODE_CLONE) {
    memcpy(data,desired_lamps,7);n=7;
  }
  n=tu_min16(n,size);memcpy(buf,data,n);return n;
}
void tud_hid_set_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,const uint8_t *buf,uint16_t len) {
  if(bridge_mode==MODE_DS3&&instance==0) {
    if(type==HID_REPORT_TYPE_FEATURE) {
      bool ok=ds3_feature_set(id,buf,len);
      if(ok&&id==0xf4&&ds3_streaming())ds3_enable_anchor(now_ms());
      if(ok&&id==0xf4&&!ds3_streaming())ds3_disable_anchor(now_ms());
      bridge_log("DS3_SET_FEATURE id=%02X bytes=%u accepted=%u\r\n",id,len,ok);return;
    }
    if(type==HID_REPORT_TYPE_OUTPUT) {
      if(id==0)ds3_trace_out(buf,len,now_ms());else ds3_trace_control_output();
      if(id==0&&len==1&&buf[0]==1){ds3_linux_usb_poke();bridge_log("DS3_ENABLE Linux USB one-byte output poke\r\n");return;}
      uint8_t small=0,large=0,mask=0;bool commit=false;
      bool ok=ds3_output(id,buf,len,now_ms(),&small,&large,&mask,&commit);
      if(rumble_probe&&ok){mask=(small||large)?15:0;commit=true;bridge_log("DS3_PROBE any rumble -> mask=%X\r\n",mask);}
      bridge_log("DS3_OUTPUT id=%02X bytes=%u accepted=%u small=%u large=%u commit=%u mask=%X\r\n",id,len,ok,small,large,commit,mask);
      for(unsigned off=0;off<len&&off<64;off+=16) {
        char hex[80];int k=snprintf(hex,sizeof(hex),"DS3_HEX %02u",off);
        for(unsigned j=off;j<len&&j<off+16&&j<64;j++)k+=snprintf(hex+k,sizeof(hex)-(unsigned)k," %02X",buf[j]);
        bridge_log("%s\r\n",hex);
      }
      if(commit) {
        uint8_t data[7]={0};for(unsigned i=0;i<4;i++)data[i+1]=(mask&(1u<<i))?255:0;
        queue_lamps(data);
        ds3_lease_active=mask!=0;ds3_lease_until=now_ms()+1500;
      }
      return;
    }
  }
  if(bridge_mode==MODE_CLONE&&instance==0&&id==0&&type==HID_REPORT_TYPE_OUTPUT&&len==7)
    queue_lamps(buf);
  else bridge_log("FAIL rejected HID output instance=%u id=%u type=%u length=%u\r\n",instance,id,type,len);
}
bool tud_hid_set_idle_cb(uint8_t instance,uint8_t rate) {
  if(instance>=2)return false;
  idle_rate[instance]=rate;return true;
}
void tud_mount_cb(void){initial_send=true;if(bridge_mode==MODE_DS3)ds3_session_configured(now_ms());bridge_log("DEVICE_MOUNT mode=%c debug=%u\r\n",'A'+bridge_mode,bridge_debug);}
void tud_umount_cb(void) {
  ds3_output_reset();ds3_lease_active=false;
  if(bridge_mode==MODE_DS3)bridge_ds3_bus_reset();
  __atomic_fetch_add(&lamp_epoch,1,__ATOMIC_RELAXED);
  lamp_t stale;while(queue_try_remove(&lamps,&stale)){}
  local_lamps_init(&local_policy,local_enabled,local_first,local_hold_ms);
  remote_desired_valid=false;local_last_mask=255;
  const uint8_t off[7]={0};enqueue_lamps(off,false);
}
void tud_cdc_line_coding_cb(uint8_t itf,const cdc_line_coding_t *coding) {
  (void)itf;
  // Delay permits the SET_LINE_CODING control status stage to complete.
  if(bridge_debug&&coding->bit_rate==1200){action=ACTION_BOOTSEL;action_at=now_ms()+250;}
}
static void status(void) {
  bridge_log("STATUS mode=%c debug=%u recovery=%u host=%lu speed=%lu inputs=%lu seen20=%05lX input_drops=%lu bad_input=%lu lamp_ok=%lu lamp_fail=%lu log_drops=%lu injected=%u\r\n",
    'A'+bridge_mode,bridge_debug,BUZZ_RECOVERY,(unsigned long)host_mounted,(unsigned long)host_speed,(unsigned long)host_count,
    (unsigned long)seen_buttons,(unsigned long)input_drops,(unsigned long)bad_input,
    (unsigned long)lamp_ok,(unsigned long)lamp_fail,(unsigned long)log_drops,injected);
  bridge_log("RAW %02X %02X %02X %02X %02X\r\n",live_raw[0],live_raw[1],live_raw[2],live_raw[3],live_raw[4]);
  bridge_log("LOCAL_STATUS enabled=%u policy=%s hold_ms=%lu owner=%s mask=%X\r\n",local_enabled,
    local_first?"first":"follow",(unsigned long)local_hold_ms,local_policy.remote_owned?"host":"local",local_policy.mask);
  if(bridge_mode==MODE_DS3)bridge_log("DS3_STATUS streaming=%u auto_ps=%u ps_pulses=%lu lamp_lease=%u rumble_probe=%u controls=%lu in_acked=%lu out=%lu jitter=%u wake_used=%u wake_left=%u wake_pending=%u\r\n",
    ds3_streaming(),ds3_auto_ps,(unsigned long)ds3_ps_count(),ds3_lease_active,rumble_probe,
    (unsigned long)ds3_trace_counts()->controls,(unsigned long)ds3_trace_counts()->in_acked,(unsigned long)ds3_trace_counts()->out_reports,
    ds3_jitter_enabled(),ds3_wake_used(),3u-ds3_wake_used(),ds3_wake_pending());
}
static void command(const char *line) {
  char mode,extra;unsigned debug,bits;
  if(!strcmp(line,"status")){status();return;}
  if(!strcmp(line,"ds3log")){ds3_journal_dump_start();return;}
  if(!strcmp(line,"ds3log save")){action=ACTION_LOG_SAVE;action_at=now_ms()+250;return;}
  if(!strcmp(line,"ds3 auto on")||!strcmp(line,"ds3 auto off")) {
    ds3_auto_ps=!strcmp(line,"ds3 auto on");schedule_save(bridge_mode,bridge_debug);return;
  }
  if(!strcmp(line,"ds3 ps")){ds3_request_ps(now_ms());return;}
  if(!strcmp(line,"jitter on")||!strcmp(line,"jitter off")) {
    ds3_jitter_enable(!strcmp(line,"jitter on"));schedule_save(bridge_mode,bridge_debug);return;
  }
  if(!strcmp(line,"rumble probe on")||!strcmp(line,"rumble probe off")) {
    rumble_probe=!strcmp(line,"rumble probe on");schedule_save(bridge_mode,bridge_debug);return;
  }
  if(!strcmp(line,"local rearm")) {
    __atomic_fetch_add(&lamp_epoch,1,__ATOMIC_RELAXED);
    lamp_t stale;while(queue_try_remove(&lamps,&stale)){}
    local_lamps_init(&local_policy,local_enabled,local_first,local_hold_ms);
    remote_desired_valid=false;local_last_mask=255;bridge_log("LOCAL_REARM explicit local ownership\r\n");return;
  }
  if(!strcmp(line,"local on")||!strcmp(line,"local off")||!strcmp(line,"local first")||!strcmp(line,"local follow")) {
    if(!strcmp(line,"local on"))local_enabled=true;
    if(!strcmp(line,"local off"))local_enabled=false;
    if(!strcmp(line,"local first"))local_first=true;
    if(!strcmp(line,"local follow"))local_first=false;
    schedule_save(bridge_mode,bridge_debug);return;
  }
  if(sscanf(line,"local hold %u %c",&bits,&extra)==1&&bits>=100&&bits<=10000) {
    local_hold_ms=bits;schedule_save(bridge_mode,bridge_debug);return;
  }
  if(!strcmp(line,"help")) {
    bridge_log("COMMANDS status | ds3log [save] | jitter on/off | ds3 auto on/off | ds3 ps | config a/b/c 0/1 | local on/off/first/follow | local hold 100..10000 | local rearm | rumble probe on/off | mode a/b/c | debug on/off | lamps 0..F | inject HEX20 | live | trial a/b/c (30s) | reboot | boot\r\n");return;
  }
  if(sscanf(line,"config %c %u %c",&mode,&debug,&extra)==2&&
     (mode=='a'||mode=='b'||mode=='c')&&debug<2){schedule_save(mode-'a',debug!=0);return;}
  if(!strcmp(line,"mode a")||!strcmp(line,"mode b")||!strcmp(line,"mode c")){schedule_save(line[5]-'a',bridge_debug);return;}
  if(!strcmp(line,"debug on")||!strcmp(line,"debug off")){schedule_save(bridge_mode,!strcmp(line,"debug on"));return;}
  if(sscanf(line,"lamps %x %c",&bits,&extra)==1&&bits<=15) {
    uint8_t data[7]={0};for(unsigned j=0;j<4;j++)data[j+1]=(bits&(1u<<j))?255:0;
    queue_lamps(data);return;
  }
  if(sscanf(line,"inject %x %c",&bits,&extra)==1&&bits<=0xfffff) {
    memcpy(inject_raw,live_raw,5);inject_raw[2]=(uint8_t)bits;inject_raw[3]=(uint8_t)(bits>>8);
    inject_raw[4]=(uint8_t)((live_raw[4]&0xf0)|(bits>>16));
    injected=true;inject_dirty=true;bridge_log("SIMULATED bits=%05X (not physical proof)\r\n",bits);return;
  }
  if(!strcmp(line,"live")){injected=false;initial_send=true;bridge_log("LIVE input restored\r\n");return;}
  if(!strcmp(line,"trial a")||!strcmp(line,"trial b")||!strcmp(line,"trial c")) {
    watchdog_hw->scratch[0]=TRIAL_MAGIC;watchdog_hw->scratch[1]=line[6]-'a';
    action=ACTION_REBOOT;action_at=now_ms()+250;
    bridge_log("TRIAL clean mode %c for 30 seconds; saved default retained\r\n",line[6]-'a'+'A');return;
  }
  if(!strcmp(line,"boot")){action=ACTION_BOOTSEL;action_at=now_ms()+250;bridge_log("BOOTSEL in 250 ms\r\n");return;}
  if(!strcmp(line,"reboot")){action=ACTION_REBOOT;action_at=now_ms()+250;return;}
  if(line[0])bridge_log("FAIL unknown command; use help\r\n");
}
static void cdc_task(void) {
  static log_t l;static bool pending;static unsigned offset;
  static char line[96];static unsigned used;static bool overflow;
  if(!bridge_debug||!tud_cdc_connected())return;
  while(tud_cdc_available()) {
    char c=(char)tud_cdc_read_char();
    if(c=='\r'||c=='\n') {
      line[used]=0;if(!overflow)command(line);else bridge_log("FAIL command too long\r\n");
      used=0;overflow=false;
    } else if(used<sizeof(line)-1)line[used++]=c;else overflow=true;
  }
  if(!pending) {
    if(ds3_journal_dump_active())pending=ds3_journal_dump_line(l.text,sizeof(l.text));
    else pending=queue_try_remove(&logs,&l);
    if(pending)offset=0;
  }
  if(pending) {
    offset+=tud_cdc_write(l.text+offset,strlen(l.text)-offset);
    if(offset==strlen(l.text))pending=false;
  }
  tud_cdc_write_flush();
}
static void device_reports_task(void) {
  static bool pending;static uint8_t sent_mask;
  unsigned count=bridge_mode==MODE_TWO_PADS?2:1;
  sample_t s;
  if(!tud_mounted()||injected) {
    while(queue_try_remove(&reports,&s))memcpy(live_raw,s.raw,5);
    if(!tud_mounted()){pending=false;return;}
  }
  if(bridge_mode==MODE_DS3&&!ds3_send_allowed(now_ms())) {
    while(queue_try_remove(&reports,&s)){memcpy(live_raw,s.raw,5);if(!injected)memcpy(current_raw,s.raw,5);}
    if(injected){memcpy(current_raw,inject_raw,5);inject_dirty=false;}
    pending=false;initial_send=true;return;
  }
  if(!pending) {
    bool updated=false;
    if(injected&&inject_dirty){memcpy(current_raw,inject_raw,5);inject_dirty=false;updated=true;}
    else if(!injected&&queue_try_remove(&reports,&s)) {
      memcpy(live_raw,s.raw,5);memcpy(current_raw,s.raw,5);updated=true;
    }
    if(initial_send){memcpy(current_raw,injected?inject_raw:live_raw,5);updated=true;initial_send=false;}
    sent_mask=(uint8_t)((1u<<count)-1);
    for(unsigned i=0;i<count;i++) {
      if(updated||(bridge_mode==MODE_DS3&&now_ms()-last_sent[i]>=10)||
         (idle_rate[i]&&now_ms()-last_sent[i]>=(uint32_t)idle_rate[i]*4))sent_mask&=(uint8_t)~(1u<<i);
    }
    pending=sent_mask!=((1u<<count)-1);
  }
  if(!pending)return;
  for(unsigned i=0;i<count;i++) {
    if((sent_mask&(1u<<i))||!tud_hid_n_ready(i))continue;
    uint8_t data[48];unsigned len;
    if(bridge_mode==MODE_DS3) {
      ds3_packet(buzz_buttons(current_raw),data);
      if(tud_hid_n_report(i,1,data,48)){sent_mask|=(uint8_t)(1u<<i);last_sent[i]=now_ms();ds3_trace_in(true,true,now_ms());}
      continue;
    }
    if(bridge_mode==MODE_CLONE){memcpy(data,current_raw,5);len=5;}
    else {buzz_pad_report(current_raw,i,data);len=4;}
    if(tud_hid_n_report(i,0,data,len)){sent_mask|=(uint8_t)(1u<<i);last_sent[i]=now_ms();}
  }
  if(sent_mask==((1u<<count)-1))pending=false;
}
void tud_hid_report_complete_cb(uint8_t instance,const uint8_t *report,uint16_t len) {
  if(bridge_mode==MODE_DS3&&instance==0) {
    ds3_trace_in(false,true,now_ms());
    if(len==49&&report[0]==1)ds3_input_acked((report[4]&1)!=0);
  }
}
void tud_hid_report_failed_cb(uint8_t instance,hid_report_type_t type,const uint8_t *report,uint16_t len) {
  (void)report;(void)len;
  if(bridge_mode==MODE_DS3&&instance==0&&type==HID_REPORT_TYPE_INPUT)ds3_trace_in(false,false,now_ms());
}
static void action_task(void) {
  if(!action||!due(now_ms(),action_at))return;
  if(!host_core_ready)return;
  tud_disconnect();sleep_ms(50);
  if(bridge_mode==MODE_DS3&&!ds3_journal_save(bridge_debug?DS3_SAVE_COMMAND:DS3_SAVE_RECOVERY)) {
    action=ACTION_NONE;tud_connect();bridge_log("FAIL DS3 log save; other flash bank preserved\r\n");return;
  }
  if(action==ACTION_BOOTSEL)reset_usb_boot(0,0);
  if(action==ACTION_LOG_SAVE){action=ACTION_NONE;tud_connect();bridge_log("DS3LOG_SAVED explicit detached checkpoint\r\n");return;}
  if(action==ACTION_SAVE) {
    const void *old=(const void *)(XIP_BASE+SETTINGS_OFFSET);
    bridge_settings_page(settings_page,&pending_settings,ds3_jitter_enabled());
    int rc=PICO_OK;
    if(memcmp(old,settings_page,sizeof(settings_page))) {
      rc=flash_safe_execute(settings_write,NULL,1000);
    }
    if(rc!=PICO_OK||memcmp(old,settings_page,sizeof(settings_page))) {
      action=ACTION_NONE;tud_connect();bridge_log("FAIL settings write rc=%d; old active mode retained\r\n",rc);return;
    }
  }
  watchdog_reboot(0,0,10);while(true)tight_loop_contents();
}
int main(void) {
  set_sys_clock_khz(120000,true);settings_load();
  pico_unique_board_id_t unique;pico_get_unique_board_id(&unique);
  uint8_t address[6]={0x02,unique.id[3],unique.id[4],unique.id[5],unique.id[6],unique.id[7]};ds3_identity(address);
  uint32_t seed=time_us_32();for(unsigned i=0;i<sizeof(unique.id);i++)seed=(seed*33u)^unique.id[i];
  ds3_jitter_seed(seed,now_ms());
  ds3_trace_init();ds3_journal_init();ds3_session_reset(now_ms(),ds3_auto_ps);
  if(watchdog_hw->scratch[0]==TRIAL_MAGIC) {
    if(watchdog_hw->scratch[1]<3) {
      bridge_mode=(uint8_t)watchdog_hw->scratch[1];bridge_debug=false;
      watchdog_hw->scratch[1]=3;trial_deadline=now_ms()+30000;
    } else watchdog_hw->scratch[0]=0;
  }
  bridge_descriptors_init();
  local_lamps_init(&local_policy,local_enabled,local_first,local_hold_ms);
  queue_init(&reports,sizeof(sample_t),64);queue_init(&policy_reports,sizeof(sample_t),64);
  queue_init(&logs,sizeof(log_t),64);queue_init(&lamps,sizeof(lamp_t),16);
  gpio_init(PICO_DEFAULT_LED_PIN);gpio_set_dir(PICO_DEFAULT_LED_PIN,GPIO_OUT);
  tud_init(0);multicore_launch_core1(core1_main);
  bridge_log("BUZZ_BRIDGE modec2-jitter mode=%c debug=%u recovery=%u; help for commands\r\n",'A'+bridge_mode,bridge_debug,BUZZ_RECOVERY);
  while(true) {
    tud_task();device_reports_task();cdc_task();local_task();ds3_journal_task(host_core_ready);ds3_activation_task();action_task();
    if(bridge_mode==MODE_DS3)ds3_jitter_tick(now_ms());
    if(bridge_mode==MODE_DS3&&ds3_lease_active&&due(now_ms(),ds3_lease_until)) {
      ds3_lease_active=false;const uint8_t off[7]={0};queue_lamps(off);bridge_log("DS3_LAMP_TIMEOUT all off\r\n");
    }
    if(trial_deadline&&due(now_ms(),trial_deadline)){trial_deadline=0;action=ACTION_REBOOT;action_at=now_ms();}
    gpio_put(PICO_DEFAULT_LED_PIN,host_mounted?1:((now_ms()/500)&1));
  }
}
