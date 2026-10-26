/* SPDX-License-Identifier: MIT. Minimal file-only probe; no library imports. */
#include <sys/prx_module.h>
#include <sys/file.h>

#ifndef DIAG_VARIANT
#error DIAG_VARIANT must be a one-letter string
#endif

__attribute__((noinline)) int module_start(size_t args,void *argp)
{
 static const char name[]=DIAG_VARIANT "\n";
 int fd=-1; uint64_t written=0;
 (void)args; (void)argp;
 if(!sysLv2FsOpen("/dev_hdd0/tmp/ps3buzz_diag_" DIAG_VARIANT ".txt",
                 SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fd,0666,NULL,0)) {
  (void)sysLv2FsWrite(fd,name,sizeof(name)-1,&written);
  (void)sysLv2FsFsync(fd); (void)sysLv2FsClose(fd);
 }
 return SYS_PRX_NO_RESIDENT;
}

/* Cobra calls its management entries as bare thread entries and ignores the
 * exit code (modules_c.c). Keep module_start's file-only/return-1 contract,
 * wrap it in the required thread exit, and expose a matching empty stop entry.
 * These Cobra-specific probes are explicitly unloaded after checking a marker.
 * They cannot auto-unload merely by returning 1 on this loader. */
void diag_cobra_start(void *arg)
{
 int result=module_start(0,arg);
 lv2syscall1(41,(uint64_t)result);
 __builtin_unreachable();
}
void diag_cobra_stop(void *arg)
{
 (void)arg;
 lv2syscall1(41,0);
 __builtin_unreachable();
}
