/* Script the production entry-1 app, mocking only UI and platform boundaries. */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#define main buzz_app_main
#include "../ps3/plugin/autoload/app.c"
#undef main
typedef struct {const char *screen;int choice;} step;
static const step *script;
static size_t cursor,length;
static int installs,loads,edits,disabled,initial_disabled,exit_calls,quit,fail_install;
static int disable_writes,disable_unlinks,flushes,syncs,closes,boot_enable;
static FILE *const fake_file=(FILE *)(uintptr_t)1;
#define ROOT {"root",0}
#define EXIT {"root",-1}
#define START(c) {"Start for this session?",c}
#define ENABLE(c) {"Enable Buzz! Bridge at boot?",c}
#define DISABLE(c) {"Disable Buzz! Bridge at boot?",c}
#define BACK CELL_MSGDIALOG_BUTTON_ESCAPE
#define YES CELL_MSGDIALOG_BUTTON_YES
#define NO CELL_MSGDIALOG_BUTTON_NO
static int reply(const char *screen){
 assert(cursor<length);
 if(!strstr(screen,script[cursor].screen)){fprintf(stderr,"step %zu expected %s, got %s\n",cursor,script[cursor].screen,screen);abort();}
 int choice=script[cursor++].choice;
 if(!strcmp(script[cursor-1].screen,"root")&&cursor>1)assert(installs==1&&!loads&&!edits);
 return choice;
}
int app_ui_init(void){return 0;}
void app_ui_exit(int rc){assert(rc==0);exit_calls++;}
int app_exiting(void){return quit;}
void app_log(const char *fmt,...){(void)fmt;}
void app_stage(const char *text){(void)text;}
int app_dialog(const char *text,int question){
 if(question){assert(strstr(text,"Circle: back."));assert(!strstr(text,"Circle: exit"));}
 return reply(text);
}
int bb_main_menu(void){return reply("root");}
int cellSysmoduleLoadModule(int m){(void)m;return 0;}
int cellSysmoduleIsLoaded(int m){(void)m;return 0;}
int cellSysmoduleUnloadModule(int m){(void)m;return 0;}
int netCtlInit(void){return 0;}
int netCtlTerm(void){return 0;}
int netCtlGetInfo(int k,union net_ctl_info *n){(void)k;memset(n,0,sizeof(*n));return -1;}
int buzz_is_hen(void){return 0;}
int flow_stat(const char *p,struct stat *st){(void)st;return !strcmp(p,BUZZ_DISABLE)&&disabled?0:-1;}
int flow_mkdir(const char *p,int mode){(void)p;(void)mode;return 0;}
FILE *flow_fopen(const char *p,const char *mode){assert(!strcmp(p,BUZZ_DISABLE)&&!strcmp(mode,"wb"));disabled=1;disable_writes++;return fake_file;}
int flow_fflush(FILE *f){assert(f==fake_file);flushes++;return 0;}
int flow_fileno(FILE *f){assert(f==fake_file);return 7;}
int flow_fsync(int fd){assert(fd==7);syncs++;return 0;}
int flow_fclose(FILE *f){assert(f==fake_file);closes++;return 0;}
int flow_unlink(const char *p){assert(!strcmp(p,BUZZ_DISABLE)&&disabled);disabled=0;disable_unlinks++;return 0;}
int buzz_install(const char *source,const char *dest,char *msg,size_t cap){
 assert(!strcmp(source,"/dev_hdd0/game/BUZZB0001/USRDIR")&&!strcmp(dest,BUZZ_DIR));
 assert(disabled&&disable_writes==1&&flushes==1&&syncs==1&&closes==1&&!disable_unlinks);
 installs++;snprintf(msg,cap,"fixture install result");return fail_install?-1:0;
}
int buzz_mapi(unsigned op,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
 assert(op==0x47&&a>=1&&a<=6&&b&&c&&!d&&installs==1&&!disabled);return 0;
}
int buzz_load_vsh(unsigned slot,const char *p){assert(slot==1&&!strcmp(p,BUZZ_HELPER)&&!disabled);loads++;return 0;}
int buzz_edit_boot_file(const char *p,int enable,char *msg,size_t cap){
 assert(!strcmp(p,BUZZ_BOOT)&&installs==1);edits++;boot_enable=enable;
 if(!enable)assert(disabled&&disable_writes==2);
 snprintf(msg,cap,"fixture boot edit");return 1;
}
static void run(const step *s,size_t n,int installed_before){
 script=s;length=n;cursor=0;installs=loads=edits=exit_calls=quit=0;
 disable_writes=disable_unlinks=flushes=syncs=closes=0;
 initial_disabled=disabled=installed_before;boot_enable=-1;
 assert(buzz_app_main()==0&&cursor==length&&exit_calls==1);
 assert(installs==1&&disable_writes==(edits&&boot_enable==0?2:1));
 if(!edits&&!loads)assert(disabled==(fail_install?1:initial_disabled));
}
#define RUN(s,d) run(s,sizeof(s)/sizeof(*(s)),d)
int main(void){
 const step start_back[]={ROOT,START(BACK),EXIT};
 const step enable_back[]={ROOT,START(NO),ENABLE(BACK),START(BACK),EXIT};
 const step disable_back[]={ROOT,START(NO),ENABLE(NO),DISABLE(BACK),ENABLE(BACK),START(BACK),EXIT};
 const step repeat[]={ROOT,START(BACK),ROOT,START(NO),ENABLE(BACK),START(BACK),ROOT,START(NO),ENABLE(NO),DISABLE(BACK),ENABLE(BACK),START(BACK),EXIT};
 const step session_start[]={ROOT,START(YES),{"Session helper slot 1",BACK}};
 const step enable_boot[]={ROOT,START(NO),ENABLE(YES),{"Boot helper enabled",BACK}};
 const step disable_boot[]={ROOT,START(NO),ENABLE(NO),DISABLE(YES),{"Disabled.",BACK}};
 const step no_edit_exit[]={ROOT,START(NO),ENABLE(NO),DISABLE(NO)};
 for(int d=0;d<=1;d++){
  RUN(start_back,d);RUN(enable_back,d);RUN(disable_back,d);RUN(repeat,d);
  RUN(session_start,d);assert(loads==1&&!edits);
  RUN(enable_boot,d);assert(edits==1&&boot_enable==1&&!loads&&!disabled);
  RUN(disable_boot,d);assert(edits==1&&boot_enable==0&&!loads&&disabled);
  RUN(no_edit_exit,d);assert(!loads&&!edits);
 }
 fail_install=1;
 const step failed[]={ROOT,{"fixture install result",BACK},ROOT,{"Payload installation failed",BACK},EXIT};
 RUN(failed,0);assert(!loads&&!edits&&disabled);
 puts("PASS entry 1: back from all three dialogs to root, repeated entry installs once, safety-disable ordering/preservation, session Yes and completed boot edits exit, final No unchanged, failed install not retried");
 return 0;
}
