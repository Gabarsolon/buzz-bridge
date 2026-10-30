/* SPDX-License-Identifier: MIT. Same transaction/recovery code on PC/PPU. */
#include "netemu_installer.h"
#include "bb_dir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
static const char *const allowed[]={
#include "netemu_allowlist.h"
 NULL};
int bb_netemu_known(const char *hash){for(unsigned i=0;allowed[i];i++)if(!strcmp(hash,allowed[i]))return 1;return 0;}
int bb_netemu_classify(const char *hash){return !strcmp(hash,BB_STOCK_SELF)?0:bb_netemu_known(hash)?1:2;}
const char *bb_netemu_name(const char *h){
 if(!strcmp(h,BB_STOCK_SELF))return "Original Evilnat 4.93";
 if(!strcmp(h,BB_MODIFIED_SELF))return "modified ps2_netemu (this build)";
 return bb_netemu_known(h)?"older modified build":"UNKNOWN FILE";
}
/* Timestamp sorts independently of hash. Accept the PC installer name and our
 * collision suffix. Partial/corrupt backups fail the hash8 name check. */
static int copy_name(const char *s){
 size_t n=strlen(s);if(n<40||strncmp(s,"ps2_netemu_",11)||s[19]!='_'||strcmp(s+n-5,".self"))return 0;
 for(int i=11;i<19;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
 for(int i=20;i<35;i++)if(i==28?s[i]!='_':s[i]<'0'||s[i]>'9')return 0;
 if(n==40)return 1;if(s[35]!='_'||n==41)return 0;
 for(size_t i=36;i<n-5;i++)if(s[i]<'0'||s[i]>'9')return 0;
 return 1;
}
int bb_netemu_copies(const bb_netemu_paths *p,size_t offset,bb_netemu_copy *out,size_t count,size_t *total,bb_netemu_copy *oldest){
 bb_dir d;*total=0;if(oldest)memset(oldest,0,sizeof(*oldest));
 if(bb_dir_open(&d,p->directory))return -1;
 bb_dir_entry e;int rc,n=0;
 while((rc=bb_dir_read(&d,&e))>0){
  if(e.directory==1||!e.path_known||!copy_name(e.name))continue;
  if(bb_tick)bb_tick("Checking saved netemu backups");
  bb_netemu_copy c;memset(&c,0,sizeof(c));snprintf(c.path,sizeof(c.path),"%s",e.path);snprintf(c.name,sizeof(c.name),"%s",e.name);
  c.valid=!bb_file_hash(c.path,c.hash)&&!strncmp(c.hash,c.name+11,8);
  if(oldest&&c.valid&&bb_netemu_classify(c.hash)!=1&&(!oldest->valid||strcmp(c.name+20,oldest->name+20)<0))*oldest=c;
  if(*total>=offset&&(size_t)n<count)out[n++]=c;
  ++*total;
 }
 bb_dir_close(&d);return rc<0?-1:n;
}
int bb_netemu_status(const bb_netemu_paths *p,char *msg,size_t cap){
 char hash[65];if(bb_tick)bb_tick("Hashing installed netemu");
 if(bb_file_hash(p->current,hash))return bb_error(msg,cap,"Installed netemu could not be read. STATUS does not mount writable flash.");
 snprintf(msg,cap,"Installed: %s\nSHA256: %s\n\nBackups: %s\nChoose the backup list to see every saved file and its hash.\n\nCFW only for replacement. Modified netemu needs original, unpatched ISOs.",bb_netemu_name(hash),hash,p->directory);return 0;
}
static int save_backup(const bb_netemu_paths *p,const unsigned char *data,size_t n,const char *hash,char path[1024]){
 time_t t=time(NULL);struct tm *tm=gmtime(&t);char stamp[32],check[65];
 if(!tm||!strftime(stamp,sizeof(stamp),"%Y%m%d_%H%M%S",tm))return -1;
 for(unsigned serial=0;serial<10000;serial++){
  int len=snprintf(path,1024,"%s/ps2_netemu_%.8s_%s_%04u.self",p->directory,hash,stamp,serial);
  if(len<0||len>=1024)return -1;
  /* O_EXCL is authoritative, even for an unreadable pre-existing backup. */
  if(bb_save(path,data,n,1)){if(errno==EEXIST)continue;return -1;}
  return bb_file_hash(path,check)||strcmp(check,hash)?-1:0;
 }
 return -1;
}
int bb_netemu_replace(const bb_netemu_paths *p,const bb_netemu_ops *ops,const char *source,const char *wanted,const char *confirmed,int unknown_confirmed,char *msg,size_t cap){
 unsigned char *newdata=NULL,*before=NULL;size_t nn=0,bn=0;char hash[65],backup[1024]="";int rc=-1,mounted=0,rolled=0;
 if(!ops||!ops->is_hen||!ops->mount||ops->is_hen())return bb_error(msg,cap,"CFW only. HEN detected or platform check unavailable.");
 if(!confirmed||strlen(confirmed)!=64)return bb_error(msg,cap,"Missing current-file confirmation. Nothing installed.");
 if(bb_tick)bb_tick("Verifying source and installed netemu");
 if(bb_read(source,&newdata,&nn,BB_MAX_FILE)||!bb_hash_is(newdata,nn,wanted)){bb_error(msg,cap,"Source file hash mismatch or unreadable. Nothing installed.");goto finish;}
 if(bb_read(p->current,&before,&bn,BB_MAX_FILE)||!bb_hash_is(before,bn,confirmed)){bb_error(msg,cap,"Installed file changed after confirmation or is unreadable. Nothing installed.");goto finish;}
 if(!strcmp(wanted,confirmed)){snprintf(msg,cap,"That exact netemu hash is already installed. Nothing to do.");rc=0;goto finish;}
 if(!bb_netemu_known(confirmed)&&!unknown_confirmed){bb_error(msg,cap,"Unknown installed file requires the extra OVERWRITE confirmation. Nothing installed.");goto finish;}
 if(bb_tick)bb_tick("Saving current netemu backup and verifying SHA256");
 if(save_backup(p,before,bn,confirmed,backup)){bb_error(msg,cap,"Backup write, sync or hash verification failed. No writable mount attempted. Existing backups were preserved.");goto finish;}
 /* Rollback uses the just-saved HDD backup, reloaded and verified before write.
  * It stays on HDD on every path, including abrupt process/power loss. */
 free(before);before=NULL;
 if(bb_read(backup,&before,&bn,BB_MAX_FILE)||!bb_hash_is(before,bn,confirmed)){bb_error(msg,cap,"Saved backup changed or cannot be read. No writable mount attempted.");goto finish;}
 if(bb_tick)bb_tick("Mounting writable flash through local webMAN");
 mounted=1;if(ops->mount(1,ops->context)){bb_error(msg,cap,"No writable dev_flash path. CFW and webMAN local dev_blind service are required. No flash write attempted.");goto finish;}
 if(bb_file_hash(p->writable,hash)||strcmp(hash,confirmed)){bb_error(msg,cap,"Writable dev_blind target missing or changed after confirmation. No flash write attempted.");goto finish;}
 if(bb_tick)bb_tick("Writing netemu and verifying complete read-back");
 if(bb_save(p->writable,newdata,nn,0)||bb_file_hash(p->writable,hash)||strcmp(hash,wanted))goto rollback;
 if(bb_tick)bb_tick("Unmounting writable flash");
 if(ops->mount(0,ops->context))goto rollback;
 mounted=0;snprintf(msg,cap,"%s installed. SHA256 read-back verified.\nBackup: %s\n\n%s\nUse original, unpatched ISOs with modified netemu.",bb_netemu_name(wanted),backup,!strcmp(wanted,BB_STOCK_SELF)?"Done. The original is back.":"Done. You can start your PS2 game now.");rc=0;goto finish;
rollback:
 if(bb_tick)bb_tick("Replacement failed; restoring the just-saved backup automatically");
 /* A failed unmount reply may follow an actual unmount. Always remount; even
  * on a failed reply try recovery on an accessible dev_blind. */
 (void)ops->mount(1,ops->context);
 rolled=!bb_save(p->writable,before,bn,0)&&!bb_file_hash(p->writable,hash)&&!strcmp(hash,confirmed);
 snprintf(msg,cap,"%s\nBackup: %s",rolled?"Replacement/read-back/unmount failed. Previous netemu automatically restored and SHA256 read-back verified.":"Replacement AND automatic restore failed. Do not start a PS2 game. Recover from the saved HDD backup with your file manager or reinstall firmware.",backup);
finish:
 if(mounted){
  if(bb_tick)bb_tick("Unmounting writable flash after error");
  if(ops->mount(0,ops->context)){size_t len=strlen(msg);if(len<cap)snprintf(msg+len,cap-len,"\nUnmount failed.");rc=-1;}
 }
 free(before);free(newdata);return rc;
}
