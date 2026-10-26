/* SPDX-License-Identifier: MIT. Actual production logic/editor, no PS3 mocks. */
#include "../autoload/logic.h"
#include "../autoload/files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
static int reject_replace;
int buzz_host_replace(const char *src,const char *dst)
{return reject_replace?0:MoveFileExA(src,dst,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
#endif
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);}while(0)
static void put(const char *p,const char *s) {FILE *f=fopen(p,"wb");assert(f);assert(fwrite(s,1,strlen(s),f)==strlen(s));assert(!fclose(f));}
static int equal(const char *p,const char *s) {char b[18000];size_t n;int present;return !buzz_file_read(p,b,sizeof(b),&n,&present)&&present&&n==strlen(s)&&!memcmp(b,s,n);}
int main(int argc,char **argv)
{
 CHECK(argc==2);
 CHECK(buzz_process_match(0x1010200,"main_EBOOT.BIN","BCES00347"));
 CHECK(buzz_process_match(0x1010200,"main_EBOOT.BIN","BCES00303"));
 CHECK(buzz_process_match(0x1010200,"main_EBOOT.BIN","BCES00644"));
 CHECK(buzz_process_match(0x1030200,"01030200_main_EBOOT.BIN","BCES00347"));
 CHECK(!buzz_process_match(0x1000300,"main_EBOOT.BIN","BCES00347"));
 CHECK(!buzz_process_match(0,"main_EBOOT.BIN","BCES00347"));
 CHECK(!buzz_process_match(0x1010200,"main_EBOOT.BIN","BCES00348"));
 CHECK(!buzz_process_match(0x1010200,"main_EBOOT.BIN","BCES00347x"));
 CHECK(!buzz_process_match(0x1010200,"other_EBOOT.BIN","BCES00347"));
 CHECK(!buzz_process_match(0x1010200,"zzzzzzzz_main_EBOOT.BIN","BCES00347"));
 CHECK(!buzz_process_match(0x1010200,"01030200_main_EBOOT.BIN","BCES00347"));
 CHECK(!buzz_process_match(0x1010200,"main_vsh.self","BCES00347"));
 CHECK(!buzz_process_match(0x1010200,NULL,"BCES00347"));
 buzz_attempt a={.pid=0x1010200};
 CHECK(!buzz_should_attempt(&a,1,0));CHECK(!buzz_should_attempt(&a,1,14999999));
 CHECK(buzz_should_attempt(&a,1,15000000));
 CHECK(!buzz_should_attempt(&a,0,16000000));CHECK(!a.waiting);
 CHECK(!buzz_should_attempt(&a,1,70000000));CHECK(!buzz_should_attempt(&a,1,84999999));
 CHECK(buzz_should_attempt(&a,1,85000000));buzz_mark_attempt(&a);
 CHECK(!buzz_should_attempt(&a,1,UINT64_C(86400000000)));
 CHECK(!buzz_should_attempt(&a,0,0));CHECK(!buzz_should_attempt(&a,1,0));
 buzz_attempt fresh={.pid=0x1030200};CHECK(!buzz_should_attempt(&fresh,1,100000));CHECK(buzz_should_attempt(&fresh,1,15100000));
 fresh.ready_since=1000;fresh.waiting=1;CHECK(!buzz_should_attempt(&fresh,1,0));
 CHECK(buzz_crc32((const unsigned char *)"123456789",9)==0xcbf43926);
 char out[18000];size_t n;
 const char *unrelated="# keep comment\r\n/dev_hdd0/webftp_server.sprx\r\n\r\n";
 CHECK(buzz_boot_edit(unrelated,strlen(unrelated),1,out,sizeof(out),&n)==1);
 CHECK(n==strlen(unrelated)+strlen(BUZZ_HELPER)+2);CHECK(!memcmp(out,unrelated,strlen(unrelated)));
 char enabled[18000];memcpy(enabled,out,n);size_t enabled_n=n;
 CHECK(buzz_boot_edit(enabled,n,1,out,sizeof(out),&n)==0);
 CHECK(buzz_boot_edit(enabled,enabled_n,0,out,sizeof(out),&n)==1);CHECK(n==strlen(unrelated)&&!memcmp(out,unrelated,n));
 const char *duplicate="/other\n " BUZZ_HELPER " \r\n" BUZZ_HELPER "\n" BUZZ_HELPER ".extra\n";
 CHECK(buzz_boot_edit(duplicate,strlen(duplicate),0,out,sizeof(out),&n)==1);
 CHECK(n==strlen("/other\n" BUZZ_HELPER ".extra\n"));CHECK(!memcmp(out,"/other\n" BUZZ_HELPER ".extra\n",n));
 CHECK(buzz_boot_edit("/other",6,1,out,sizeof(out),&n)==1);CHECK(!memcmp(out,"/other\n",7));
 CHECK(buzz_boot_edit("x\0y",3,1,out,sizeof(out),&n)==-1);
 CHECK(buzz_boot_edit("x\ry",3,1,out,sizeof(out),&n)==-1);
 CHECK(buzz_boot_edit("",0,1,out,1,&n)==-1);
 memset(enabled,'x',BUZZ_BOOT_MAX);CHECK(buzz_boot_edit(enabled,BUZZ_BOOT_MAX,1,out,sizeof(out),&n)==-1);
 char path[640],backup[700],temp[700],err[700];
 snprintf(path,sizeof(path),"%s/boot_plugins.txt",argv[1]);snprintf(backup,sizeof(backup),"%s.ps3buzz.bak",path);snprintf(temp,sizeof(temp),"%s.ps3buzz.new",path);
 put(path,unrelated);
 CHECK(buzz_edit_boot_file(path,1,err,sizeof(err))==1);CHECK(equal(backup,unrelated));
 size_t current_n;int present;CHECK(!buzz_file_read(path,enabled,sizeof(enabled),&current_n,&present));enabled[current_n]=0;
 CHECK(buzz_edit_boot_file(path,1,err,sizeof(err))==0);CHECK(equal(backup,unrelated));
 CHECK(buzz_edit_boot_file(path,0,err,sizeof(err))==1);CHECK(equal(path,unrelated));CHECK(equal(backup,enabled));
 CHECK(buzz_edit_boot_file(path,0,err,sizeof(err))==0);
#ifdef _WIN32
 reject_replace=1;CHECK(buzz_edit_boot_file(path,1,err,sizeof(err))==-1);CHECK(equal(path,unrelated));CHECK(equal(backup,unrelated));reject_replace=0;
#endif
 put(temp,"stale");CHECK(buzz_edit_boot_file(path,1,err,sizeof(err))==-1);CHECK(equal(path,unrelated));remove(temp);
 put(path,"bad\rlist");CHECK(buzz_edit_boot_file(path,1,err,sizeof(err))==-1);CHECK(equal(path,"bad\rlist"));
 remove(path);CHECK(buzz_edit_boot_file(path,0,err,sizeof(err))==0);CHECK(buzz_edit_boot_file(path,1,err,sizeof(err))==1);CHECK(equal(backup,""));
 printf("PASS: %u autoload assertions (matching, continuous settle, no retry, boot bytes/backups/idempotency/errors)\n",checks);return 0;
}
