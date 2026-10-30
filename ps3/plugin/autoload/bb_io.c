/* SPDX-License-Identifier: MIT */
#include "bb_io.h"
#include <string.h>
#ifdef __lv2ppu__
#include <sys/file.h>
#include "app_health.h"
int bb_io_open(bb_io *f,const char *p,int writable){
 memset(f,0,sizeof(*f));f->fd=-1;sysFSStat st;
 if(sysLv2FsOpen(p,writable?SYS_O_RDWR:SYS_O_RDONLY,&f->fd,0,NULL,0))return -1;
 atomic_fetch_add(&app_files,1);
 if(sysLv2FsFStat(f->fd,&st)){sysLv2FsClose(f->fd);atomic_fetch_sub(&app_files,1);f->fd=-1;return -1;}f->size=st.st_size;f->mtime=(int64_t)st.st_mtime;return 0;
}
int bb_io_read(bb_io *f,uint64_t off,void *p,size_t n){uint64_t at,got;return off>f->size||n>f->size-off||sysLv2FsLSeek64(f->fd,off,SEEK_SET,&at)||at!=off||sysLv2FsRead(f->fd,p,n,&got)||got!=n?-1:0;}
int bb_io_write(bb_io *f,uint64_t off,const void *p,size_t n){uint64_t at,got;return off>f->size||n>f->size-off||sysLv2FsLSeek64(f->fd,off,SEEK_SET,&at)||at!=off||sysLv2FsWrite(f->fd,p,n,&got)||got!=n?-1:0;}
int bb_io_sync(bb_io *f){return sysLv2FsFsync(f->fd)?-1:0;}
int bb_io_close(bb_io *f){int rc=sysLv2FsClose(f->fd)?-1:0;atomic_fetch_sub(&app_files,1);f->fd=-1;return rc;}
#else
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define seek64 _fseeki64
#define tell64 _ftelli64
#define fileno _fileno
#define fsync _commit
#else
#include <unistd.h>
#define seek64 fseeko
#define tell64 ftello
_Static_assert(sizeof(off_t)>=8,"64-bit ISO I/O required");
#endif
int bb_io_open(bb_io *f,const char *p,int writable){
 memset(f,0,sizeof(*f));f->f=fopen(p,writable?"r+b":"rb");if(!f->f)return -1;
 struct stat st;if(stat(p,&st)||seek64(f->f,0,SEEK_END)){fclose(f->f);f->f=NULL;return -1;}
 int64_t n=tell64(f->f);if(n<0){fclose(f->f);f->f=NULL;return -1;}f->size=(uint64_t)n;f->mtime=st.st_mtime;return 0;
}
int bb_io_read(bb_io *f,uint64_t off,void *p,size_t n){return off>f->size||n>f->size-off||seek64(f->f,(int64_t)off,SEEK_SET)||fread(p,1,n,f->f)!=n?-1:0;}
int bb_io_write(bb_io *f,uint64_t off,const void *p,size_t n){return off>f->size||n>f->size-off||seek64(f->f,(int64_t)off,SEEK_SET)||fwrite(p,1,n,f->f)!=n?-1:0;}
int bb_io_sync(bb_io *f){return fflush(f->f)||fsync(fileno(f->f))?-1:0;}
int bb_io_close(bb_io *f){int rc=fclose(f->f);f->f=NULL;return rc?-1:0;}
#endif
