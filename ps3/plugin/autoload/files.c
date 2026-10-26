/* SPDX-License-Identifier: MIT. Ordinary app/host filesystem; no VSH process CRT. */
#include "logic.h"
#include "files.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define read _read
#define write _write
#define close _close
#define fsync _commit
#define open _open
#define unlink _unlink
#define O_BINARY _O_BINARY
#else
#include <unistd.h>
#define O_BINARY 0
#endif
static int fail(char *msg,size_t cap,const char *why) {snprintf(msg,cap,"%s (errno %d)",why,errno);return -1;}
static int exists(const char *path) {struct stat st;return stat(path,&st)==0;}
int buzz_file_read(const char *path,char *data,size_t cap,size_t *n,int *present)
{
 *n=0;*present=0;int fd=open(path,O_RDONLY|O_BINARY);
 if(fd<0)return errno==ENOENT?0:-1;
 *present=1;
 while(*n<cap) {int got=(int)read(fd,data+*n,(unsigned)(cap-*n));if(got<0){close(fd);return -1;}if(!got)break;*n+=(size_t)got;}
 char extra;int got=(int)read(fd,&extra,1);int rc=close(fd);return got!=0||rc?-1:0;
}
static int write_new(const char *path,const char *data,size_t n)
{
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0666);if(fd<0)return -1;
 size_t at=0;int rc=0;
 while(at<n){int put=(int)write(fd,data+at,(unsigned)(n-at));if(put<=0){rc=-1;break;}at+=(size_t)put;}
 if(!rc && fsync(fd))rc=-1;
 if(close(fd))rc=-1;
 if(rc)unlink(path);return rc;
}
static int same_file(const char *path,const char *data,size_t n,int was_present)
{
 char current[BUZZ_BOOT_MAX+1];size_t size=0;int present;
 return !buzz_file_read(path,current,sizeof(current),&size,&present) && present==was_present && size==n && !memcmp(current,data,n);
}
int buzz_edit_boot_file(const char *path,int enable,char *error,size_t cap)
{
 char original[BUZZ_BOOT_MAX+1],updated[BUZZ_BOOT_MAX+128];size_t n=0,out=0;int present;
 char tmp[640],bak[640],btmp[640],previous[640];
 if(strlen(path)>550)return fail(error,cap,"Boot path too long");
 snprintf(tmp,sizeof(tmp),"%s.ps3buzz.new",path);snprintf(bak,sizeof(bak),"%s.ps3buzz.bak",path);
 snprintf(btmp,sizeof(btmp),"%s.ps3buzz.bak.new",path);snprintf(previous,sizeof(previous),"%s.ps3buzz.bak.previous",path);
 if(buzz_file_read(path,original,sizeof(original),&n,&present))return fail(error,cap,"Cannot read bounded boot list");
 int changed=buzz_boot_edit(original,n,enable,updated,sizeof(updated),&out);
 if(changed<0)return fail(error,cap,"Invalid or oversized boot list; left untouched");
 if(!changed)return 0;
 if(exists(tmp)||exists(btmp))return fail(error,cap,"Unfinished boot transaction; inspect .ps3buzz.new files");
 if(write_new(btmp,original,n))return fail(error,cap,"Cannot stage byte-exact boot backup");
 if(write_new(tmp,updated,out)){unlink(btmp);return fail(error,cap,"Cannot stage updated boot list");}
 if(!same_file(path,original,n,present)){unlink(tmp);unlink(btmp);return fail(error,cap,"Boot list changed concurrently; left untouched");}
 if(exists(bak)) {
  if(exists(previous)&&unlink(previous))goto failed;
  if(rename(bak,previous))goto failed;
 }
 if(rename(btmp,bak))goto failed;
 /* SDK rename wraps LV2 FsRename. Fail without deleting original if replacement is unsupported. */
#ifdef _WIN32
 /* Host Windows C rename refuses existing destinations; use a same-volume replace. */
 {
  extern int buzz_host_replace(const char *,const char *);
  if(!buzz_host_replace(tmp,path))goto failed;
 }
#else
 if(rename(tmp,path))goto failed;
#endif
 return 1;
failed:
 unlink(tmp);unlink(btmp);return fail(error,cap,"Boot transaction failed; original boot list retained; inspect backup");
}
int buzz_install(const char *source,const char *dest,char *error,size_t cap)
{
 static const char *names[]={"ps3buzz.sprx","ps3buzz.cfg","ps3buzz_vsh.sprx"};
 char from[3][640],to[3][640],tmp[3][640],old[3][640];int had[3]={0},committed=0,staged=0;
 char *data=malloc(2*1024*1024);if(!data)return fail(error,cap,"No memory to stage payloads");
 for(int i=0;i<3;i++) {
  snprintf(from[i],640,"%s/%s",source,names[i]);snprintf(to[i],640,"%s/%s",dest,names[i]);
  snprintf(tmp[i],640,"%s/%s.install-new",dest,names[i]);snprintf(old[i],640,"%s/%s.install-old",dest,names[i]);
  if(exists(tmp[i])||exists(old[i])) {fail(error,cap,"Previous payload transaction needs recovery");goto cleanup;}
 }
 for(int i=0;i<3;i++) {
  size_t n;int present;
  if(buzz_file_read(from[i],data,2*1024*1024,&n,&present)||!present||!n) {fail(error,cap,"Packaged payload missing/oversized");goto cleanup;}
  uint32_t crc=buzz_crc32((unsigned char *)data,n);
  if(write_new(tmp[i],data,n)) {fail(error,cap,"Payload staging write failed");goto cleanup;}
  staged++;
  size_t verify;int exists_now;
  if(buzz_file_read(tmp[i],data,2*1024*1024,&verify,&exists_now)||!exists_now||verify!=n||buzz_crc32((unsigned char *)data,n)!=crc) {fail(error,cap,"Payload staging verification failed");goto cleanup;}
 }
 for(int i=0;i<3;i++) {
  had[i]=exists(to[i]);
  if(had[i] && rename(to[i],old[i])) {fail(error,cap,"Cannot preserve installed payload");goto rollback;}
  if(rename(tmp[i],to[i])) {
   if(had[i] && rename(old[i],to[i])) {fail(error,cap,"Restore .install-old manually; rename failed");goto cleanup;}
   fail(error,cap,"Cannot activate staged payload");goto rollback;
  }
  committed++;
 }
 for(int i=0;i<3;i++)if(had[i])unlink(old[i]);
 free(data);return 0;
rollback:
 for(int i=committed-1;i>=0;i--) {
  if(unlink(to[i]) || (had[i] && rename(old[i],to[i]))) {fail(error,cap,"Restore .install-old files manually; rollback failed");break;}
 }
cleanup:
 for(int i=0;i<staged;i++)unlink(tmp[i]);free(data);return -1;
}
