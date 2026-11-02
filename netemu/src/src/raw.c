/* Included in overlay.c: single HID writer, bounded seqlock reader, no locks,
 * allocation, USB claims or runtime code stores. Native stack is ABI-owned. */
extern long native_capture(u64,const u8*,u64,int);
extern long native_pad(u64,u64,u64,u64,u64);
extern u64 native_clock(void);
#define FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define FIFO_IN 0xb5b888ULL
#define FIFO_OUT 0xb5bc88ULL
#define FRAME 21
#define STALE_TICKS 79800000ULL
#define CM(x) do{if((LIGHT_MODE!=1&&LIGHT_MODE!=3))capture_mark(x);}while(0)
#define CH(x) do{if((LIGHT_MODE!=1&&LIGHT_MODE!=3))capture_host(x);}while(0)
#define CD(x) do{if((LIGHT_MODE!=1&&LIGHT_MODE!=3))capture_detail(x);}while(0)
static int lights_restart(void);
static void lights_pump(u64,u32,int);
static void capture_mark(u32);
static void capture_detail(u32);
static void capture_mapper(u32);
static void capture_host(u32);
extern long native_normalize(u64,u64);
extern long native_mapper(u64,u64,u64);

#if LIGHT_MODE != 3
long s2_capture(u64 object,const u8*report,u64 n,int status){
 if(LIGHT_MODE==1||LIGHT_MODE==3)return native_capture(object,report,n,status);
 const volatile u8*o=(const volatile u8*)object;
 /* Observe the original generic callback; it is not a second producer. */
 CM(1);
 u32 bits=n==6?4:n==5?8:16;
 if(n && rw(report,1) && !report[0])bits|=32;
 if(!(object&7) && rw((const void*)o,0x2e)){
  bits|=64;
  if(*(const volatile u16*)(o+0x2a)==0x054c)bits|=1;
  if((*(const volatile u16*)(o+0x2c)==2 || *(const volatile u16*)(o+0x2c)==0x1000))bits|=2;
  if((bits&3)==3){bits|=128;
   capture_host(*(const volatile u16*)(o+0x2c)==0x1000?256:512);
   if(n==6 && rw(report,6) && !report[0] && (report[5]&0xf0)==0xf0)bits|=512;
  }
 }
 capture_detail(bits);
 return native_capture(object,report,n,status);
}

#endif
/* Before stock normalization: T+40=physical interface I, request+0=USB
 * buffer, +8=T, +10=status, +18=physical byte count. One producer, the
 * native HID worker, rather than two overlapping callback captures. */
long s3_normalize(u64 transport,u64 request){
 CM(16);
 const volatile u8*t=(const volatile u8*)transport;
 const volatile u8*r=(const volatile u8*)request;
 if(!(transport&7) && !(request&7) && rw((const void*)t,0x48) && rw((const void*)r,0x20) &&
    *(const volatile u64*)(r+8)==transport){
  u64 object=*(const volatile u64*)(t+0x40);
  const volatile u8*o=(const volatile u8*)object;
  if(!(object&7) && rw((const void*)o,0x1a)){
   CD(256);
   if(*(const volatile u16*)(o+0x16)==0x054c){
    if(*(const volatile u16*)(o+0x18)==0x1000)CH(1024);
    if(*(const volatile u16*)(o+0x18)==2)CH(2048);
   }
   if(*(const volatile u16*)(o+0x16)==0x054c && (*(const volatile u16*)(o+0x18)==2 || *(const volatile u16*)(o+0x18)==0x1000)){
    CM(2);
    const u8*report=(const u8*)*(const volatile u64*)r;
    u64 n=*(const volatile u64*)(r+0x18);
    int status=*(const volatile int*)(r+0x10);
    int valid=!status && !t[0x38] && n==5 && rw(report,5) &&
     (report[4]&0xf0)==0xf0;
    if(valid)CM(4);
  u64 now=native_clock();
  if(valid &&
     (!S->owner || S->owner==(u32)object || !S->timestamp || now<S->timestamp || now-S->timestamp>=STALE_TICKS)){
   u32 e=S->epoch;S->epoch=e+1;FENCE();
   if(lights_restart() || S->owner!=(u32)object || !S->timestamp || now<S->timestamp || now-S->timestamp>=STALE_TICKS){
    S->owner=(u32)object;S->generation++;if(!S->generation)S->generation=1;S->produced=0;
   }
   u32 seq=S->produced+1;
   S->queue[(seq-1)&7]=((seq&4095)<<20)|((report[2]|((u32)report[3]<<8)|((u32)report[4]<<16))&0xfffff);
   S->timestamp=now;FENCE();S->produced=seq;FENCE();S->epoch=e+2;CM(8);
   lights_pump(object,(u32)(report[2]|((u32)report[3]<<8)|((u32)report[4]<<16))&0xfffff,0);
  }else if(status!=0 && S->owner==(u32)object){
   u32 e=S->epoch;S->epoch=e+1;FENCE();S->timestamp=0;S->owner=0;S->produced=0;S->generation++;if(!S->generation)S->generation=1;FENCE();S->epoch=e+2;
   lights_pump(object,0,status);
  }
   }
  }
 }
 return native_normalize(transport,request);
}

#if LIGHT_MODE != 3
long s3_mapper(u64 object,u64 parsed,u64 output){
 if(LIGHT_MODE==1||LIGHT_MODE==3)return native_mapper(object,parsed,output);
 capture_mapper(1);
 if(!(object&7) && rw((const void*)object,0x2d8) &&
    (*(const volatile u64*)(object+0x2d0)==0x939648ULL || *(const volatile u64*)(object+0x2d0)==0x93963cULL)){
  capture_mapper(2);
 }
 return native_mapper(object,parsed,output);
}

#endif
/* Host mapped-data boundary. Exact caller record supplies identity; never
 * reinterpret mapped buttons as a full raw USB report. No new input writer. */
extern long native_host(int,u8,void*,u64);
long s4_host(int handle,u8 mode,void*buffer,u64 capacity){
 if(LIGHT_MODE==3){ /* host-refresh thread, every 16 ms, holds no lock here */
  if(S->owner)lights_pump(S->owner,0,0);
  return native_host(handle,mode,buffer,capacity);
 }
 if(LIGHT_MODE==1)return native_host(handle,mode,buffer,capacity);
 CH(1);
 long result=native_host(handle,mode,buffer,capacity);
 u64 cache=*(const volatile u64*)0x2456618ULL;
 if(!(cache&7) && rw((const void*)cache,7*0xf8)){
  for(u32 i=0;i<7;i++){
   u64 record=cache+i*0xf8;
   if(record+0x40!=(u64)buffer || *(const volatile int*)(record+0xd0)!=handle)continue;
   CH(32);
   if(*(const volatile u16*)(record+0xd4)==0x054c && (*(const volatile u16*)(record+0xd6)==2 || *(const volatile u16*)(record+0xd6)==0x1000)){
    CH(2);
    /* A handle bit alone does not establish the provider. CF71C validates
     * class and selector before calling its live registered vtable. Confirm
     * that registration too; a positive result excludes its early no-data
     * return. Unknown selectors/providers remain B1 with N0T0. */
    if(mode==2 && ((u32)handle&0xff00)==0x100 && result>0){
     u64 manager=*(const volatile u64*)0x247e000ULL;
     if(!(manager&7) && rw((const void*)manager,0x18)){
      u64 providers=*(const volatile u64*)(manager+0x10);
      if(!(providers&7) && rw((const void*)providers,8)){
       u64 provider=*(const volatile u64*)providers;
       if(!(provider&7) && rw((const void*)provider,8) &&
          *(const volatile u64*)provider==0x9378f8ULL){
        capture_host((((u32)handle>>16)&0x40)?8:16);
        if(result>=16 && (u64)result<=capacity && capacity<=128 && rw(buffer,capacity))CH(4);
       }
      }
     }
    }
   }
   break;
  }
 }
 return result;
}

static void putle(u8*p,u32 v){for(u32 k=0;k<4;k++)p[k]=(u8)(v>>(8*k));}
#include "lights.c"
/* Cache identity fields are populated by the original host manager, not guessed
 * from button combinations. Host index=2*slot+port, bridge source=4*port+slot. */
static u32 buzz_sources(void){
 u64 manager=0x2456608ULL;
 u64 root=*(const volatile u64*)manager, cache=*(const volatile u64*)(manager+16);
 if((cache&1)||!rw((const void*)root,0x700)||!rw((const void*)cache,7*0xf8))return 255;
 u32 present=*(const volatile u8*)(root+0x6c9),mask=0;
 for(u32 i=0;i<7;i++)if((present>>i)&1){
  const volatile unsigned short*p=(const volatile unsigned short*)(cache+i*0xf8+0xd4);
  if(p[0]==0x054c&&(p[1]==0x0002 || p[1]==0x1000))mask|=1U<<((i&1)*4+(i>>1));
 }
 return mask;
}
#if DELIVERY
long s2_pad(u64 port,u64 input,u64 output,u64 ilen,u64 olen){
 u64 ib=*(const volatile u64*)0xb5c1d0ULL,ob=*(const volatile u64*)0xb5c1c8ULL;
 if(port!=0||ilen!=FRAME||olen!=FRAME||input>256-FRAME||output>256-FRAME||ib!=FIFO_IN||ob!=FIFO_OUT)
  return native_pad(port,input,output,ilen,olen);
 const u8*in=(const u8*)(ib+input);
 if(in[0]!=1||in[1]!=0x42||in[2]||in[3]||in[4]||in[5]!='B'||in[6]!='Z'||in[7]!=0xe4||in[8]!=1)
  return native_pad(port,input,output,ilen,olen);
 #if LIGHT_MODE == 3
 u32 lamp_valid=in[19],lamp_mask=in[17];
 if(in[20]||lamp_valid>1||(lamp_valid?(lamp_mask>15||in[18]!=(u8)(0xa5^lamp_mask)):(lamp_mask||in[18])))
  return native_pad(port,input,output,ilen,olen);
 {volatile struct Lamps*lp=lamps();if(lp)lp->desired=lamp_valid?(lamp_mask|16):0;}
#else
 for(u32 i=17;i<FRAME;i++)if(in[i])return native_pad(port,input,output,ilen,olen);
#endif
 u32 gen=le32(in+9),cursor=le32(in+13);u8 reply[32]={255,0x79,0x5a,'B','Z',0xe4,0,RAW_MODE};
 if((LIGHT_MODE!=1&&LIGHT_MODE!=3)){volatile struct Lamps*diagnostic_lamps=lamps();
 if(diagnostic_lamps)diagnostic_lamps->diagnostic_guest=1;}
 if(lights_guest(gen,cursor))goto send;
 u32 e=S->epoch;FENCE();
 if(!(e&1) && ACTIVE==1){
  u32 g=S->generation,p=S->produced,c=gen==g?cursor:0,seq=c,word=0;
  u64 t=S->timestamp,now=native_clock();
  if(t && now>=t && now-t<STALE_TICKS){
   if((u32)(p-c)>8){reply[6]=3;seq=p;} /* explicit overflow/resync; never spin */
   else if(p!=c){reply[6]=1;seq=c+1;word=S->queue[(seq-1)&7];
    if((word>>20)!=(seq&4095))reply[6]=4;
   }else reply[6]=2;
   putle(reply+8,g);putle(reply+12,seq);
   reply[16]=(u8)word;reply[17]=(u8)(word>>8);reply[18]=(u8)((word>>16)&15);
   reply[19]=8;reply[20]=(u8)buzz_sources();reply[21]=(u8)(p-seq);
   putle(reply+24,(u32)(now-t));
#if LIGHT_MODE == 2
   reply[22]=(u8)lights_cap();
#endif
  }
  FENCE();if(S->epoch!=e)reply[6]=4;
 }else if(e&1)reply[6]=4;
 /* This is the existing native SIO2 output ring, whose exact base/extent were
  * validated above. No guest-provided pointer becomes a native store target. */
send:;
 volatile u8*out=(volatile u8*)(FIFO_OUT+output);
 /* Compact only the physical representation; queue/acknowledgement fields
  * retain their full width. No USB output capability or operation exists. */
 for(u32 i=0;i<19;i++)out[i]=reply[i];
 out[19]=reply[20];out[20]=reply[21];
 return 1;
}


#else
long s2_pad(u64 a,u64 b,u64 c,u64 d,u64 e){return native_pad(a,b,c,d,e);}
#endif
