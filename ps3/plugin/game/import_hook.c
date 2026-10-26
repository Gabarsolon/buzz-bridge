/* SPDX-License-Identifier: MIT */
#include "import_hook.h"
#include <string.h>
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static int word(const buzz_import_memory *m,uint32_t addr,uint32_t *v)
{ uint8_t b[4]; if((addr&3) || addr>UINT32_MAX-4 || !m->read(m->arg,addr,b,4)) return 0; *v=be32(b); return 1; }
int buzz_import_discover(buzz_import_plan *p,const buzz_import_memory *m,uint32_t begin,uint32_t end)
{
 buzz_import_plan next=*p; uint32_t addr; unsigned i;
 if(!p->count || p->count>BUZZ_IMPORT_MAX || begin>=end || (begin&3) || end-begin>0x10000) return 0;
 for(i=0;i<p->count;i++) {
  unsigned j;
  if(!p->patch[i].nid || !m->valid_opd(m->arg,p->patch[i].replacement)) return 0;
  for(j=0;j<i;j++) if(p->patch[i].nid==p->patch[j].nid) return 0;
  next.patch[i].slot=0; next.patch[i].installed=0;
 }
 for(addr=begin;addr<end;) {
  uint8_t h[44]; uint32_t nt,st,name; uint16_t count; unsigned j;
  char lib[9]; int pad_library;
  if(end-addr<sizeof(h) || !m->read(m->arg,addr,h,sizeof(h)) || h[0]!=44) return 0;
  count=be16(h+6); nt=be32(h+20); st=be32(h+24); name=be32(h+16);
  if(count>512 || nt>UINT32_MAX-(uint32_t)count*4 || st>UINT32_MAX-(uint32_t)count*4) return 0;
  if(!m->read(m->arg,name,lib,7)) return 0;
  /* Only cellUsbd and sys_io are in scope. Other import headers are skipped. */
  if(!memcmp(lib,"sys_io\0",7)) { j=0; pad_library=1; }
  else if(m->read(m->arg,name,lib,9) && !memcmp(lib,"cellUsbd\0",9)) { j=0; pad_library=0; }
  else { addr+=44; continue; }
  if(be16(h+8) || be16(h+10)) return 0;
  for(;j<count;j++) {
   uint32_t nid,original;
   if(!word(m,nt+j*4,&nid)) return 0;
   if((nid==0x8b72cda1u)!=pad_library) continue;
   for(i=0;i<p->count;i++) if(next.patch[i].nid==nid) {
    if(next.patch[i].slot || !word(m,st+j*4,&original) || !m->valid_opd(m->arg,original)) return 0;
    next.patch[i].slot=st+j*4; next.patch[i].original=original;
   }
  }
  addr+=44;
 }
 for(i=0;i<p->count;i++) if(!next.patch[i].slot) return 0;
 *p=next; return 1;
}
int buzz_import_restore(buzz_import_plan *p,const buzz_import_memory *m)
{
 unsigned i=p->count; int ok=1;
 while(i) {
  buzz_import_patch *x=&p->patch[--i];
  if(x->installed) {
   if(m->replace(m->arg,x->slot,x->replacement,x->original)) x->installed=0;
   else ok=0;
  }
 }
 return ok;
}
int buzz_import_install(buzz_import_plan *p,const buzz_import_memory *m)
{
 unsigned i; uint32_t v;
 for(i=0;i<p->count;i++) {
  buzz_import_patch *x=&p->patch[i];
  if(x->installed || !x->slot || !word(m,x->slot,&v) || v!=x->original) return 0;
 }
 for(i=0;i<p->count;i++) {
  buzz_import_patch *x=&p->patch[i];
  if(!m->replace(m->arg,x->slot,x->original,x->replacement)) {
   (void)buzz_import_restore(p,m); return 0;
  }
  x->installed=1;
 }
 return 1;
}
