/* SPDX-License-Identifier: MIT. Bounded port of universal/tools/bb_patch.py.
 * ISO9660 plain sectors only. No image copy, extent relocation, or IOPRP edit. */
#include "iso_patch.h"
#include "module_hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "bb_io.h"
typedef unsigned char byte;
static const char *const paths[2]={"/IOP/USBD.IRX","/SYSTEM/USBD.IRX"};
const char *const bb_setup_names[7]={"1) Buzz! buzzers only","2) Buzz! buzzers + 1 DualShock","3) 2 DualShocks, shared","4) 2 DualShocks, one player each","5) 1 DualShock, shared","6) Buzz! buzzers + 2 DualShocks (any slots)","7) More than 2 controllers (any slots)"};
const char *const bb_layout_names[14]={"buzzers","buzzers_ds","two_ds_shared","two_ds_single","one_ds_shared","buzzers_ds2_tap1","four_ds_tap1","buzzers_ds2_tap2","eight_ds_tap12","any_buzzers_ds","any_ds4","any_2buzzers_ds","any_ds8","custom"};
static uint32_t le32(const byte *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int bounded(uint64_t off,uint64_t n,uint64_t size){return off<=size&&n<=size-off;}
typedef struct {uint32_t lba,size;int dir;unsigned len;char name[256];} record;
typedef struct {bb_io file;uint64_t size;record root;} iso;
static int readat(iso *i,uint64_t off,void *p,size_t n){return bb_io_read(&i->file,off,p,n);}
static int rec(const byte *p,size_t n,size_t at,uint64_t size,record *r){
 if(at>=n)return -1;unsigned len=p[at];if(len<34||len>n-at||at%2048+len>2048)return -1;p+=at;
 uint32_t lba=le32(p+2),length=le32(p+10);unsigned names=p[32];
 if(lba!=bb_be32(p+6)||length!=bb_be32(p+14)||33+names>len||(p[25]&0x80)||p[26]||p[27]||!bounded((uint64_t)lba*2048,length,size))return -1;
 r->lba=lba;r->size=length;r->dir=!!(p[25]&2);r->len=len;
 unsigned j=0;for(;j<names&&p[33+j]!=';';j++){unsigned c=p[33+j];if(c>=128)return -1;r->name[j]=(char)toupper((int)c);}while(j&&r->name[j-1]=='.')j--;r->name[j]=0;return 0;
}
static int find_child(iso *i,const record *dir,const char *name,int want_dir,record *out){
 if(!dir->dir||dir->size>16*1024*1024)return -1;byte *data=malloc(dir->size?dir->size:1);if(!data)return -1;if(readat(i,(uint64_t)dir->lba*2048,data,dir->size)){free(data);return -1;}
 int hits=0,rc=0;size_t at=0;while(at<dir->size){if(!data[at]){at=(at/2048+1)*2048;continue;}record r;if(rec(data,dir->size,at,i->size,&r)){rc=-1;break;}at+=r.len;if(!strcmp(r.name,name)&&r.dir==want_dir){*out=r;hits++;}}
 free(data);return rc||hits>1?-1:hits;
}
static int known_sha(const char *h){
 const char *const known[]={"cbae284db35baceeaaed642351df99817ec47586","80ebc1a552dffaf5640714b69bac4cbc173e3d22","4a9d0d25b523f72f1be111bf9e0a8d88b96db873",
#ifdef BB_TEST_FIXTURES
#include "test_iso_hashes.h"
#endif
 NULL};for(unsigned i=0;known[i];i++)if(!strcmp(known[i],h))return 1;return 0;
}
static size_t magic_offset(const byte *p,size_t n,int *count){size_t off=0;*count=0;for(size_t j=0;j+8<=n;j++)if(!memcmp(p+j,"BBZCFG01",8)){off=j;(*count)++;}return off;}
int bb_iso_inspect(const char *path,bb_iso_info *info,char *error,size_t cap){
 memset(info,0,sizeof(*info));iso i={0};if(bb_io_open(&i.file,path,0))return bb_error(error,cap,"Cannot open ISO. Use an extracted, readable .iso file.");int rc=-1;byte pvd[2048];byte *module=NULL;
 i.size=i.file.size;info->image_size=i.size;info->mtime=i.file.mtime;
 if(readat(&i,16*2048,pvd,2048)||memcmp(pvd,"\1CD001\1",7)||rec(pvd,2048,156,i.size,&i.root)||!i.root.dir)goto finish;
 memcpy(info->volume,pvd+40,32);info->volume[32]=0;for(int k=31;k>=0&&info->volume[k]==' ';k--)info->volume[k]=0;
 /* Cheap title ID from root filename; no executable or game code read. */
 {if(i.root.size>16*1024*1024)goto finish;byte *root=malloc(i.root.size?i.root.size:1);if(!root)goto finish;if(readat(&i,(uint64_t)i.root.lba*2048,root,i.root.size)){free(root);goto finish;}
  size_t at=0;while(at<i.root.size){if(!root[at]){at=(at/2048+1)*2048;continue;}record r;if(rec(root,i.root.size,at,i.size,&r)){free(root);goto finish;}at+=r.len;if(!r.dir&&strlen(r.name)==11&&r.name[4]=='_'&&r.name[8]=='.')snprintf(info->title_id,sizeof(info->title_id),"%.11s",r.name);}free(root);}
 int hits=0;record target={0};for(int p=0;p<2;p++){record dir,r;int found=find_child(&i,&i.root,p?"SYSTEM":"IOP",1,&dir);if(found<0)goto finish;if(!found)continue;found=find_child(&i,&dir,"USBD.IRX",0,&r);if(found<0)goto finish;if(found){hits++;target=r;info->path_index=p;}}
 if(hits!=1||!target.size||target.size>2*1024*1024)goto finish;
 info->offset=(uint64_t)target.lba*2048;info->length=target.size;module=malloc(target.size);if(!module||readat(&i,info->offset,module,target.size))goto finish;
 byte hash[20];bb_sha1(module,target.size,hash);bb_hex(hash,20,info->sha1);info->known=known_sha(info->sha1);int count;magic_offset(module,target.size,&count);info->patched=count>0;
 snprintf(error,cap,"%s %s\n%s: %u bytes\nSHA1 %s\n%s",info->volume,info->title_id,paths[info->path_index],info->length,info->sha1,info->patched?"Already patched. Keep stock netemu; use Restore before the no-ISO-patch route.":info->known?"Known driver. Check only: no writes.":"Unknown driver. Patching refused; no force override.");rc=0;
finish:free(module);bb_io_close(&i.file);if(rc)bb_error(error,cap,"Unexpected ISO9660 layout: need one unambiguous /IOP/USBD.IRX or /SYSTEM/USBD.IRX, plain 2048-byte sectors, bounded single extents.");return rc;
}
int bb_iso_config(byte *p,size_t n,const bb_iso_options *o){
 int count;size_t off=magic_offset(p,n,&count);if(count!=1||n-off<16||p[off+8]!=1||o->layout>13||o->flags>255||o->attach_ms>60000||o->heartbeat_ms>60000)return -1;
 unsigned packed=0;if(o->layout==13){unsigned players=2*o->pairs+o->whole+2*o->shared;if(o->pairs>2||o->whole>8||o->shared>4||players<1||players>8)return -1;packed=o->pairs+3*o->whole+27*o->shared;}
 unsigned flags=o->flags;if(o->layout>=5&&!(flags&0x30))flags|=0x10;
 if((flags&8)&&(o->layout<5||o->layout>=9||(flags&6)))return -1;
 p[off+9]=(byte)o->layout;p[off+10]=(byte)flags;p[off+11]=(byte)packed;p[off+12]=(byte)o->attach_ms;p[off+13]=(byte)(o->attach_ms>>8);p[off+14]=(byte)o->heartbeat_ms;p[off+15]=(byte)(o->heartbeat_ms>>8);return 0;
}
static int extent_read(const char *path,const bb_iso_info *info,byte **out){
 bb_io f;if(bb_io_open(&f,path,0))return -1;byte *p=malloc(info->length);int rc=!p||bb_io_read(&f,info->offset,p,info->length);if(bb_io_close(&f))rc=-1;if(rc){free(p);return -1;}*out=p;return 0;
}
static int extent_write(const char *path,const bb_iso_info *i,const byte *before,const byte *after){
 bb_io f;if(bb_io_open(&f,path,1))return -2;byte *check=malloc(i->length);int rc=-2,started=0;if(!check)goto finish;
 if(f.size!=i->image_size||bb_io_read(&f,i->offset,check,i->length)||memcmp(check,before,i->length))goto finish;
 started=1;rc=-1;
 if(bb_io_write(&f,i->offset,after,i->length)||bb_io_sync(&f))goto finish;
 if(bb_io_read(&f,i->offset,check,i->length)||memcmp(check,after,i->length))goto finish;rc=0;
finish:free(check);if(bb_io_close(&f))rc=started?-1:-2;return rc;
}
static int sidepaths(const char *path,char orig[1024],char patched[1024],char side[1024]){if(strlen(path)>960)return -1;snprintf(orig,1024,"%s.usbd.orig",path);snprintf(patched,1024,"%s.usbd.buzz",path);snprintf(side,1024,"%s.bbps3.json",path);return 0;}
static void hash256(const byte *p,size_t n,char h[65]){byte d[32];bb_sha256(p,n,d);bb_hex(d,32,h);}
int bb_iso_patch(const char *path,const char *mod,const bb_iso_options *o,int check_only,char *error,size_t cap){
 bb_iso_info i;if(bb_iso_inspect(path,&i,error,cap))return -1;if(i.patched||!i.known)return bb_error(error,cap,i.patched?"ISO already patched. Restore first. Do not combine with modified netemu.":"Unknown USBD.IRX SHA1; patch refused.");
 byte *m=NULL,*orig=NULL,*patched=NULL;size_t mn;int rc=-1;char op[1024],pp[1024],sp[1024],side[1200],oh[65],ph[65];
 if(sidepaths(path,op,pp,sp)||bb_read(mod,&m,&mn,2*1024*1024)||!bb_hash_is(m,mn,BB_MODULE_HASH)||mn>i.length||bb_iso_config(m,mn,o)){bb_error(error,cap,"Bundled module invalid, configuration invalid, or module does not fit original file extent. No relocation supported.");goto finish;}
 if(check_only){snprintf(error,cap,"%s %s\nCheck only passed: %s, layout %u (%s).\nOnly %u bytes would change; stock netemu required. No writes.",i.volume,i.title_id,paths[i.path_index],o->layout,bb_layout_names[o->layout],i.length);rc=0;goto finish;}
 if(extent_read(path,&i,&orig))goto io_fail;hash256(orig,i.length,oh);if(!known_sha(i.sha1))goto io_fail;
 patched=calloc(1,i.length);if(!patched)goto io_fail;memcpy(patched,m,mn);hash256(patched,i.length,ph);
 snprintf(side,sizeof(side),"{\n\"format\":\"BBISO1\",\n\"image_size\":%llu,\n\"mtime\":%lld,\n\"offset\":%llu,\n\"length\":%u,\n\"path_index\":%d,\n\"layout\":%u,\n\"original_sha1\":\"%s\",\n\"original_sha256\":\"%s\",\n\"patched_sha256\":\"%s\"\n}\n",(unsigned long long)i.image_size,(long long)i.mtime,(unsigned long long)i.offset,i.length,i.path_index,o->layout,i.sha1,oh,ph);
 /* Preserve the first original extent. After a clean restore, generated
  * expected bytes/metadata may be regenerated for another layout while the
  * ISO is still original. This also recovers interrupted sidecar staging. */
 int reuse=0;
 {struct stat st;if(!stat(op,&st)){
   char h[65];if(bb_file_hash(op,h)||strcmp(h,oh)){bb_error(error,cap,"Existing original-extent backup differs or is corrupt. It will not be overwritten. Keep it for inspection.");goto finish;}reuse=1;
  }else if(!stat(pp,&st)||!stat(sp,&st)){bb_error(error,cap,"Orphan recovery metadata exists without its original backup. Keep it for inspection; patch refused.");goto finish;}}
 if(bb_tick)bb_tick("Saving and verifying small ISO recovery files");
 if((!reuse&&bb_save(op,orig,i.length,1))||bb_save(pp,patched,i.length,!reuse)||bb_save(sp,side,strlen(side),!reuse))goto io_fail;
 {char h[65];byte *s=NULL;size_t n;if(bb_file_hash(op,h)||strcmp(h,oh)||bb_file_hash(pp,h)||strcmp(h,ph)||bb_read(sp,&s,&n,4096))goto io_fail;int bad=n!=strlen(side)||memcmp(s,side,n);free(s);if(bad)goto io_fail;}
 /* Recheck layout, size and original extent after sidecar writes. */
 {bb_iso_info again;if(bb_iso_inspect(path,&again,error,cap)||again.image_size!=i.image_size||again.offset!=i.offset||again.length!=i.length||strcmp(again.sha1,i.sha1))goto io_fail;}
 if(bb_tick)bb_tick("Writing only the module extent and verifying read-back");
 int write_result=extent_write(path,&i,orig,patched);
 if(write_result){
  if(write_result==-2){bb_error(error,cap,"ISO changed before writing, or write access unavailable. No overwrite attempted; recovery files retained.");goto finish;}
  byte *cur=NULL;int restored=!extent_read(path,&i,&cur)&&!extent_write(path,&i,cur,orig);free(cur);
  bb_error(error,cap,restored?"ISO write failed. Original extent automatically restored and verified; recovery files retained.":"ISO write/automatic restore failed. Keep all sidecars; use Restore before playing.");goto finish;
 }
 snprintf(error,cap,"Patched %s %s in place: %s (layout %u).\nSmall recovery files are beside the ISO. Keep stock netemu; do not combine routes.",i.volume,i.title_id,bb_layout_names[o->layout],o->layout);rc=0;goto finish;
io_fail:bb_error(error,cap,"ISO or recovery I/O/verification failed. No ISO write attempted; retain existing sidecars.");
finish:free(m);free(orig);free(patched);return rc;
}
int bb_iso_restore(const char *path,int check_only,char *error,size_t cap){
 bb_iso_info i;if(bb_iso_inspect(path,&i,error,cap))return -1;char op[1024],pp[1024],sp[1024],oh[65],ph[65],sha1[41];byte *s=NULL,*orig=NULL,*patched=NULL,*cur=NULL;size_t sn,on,pn;int rc=-1;
 if(sidepaths(path,op,pp,sp)||bb_read(sp,&s,&sn,4096)||bb_read(op,&orig,&on,2*1024*1024)||bb_read(pp,&patched,&pn,2*1024*1024))goto failed;
 unsigned long long size,off;long long mtime;unsigned len,pi,layout;int used=0;
 if(sscanf((char*)s,"{\n\"format\":\"BBISO1\",\n\"image_size\":%llu,\n\"mtime\":%lld,\n\"offset\":%llu,\n\"length\":%u,\n\"path_index\":%u,\n\"layout\":%u,\n\"original_sha1\":\"%40[0-9a-f]\",\n\"original_sha256\":\"%64[0-9a-f]\",\n\"patched_sha256\":\"%64[0-9a-f]\"\n}\n%n",&size,&mtime,&off,&len,&pi,&layout,sha1,oh,ph,&used)!=9||(size_t)used!=sn||size!=i.image_size||off!=i.offset||len!=i.length||pi!=(unsigned)i.path_index||on!=len||pn!=len||layout>13||strlen(oh)!=64||strlen(ph)!=64||!bb_hash_is(orig,on,oh)||!bb_hash_is(patched,pn,ph))goto failed;
 {byte h[20];char hs[41];bb_sha1(orig,on,h);bb_hex(h,20,hs);if(strcmp(hs,sha1)||!known_sha(hs))goto failed;}
 if(extent_read(path,&i,&cur))goto failed;
 for(size_t j=0;j<on;j++)if(cur[j]!=orig[j]&&cur[j]!=patched[j])goto failed;
 if(!check_only&&memcmp(cur,orig,on)&&extent_write(path,&i,cur,orig))goto failed;
 snprintf(error,cap,check_only?"Restore check passed. No writes. Original mtime: %lld; recovery files verified.":"Original USBD extent restored and read-back verified. Recovery files retained. Original mtime: %lld.",mtime);rc=0;goto finish;
failed:bb_error(error,cap,"Restore refused or failed: missing/corrupt sidecars, changed image layout, unrelated extent bytes, or I/O error. Keep recovery files.");
finish:free(s);free(orig);free(patched);free(cur);return rc;
}
