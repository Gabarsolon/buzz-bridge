/* SPDX-License-Identifier: MIT. Same LV2 calls used by bb_io; no libc stat. */
#include "bb_dir.h"
#include "app_ui.h"
#include <stdio.h>
#include <string.h>
#ifdef BB_DIR_HOST_TEST
#include "dir_mock.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/file.h>
#include "app_health.h"
#endif
int bb_dir_open(bb_dir *d,const char *path)
{
 d->fd=-1;
 if(strlen(path)>=sizeof(d->root)){app_log("directory path too long: %s",path);return -1;}
 strcpy(d->root,path);int rc=sysLv2FsOpenDir(path,&d->fd);
 if(rc){app_log("sysLv2FsOpenDir rc=%08x path=%s",(unsigned)rc,path);return -1;}
#ifndef BB_DIR_HOST_TEST
 atomic_fetch_add(&app_dirs,1);
#endif
 return 0;
}
int bb_dir_read(bb_dir *d,bb_dir_entry *e)
{
 for(;;){
  sysFSDirent de={0};uint64_t got=0;
  int rc=sysLv2FsReadDir(d->fd,&de,&got);
  if(rc){app_log("sysLv2FsReadDir rc=%08x path=%s",(unsigned)rc,d->root);return -1;}
  if(!got)return 0;
  if(!strcmp(de.d_name,".")||!strcmp(de.d_name,".."))continue;
  memset(e,0,sizeof(*e));
  snprintf(e->name,sizeof(e->name),"%.*s",(int)de.d_namlen,de.d_name);
  e->directory=de.d_type==DT_DIR?1:de.d_type==DT_REG?0:-1;
  int n=snprintf(e->path,sizeof(e->path),"%s/%s",d->root,e->name);
  if(n<0||(unsigned)n>=sizeof(e->path)){
   app_log("directory entry path too long: %s/%s",d->root,e->name);return 1;
  }
  e->path_known=1;sysFSStat st={0};rc=sysLv2FsStat(e->path,&st);
  if(rc){app_log("sysLv2FsStat rc=%08x path=%s (entry retained as ?)",(unsigned)rc,e->path);return 1;}
  e->details_known=1;e->size=st.st_size;e->directory=S_ISDIR(st.st_mode);
  return 1;
 }
}
void bb_dir_close(bb_dir *d)
{
 int rc=sysLv2FsCloseDir(d->fd);
 if(rc)app_log("sysLv2FsCloseDir rc=%08x path=%s",(unsigned)rc,d->root);
#ifndef BB_DIR_HOST_TEST
 atomic_fetch_sub(&app_dirs,1);
#endif
 d->fd=-1;
}
int bb_path_exists(const char *path){sysFSStat st;return !sysLv2FsStat(path,&st);}
int bb_iso_name(const char *s)
{
 size_t n=strlen(s);if(n<4)return 0;s+=n-4;
 return s[0]=='.'&&(s[1]=='i'||s[1]=='I')&&(s[2]=='s'||s[2]=='S')&&(s[3]=='o'||s[3]=='O');
}
int bb_dir_visible(const bb_dir_entry *e)
{return !e->details_known||e->directory||bb_iso_name(e->name);}
void bb_dir_label(const bb_dir_entry *e,char *label,unsigned cap)
{
 char size[64]="";
 if(e->details_known&&!e->directory){
  uint64_t unit=e->size>=1073741824ULL?1073741824ULL:1048576ULL;
  /* Integer arithmetic: safe even above 4 GiB, no float or size*10 overflow. */
  snprintf(size,sizeof(size),"[%llu.%llu %s]",(unsigned long long)(e->size/unit),
   (unsigned long long)((e->size%unit)*10/unit),unit==1073741824ULL?"GB":"MB");
 }
 snprintf(label,cap,"%s%s%s%s%s",e->details_known?"":"? ",e->directory==1?"[Folder] ":"",size,*size?" ":"",e->name);
}
