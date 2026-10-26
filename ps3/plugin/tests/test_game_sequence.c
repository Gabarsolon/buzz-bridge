/* SPDX-License-Identifier: MIT. Independent transcription of BCES00347's public
 * call order, not game code. No game binary is required by this test. */
#include "cellusbd_adapter.h"
#include "import_hook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,lock_depth,native_calls,native_registers,native_ends;
static int32_t native_pipe_id=77,native_close_result=321;
static int32_t native_init_result;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static buzz_cell cell;
static buzz_hub hub;
static uint64_t now_us;
static unsigned presentation[2],input_submits;
static unsigned forbidden_virtual_lookups,open_traces,first_inputs;
static void trace(unsigned event,int32_t object,uint32_t result,uint32_t count)
{
 if(event==BZ_TRACE_INPUT_SUBMIT) { CHECK(count==8 && !result); ++input_submits; return; }
 if(event==BZ_TRACE_OPEN) { CHECK(result>BZ_CELL_PIPE_BASE && (count==0 || count==0x81)); ++open_traces; }
 if(event==BZ_TRACE_FIRST_INPUT) { CHECK(!result && count==5); ++first_inputs; }
 if(event==BZ_TRACE_LOOKUP_CALL || event==BZ_TRACE_LOOKUP || event==BZ_TRACE_PROBE_CALL ||
    event==BZ_TRACE_PROBE || event==BZ_TRACE_ATTACH_CALL || event==BZ_TRACE_ATTACH) {
  unsigned d=(unsigned)(object-BZ_CELL_DEVICE_BASE),expected=0;
  CHECK(d<2);
  switch(event) {
   case BZ_TRACE_LOOKUP_CALL: expected=0; CHECK(!lock_depth); break;
   case BZ_TRACE_LOOKUP: expected=1; CHECK(result==0); break;
   case BZ_TRACE_PROBE_CALL: expected=2; CHECK(!lock_depth); break;
   case BZ_TRACE_PROBE: expected=3; CHECK(!lock_depth && !result); break;
   case BZ_TRACE_ATTACH_CALL: expected=4; CHECK(!lock_depth); break;
   case BZ_TRACE_ATTACH: expected=5; CHECK(!lock_depth && !result); break;
  }
  CHECK(presentation[d]++==expected);
 }
}
static void enter(void *p) { (void)p; CHECK(lock_depth==0); lock_depth=1; }
static void leave(void *p) { (void)p; CHECK(lock_depth==1); lock_depth=0; }
static int32_t real_init(void) { CHECK(!lock_depth); ++native_calls; return native_init_result; }
static int32_t real_end(void) { CHECK(!lock_depth); ++native_ends; return 0; }
static int32_t real_reg(buzz_cell_ldd *p,uint16_t v,uint16_t a)
{ CHECK(!lock_depth && p && v && a); ++native_registers; return 0; }
static int32_t real_reg2(buzz_cell_ldd *p,uint16_t v,uint16_t a,uint16_t b)
{ CHECK(b>=a); return real_reg(p,v,a); }
static int32_t real_unreg(buzz_cell_ldd *p) { CHECK(!lock_depth && p); ++native_calls; return 0; }
static void *real_scan(int32_t d,void *a,uint8_t t)
{ CHECK(!lock_depth && d==99 && !a && t==1); ++native_calls; return &hub; }
static int32_t real_open(int32_t d,void *e)
{ CHECK(!lock_depth && d==99 && !e); ++native_calls; return native_pipe_id; }
static int32_t real_close(int32_t p) { CHECK(!lock_depth && p==native_pipe_id); ++native_calls; return native_close_result; }
static int32_t real_control(int32_t p,buzz_setup *s,void *b,buzz_cell_done cb,void *a)
{ CHECK(!lock_depth && p==native_pipe_id && s && !b && !cb && !a); ++native_calls; return 123; }
static int32_t real_interrupt(int32_t p,void *b,int32_t n,buzz_cell_done cb,void *a)
{ CHECK(!lock_depth && p==native_pipe_id && !b && n==-8 && !cb && !a); ++native_calls; return 456; }
static int32_t real_set(int32_t d,void *p) { CHECK(!lock_depth && d==99 && p==&hub); ++native_calls; return 654; }
static void *real_get(int32_t d) { CHECK(!lock_depth && d==99); ++native_calls; return &hub; }
static int32_t real_location(int32_t d,uint8_t *p)
{ CHECK(!lock_depth && p); if(d==99) { ++native_calls; return 987; }
 ++forbidden_virtual_lookups; return (int32_t)0x801100ffu; /* exact console 0.6.1 result */ }
static const buzz_cell_real real_api={real_init,real_end,real_reg,real_reg2,real_unreg,real_scan,
 real_open,real_close,real_control,real_interrupt,real_set,real_get,real_location};
typedef struct {
 int32_t dev,control,in;
 uint8_t report[16],product[128],last[5],location[7];
 unsigned ready,received,light_completions;
} game_device;
static game_device game[2];
static unsigned attached,detached,probes;
static void light_done(int32_t result,int32_t count,void *arg)
{ game_device *g=arg; CHECK(!lock_depth && !result && count==7); ++g->light_completions; }
static void input_done(int32_t result,int32_t count,void *arg)
{
 game_device *g=arg;
 CHECK(!lock_depth && result==0 && count==5);
 CHECK(!buzz_cell_can_unload(&cell));
 memcpy(g->last,g->report,5); ++g->received;
 CHECK(buzz_cell_interrupt(&cell,g->in,g->report,8,input_done,g)==0);
}
static void interface_done(int32_t result,int32_t count,void *arg)
{
 game_device *g=arg; uint8_t lights[7]={0}; buzz_setup setup={0x21,9,0x200,0,7};
 CHECK(!lock_depth && !result && count==0);
 CHECK(buzz_cell_control(&cell,g->control,&setup,lights,light_done,g)==0);
 ++g->ready;
 CHECK(buzz_cell_interrupt(&cell,g->in,g->report,8,input_done,g)==0);
}
static void config_done(int32_t result,int32_t count,void *arg)
{
 game_device *g=arg; buzz_setup setup={1,11,0,0,0};
 CHECK(!lock_depth && !result && !count);
 CHECK(buzz_cell_control(&cell,g->control,&setup,NULL,interface_done,g)==0);
}
static void product_done(int32_t result,int32_t count,void *arg)
{
 game_device *g=arg; buzz_setup setup={0,9,1,0,0};
 CHECK(!lock_depth && !result && count>2 && g->product[1]==3);
 CHECK(g->product[2]=='L' && g->product[3]==0);
 CHECK(buzz_cell_control(&cell,g->control,&setup,NULL,config_done,g)==0);
}
static int32_t probe(int32_t dev)
{
 const uint8_t *desc; CHECK(!lock_depth); ++probes;
 if(attached>=2) return -1;
 desc=buzz_cell_scan(&cell,dev,NULL,1); CHECK(desc && desc[8]==0x4c && desc[9]==5 && desc[10]==2 && !desc[11]);
 return 0;
}
static int32_t attach(int32_t dev)
{
 game_device *g=&game[(unsigned)(dev-BZ_CELL_DEVICE_BASE)];
 const uint8_t *config,*interface,*endpoint,*desc;
 buzz_setup setup={0x80,6,0x301,0x409,128};
 CHECK(!lock_depth); g->dev=dev;
 CHECK(buzz_cell_location(&cell,dev,g->location)==0);
 config=buzz_cell_scan(&cell,dev,NULL,2); CHECK(config && config[4]==1);
 interface=buzz_cell_scan(&cell,dev,(void *)config,4); CHECK(interface && !interface[2] && !interface[3]);
 endpoint=buzz_cell_scan(&cell,dev,(void *)interface,5); CHECK(endpoint && endpoint[2]==0x81 && endpoint[3]==3);
 CHECK(endpoint[4]==8 && !endpoint[5] && endpoint[6]==10);
 CHECK(!buzz_cell_scan(&cell,dev,(void *)endpoint,5)); /* no interrupt-OUT */
 CHECK(!buzz_cell_scan(&cell,dev,(void *)hub.slots,5));
 desc=buzz_cell_scan(&cell,dev,NULL,1); CHECK(desc && desc[15]==1);
 g->control=buzz_cell_open(&cell,dev,NULL); CHECK(g->control>0);
 g->in=buzz_cell_open(&cell,dev,(void *)endpoint); CHECK(g->in>0 && g->in!=g->control);
 CHECK(buzz_cell_set_private(&cell,dev,g)==0);
 CHECK(buzz_cell_control(&cell,g->control,&setup,g->product,product_done,g)==0);
 /* Deliberately destroy the stack request after submit: the adapter owns a copy. */
 memset(&setup,0xa5,sizeof(setup)); ++attached; return 0;
}
static int32_t detach(int32_t dev)
{
 game_device *g=buzz_cell_get_private(&cell,dev);
 CHECK(!lock_depth && g && g->dev==dev);
 CHECK(buzz_cell_close(&cell,g->control)==0); CHECK(buzz_cell_close(&cell,g->in)==0);
 ++detached; --attached; return 0;
}
static buzz_cell_ldd ldd[4]={{"buzzer0",probe,attach,detach},{"buzzer1",probe,attach,detach},
 {"buzzer2",probe,attach,detach},{"buzzer9",probe,attach,detach}};
static void advance(uint64_t delta) { now_us+=delta; buzz_cell_poll(&cell,now_us); }
static int attach_permit;
static int may_attach(void) {CHECK(!lock_depth);return attach_permit;}
static void test_game(void)
{
 buzz_config cfg; buzz_cell_sync sync={enter,leave,NULL}; unsigned i,d,before; uint8_t loc[7];
 buzz_defaults(&cfg); cfg.enabled=1; buzz_hub_init(&hub,&cfg);
 for(i=0;i<8;i++) CHECK(buzz_claim(&hub,BUZZ_PHONE,i+1,i+1)==(int)i+1);
 buzz_cell_construct(&cell,&hub,&real_api,&sync);
 cell.trace=trace;
 CHECK(buzz_cell_init(&cell)==0); /* this branch owns Init and later calls End */
 CHECK(buzz_cell_register(&cell,&ldd[0],0x54c,0x1000,0x1040,2)==0);
 CHECK(buzz_cell_register(&cell,&ldd[1],0x54c,1,0x41,2)==0);
 CHECK(buzz_cell_register(&cell,&ldd[2],0x54c,2,0x42,2)==0);
 CHECK(buzz_cell_register(&cell,&ldd[3],0x46d,0xc220,0xc220,1)==0);
 CHECK(native_registers==4 && attached==0);
 advance(199999); CHECK(attached==0); advance(1); CHECK(attached==1 && probes==1 && !cell.owner[1]);
 for(i=0;i<4;i++) advance(100);
 advance(6000);CHECK(first_inputs==1 && !cell.owner[1]);advance(1);
 CHECK(attached==2 && probes==2 && presentation[0]==6 && presentation[1]==6);
 for(i=0;i<4;i++) advance(100);
 CHECK(game[0].ready==1 && game[1].ready==1 && game[0].light_completions==1);
 CHECK(input_submits==2);
 CHECK(open_traces==4 && buzz_cell_ready(&cell) && !forbidden_virtual_lookups);
 CHECK(memcmp(game[0].location,game[1].location,7));
 advance(6000);
 CHECK(first_inputs==2);
 CHECK(!memcmp(game[0].last,"\x7f\x7f\0\0\xf0",5));
 /* Independent game-side extraction of b2/b3/b4; test both dongles and every bit. */
 for(i=0;i<40;i++) {
  unsigned handset=i/5,button=i%5; uint32_t expected;
  for(d=0;d<8;d++) hub.slots[d].buttons=0;
  hub.slots[handset].buttons=(uint8_t)(1u<<button);
  advance(6000); advance(6000);
  for(d=0;d<2;d++) {
   uint32_t actual=(uint32_t)game[d].last[2]|(uint32_t)game[d].last[3]<<8|(uint32_t)(game[d].last[4]&15)<<16;
   expected=handset/4==d ? 1u<<((handset%4)*5+button) : 0;
   CHECK(actual==expected);
  }
 }
 for(d=0;d<2;d++) {
  buzz_setup setup={0x21,9,0x200,0,7}; uint8_t lights[7]={0,0xff,0,0xff,0,0,0};
  CHECK(buzz_cell_control(&cell,game[d].control,&setup,lights,light_done,&game[d])==0);
  memset(lights,0,sizeof(lights)); advance(100);
  CHECK(hub.slots[d*4].light==1 && hub.slots[d*4+1].light==0 && hub.slots[d*4+2].light==1);
 }
 /* Unknown real device/pipe calls forward verbatim, including native errors. */
 CHECK(buzz_cell_scan(&cell,99,NULL,1)==&hub); CHECK(buzz_cell_open(&cell,99,NULL)==77);
 CHECK(buzz_cell_close(&cell,77)==321);
 { buzz_setup s={0,9,1,0,0}; CHECK(buzz_cell_control(&cell,77,&s,NULL,NULL,NULL)==123); }
 CHECK(buzz_cell_interrupt(&cell,77,NULL,-8,NULL,NULL)==456);
 CHECK(buzz_cell_set_private(&cell,99,&hub)==654 && buzz_cell_get_private(&cell,99)==&hub);
 CHECK(buzz_cell_location(&cell,99,loc)==987);
 CHECK(native_calls==9);
 before=game[0].received;
 { int32_t old=game[0].in; void *ep=buzz_cell_scan(&cell,game[0].dev,NULL,5);
  CHECK(buzz_cell_close(&cell,old)==0); advance(10000); CHECK(game[0].received==before);
  CHECK(buzz_cell_interrupt(&cell,old,game[0].report,8,input_done,&game[0])==BZ_CELL_PIPE);
  game[0].in=buzz_cell_open(&cell,game[0].dev,ep); CHECK(game[0].in!=old);
 }
 for(i=0;i<4;i++) CHECK(buzz_cell_unregister(&cell,&ldd[i])==0);
 CHECK(detached==2 && attached==0 && buzz_cell_can_unload(&cell));
 advance(100000); CHECK(game[0].received==before);
 CHECK(buzz_cell_end(&cell)==0 && native_ends==1);
 /* Adopting an existing LDD must not re-register it with native libusbd. */
 memset(game,0,sizeof(game)); buzz_cell_construct(&cell,&hub,&real_api,&sync);
 native_init_result=(int32_t)0x80110002u;
 CHECK(buzz_cell_init(&cell)==native_init_result); /* other branch tolerates ALREADY_INITIALIZED */
 native_pipe_id=BZ_CELL_PIPE_BASE+1;
 CHECK(buzz_cell_open(&cell,99,NULL)==native_pipe_id);
 CHECK(buzz_cell_interrupt(&cell,native_pipe_id,NULL,-8,NULL,NULL)==456);
 CHECK(buzz_cell_adopt(&cell,&ldd[1],0x54c,1,0x41)==0); advance(200000);
 for(i=0;i<4;i++)advance(100);advance(6000);advance(1);
 CHECK(native_registers==4 && attached==2 && game[0].control!=native_pipe_id);
 /* A later real allocation that collides with a live virtual pipe fails closed. */
 native_pipe_id=game[0].control; native_close_result=0;
 CHECK(buzz_cell_open(&cell,99,NULL)==BZ_CELL_NOMEM && cell.namespace_fault);
 CHECK(buzz_cell_unregister(&cell,&ldd[1])==0);
}
static void test_console_061_replay(void)
{
 buzz_config cfg; buzz_cell_sync sync={enter,leave,NULL}; unsigned calls=native_calls,regs=native_registers;
 uint8_t loc[7]; int32_t dev=BZ_CELL_DEVICE_BASE;
 memset(game,0,sizeof(game)); memset(presentation,0,sizeof(presentation));
 input_submits=open_traces=first_inputs=probes=0; attached=0; now_us=0;
 buzz_defaults(&cfg); cfg.enabled=1; cfg.pad_slot[0][0]=1; buzz_hub_init(&hub,&cfg);
 buzz_cell_construct(&cell,&hub,&real_api,&sync); cell.trace=trace;
 CHECK(!buzz_cell_ready(&cell));
 CHECK(buzz_cell_location(&cell,dev,loc)==BZ_CELL_DEVICE); /* local absent, no real call */
 CHECK(!buzz_cell_scan(&cell,dev,NULL,1));
 CHECK(buzz_cell_open(&cell,dev,NULL)==BZ_CELL_DEVICE);
 CHECK(buzz_cell_adopt(&cell,&ldd[2],0x54c,2,0x42)==0);
 cell.present_allowed=0;advance(1000000);CHECK(!attached && !cell.owner[0] && !cell.owner[1]);
 cell.present_allowed=1;cell.may_attach=may_attach;attach_permit=0;
 advance(1000000);CHECK(!attached && !cell.owner[0] && !cell.owner[1]);
 attach_permit=1;cell.attach_after_us=now_us+200000;
 advance(199999); CHECK(!attached && !buzz_cell_ready(&cell)); advance(1);
 CHECK(attached==1 && probes==1 && !cell.owner[1] && !buzz_cell_ready(&cell));
 for(unsigned i=0;i<4;i++)advance(100);advance(6000);advance(1);
 CHECK(attached==2 && presentation[0]==6 && presentation[1]==6 && open_traces==4);
 for(unsigned i=0;i<4;i++) advance(2000);
 CHECK(game[0].ready && game[1].ready && buzz_cell_ready(&cell));
 advance(6000);
 CHECK(first_inputs==2 && input_submits==2);
 /* Replay pad masks observed on console: square=32768, triangle=4096, release. */
 buzz_pad_update(&hub,0,1,1,32768); advance(6000); advance(6000);
 CHECK(game[0].last[2]==8 && game[1].last[2]==0);
 buzz_pad_update(&hub,0,1,1,4096); advance(6000); advance(6000);
 CHECK(game[0].last[2]==16);
 buzz_pad_update(&hub,0,1,1,0); advance(6000); advance(6000);
 CHECK(!memcmp(game[0].last,"\x7f\x7f\0\0\xf0",5));
 CHECK(native_calls==calls && native_registers==regs && !forbidden_virtual_lookups);
 CHECK(buzz_cell_unregister(&cell,&ldd[2])==0 && !buzz_cell_ready(&cell));
 CHECK(!cell.owner[0] && !cell.owner[1] && !cell.usb.attached);
 puts("Console 0.6.1 replay PASS: adopt -> local lookup -> probe/attach x2; no real virtual-ID export; deferred setup/IN; pad 32768/4096/release; detach clears readiness.");
}
static uint8_t memory[4096]; static unsigned writes,fail_write;
static void put32(unsigned a,uint32_t v) { memory[a]=(uint8_t)(v>>24); memory[a+1]=(uint8_t)(v>>16); memory[a+2]=(uint8_t)(v>>8); memory[a+3]=(uint8_t)v; }
static uint32_t get32(unsigned a) { return (uint32_t)memory[a]<<24|(uint32_t)memory[a+1]<<16|(uint32_t)memory[a+2]<<8|memory[a+3]; }
static int read_mem(void *a,uint32_t p,void *b,size_t n)
{ (void)a; if(p>sizeof(memory) || n>sizeof(memory)-p) return 0; memcpy(b,memory+p,n); return 1; }
static int replace_mem(void *a,uint32_t p,uint32_t old,uint32_t next)
{ (void)a; ++writes; if(writes==fail_write || p>sizeof(memory)-4 || get32(p)!=old) return 0; put32(p,next); return 1; }
static int valid_opd(void *a,uint32_t p) { (void)a; return p>=0x800 && p<0xc00 && !(p&7); }
static void test_imports(void)
{
 buzz_import_plan plan={0}; buzz_import_memory mem={NULL,read_mem,replace_mem,valid_opd}; unsigned i;
 memset(memory,0,sizeof(memory)); memory[0x100]=44; memory[0x107]=13;
 put32(0x110,0x200); memcpy(memory+0x200,"cellUsbd",9); put32(0x114,0x300); put32(0x118,0x400);
 memory[0x12c]=44; memory[0x133]=1; put32(0x13c,0x220); memcpy(memory+0x220,"sys_io",7);
 put32(0x140,0x334); put32(0x144,0x434);
 plan.count=14;
 for(i=0;i<14;i++) { uint32_t nid=i==13?0x8b72cda1u:0x1000+i; put32(0x300+i*4,nid); put32(0x400+i*4,0x800+i*8); plan.patch[i].nid=nid; plan.patch[i].replacement=0x900+i*8; }
 CHECK(buzz_import_discover(&plan,&mem,0x100,0x158)); CHECK(writes==0);
 fail_write=3; CHECK(!buzz_import_install(&plan,&mem));
 for(i=0;i<14;i++) CHECK(get32(0x400+i*4)==0x800+i*8 && !plan.patch[i].installed);
 fail_write=0; CHECK(buzz_import_install(&plan,&mem));
 for(i=0;i<14;i++) CHECK(get32(0x400+i*4)==0x900+i*8);
 put32(0x400,0xa00); CHECK(!buzz_import_restore(&plan,&mem));
 CHECK(plan.patch[0].installed && get32(0x400)==0xa00); /* leave another hook alone */
 put32(0x400,0x900); CHECK(buzz_import_restore(&plan,&mem));
 memory[0x107]=12; CHECK(!buzz_import_discover(&plan,&mem,0x100,0x158));
 CHECK(!buzz_import_discover(&plan,&mem,0x100,0x157));
}
int main(void)
{ test_game(); test_console_061_replay(); test_imports(); CHECK(!lock_depth); printf("Game sequence/import adapter: %u checks passed; 2 dongles, 40 button bits, callback rearm, real pass-through, cancellation, late adoption, rollback.\n",checks); return 0; }
