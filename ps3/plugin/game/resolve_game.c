/* SPDX-License-Identifier: MIT */
#include "resolve_game.h"
#include "profile.h"
#include <string.h>
typedef struct {
 const char *name; int32_t manager_delta; uint32_t manager_size; int32_t core_delta;
 uint32_t core_size,getter_offset; int32_t registration_delta,core_toc_delta;
 uint32_t callback_size,bridge_size,bridge_call_offset; int32_t bridge_delta;
 uint32_t ldd_offsets[3],crcs[4];
} buzz_driver_family;
#include "driver_shapes.h"
static const uint32_t nids[14]={0xd0e766fe,0x35f22ac3,0x7fe92c54,0xbd554bcb,0x90460081,0x2fb08e1e,0x254289ac,0x9763e962,0x97cf128e,0x0f411262,0x63bfdb97,0x5de3af36,0xdb819e03,0x8b72cda1};
typedef struct { uint32_t address,file_size,mem_size,flags; } segment;
typedef struct { uint32_t at,size; } region;
typedef struct {
 const buzz_import_memory *m; segment seg[8]; unsigned count; uint32_t toc;
 buzz_game_layout game; uint32_t regcall,initcall,incall[2],ldd_table;
} image;
static uint32_t be32(const uint8_t *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint16_t be16(const uint8_t *p) {return (uint16_t)((unsigned)p[0]<<8|p[1]);}
static int read_at(const buzz_import_memory *m,uint32_t a,void *p,size_t n)
{return a>=0x10000 && a<0x40000000 && n<=0x40000000-a && m->read(m->arg,a,p,n);}
static int word(const buzz_import_memory *m,uint32_t a,uint32_t *v)
{uint8_t b[4];if((a&3)||!read_at(m,a,b,4))return 0;*v=be32(b);return 1;}
static int within(const image *im,uint32_t a,uint32_t n,unsigned flags,int file)
{
 for(unsigned i=0;i<im->count;i++) {
  const segment *s=&im->seg[i];uint32_t size=file?s->file_size:s->mem_size;
  if((s->flags&flags)==flags && a>=s->address && n<=size && a-s->address<=size-n)return 1;
 }
 return 0;
}
static int branch(const image *im,uint32_t a,uint32_t *target)
{
 uint32_t w;if(!word(im->m,a,&w)||w>>26!=18||(w&2))return 0;
 int32_t d=(int32_t)(w&0x3fffffc);if(d&0x2000000)d-=0x4000000;
 *target=a+(uint32_t)d;return within(im,*target,4,1,1);
}
static int header(image *im)
{
 uint8_t h[64],p[56],q[40];uint32_t ph,entry,prx=0;unsigned n;
 if(!read_at(im->m,0x10000,h,64)||memcmp(h,"\177ELF\2\2\1",7)||be16(h+18)!=21||be16(h+16)!=2||be32(h+32)||be32(h+24))return 0;
 ph=be32(h+36);entry=be32(h+28);n=be16(h+56);
 if(be16(h+54)!=56||!n||n>32||ph>0x10000-n*56)return 0;
 for(unsigned i=0;i<n;i++) {
  if(!read_at(im->m,0x10000+ph+i*56,p,56))return 0;
  uint32_t type=be32(p),flags=be32(p+4),a=be32(p+20),fs=be32(p+36),ms=be32(p+44);
  if(be32(p+16)||be32(p+32)||be32(p+40))return 0;
  if(type==1 && ms) {
   if(im->count==8||a<0x10000||a>=0x40000000||ms>0x8000000||ms>0x40000000-a||fs>ms||fs>0x4000000)return 0;
   for(unsigned j=0;j<im->count;j++)if(a<im->seg[j].address+im->seg[j].mem_size && im->seg[j].address<a+ms)return 0;
   im->seg[im->count++]=(segment){a,fs,ms,flags};
  }
  if(type==0x60000002) {if(prx)return 0;prx=a;}
 }
 if(!im->count||!prx||!within(im,entry,8,0,1)||!word(im->m,entry+4,&im->toc)||!within(im,im->toc,4,0,0))return 0;
 if(!within(im,prx,40,0,1)||!read_at(im->m,prx,q,40)||be32(q+4)!=0x1b434cec)return 0;
 im->game.begin=be32(q+24);im->game.end=be32(q+28);
 uint32_t b=im->game.begin,e=im->game.end;
 return b<e && !(b&3) && e-b<=65536 && !((e-b)%44) && within(im,b,e-b,0,1);
}
static int import_tables(image *im)
{
 unsigned usb_count=0;
 for(uint32_t a=im->game.begin;a<im->game.end;a+=44) {
  uint8_t h[44],name[9];if(!read_at(im->m,a,h,44)||h[0]!=44)return 0;
  uint32_t np=be32(h+16),nt=be32(h+20),st=be32(h+24);unsigned n=be16(h+6);
  if(n>512||!within(im,np,9,0,1)||!read_at(im->m,np,name,9))return 0;
  int usb=!memcmp(name,"cellUsbd\0",9),pad=!memcmp(name,"sys_io\0",7);
  if(!usb&&!pad)continue;
  if(be16(h+8)||be16(h+10)||!within(im,nt,n*4,0,1)||!within(im,st,n*4,2,1))return 0;
  for(unsigned j=0;j<n;j++) {
   uint32_t nid;if(!word(im->m,nt+j*4,&nid))return 0;
   if(usb)usb_count++;
   for(unsigned k=0;k<14;k++)if(nid==nids[k] && (k==13?pad:usb)) {
    if(im->game.slots[k])return 0;im->game.slots[k]=st+j*4;
   }
  }
 }
 if(usb_count!=13)return 0;
 for(unsigned k=0;k<14;k++) {
  if(!im->game.slots[k])return 0;
  for(unsigned j=0;j<k;j++)if(im->game.slots[j]==im->game.slots[k])return 0;
 }
 return 1;
}
static int trampolines(image *im)
{
 /* Scan only file-backed executable segments, in 1 KiB batches. */
 for(unsigned s=0;s<im->count;s++)if(im->seg[s].flags&1) {
  segment *seg=&im->seg[s];uint8_t b[1052];
  for(uint32_t off=0;off<seg->file_size;off+=1024) {
   uint32_t n=seg->file_size-off;if(n>sizeof(b))n=sizeof(b);
   if(!read_at(im->m,seg->address+off,b,n))return 0;
   for(unsigned j=0;j+32<=n && j<1024;j+=4)if(be32(b+j)==0x39800000 && (be32(b+j+4)&0xffff0000)==0x658c0000 && (be32(b+j+8)&0xffff0000)==0x818c0000) {
    uint32_t slot=(be32(b+j+4)<<16)+(uint32_t)(int32_t)(int16_t)be32(b+j+8);
    static const uint32_t tail[5]={0xf8410028,0x800c0000,0x804c0004,0x7c0903a6,0x4e800420};
    int valid=1;for(unsigned v=0;v<5;v++)if(be32(b+j+12+v*4)!=tail[v])valid=0;
    if(!valid)continue;
    for(unsigned k=0;k<14;k++)if(slot==im->game.slots[k]) {
     if(im->game.stubs[k])return 0;im->game.stubs[k]=seg->address+off+j;
    }
   }
  }
 }
 for(unsigned k=0;k<14;k++)if(!im->game.stubs[k])return 0;
 return 1;
}
static int calls(image *im)
{
 unsigned nr=0,ni=0,nn=0;
 for(unsigned s=0;s<im->count;s++)if(im->seg[s].flags&1) {
  segment *seg=&im->seg[s];uint8_t b[1024];
  for(uint32_t off=0;off<seg->file_size;off+=sizeof(b)) {
   uint32_t n=seg->file_size-off;if(n>sizeof(b))n=sizeof(b);
   if(!read_at(im->m,seg->address+off,b,n))return 0;
   for(unsigned j=0;j+4<=n;j+=4) {
    uint32_t w=be32(b+j),a=seg->address+off+j;
    if((w&0xfc000003)!=0x48000001)continue;
    int32_t d=(int32_t)(w&0x3fffffc);if(d&0x2000000)d-=0x4000000;
    uint32_t t=a+(uint32_t)d;
    if(t==im->game.stubs[3]){im->regcall=a;nr++;}
    if(t==im->game.stubs[0]){im->initcall=a;ni++;}
    if(t==im->game.stubs[9]){if(nn<2)im->incall[nn]=a;nn++;}
   }
  }
 }
 return nr==1&&ni==1&&nn==2;
}
static int record(image *im)
{
 unsigned found=0;
 for(unsigned s=0;s<im->count;s++)if(!(im->seg[s].flags&1)) {
  segment *seg=&im->seg[s];uint8_t b[1044];
  for(uint32_t off=0;off<seg->file_size;off+=1024) {
   uint32_t n=seg->file_size-off;if(n>sizeof(b))n=sizeof(b);
   if(!read_at(im->m,seg->address+off,b,n))return 0;
   for(unsigned j=0;j+24<=n&&j<1024;j+=4)if(be16(b+j+16)==0x54c && be16(b+j+18)==2 && be16(b+j+20)==0x42) {
    char name[8];uint32_t np=be32(b+j);
    if(!within(im,np,8,0,1)||!read_at(im->m,np,name,8)||memcmp(name,"buzzer2\0",8))continue;
    uint32_t ldd=seg->address+off+j;if(ldd<0x10030)return 0;
    uint8_t table[120];if(!within(im,ldd-48,120,2,1)||!read_at(im->m,ldd-48,table,120))return 0;
    static const uint16_t filters[4][3]={{0x54c,0x1000,0x1040},{0x54c,1,0x41},{0x54c,2,0x42},{0x46d,0xc220,0xc220}};
    for(unsigned r=0;r<4;r++) {
     if(memcmp(table+r*24+4,b+j+4,12))return 0;
     for(unsigned k=0;k<3;k++)if(be16(table+r*24+16+2*k)!=filters[r][k])return 0;
    }
    if(be16(table+112))return 0;
    im->game.ldd=ldd;im->ldd_table=ldd-48;
    for(unsigned k=0;k<3;k++)im->game.opds[k]=be32(b+j+4+k*4);
    found++;
   }
  }
 }
 return found==1;
}
static uint32_t crc_word(uint32_t crc,uint32_t w)
{
 for(unsigned i=0;i<4;i++) {
  crc^=(w>>(24-i*8))&255;
  for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
 }
 return crc;
}
static uint32_t coordinate(const region *units,unsigned count,uint32_t a)
{
 for(unsigned i=0;i<count;i++)if(a>=units[i].at && a-units[i].at<units[i].size)return ((i+1)<<24)|(a-units[i].at);
 return 0;
}
static int shape_crc(image *im,const region *units,const uint32_t *tocs,unsigned count,unsigned index,uint32_t *out)
{
 uint32_t crc=UINT32_MAX;region r=units[index];uint8_t bytes[1024];
 if(!within(im,r.at,r.size,1,1)||r.size>8192 || r.size&3)return 0;
 for(uint32_t off=0;off<r.size;off+=sizeof(bytes)) {
  uint32_t n=r.size-off;if(n>sizeof(bytes))n=sizeof(bytes);
  if(!read_at(im->m,r.at+off,bytes,n))return 0;
  for(unsigned j=0;j<n;j+=4) {
   uint32_t a=r.at+off+j,w=be32(bytes+j),extra=0;
   if(w>>26==18 && !(w&2)) {
    int32_t d=(int32_t)(w&0x3fffffc);if(d&0x2000000)d-=0x4000000;
    uint32_t target=a+(uint32_t)d;
    if(target<r.at || target-r.at>=r.size) {
     w&=0xfc000003;extra=coordinate(units,count,target);
     for(unsigned k=0;k<14;k++)if(target==im->game.stubs[k])extra=nids[k];
    }
   } else if(w>>26==32 && ((w>>16)&31)==2) {
    uint32_t value;if(!word(im->m,tocs[index]+(uint32_t)(int32_t)(int16_t)w,&value))return 0;
    extra=coordinate(units,count,value);
    if(!extra) {
     uint32_t code,toc;
     if(word(im->m,value,&code)&&word(im->m,value+4,&toc)) {
      extra=coordinate(units,count,code);if(extra && toc!=tocs[index])return 0;
     }
    }
    w&=0xffff0000;
   }
   crc=crc_word(crc,w);crc=crc_word(crc,extra);
  }
 }
 *out=~crc;return 1;
}
static int family(image *im,const buzz_driver_family *f,buzz_game_layout *out)
{
 uint32_t manager=im->regcall+(uint32_t)f->manager_delta,core=im->incall[0]+(uint32_t)f->core_delta;
 uint32_t getter=manager+f->getter_offset,w,root,table,core_toc=im->toc+(uint32_t)f->core_toc_delta;
 if(!word(im->m,getter,&w)||(w&0xffff0000)!=0x81220000||!word(im->m,im->toc+(uint32_t)(int32_t)(int16_t)w,&root))return 0;
 if(!within(im,root,0xc0,2,0))return 0;
 if(!word(im->m,im->regcall+(uint32_t)f->registration_delta,&w)||(w&0xffff0000)!=0x81220000||!word(im->m,im->toc+(uint32_t)(int32_t)(int16_t)w,&table)||table!=im->ldd_table)return 0;
 for(unsigned k=0;k<3;k++) {
  uint32_t code,toc;if(!word(im->m,im->game.opds[k],&code)||!word(im->m,im->game.opds[k]+4,&toc)||code!=manager+f->ldd_offsets[k]||toc!=im->toc)return 0;
 }
 region units[4]={{manager,f->manager_size},{core,f->core_size},{UINT32_MAX,f->callback_size},{0,0}};
 uint32_t tocs[4]={im->toc,core_toc,core_toc,im->toc},cb_opds[6]={0};unsigned callbacks=0;
 if(!within(im,core,f->core_size,1,1))return 0;
 for(uint32_t a=core;a<core+f->core_size;a+=4) {
  if(!word(im->m,a,&w))return 0;
  if((w&0xffff0000)==0x80c20000) {
   uint32_t opd,code,toc;
   if(!word(im->m,core_toc+(uint32_t)(int32_t)(int16_t)w,&opd)||!word(im->m,opd,&code)||!word(im->m,opd+4,&toc)||toc!=core_toc)return 0;
   for(unsigned k=0;k<callbacks;k++)if(cb_opds[k]==opd)return 0;
   if(callbacks==6)return 0;cb_opds[callbacks++]=opd;
   if(code<units[2].at)units[2].at=code;
  }
 }
 if(callbacks!=6)return 0;
 unsigned n=3;
 if(f->bridge_size) {
  uint32_t target;if(!branch(im,manager+f->bridge_call_offset,&target))return 0;
  units[3]=(region){target+(uint32_t)f->bridge_delta,f->bridge_size};n=4;
 }
 for(unsigned i=0;i<n;i++) {
  uint32_t crc;if(!shape_crc(im,units,tocs,n,i,&crc)||crc!=f->crcs[i])return 0;
 }
 *out=im->game;out->ready=root+8+0x94;out->count=root+8+0xa8;out->presence[0]=root+8+0x69;out->presence[1]=root+8+0x8d;
 return 1;
}
int buzz_game_exact(const buzz_import_memory *m,unsigned index)
{
 if(index>=BUZZ_PROFILE_COUNT)return 0;const buzz_exact_profile *p=&buzz_profiles[index];
 for(unsigned i=0;i<p->check_count;i++) {
  uint8_t b[1024];const buzz_fingerprint *f=&p->checks[i];uint32_t crc=UINT32_MAX;
  if(f->size>sizeof(b)||!read_at(m,f->address,b,f->size))return 0;
  for(unsigned j=0;j<f->size;j++) {
   crc^=b[j];for(unsigned k=0;k<8;k++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
  }
  if(~crc!=f->crc)return 0;
 }
 return 1;
}
int buzz_game_resolve(const buzz_import_memory *m,buzz_game_layout *out,const char **reason)
{
 image im;memset(&im,0,sizeof(im));im.m=m;unsigned matches=0;
 *reason="ELF/program-header/PRX-parameter shape";if(!header(&im))return 0;
 *reason="missing/duplicate/unsupported cellUsbd or pad imports";if(!import_tables(&im))return 0;
 *reason="missing/duplicate import trampoline/slot shape";if(!trampolines(&im))return 0;
 *reason="missing/ambiguous registration/init/interrupt callsites";if(!calls(&im))return 0;
 *reason="missing/ambiguous buzzer LDD/VID-PID/filter table";if(!record(&im))return 0;
 for(unsigned i=0;i<sizeof(buzz_families)/sizeof(buzz_families[0]);i++) {
  buzz_game_layout candidate;if(family(&im,&buzz_families[i],&candidate)) {*out=candidate;out->family=i;matches++;}
 }
 *reason="unrecognized/ambiguous driver code, callbacks, TOCs or manager shape";if(matches!=1)return 0;
 out->profile=-1;
 for(unsigned i=0;i<BUZZ_PROFILE_COUNT;i++) {
  const buzz_exact_profile *p=&buzz_profiles[i];
  if(!memcmp(out->slots,p->slots,sizeof(out->slots)) && out->ldd==p->ldd) {
   *reason="known layout fingerprint mismatch; no generic fallback";
   if(!buzz_game_exact(m,i))return 0;
   out->profile=(int)i;
  }
 }
 *reason=out->profile>=0?"exact immutable fingerprints + structural checks":"UNVERIFIED structural checks";
 return 1;
}
