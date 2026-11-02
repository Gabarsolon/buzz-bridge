/* Persistent output lives ONLY in an allocator-owned, range-checked arena.
 * The HID manager's own critical section protects the borrowed interface.
 * No interface pointer is dereferenced after releasing that mutex. The native
 * 5fb48 completion adapter checks live list membership before calling us.
 * A timed-out transfer is quarantined forever: neither buffer nor context is
 * reused or freed while the native USB layer could still own it. */
extern u64 native_alloc(u64,u64,u64);
extern long native_lock(const volatile u64*,u64);
extern long native_unlock(u64);
extern long native_set_report(u64,u64,u64,const volatile u8*,u64,volatile void*);
extern const u64 output_callback_descriptor[];
#define LIGHT_MAGIC 0x425a5334U
#define LIGHT_PERIOD 1276800ULL /* 16 ms */
#define LIGHT_TIMEOUT 79800000ULL
struct LampRequest { u32 seq,gen; u8 data[7],pad; };
struct Probe { u32 seen,meta,handle,ids; u64 provider; int length; u32 reserved; };
struct Lamps {
 u32 magic, generation, accepted, completed;
 int result; u32 tail,head,inflight;
 u64 started,last_submit;
 u32 flight_seq,flight_gen,done; int usb_result;
 u32 usb_length,poison,pattern,last_valid;
 u8 payload[8],last[8];
 u64 context[3];
 union {struct LampRequest q[8]; struct Probe fallback;}; /* C5 LIGHT_MODE=0 only */
 u32 reset,off_pending,guest_seen,restart;
 u32 desired; /* guest lamp request: bit4 valid, bits0..3 mask; written by the SIO2 poll, read by the HID pump */
 u32 diagnostic_guest; /* LIGHT_MODE 3: the pump busy flag (one pump at a time) */
 u32 capture_stage; /* sticky independent H/M/V/A/U bits, atomic OR */
 u32 capture_detail; /* K/B/L/N/T/F/P/Q/J, atomic OR */
 u32 mapper_seen; /* W any mapper entry; F exact Buzz mapping record */
 u32 host_seen; /* Global K/J/B/G/P/changed/guard/dispatch histories. */
 struct Probe probe[7];
};
_Static_assert(sizeof(struct Lamps)<=512,"owned arena");
static __attribute__((noinline)) volatile struct Lamps* lamps(void){
 u64 a=S->reverse_arena;
 if(!arena_rw(a))return 0;
 volatile struct Lamps*l=(volatile struct Lamps*)a;
 return l->magic==LIGHT_MAGIC?l:0;
}
static void capture_mark(u32 stage){
 volatile struct Lamps*l=lamps();if(l)__atomic_fetch_or(&l->capture_stage,stage,__ATOMIC_RELAXED);
}
static void capture_detail(u32 bits){
 volatile struct Lamps*l=lamps();if(l)__atomic_fetch_or(&l->capture_detail,bits,__ATOMIC_RELAXED);
}
static void capture_mapper(u32 bits){
 volatile struct Lamps*l=lamps();if(l)__atomic_fetch_or(&l->mapper_seen,bits,__ATOMIC_RELAXED);
}
static void capture_host(u32 bits){
 volatile struct Lamps*l=lamps();if(l)__atomic_fetch_or(&l->host_seen,bits,__ATOMIC_RELAXED);
}
static int lights_ready(void){return lamps()!=0;}
static __attribute__((noinline,used)) void lights_init(void){
 u32 empty=0;
 if(S->reverse_arena || !__atomic_compare_exchange_n((volatile u32*)0x930f5cULL,&empty,3,1,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return;
 /* b9848 lazily initializes the allocator from the already published pool.
  * Never call it while that native map is absent (its original ABI asserts). */
 u64 base=*(const volatile u64*)0x934980ULL;
 if((base&~0xf800000ULL)!=0x80000000ULL ||
    *(const volatile u64*)0x24565b8ULL!=base){S->reverse_arena=1;return;}
 u64 a=native_alloc(ARENA_BYTES,3,0);
 if(!arena_rw(a)){S->reverse_arena=1;return;}
 /* First 512 bytes retain Lamps ABI. The dedicated 800-byte scratch follows,
  * with its own lock at +1312. All stores stay inside this one allocation. */
 volatile u8*p=(volatile u8*)a;for(u32 i=0;i<ARENA_BYTES;i++)p[i]=0;
 ((volatile struct Lamps*)a)->magic=LIGHT_MAGIC;
 FENCE();S->reverse_arena=a;
}
static u8* scratch(u32 offset,u32 n){
 volatile struct Lamps*l=lamps();
 return l && offset<=800 && n<=800-offset?(u8*)l+512+offset:0;
}
static int scratch_lock(void){
 u8*p=scratch(0,800);u32 empty=0;
 return p && __atomic_compare_exchange_n((volatile u32*)(p+800),&empty,1,1,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void scratch_unlock(void){
 u8*p=scratch(0,800);if(p)__atomic_store_n((volatile u32*)(p+800),0,__ATOMIC_RELEASE);
}
static void lights_reset(void){
 volatile struct Lamps*l=lamps();if(l){l->guest_seen=0;l->restart=1;l->reset=1;FENCE();}
}
/* Invoked by native 5fb48 as (interface, length, result, user_context).
 * Only publish completion. No allocation, submission or locks in callback. */
void s3_complete(u64 interface,u64 n,int result,u64 context){
 (void)interface;
 volatile struct Lamps*l=lamps();
 if(!l||context!=(u64)l||!l->inflight)return;
 l->usb_length=(u32)n;l->usb_result=result;FENCE();l->done=1;
}
/* First valid generic HID interface for the selected input owner. Called only
 * while holding manager+0x30, the critical section used by enumeration/removal and
 * completion dispatcher. All pointer loads are bounded before dereference. */
static __attribute__((unused)) u64 owner_interface(void){
 u64 o=S->owner;
 if((o&7)||!rw((const void*)o,0x380))return 0;
 u32 handle=*(const volatile u32*)(o+0x18);
 u64 root=*(const volatile u64*)0x247e310ULL;
 if((root&7)||!rw((const void*)root,0x30))return 0;
 u64 begin=*(const volatile u64*)(root+0x20),end=*(const volatile u64*)(root+0x28);
 if((begin&7)||(end&7)||end<begin||end-begin>16*8||!rw((const void*)begin,end-begin))return 0;
 for(u64 a=begin;a<end;a+=8){
  u64 reg=*(const volatile u64*)a;
  if((reg&7)||!rw((const void*)reg,0x48))continue;
  u64 b=*(const volatile u64*)(reg+0x38),e=*(const volatile u64*)(reg+0x40);
  if((b&7)||(e&7)||e<b||e-b>16*8||!rw((const void*)b,e-b))continue;
  for(u64 v=b;v<e;v+=8){
   u64 wrap=*(const volatile u64*)v;
   if((wrap&7)||!rw((const void*)wrap,0x18))continue;
   u64 worker=*(const volatile u64*)(wrap+0x10);
   if((worker&7)||!rw((const void*)worker,0xd4))continue;
   if(*(const volatile u32*)(worker+0xd0)==handle)return *(const volatile u64*)(worker+0x40);
  }
 }
 return 0;
}
static __attribute__((unused)) u64 wired_interface(u64 manager){
 u64 selected=S->owner;if(!selected)return 0;
 u64 begin=*(const volatile u64*)(manager+0x20),end=*(const volatile u64*)(manager+0x28),found=0;
 if((begin&7)||(end&7)||end<begin||end-begin>16*8||!rw((const void*)begin,end-begin))return 0;
 for(u64 at=begin;at<end;at+=8){
  u64 d=*(const volatile u64*)at;if((d&7)||!rw((const void*)d,0x60))return 0;
  u64 desc=*(const volatile u64*)(d+0x18);
  if(!rw((const void*)desc,18))return 0;
  const volatile u8*b=(const volatile u8*)desc;
  if(b[0]<18||b[8]!=0x4c||b[9]!=5||!((b[10]==2&&!b[11])||(!b[10]&&b[11]==0x10)))continue;
  u64 ib=*(const volatile u64*)(d+0x30),ie=*(const volatile u64*)(d+0x38);
  if((ib&7)||(ie&7)||ie<ib||ie-ib>16*8||!rw((const void*)ib,ie-ib))return 0;
  for(u64 ip=ib;ip<ie;ip+=8){
   u64 i=*(const volatile u64*)ip;if((i&7)||!rw((const void*)i,0x88))return 0;
   if(*(const volatile u64*)i!=0x936838ULL||*(const volatile u64*)(i+0x40)!=d)continue;
   u64 id=*(const volatile u64*)(i+0x60);if(!rw((const void*)id,9))return 0;
   if(*(const volatile u8*)(id+2)!=0)continue;
   if(i==selected)return i;
  }
 }return found;
}
static long lights_submit(volatile struct Lamps*l){
#if LIGHT_MODE == 0
 (void)l;return -19; /* S4A: execute no physical output API. */
#else
 u64 manager=*(const volatile u64*)0x24389a8ULL;
 if((manager&7)||!rw((const void*)manager,0x38))return -19;
 /* ba24c saves syscall-5 state; b9074 restores it via syscall 6.
  * This target's wrapper ignores the usual timeout argument. */
 if(native_lock((const volatile u64*)(manager+0x30),0))return -16;
 u64 interface=wired_interface(manager);long r=-19;
 if(interface){
  l->context[0]=(u64)output_callback_descriptor;l->context[1]=(u64)l;l->context[2]=0;
  FENCE();r=native_set_report(interface,1,0,l->payload,7,l->context);
 }
 native_unlock(*(const volatile u64*)(manager+0x30));return r;
#endif
}
/* A fresh guest starts with generation/cursor zero. After an established
 * session this asks the sole HID writer to discard pre-reset input history.
 * The reader publishes a flag only; it does not become a second ring writer. */
static int lights_guest(u32 gen,u32 cursor){
 volatile struct Lamps*l=lamps();if(!l)return 0;
 if(gen)l->guest_seen=1;
 else if(!cursor&&l->guest_seen){l->guest_seen=0;l->restart=1;l->reset=1;FENCE();}
 return l->restart!=0;
}
static int lights_restart(void){
 volatile struct Lamps*l=lamps();if(!l||!l->restart)return 0;
 l->restart=0;return 1;
}
static u32 lights_cap(void){
#if LIGHT_MODE == 0
 return 0;
#endif
 volatile struct Lamps*l=lamps();
 return l&&!l->poison&&!l->reset&&!l->off_pending&&S->owner&&ACTIVE==1&&l->generation==S->generation;
}
static __attribute__((unused)) void lights_status(const u8*in,u8*reply){
 volatile struct Lamps*l=lamps();u32 gen=le32(in+8),seq=le32(in+12);
 putle(reply+8,S->generation);putle(reply+12,seq);
 if(!l){reply[6]=6;return;}
 if(l->inflight&&!l->done&&native_clock()-l->started>=LIGHT_TIMEOUT){
  l->poison=1;l->result=-110;l->completed=l->accepted;
  /* do not touch the USB-owned context or payload */
 }
 reply[6]=lights_cap()?1:6;
 if(gen!=S->generation)reply[6]=5;
 if(in[6]==2&&reply[6]==1){
#if LIGHT_MODE != 2
  reply[6]=6;
#else
  if(in[23]!=7||in[24]||in[25]!=1||in[26]||in[16]||in[21]||in[22])reply[6]=7;
  for(u32 i=17;i<21;i++)if(in[i]!=0&&in[i]!=255)reply[6]=7;
  if(!seq)reply[6]=7;
  if(reply[6]==1&&seq==l->accepted){
   const volatile struct LampRequest*q=&l->q[(l->tail-1)&7];
   for(u32 i=0;i<7;i++)if(in[16+i]!=q->data[i])reply[6]=7;
  }
  if(reply[6]==1 && seq!=l->accepted){
   if(!seq||(int)(seq-l->accepted)<=0)reply[6]=7;
   else if((u32)(l->tail-l->head)>=8)reply[6]=8;
   else {
    u32 tail=l->tail;volatile struct LampRequest*q=&l->q[tail&7];
    q->seq=seq;q->gen=gen;for(u32 i=0;i<7;i++)q->data[i]=in[16+i];
    FENCE();
    if(gen!=S->generation||gen!=l->generation)reply[6]=5;
    else {l->accepted=seq;FENCE();l->tail=tail+1;}
   }
  }
#endif
 }
 putle(reply+16,l->accepted);putle(reply+20,l->completed);putle(reply+24,(u32)l->result);
 putle(reply+28,(lights_cap()?1:0)|(l->inflight?2:0)|((u32)(l->tail-l->head)<<8)|(l->poison?4:0));
}
/* HID input worker entry, outside SIO2. No new worker/thread/USB ownership.
 * Only this producer uses the fixed-pattern queue in S3A. */
static void lights_pump_run(u64 object,u32 word,int status){
 (void)object;(void)word;
 volatile struct Lamps*l=lamps();if(!l)return;
 u64 now=native_clock();
 if(l->generation!=S->generation){
  l->head=l->tail;l->generation=S->generation;l->accepted=l->completed=0;l->last_valid=0;l->pattern=0;
  /* Keep the old transfer's storage until its callback. A generation change
   * alone is not a timeout. Recover with an off command after retirement. */
  l->reset=1;
 }
 if(l->inflight){
  FENCE();
  if(l->done){
   if(!l->poison&&l->flight_gen==l->generation){
    l->completed=l->flight_seq;l->result=l->usb_result?l->usb_result:(l->usb_length==7?0:-5);
    if(!l->result){for(u32 i=0;i<7;i++)l->last[i]=l->payload[i];l->last_valid=1;}
   }
   l->inflight=0;
  }else if(now-l->started>=LIGHT_TIMEOUT){
   l->poison=1;l->result=-110;l->completed=l->accepted;l->head=l->tail;
  }
 }
 if(status||!S->owner){l->head=l->tail;l->last_valid=0;return;}
 if(l->poison||l->inflight)return;
 if(l->reset){
  l->head=l->tail;l->accepted=l->completed=0;l->result=0;l->last_valid=0;
  volatile struct LampRequest*q=&l->q[l->tail&7];q->seq=0;q->gen=l->generation;
  for(u32 i=0;i<7;i++)q->data[i]=0;
  FENCE();l->tail++;l->off_pending=1;l->reset=0;
 }
 if(ACTIVE!=1&&!l->off_pending)return;
#if LIGHT_MODE == 1
 /* Each handset's own lamp follows its own five buttons. */
 u32 on=0;for(u32 g=0;g<4;g++)if((word>>(5*g))&0x1f)on|=1U<<g;
 if(on!=l->pattern && l->tail==l->head){
  volatile struct LampRequest*q=&l->q[l->tail&7];q->seq=l->accepted+1;q->gen=l->generation;
  for(u32 i=0;i<7;i++)q->data[i]=(i>=1&&i<=4&&((on>>(i-1))&1))?255:0;
  l->accepted=q->seq;FENCE();l->tail++;l->pattern=on;
 }
#endif
#if LIGHT_MODE == 3
 /* Level-triggered: converge on the lamp mask the game last requested. */
 u32 on=l->desired&15;
 if(on!=l->pattern && l->tail==l->head){
  volatile struct LampRequest*q=&l->q[l->tail&7];q->seq=l->accepted+1;q->gen=l->generation;
  for(u32 i=0;i<7;i++)q->data[i]=(i>=1&&i<=4&&((on>>(i-1))&1))?255:0;
  l->accepted=q->seq;FENCE();l->tail++;l->pattern=on;
 }
#endif
 if(l->head==l->tail||now-l->last_submit<LIGHT_PERIOD)return;
 volatile struct LampRequest*q=&l->q[l->head&7];
 if(q->gen!=l->generation){l->head++;return;}
 int same=l->last_valid&&!l->result;for(u32 i=0;i<7;i++)if(q->data[i]!=l->last[i])same=0;
 if(same){l->completed=q->seq;l->result=0;l->head++;return;}
 for(u32 i=0;i<7;i++)l->payload[i]=q->data[i];
 l->flight_seq=q->seq;l->flight_gen=q->gen;l->done=0;l->started=now;l->inflight=1;
 FENCE();long result=lights_submit(l);l->last_submit=now;l->head++;l->off_pending=0;
 if(result){l->inflight=0;l->completed=l->flight_seq;l->result=(int)result;}
}

/* Serialize the two callers; the state machine above is single-threaded. */
static __attribute__((unused)) void lights_pump(u64 object,u32 word,int status){
 volatile struct Lamps*l=lamps();if(!l)return;
 if(__atomic_exchange_n(&l->diagnostic_guest,1,__ATOMIC_ACQUIRE))return;
 lights_pump_run(object,word,status);
 __atomic_store_n(&l->diagnostic_guest,0,__ATOMIC_RELEASE);
}
