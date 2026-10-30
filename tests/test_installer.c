/* Production transaction with fault-injected I/O, only inside a PC fake root. */
#include "netemu_installer.h"
#include "bb_dir.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <setjmp.h>
#include <time.h>
static int injected_save(const char *,const void *,size_t,int);
static int injected_hash(const char *,char [65]);
static time_t fixed_time(time_t *v){time_t t=1793404800;if(v)*v=t;return t;}
#define bb_save injected_save
#define bb_file_hash injected_hash
#define time fixed_time
#include "../ps3/plugin/autoload/netemu_installer.c"
#undef time
#undef bb_file_hash
#undef bb_save
static int scenario,hen,mounts,unmounts,writes,backup_writes,unmount_fail;
static const char *target;static char backup_path[1024];static jmp_buf power_cut;
static int is_hen(void){return hen;}
static int mount_mock(int on,void *ctx){
 (void)ctx;if(on){mounts++;if(scenario==5&&mounts==1)assert(!bb_save(target,"race",4,0));return scenario==2?-1:0;}
 unmounts++;if(unmount_fail){unmount_fail--;return -1;}return 0;
}
static int injected_save(const char *p,const void *data,size_t n,int exclusive){
 if(exclusive){
  int existed=GetFileAttributesA(p)!=INVALID_FILE_ATTRIBUTES;
  if(!existed){backup_writes++;snprintf(backup_path,sizeof(backup_path),"%s",p);}
  if(!existed&&scenario==8){assert(!bb_save(p,data,n/2,1));errno=EIO;return -1;}
 }
 if(!strcmp(p,target)&&!exclusive){
  writes++;
  if((writes==1&&(scenario==3||scenario==4||scenario==10))||scenario==11){
   assert(!bb_save(p,data,n/2,0));if(scenario==10)longjmp(power_cut,1);
   return scenario==4?0:-1; /* mismatch pretends a successful short write */
  }
 }
 return bb_save(p,data,n,exclusive);
}
static int injected_hash(const char *p,char h[65]){
 int rc=bb_file_hash(p,h);if(!rc&&scenario==9&&!strcmp(p,backup_path))h[0]=h[0]=='0'?'1':'0';return rc;
}
static HANDLE listing;static WIN32_FIND_DATAA entry;static int first;
int bb_dir_open(bb_dir *d,const char *p){
 char pattern[1100];snprintf(pattern,sizeof(pattern),"%s/*",p);snprintf(d->root,sizeof(d->root),"%s",p);
 listing=FindFirstFileA(pattern,&entry);first=1;return listing==INVALID_HANDLE_VALUE?-1:0;
}
int bb_dir_read(bb_dir *d,bb_dir_entry *e){
 do {if(!first&&!FindNextFileA(listing,&entry))return GetLastError()==ERROR_NO_MORE_FILES?0:-1;first=0;}while(!strcmp(entry.cFileName,".")||!strcmp(entry.cFileName,".."));
 memset(e,0,sizeof(*e));snprintf(e->name,sizeof(e->name),"%s",entry.cFileName);snprintf(e->path,sizeof(e->path),"%s/%s",d->root,e->name);e->path_known=e->details_known=1;e->directory=!!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);return 1;
}
void bb_dir_close(bb_dir *d){(void)d;assert(FindClose(listing));}
static void equal_hash(const char *p,const char *h){char actual[65];assert(!bb_file_hash(p,actual));assert(!strcmp(actual,h));}
static void copy(const char *src,const char *dst){unsigned char *p;size_t n;assert(!bb_read(src,&p,&n,BB_MAX_FILE));assert(!bb_save(dst,p,n,1));free(p);}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);assert(argc==4);const char *root=argv[1],*original=argv[2],*patched=argv[3];
 equal_hash(original,BB_STOCK_SELF);equal_hash(patched,BB_MODIFIED_SELF);
 assert(bb_netemu_classify(BB_STOCK_SELF)==0&&bb_netemu_classify(BB_MODIFIED_SELF)==1&&bb_netemu_classify("unknown")==2);
 assert(!strcmp(bb_netemu_name(BB_MODIFIED_SELF),"modified ps2_netemu (this build)"));
 assert(strstr(bb_netemu_name(BB_STOCK_SELF),"Evilnat 4.93"));
 for(unsigned i=0;allowed[i];i++){
  assert(bb_netemu_known(allowed[i]));
  if(strcmp(allowed[i],BB_STOCK_SELF)&&strcmp(allowed[i],BB_MODIFIED_SELF)){
   assert(bb_netemu_classify(allowed[i])==1);
   assert(!strcmp(bb_netemu_name(allowed[i]),"older modified build"));
  }
 }
 char dir[1024],current[1024],msg[2400],h[65],kept[1024];
 for(scenario=0;scenario<20;scenario++){
  snprintf(dir,sizeof(dir),"%s/case-%d",root,scenario);assert(!_mkdir(dir));snprintf(current,sizeof(current),"%s/current.self",dir);copy(original,current);
  bb_netemu_paths paths={current,current,dir};bb_netemu_ops ops={is_hen,mount_mock,NULL};
  hen=mounts=unmounts=writes=backup_writes=unmount_fail=0;backup_path[0]=0;target=current;
  if(scenario==1)hen=1;if(scenario==6)unmount_fail=1;if(scenario==12)unmount_fail=100;
  if(scenario==13||scenario==14)assert(!bb_save(current,"unknown original firmware",25,0));
  assert(!bb_file_hash(current,h));
  if(scenario==15)strcpy(h,BB_MODIFIED_SELF); /* stale displayed current hash */
  if(scenario==17)paths.writable="missing-writable-path/current.self";
  int rc;
  if(scenario==10&&setjmp(power_cut)){
   /* Simulated abrupt loss: no cleanup/rollback code got to run. The full HDD
    * backup persists. Recover on the next run through the normal restore path. */
   assert(writes==1&&unmounts==0);equal_hash(backup_path,BB_STOCK_SELF);
   char partial[65];assert(!bb_file_hash(current,partial));assert(strcmp(partial,BB_STOCK_SELF));
   snprintf(kept,sizeof(kept),"%s",backup_path);scenario=19; /* disable failure injection */
   assert(!bb_netemu_replace(&paths,&ops,kept,BB_STOCK_SELF,partial,1,msg,sizeof(msg)));
   equal_hash(current,BB_STOCK_SELF);equal_hash(kept,BB_STOCK_SELF);scenario=10;
   puts("PASS power-cut: partial target, intact backup, verified restore on next run");continue;
  }
  rc=bb_netemu_replace(&paths,&ops,patched,scenario==16?BB_STOCK_SELF:BB_MODIFIED_SELF,scenario==18?BB_STOCK_SELF:h,scenario==14,msg,sizeof(msg));
  if(scenario==18){/* test same-target no-op separately */assert(!rc);int before=mounts;
   assert(!bb_netemu_replace(&paths,&ops,patched,BB_MODIFIED_SELF,BB_MODIFIED_SELF,0,msg,sizeof(msg)));assert(mounts==before&&strstr(msg,"Nothing to do"));}
  else if(scenario==0||scenario==7||scenario==14||scenario==19){
   assert(!rc&&mounts==1&&unmounts==1);equal_hash(current,BB_MODIFIED_SELF);equal_hash(backup_path,h);
   snprintf(kept,sizeof(kept),"%s",backup_path);
   assert(!bb_netemu_replace(&paths,&ops,kept,h,BB_MODIFIED_SELF,0,msg,sizeof(msg)));equal_hash(current,h);equal_hash(kept,h);
   if(scenario==7){ /* frozen timestamp: two snapshots with same hash must differ */
    assert(!bb_netemu_replace(&paths,&ops,patched,BB_MODIFIED_SELF,h,1,msg,sizeof(msg)));assert(strcmp(backup_path,kept));equal_hash(kept,h);
   }
  }else{
   assert(rc);
   if(scenario==1||scenario==8||scenario==9||scenario==13||scenario==15||scenario==16)assert(!mounts&&!unmounts&&!writes);
   if(scenario==2||scenario==17)assert(mounts==1&&unmounts==1&&!writes);
   if(scenario==3||scenario==4||scenario==6||scenario==12){equal_hash(current,h);assert(strstr(msg,"automatically restored"));assert(mounts==2&&unmounts==((scenario==6||scenario==12)?2:1)&&writes==2);}
   if(scenario==11){assert(strstr(msg,"automatic restore failed"));equal_hash(backup_path,h);assert(unmounts==1);}
   if(scenario==5){assert(!writes&&unmounts==1);equal_hash(backup_path,h);}
  }
  printf("PASS transaction scenario %d: %s\n",scenario,msg);
 }
 /* Backup selection: unknown original beats later known stock; older modified
  * and partial backup are excluded. Pagination retains every candidate. */
 snprintf(dir,sizeof(dir),"%s/selection",root);assert(!_mkdir(dir));bb_netemu_paths paths={current,current,dir};
 snprintf(kept,sizeof(kept),"%s/ps2_netemu_74e890bf_20200101_000000.self",dir);copy(patched,kept);
 const char *unknown="another firmware original";unsigned char digest[32];char uh[65];bb_sha256(unknown,strlen(unknown),digest);bb_hex(digest,32,uh);
 snprintf(kept,sizeof(kept),"%s/ps2_netemu_%.8s_20210101_000000.self",dir,uh);assert(!bb_save(kept,unknown,strlen(unknown),1));
 snprintf(kept,sizeof(kept),"%s/ps2_netemu_bad3e45c_20220101_000000.self",dir);copy(original,kept);
 snprintf(kept,sizeof(kept),"%s/ps2_netemu_bad3e45c_20190101_000000.self",dir);assert(!bb_save(kept,"partial",7,1));
 bb_netemu_copy oldest,page[2];size_t total;int n=bb_netemu_copies(&paths,0,page,2,&total,&oldest);assert(n==2&&total==4&&oldest.valid&&!strcmp(oldest.hash,uh));
 assert(bb_netemu_copies(&paths,2,page,2,&total,NULL)==2&&total==4);
 assert(bb_netemu_copies(&paths,4,page,2,&total,NULL)==0&&total==4);
 puts("PASS backups: collision preservation, unknown-original oldest selection, modified/partial exclusion, complete pagination");
 return 0;
}
