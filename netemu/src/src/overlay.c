/* Freestanding PPC64 BE, compiled for ISA 2.02. No mutable globals. */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
extern int raw_read(int,u64,void*,u64);
extern int cobra_open(int,const char*); extern int cobra_stat(int,void*);
extern const u8 bridge[], sony0[], sony1[], sony2[], cnf0[], cnf1[], cnf2[], cnf3[], cnf4[], cnf5[];
extern const u8 ioprp300[],ioprp310[];
struct State { u32 status; int fd; u64 start, iso_size; u32 length; u16 stride, skip;
 u32 epoch, produced; u64 timestamp; u32 owner, generation; u64 reverse_arena; u32 queue[8]; };
_Static_assert(sizeof(struct State)==96,"RW mailbox size");
_Static_assert(__builtin_offsetof(struct State,queue)==64,"queue offset");
_Static_assert(__builtin_offsetof(struct State,reverse_arena)==56,"Stage 3 arena pointer");
#define S ((volatile struct State*)0x930f20ULL)
#define ACTIVE (S->status&1U)
#define DISC_SEEN 0x100U
#define SERVED 0x200U
#define RETRIED 0x400U
/* Only the disc thread writes this word. Input/channel diagnostics use their
 * existing state and a separate arena word, never a competing status writer. */
static __attribute__((noinline)) void diag(u32 code){S->status=(S->status&0x701U)|(code<<16);}
#define DECLINE(code) do {diag(code);return 0;} while(0)
#define V ((const volatile u8*)0x3174478ULL)
#define RW_MIN 0x821000ULL
#define RW_END 0x3200000ULL
static int within(u64 a,u64 n,u64 base,u64 bytes){return a>=base && a-base<bytes && n<=bytes-(a-base);}
/* 0x19900 publishes native aliases as 0x80000000 | an 8-MiB slot.
 * Descriptor +0x28 is the native EA; +8 is the owned extent. These are
 * the four native pools, not the unrelated guest-RAM mapping. */
static int pool_rw(const void*p,u64 n){
 u64 a=(u64)p;
 for(u32 j=0;j<4;j++){
  const volatile u64*d=(const volatile u64*)(0x934958ULL+j*48);
  u64 base=d[5],bytes=d[1];
  if((base&~0xf800000ULL)==0x80000000ULL && bytes && bytes<=0x800000ULL && within(a,n,base,bytes))return 1;
 }return 0;
}
static __attribute__((noinline)) int rw(const void*p,u64 n) { u64 a=(u64)p;return within(a,n,RW_MIN,RW_END-RW_MIN)||pool_rw(p,n); }
#define ARENA_BYTES 1344U
/* b9848/b9470 own exactly the first pool's final 0x40000 bytes. */
static __attribute__((noinline)) int arena_rw(u64 a){
 u64 base=*(const volatile u64*)0x934980ULL;
 return (base&~0xf800000ULL)==0x80000000ULL &&
  *(const volatile u64*)0x24565b8ULL==base &&
  *(const volatile u64*)0x2446588ULL==base+0x20000ULL &&
  !(a&63) && within(a,ARENA_BYTES,base+0x20000ULL,0x40000ULL);
}
/* Existing CDVD guest buffer ownership: 0x1364f4 checks offset+bytes against
 * *0x2458430 and publishes *0x2458428+offset. No new mapping/allocation. */
static int output_rw(const void*p,u64 n){
 u64 a=(u64)p;if(rw(p,n))return 1;
 u64 base=*(const volatile u64*)0x2458428ULL, bytes=*(const volatile u64*)0x2458430ULL;
 return base>=RW_MIN && bytes && bytes<=0x2000000ULL && base<=~0ULL-bytes && a>=base && a-base<bytes && n<=bytes-(a-base);
}
static u32 le16(const u8*p){return p[0]|((u32)p[1]<<8);}
static u32 le32(const u8*p){return p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static u32 be32(const u8*p){return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
static int equal(const u8*a,const u8*b,u32 n){
 if(!(((u64)a|(u64)b)&7))while(n>=8){
  if(*(const u64*)a!=*(const u64*)b)return 0;a+=8;b+=8;n-=8;
 }
 for(u32 i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;
}
static void lights_init(void);
static void lights_reset(void);
static int lights_ready(void);
static u8* scratch(u32,u32);
static int scratch_lock(void);
static void scratch_unlock(void);
static int exact(int fd,u64 off,u8*b,u32 n,u64 size){return off<=size && n<=size-off && rw(b,n) && raw_read(fd,off,b,n)==(int)n;}
struct Image { int fd; u32 stride,skip; u64 size,volume; };
/* Raw Mode 1 or Mode 2 Form 1; never read data from audio/Form 2 sectors. */
static int raw_header(struct Image*i,u64 sector){
 if(i->stride==2048)return 1;u8*h=scratch(768,24);if(!h)return 0;
 if(!exact(i->fd,sector*2352,h,24,i->size))return 0;
 if(h[0]||h[11])return 0;for(u32 j=1;j<11;j++)if(h[j]!=255)return 0;
 if(i->skip==16)return h[15]==1;
 return h[15]==2 && equal(h+16,h+20,4) && !(h[18]&0x20);
}
static int logical(struct Image*i,u64 off,u8*b,u32 n){
 if(off>i->volume || n>i->volume-off || !rw(b,n))return 0;
 while(n){u64 sector=off/2048;u32 in=off%2048,k=2048-in;if(k>n)k=n;
  if(!raw_header(i,sector)||!exact(i->fd,sector*i->stride+i->skip+in,b,k,i->size))return 0;
  off+=k;b+=k;n-=k;
 }return 1;
}
struct File {u64 off;u32 size,flags;};
static int record(struct Image*i,const u8*b,u32 n,struct File*f){
 if(n<34||b[0]!=n||b[1]||b[26]||b[27]||le16(b+28)!=1||b[30]!=0||b[31]!=1)return 0;
 if(b[32]==0||33U+b[32]>n||le32(b+2)!=be32(b+6)||le32(b+10)!=be32(b+14))return 0;
 f->off=(u64)le32(b+2)*2048;f->size=le32(b+10);f->flags=b[25];
 if(f->flags&0xfc)return 0; /* reject associated, record, protection and multi-extent */
 return f->off<=i->volume && f->size<=i->volume-f->off;
}
static int nameeq(const u8*b,const char*name){
 if(name[0]=='*')return b[32]>=5&&equal(b+33,(const u8*)"IOPRP",5);
 u32 k=0,n=b[32];while(k<n&&b[33+k]!=';'){
  u8 c=b[33+k];if(c>='a'&&c<='z')c-=32;if(!name[k]||c!=(u8)name[k])return 0;k++;
 }if(name[k])return 0;
 return k==n||(k+2==n&&b[33+k]==';'&&b[34+k]=='1');
}
/* 1 found, 0 absent, -1 malformed/ambiguous/error. One bounded record at a time. */
static int find(struct Image*i,struct File dir,const char*name,struct File*out){
 u8*b=scratch(512,256);u32 p=0;int hits=0;if(!b)return -1;
 if(!(dir.flags&2)||dir.size>65536||!dir.size)return -1;
 while(p<dir.size){
  if(!logical(i,dir.off+p,b,1))return -1;
  u32 n=b[0];if(!n){p=(p/2048+1)*2048;continue;}
  if(n<34||n>dir.size-p||n>2048-p%2048||!logical(i,dir.off+p,b,n))return -1;
  struct File f;if(!record(i,b,n,&f))return -1;
  if(nameeq(b,name)){if(hits++)return -1;*out=f;}p+=n;
 }return hits;
}
static int discover(int fd,u64 size){
 struct Image i={fd,2048,0,size,size};u8*b=scratch(0,512);struct File root,cnf,dir,file,candidate,runtime;
 if(!b)DECLINE(29);
 /* The HID worker is the sole ring writer. Image initialization publishes
  * reset flags; readers fail open until that worker publishes fresh input. */
 S->status=0;lights_reset();
 S->fd=fd;S->iso_size=size;S->start=0;S->length=0;
 if(fd<0)DECLINE(2);
 if(!V[4])DECLINE(4);
 if(size<32768)DECLINE(3);
 if(V[0x804]){
  if(V[0x805]){
   if(size%2352)DECLINE(5);i.stride=2352;i.skip=16;
   if(!raw_header(&i,16)){i.skip=24;if(!raw_header(&i,16))DECLINE(5);}
  }else if(size%2048)DECLINE(5);
 }else if(size%2048)DECLINE(5);
 i.volume=(size/i.stride)*2048;
 if(!logical(&i,32768,b,190))DECLINE(6);
 if(!equal(b,(const u8*)"\1CD001\1",7))DECLINE(7);
 if(le16(b+128)!=2048||b[130]!=8||b[131]!=0||le16(b+120)!=1||b[122]!=0||b[123]!=1||le16(b+124)!=1||b[126]!=0||b[127]!=1)DECLINE(8);
 u64 vol=(u64)le32(b+80)*2048;if(le32(b+80)!=be32(b+84)||vol>i.volume||vol<32768)DECLINE(9);i.volume=vol;
 if(!record(&i,b+156,b[156],&root))DECLINE(10);
 if(find(&i,root,"SYSTEM.CNF",&cnf)!=1||(cnf.flags&2)||cnf.size>512)DECLINE(11);
 if(!logical(&i,cnf.off,b,cnf.size))DECLINE(12);
 int title=-1;if(cnf.size==56&&equal(b,cnf0,56))title=0;
 if(cnf.size==59&&equal(b,cnf1,59))title=1;
 if(cnf.size==59&&equal(b,cnf2,59))title=2;
 if(cnf.size==59&&(equal(b,cnf3,59)||equal(b,cnf4,59)||equal(b,cnf5,59)))title=1;
 if(title<0)DECLINE(13);
 diag(14);
 int count=0;
 for(u32 d=0;d<2;d++){
  int r=find(&i,root,d?"SYSTEM":"IOP",&dir);if(r<0)DECLINE(16);if(!r)continue;
  r=find(&i,dir,"USBD.IRX",&file);if(r<0)DECLINE(16);if(!r)continue;
  if(count++)DECLINE(16);candidate=file;
  /* Only the observed runtime image is supported. Unknown/embedded/duplicate
   * runtime layouts retain their original disc bytes. */
  if(find(&i,dir,"*IOPRP",&runtime)!=1 || (runtime.flags&2))DECLINE(15);
 }
 if(count!=1||(candidate.flags&2))DECLINE(16);
 u32 len=title==0?34985:title==1?34993:35009;const u8*original=title==0?sony0:title==1?sony1:sony2;
 if(candidate.size!=len)DECLINE(17);
 u32 runtime_len=title==2?278353:275345;
 const u8*runtime_original=title==2?ioprp310:ioprp300;
 if(runtime.size!=runtime_len)DECLINE(18);
 for(u32 p=0;p<runtime_len;){u32 n=runtime_len-p;if(n>512)n=512;
  if(!logical(&i,runtime.off+p,b,n)||!equal(b,runtime_original+p,n))DECLINE(19);p+=n;
 }
 /* Exact full-file comparison against SHA1-asserted immutable originals. */
 for(u32 p=0;p<len;){u32 n=len-p;if(n>512)n=512;
  if(!logical(&i,candidate.off+p,b,n)||!equal(b,original+p,n))DECLINE(20);p+=n;
 }
 S->stride=i.stride;S->skip=i.skip;S->start=candidate.off/2048*i.stride+i.skip;S->length=len;
 lights_init();if(!lights_ready())DECLINE(21);
 lights_reset();__atomic_thread_fence(__ATOMIC_SEQ_CST);S->status=1;diag(22);return 1;
}
/* Scratch lifetime is the emulator lifetime; never borrowed static padding.
 * Serialize discovery and raw-header verification without waiting on another
 * worker. Busy requests retain original bytes. Stack locals are ABI-owned;
 * no stack address is submitted to UFS by the new code. */
static int init(int fd,u64 size){
 lights_init();
 if(!lights_ready()){
  S->status=0;S->fd=fd;S->iso_size=size;S->length=0;
  diag(fd<0?2:!V[4]?4:size<32768?3:29);return 0;
 }
 if(!scratch_lock()){S->status&=~1U;diag(31);return 0;}
 int r=discover(fd,size);scratch_unlock();return r;
}
int s1_open(int unk,const char*path){
 int fd=cobra_open(unk,path);init(fd,*(const volatile u64*)(V+0x810));return fd;
}
int s1_stat(int fd,void*st){int ret=cobra_stat(fd,st);if(ret==0)init(fd,*(const volatile u64*)(V+0x810));else {S->status=0;diag(30);}return ret;}
int s1_read(int fd,u64 off,u8*buf,u64 size){
 /* Repair an early, permanently latched decline: one late attempt per image,
  * from the real backend context. Authentication is never relaxed. Failed
  * allocation has not published a pointer or submitted a transfer, so only
  * that sentinel may be cleared. A live/quarantined arena is never replaced. */
 if(!ACTIVE && !(S->status&RETRIED) && fd==S->fd && V[4]){
  /* Native pool EAs are 32-bit; the BE pointer's high word stays zero. */
  u32 failed=1;if(!(S->reverse_arena>>32))__atomic_compare_exchange_n((volatile u32*)0x930f5cULL,&failed,0,1,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED);
  init(fd,*(const volatile u64*)(V+0x810));S->status|=RETRIED;
 }
 S->status|=DISC_SEEN;
 /* Snapshot disc metadata before the backing read; a nested image change
  * cannot redirect an older request to the new image's overlay extent. */
 u32 active=ACTIVE,length=S->length;int descriptor=S->fd;
 u64 first=S->start,iso_size=S->iso_size;u16 stride=S->stride,skip=S->skip;
 int ret=raw_read(fd,off,buf,size);
 if(ret<=0||(u64)ret!=size){if(active)diag(23);return ret;}
 if(active!=1||ACTIVE!=1)return ret;
 if(fd!=descriptor||S->fd!=descriptor){diag(24);return ret;}
 if(
    S->start!=first||S->length!=length||S->iso_size!=iso_size||S->stride!=stride||S->skip!=skip||
    !length||length>35009||(stride!=2048&&stride!=2352)||skip>24){diag(25);return ret;}
 if(!output_rw(buf,size)){diag(26);return ret;}
 if(off>iso_size||size>iso_size-off){diag(27);return ret;}
 diag(22);
 u64 last=first+(length-1)/2048*stride+(length-1)%2048+1;
 if(off>=last||(off<first && size<=first-off))return ret;
 if(!scratch_lock()){diag(31);return ret;}
 struct Image i={fd,stride,skip,iso_size,0};
 /* Validate all raw headers before the first output write; errors leave buffer intact. */
 for(u32 p=0;p<length;p+=2048){u64 a=first+(u64)(p/2048)*stride;u32 n=length-p;if(n>2048)n=2048;
  if(off<a+n&&off+size>a&&!raw_header(&i,(a-skip)/stride)){scratch_unlock();diag(28);return ret;}
 }
#if SUBSTITUTE
 for(u32 p=0;p<length;p+=2048){u64 a=first+(u64)(p/2048)*stride;u32 n=length-p;if(n>2048)n=2048;
  u64 lo=off>a?off:a, hi=off+size<a+n?off+size:a+n;
  for(u64 q=lo;q<hi;q++){u64 k=p+q-a;buf[q-off]=k<BRIDGE_SIZE?bridge[k]:0;}
 }
#endif
 S->status|=SERVED;
 scratch_unlock();
 return ret;
}
#include "raw.c"
extern const u8 diagnostic_labels[];
const u8* s4_menu(u64 selected){
 u32 state=S->status,code=(state>>16)&31,flags=(state>>8)&3;
 if(S->timestamp&&S->produced)flags|=4;
 volatile struct Lamps*l=lamps();if(l&&l->diagnostic_guest&&LIGHT_MODE!=3)flags|=8;
 u32 c=l?l->capture_stage:0,d=l?l->capture_detail:0,h=l?l->host_seen:0;
 u32 ip=(h>>10)&3,op=(h>>8)&3;
 if((LIGHT_MODE==1||LIGHT_MODE==3)&&((selected&3)==1||(selected&3)==2))return diagnostic_labels+(code*16+flags)*32;
 switch(selected&3){
 case 1:{u32 bits=((c>>4)&1)|((c&14))|((d&256)?16:0)|(ip<<5);
  return diagnostic_labels+(512+bits)*32;}
 case 2:{u32 bits=(c&1)|((d&4)?2:0)|((d&32)?4:0)|((d&512)?8:0)|((l&&(l->mapper_seen&2))?16:0)|(op<<5);
  return diagnostic_labels+(640+bits)*32;}
 case 3:{
#if LIGHT_MODE == 1 || LIGHT_MODE == 3
  u32 rc=0;
  if(l){int r=l->result;rc=!r?0:r==-19?1:r==-16?2:r==-110?3:4;}
  u32 bits=l?((l->pattern?1:0)|(l->last_submit?2:0)|(l->completed?4:0)|(l->last_valid?8:0)|(l->poison?16:0)|((l->desired&16)?32:0)):0;
  return diagnostic_labels+(768+bits*5+rc)*32;
#else
  return diagnostic_labels+(768+ip*4+op)*32;
#endif
 }
 default:return diagnostic_labels+(code*16+flags)*32;
 }
}
