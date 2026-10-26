/* SPDX-License-Identifier: MIT */
#include "cellusbd_adapter.h"
#include <string.h>
/* API/error/callback evidence for each virtual branch: USB_CONTRACT_07.md.
 * RPCS3 cellUsbd HLE bodies are stubs; do not infer Sony ABI from them. */
static void lock(buzz_cell *c) { if(c->sync.lock) c->sync.lock(c->sync.arg); }
static void unlock(buzz_cell *c) { if(c->sync.unlock) c->sync.unlock(c->sync.arg); }
static int device_index(int32_t id)
{ return id>=BZ_CELL_DEVICE_BASE && id<BZ_CELL_DEVICE_BASE+2 ? id-BZ_CELL_DEVICE_BASE : -1; }
static int own_pipe(int32_t id) { return id>=BZ_CELL_PIPE_BASE && id<BZ_CELL_PIPE_BASE+0x10000; }
static int real_reserved(buzz_cell *c,int32_t id)
{ unsigned i; for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(c->real_reserved_pipes[i]==id) return 1; return 0; }
static int virtual_pipe(buzz_cell *c,int32_t id)
{ int result; if(!own_pipe(id)) return 0; lock(c); result=!real_reserved(c,id); unlock(c); return result; }
static buzz_cell_pipe *pipe_find(buzz_cell *c,int32_t id)
{ unsigned i; for(i=0;i<4;i++) if(c->pipes[i].id==id) return &c->pipes[i]; return NULL; }
void buzz_cell_construct(buzz_cell *c,buzz_hub *h,const buzz_cell_real *r,const buzz_cell_sync *s)
{
 memset(c,0,sizeof(*c)); c->real=*r; if(s) c->sync=*s;
 memcpy(c->descriptors,buzz_device_descriptor,18);
 memcpy(c->descriptors+18,buzz_config_descriptor,34);
 buzz_usb_init(&c->usb,h); c->next_pipe=1; c->present_allowed=1;
}
int32_t buzz_cell_init(buzz_cell *c) { return c->real.init(); }
static int32_t remember(buzz_cell *c,buzz_cell_ldd *ldd,uint16_t vid,uint16_t lo,uint16_t hi)
{
 unsigned i;
 if(!ldd || !ldd->name || !ldd->probe || !ldd->attach || !ldd->detach || lo>hi) return BZ_CELL_INVALID;
 /* Only the proven wired identity; wireless registrations still pass through. */
 if(vid!=0x054c || lo>2 || hi<2) return 0;
 lock(c);
 for(i=0;i<4;i++) if(c->registrations[i].ldd==ldd) { unlock(c); return 0; }
 for(i=0;i<4;i++) if(!c->registrations[i].ldd) break;
 if(i==4 || c->stopping) { unlock(c); return BZ_CELL_BUSY; }
 c->registrations[i]=(buzz_cell_registration){ldd,vid,lo,hi,0};
 c->attach_after_us=c->now_us+200000; unlock(c); return 0;
}
int32_t buzz_cell_adopt(buzz_cell *c,buzz_cell_ldd *ldd,uint16_t vid,uint16_t lo,uint16_t hi)
{ return remember(c,ldd,vid,lo,hi); }
int32_t buzz_cell_register(buzz_cell *c,buzz_cell_ldd *ldd,uint16_t vid,uint16_t lo,uint16_t hi,unsigned kind)
{
 int32_t result;
 if(kind!=1 && kind!=2) return BZ_CELL_INVALID;
 result=kind==1 ? c->real.register_extra(ldd,vid,lo) : c->real.register_extra2(ldd,vid,lo,hi);
 /* A native success remains success even if virtualization cannot be allocated. */
 if(!result) (void)remember(c,ldd,vid,lo,kind==1?lo:hi);
 return result;
}
static void cancel_pipe(buzz_cell *c,int32_t id)
{
 unsigned i,j;
 for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(c->pending[i].transfer && c->pending[i].pipe==id) {
  uint32_t t=c->pending[i].transfer;
  for(j=0;j<BUZZ_USB_TRANSFERS;j++) if(c->usb.transfers[j].id==t) c->usb.transfers[j].used=0;
  memset(&c->pending[i],0,sizeof(c->pending[i]));
 }
}
static void clear_device(buzz_cell *c,unsigned d)
{
 unsigned i;
 for(i=0;i<4;i++) if(c->pipes[i].id && (c->pipes[i].model_pipe-1)/2==d) {
  cancel_pipe(c,c->pipes[i].id); memset(&c->pipes[i],0,sizeof(c->pipes[i]));
 }
 c->private_data[d]=NULL; c->owner[d]=NULL; c->usb.configured[d]=0; c->input_started&=~(1u<<d);
 c->input_completed&=~(1u<<d); c->attached_mask&=~(1u<<d);
 c->usb.attached=c->owner[0]!=NULL || c->owner[1]!=NULL;
 for(i=0;i<4;i++) c->usb.hub->slots[d*4+i].light=0;
 ++c->usb.hub->generation;
}
int32_t buzz_cell_unregister(buzz_cell *c,buzz_cell_ldd *ldd)
{
 unsigned i; unsigned detach_mask=0;
 lock(c);
 if(c->callbacks || c->stopping) { unlock(c); return BZ_CELL_BUSY; }
 c->stopping=1;
 for(i=0;i<2;i++) if(c->owner[i]==ldd) detach_mask|=1u<<i;
 for(i=0;i<4;i++) if(c->registrations[i].ldd==ldd) memset(&c->registrations[i],0,sizeof(c->registrations[i]));
 unlock(c);
 for(i=0;i<2;i++) if(detach_mask&(1u<<i)) (void)ldd->detach(BZ_CELL_DEVICE_BASE+(int32_t)i);
 lock(c);
 for(i=0;i<2;i++) if(detach_mask&(1u<<i)) clear_device(c,i);
 if(c->trace && detach_mask) c->trace(BZ_TRACE_DETACH,0,detach_mask,0);
 c->stopping=0; unlock(c);
 return c->real.unregister_extra(ldd);
}
int32_t buzz_cell_end(buzz_cell *c)
{
 unsigned i;
 /* Do not forward End while a virtual callback is executing. */
 lock(c); if(c->callbacks || c->stopping) { unlock(c); return BZ_CELL_BUSY; } unlock(c);
 for(i=0;i<4;i++) {
  buzz_cell_ldd *ldd; lock(c); ldd=c->registrations[i].ldd; unlock(c);
  if(ldd) { int32_t r=buzz_cell_unregister(c,ldd); if(r) return r; }
 }
 lock(c); buzz_usb_reset(&c->usb); unlock(c); return c->real.end();
}
void *buzz_cell_scan(buzz_cell *c,int32_t dev,void *after,uint8_t type)
{
 int d=device_index(dev); unsigned pos=0; int found=after==NULL; void *result=NULL;
 if(d<0) return c->real.scan(dev,after,type);
 lock(c);
 if(c->owner[d]) while(pos<sizeof(c->descriptors)) {
  uint8_t *desc=c->descriptors+pos;
  if(found && desc[1]==type) { result=desc; break; }
  if(after==desc) found=1;
  if(desc[0]<2 || desc[0]>sizeof(c->descriptors)-pos) break;
  pos+=desc[0];
 }
 if(c->trace)c->trace(BZ_TRACE_SCAN,dev,result?1u:0u,type);
 unlock(c); return result;
}
int32_t buzz_cell_open(buzz_cell *c,int32_t dev,void *endpoint)
{
 int d=device_index(dev); unsigned i,model; int32_t result;
 if(d<0) {
  result=c->real.open(dev,endpoint);
  if(own_pipe(result)) {
   lock(c);
   for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(!c->real_reserved_pipes[i] || c->real_reserved_pipes[i]==result) break;
   if(pipe_find(c,result) || i==BUZZ_USB_TRANSFERS) {
    c->namespace_fault=1; unlock(c); (void)c->real.close(result); return BZ_CELL_NOMEM;
   }
   c->real_reserved_pipes[i]=result; unlock(c);
  }
  return result;
 }
 lock(c);
 if(!c->owner[d] || c->stopping) { unlock(c); return BZ_CELL_DEVICE; }
 if(endpoint && endpoint!=c->descriptors+45) { unlock(c); return BZ_CELL_INVALID; }
 model=(unsigned)d*2+(endpoint?2u:1u);
 for(i=0;i<4;i++) if(c->pipes[i].id && c->pipes[i].model_pipe==model) { unlock(c); return BZ_CELL_PIPE_BUSY; }
 for(i=0;i<4;i++) if(!c->pipes[i].id) break;
 while(c->next_pipe<0x10000 && real_reserved(c,BZ_CELL_PIPE_BASE+(int32_t)c->next_pipe)) ++c->next_pipe;
 if(i==4 || c->next_pipe>=0x10000 || c->namespace_fault) { unlock(c); return BZ_CELL_NOMEM; }
 result=BZ_CELL_PIPE_BASE+(int32_t)c->next_pipe++;
 c->pipes[i]=(buzz_cell_pipe){result,model};
 if(c->trace) c->trace(BZ_TRACE_OPEN,dev,(uint32_t)result,endpoint?0x81:0);
 unlock(c); return result;
}
int32_t buzz_cell_close(buzz_cell *c,int32_t pipe)
{
 buzz_cell_pipe *p;
 if(!virtual_pipe(c,pipe)) {
  int32_t result=c->real.close(pipe);
  if(!result && own_pipe(pipe)) { unsigned i; lock(c); for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(c->real_reserved_pipes[i]==pipe) c->real_reserved_pipes[i]=0; unlock(c); }
  return result;
 }
 lock(c); p=pipe_find(c,pipe);
 if(!p) { unlock(c); return BZ_CELL_PIPE; }
 if(c->callbacks && !c->stopping) { unlock(c); return BZ_CELL_PIPE_BUSY; }
 if(p->model_pipe%2==0) {
  unsigned bit=1u<<((p->model_pipe-1)/2); c->input_started&=~bit; c->input_completed&=~bit;
 }
 cancel_pipe(c,pipe); memset(p,0,sizeof(*p)); unlock(c); return 0;
}
static int32_t submit(buzz_cell *c,int32_t pipe,const buzz_setup *s,void *buffer,size_t length,buzz_cell_done done,void *arg,int control)
{
 unsigned i; int t; buzz_cell_pipe *p;
 if(!done || (length && !buffer)) return BZ_CELL_INVALID;
 lock(c); p=pipe_find(c,pipe);
 if(!p || c->stopping) { unlock(c); return BZ_CELL_PIPE; }
 if((p->model_pipe%2==1)!=control) { unlock(c); return BZ_CELL_INVALID; }
 for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(!c->pending[i].transfer) break;
 if(i==BUZZ_USB_TRANSFERS) { unlock(c); return BZ_CELL_NOMEM; }
 t=buzz_usb_submit(&c->usb,p->model_pipe,s,buffer,length,c->now_us);
 if(t<0) { unlock(c); return t==-2?BZ_CELL_NOMEM:BZ_CELL_INVALID; }
 c->pending[i]=(buzz_cell_pending){(uint32_t)t,pipe,buffer,arg,done,(unsigned)(!control || (s->type&0x80))};
 if(control && c->trace) c->trace(BZ_TRACE_CONTROL_SUBMIT,pipe,
  (uint32_t)s->type<<24|(uint32_t)s->request<<16|s->value,s->length);
 if(!control) {
  unsigned bit=1u<<((p->model_pipe-1)/2);
  if(!(c->input_started&bit) && c->trace) c->trace(BZ_TRACE_INPUT_SUBMIT,pipe,0,(uint32_t)length);
  c->input_started|=bit;
 }
 unlock(c); return 0; /* Public cellUsbd returns submission status, NOT a transfer ID. */
}
int32_t buzz_cell_control(buzz_cell *c,int32_t p,buzz_setup *s,void *buf,buzz_cell_done done,void *arg)
{
 if(!virtual_pipe(c,p)) return c->real.control(p,s,buf,done,arg);
 if(!s) return BZ_CELL_INVALID;
 return submit(c,p,s,buf,s->length,done,arg,1);
}
int32_t buzz_cell_interrupt(buzz_cell *c,int32_t p,void *buf,int32_t len,buzz_cell_done done,void *arg)
{
 if(!virtual_pipe(c,p)) return c->real.interrupt(p,buf,len,done,arg);
 if(len<0) return BZ_CELL_INVALID;
 return submit(c,p,NULL,buf,(size_t)len,done,arg,0);
}
int32_t buzz_cell_set_private(buzz_cell *c,int32_t dev,void *data)
{
 int d=device_index(dev);
 if(d<0) return c->real.set_private(dev,data);
 lock(c); if(!c->owner[d]) { unlock(c); return BZ_CELL_DEVICE; }
 c->private_data[d]=data; unlock(c); return 0;
}
void *buzz_cell_get_private(buzz_cell *c,int32_t dev)
{
 int d=device_index(dev); void *p;
 if(d<0) return c->real.get_private(dev);
 lock(c); p=c->private_data[d]; unlock(c); return p;
}
int32_t buzz_cell_location(buzz_cell *c,int32_t dev,uint8_t *location)
{
 int d=device_index(dev); static const uint8_t base[7]={0xfe,0x42,0x5a,0,0,0,0};
 if(d<0) return c->real.location(dev,location);
 if(!location) return BZ_CELL_INVALID;
 lock(c); if(!c->owner[d]) { unlock(c); return BZ_CELL_DEVICE; }
 memcpy(location,base,7); location[4]=(uint8_t)(d+1);
 if(c->trace)c->trace(BZ_TRACE_LOCATION,dev,0,0);
 unlock(c); return 0;
}
void buzz_cell_poll(buzz_cell *c,uint64_t now)
{
 unsigned d,r,i; buzz_usb_event event;
 if(c->paused && c->paused()) return;
 /* Never run a foreign callback on an importing game thread. The sole poll
  * caller owns callback dispatch; preflight must not hold our adapter lock. */
 lock(c);
 int permitted=c->present_allowed && c->attached_mask!=((1u<<c->usb.hub->config.dongles)-1u);
 unlock(c);
 if(permitted && c->may_attach)permitted=c->may_attach();
 if(c->progress) c->progress("usb-lock");
 lock(c); c->now_us=now;
 if(c->stopping) { unlock(c); return; }
 /* Virtual IDs belong exclusively to this adapter. Never ask native libusbd
  * to validate one: RPCS3 sys_usbd.cpp:1689 rejects unknown device handles. */
 if(permitted && c->present_allowed && c->usb.hub->config.enabled && now>=c->attach_after_us) for(d=0;d<c->usb.hub->config.dongles;d++) {
  if(c->owner[d]) continue;
  /* Finish a real deferred IN cycle before reserving the next dongle. */
  if(d && !(c->input_completed&(1u<<(d-1))))continue;
  for(r=0;r<4;r++) {
   buzz_cell_registration *reg=&c->registrations[r]; buzz_cell_ldd *ldd=reg->ldd; int32_t result;
   if(!ldd || (reg->attempted&(1u<<d))) continue;
   reg->attempted|=1u<<d;
   /* Publish the local device before lookup/probe so descriptor/location hooks
    * can answer it. This is a reservation, not successful attachment. */
   c->owner[d]=ldd; c->usb.attached=1;
   ++c->callbacks; unlock(c);
   if(c->progress) c->progress("lookup-call");
   if(c->trace) c->trace(BZ_TRACE_LOOKUP_CALL,BZ_CELL_DEVICE_BASE+(int32_t)d,0,0);
   { uint8_t location[7]; result=buzz_cell_location(c,BZ_CELL_DEVICE_BASE+(int32_t)d,location); }
   if(c->progress) c->progress("lookup-return");
   lock(c);
   if(c->paused && c->paused()) { --c->callbacks; unlock(c); return; }
   if(c->trace) c->trace(BZ_TRACE_LOOKUP,BZ_CELL_DEVICE_BASE+(int32_t)d,(uint32_t)result,0);
   if(result) { clear_device(c,d); --c->callbacks; continue; }
   unlock(c);
   if(c->progress) c->progress("probe-call");
   if(c->trace) c->trace(BZ_TRACE_PROBE_CALL,BZ_CELL_DEVICE_BASE+(int32_t)d,0,0);
   result=ldd->probe(BZ_CELL_DEVICE_BASE+(int32_t)d);
   if(c->progress) c->progress("probe-return");
   if(c->trace) c->trace(BZ_TRACE_PROBE,BZ_CELL_DEVICE_BASE+(int32_t)d,(uint32_t)result,0);
   if(c->paused && c->paused()) { lock(c); --c->callbacks; unlock(c); return; }
   if(!result) {
    if(c->progress) c->progress("attach-call");
    if(c->trace) c->trace(BZ_TRACE_ATTACH_CALL,BZ_CELL_DEVICE_BASE+(int32_t)d,0,0);
    result=ldd->attach(BZ_CELL_DEVICE_BASE+(int32_t)d);
    if(c->progress) c->progress("attach-return");
    if(c->trace) c->trace(BZ_TRACE_ATTACH,BZ_CELL_DEVICE_BASE+(int32_t)d,(uint32_t)result,0);
   }
   lock(c); --c->callbacks;
   if(c->paused && c->paused()) { unlock(c); return; }
   if(result) clear_device(c,d);
   else c->attached_mask|=1u<<d;
   if(c->owner[d]) break;
  }
 }
 buzz_usb_tick(&c->usb,now);
 /* Drain the current bounded queue. A callback can immediately submit another
  * request; its future deadline prevents recursive completion in this pump. */
 for(unsigned drained=0;drained<BUZZ_USB_EVENTS && !(c->paused && c->paused()) && buzz_usb_pop(&c->usb,&event);drained++) {
  if(event.type!=3) continue;
  for(i=0;i<BUZZ_USB_TRANSFERS;i++) if(c->pending[i].transfer==(uint32_t)event.arg2) break;
  if(i<BUZZ_USB_TRANSFERS) {
   const buzz_usb_transfer *t=buzz_usb_status(&c->usb,c->pending[i].transfer);
   buzz_cell_pending pending=c->pending[i]; uint32_t result,count;
   if(!t) continue;
   result=t->result; count=t->count;
   if(t->pipe%2==0) {
    unsigned bit=1u<<((t->pipe-1)/2);
    if(!result && count==5) {
     if(!(c->input_completed&bit) && c->trace) c->trace(BZ_TRACE_FIRST_INPUT,pending.pipe,result,count);
     c->input_completed|=bit;
    }
   }
   if(c->trace) c->trace(t->pipe%2==0?BZ_TRACE_INPUT:BZ_TRACE_CONTROL,
                         pending.pipe,result,count);
   /* Only IN transfers have copied data; OUT buffers remain caller-owned. */
   if(pending.input && count) memcpy(pending.buffer,t->data,count);
   memset(&c->pending[i],0,sizeof(c->pending[i])); buzz_usb_reap(&c->usb,pending.transfer);
   ++c->callbacks; unlock(c);
   if(c->progress) c->progress(t->pipe%2==0?"input-callback":"control-callback");
   pending.done((int32_t)result,(int32_t)count,pending.arg);
   if(c->progress) c->progress("callback-return");
   lock(c); --c->callbacks;
  }
 }
 unlock(c);
}
int buzz_cell_can_unload(buzz_cell *c)
{
 unsigned i; int result=1; lock(c);
 if(c->callbacks || c->stopping) result=0;
 for(i=0;i<4;i++) if(c->registrations[i].ldd || c->pipes[i].id) result=0;
 unlock(c); return result;
}
int buzz_cell_ready(buzz_cell *c)
{
 unsigned mask; int ready;
 if(c->paused && c->paused())return 0;
 lock(c); mask=(1u<<c->usb.hub->config.dongles)-1u;
 ready=c->usb.hub->config.enabled && !c->stopping && (c->attached_mask&mask)==mask
  && (c->input_started&mask)==mask && c->usb.configured[0]
  && (c->usb.hub->config.dongles==1 || c->usb.configured[1]);
 unlock(c); return ready;
}
