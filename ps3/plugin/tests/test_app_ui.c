/* Actual app_ui.c against deterministic firmware/RSX/time/pad mocks. */
#include "app_mock.h"
#include "../autoload/app_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
CellGcmContextData mock_context;
static const char *scenario;
static uint64_t clock_us;
static uint32_t label;
static unsigned frames,checks,writes,syncs,polls,configurations,queries,registrations,wait_commands,drains,flushes;
static int active,exit_result=-99,pad_ended,configured,resumed;
static unsigned port_polls[7];
static int queued_flip,queued_wait;
static char logs[65536];static size_t used;
static void (*event_cb)(uint64_t,uint64_t,void *);
static void (*guard)(void *);
static jmp_buf jump;
static int is(const char *s){return !strcmp(scenario,s);}
uint64_t sys_time_get_system_time(void){clock_us+=100;return clock_us;}
void sys_timer_usleep(unsigned n){
 static int watchdog_step;
 if(n==250000&&(is("timeout")||is("status_timeout")||is("late_answer")||is("long_ps_menu"))){
  if(watchdog_step){watchdog_step=0;longjmp(jump,1);}watchdog_step=1;exit_result=-77;
 }
 clock_us+=n;
}
void usleep(unsigned n){clock_us+=n;}
void sysProcessExit(int rc){exit_result=rc;longjmp(jump,1);}
int sysThreadCreate(sys_ppu_thread_t *t,void (*fn)(void *),void *a,int p,uint64_t s,uint64_t f,char *n)
{(void)t;(void)a;(void)s;(void)f;assert(p==900);if(n&&!strcmp(n,"buzzbridge_guard"))guard=fn;return is("thread_fail")?-1:0;}
int sysLv2FsMkdir(const char *p,unsigned m){(void)p;(void)m;return 0;}
int sysLv2FsOpen(const char *p,int f,int *fd,unsigned m,void *a,unsigned s)
{(void)f;(void)m;(void)a;(void)s;if(!strcmp(p,"/dev_hdd0/tmp/buzzbridge_heartbeat.log")){*fd=4;return 0;}assert(!strcmp(p,"/dev_hdd0/tmp/buzzbridge_app.log"));*fd=3;return is("log_open_fail")?-1:0;}
int sysLv2FsWrite(int fd,const void *p,uint64_t n,uint64_t *got)
{assert(fd==3&&used+n<sizeof(logs));memcpy(logs+used,p,(size_t)n);used+=(size_t)n;logs[used]=0;*got=n;writes++;return 0;}
int sysLv2FsFsync(int fd){assert(fd==3);syncs++;return is("log_sync_fail")?-1:0;}
void *memalign(size_t a,size_t n){(void)a;(void)n;static char host;return is("host_alloc_fail")?NULL:&host;}
void *rsxMemalign(size_t a,size_t n){(void)a;assert(n==1920u*1080u*4u);return is("rsx_alloc_fail")?NULL:calloc(1,n);}
int cellSysutilRegisterCallback(int i,void (*cb)(uint64_t,uint64_t,void *),void *a)
{(void)i;(void)a;event_cb=cb;return is("callback_register_fail")?-1:0;}
int cellSysutilUnregisterCallback(int i){(void)i;event_cb=NULL;return 0;}
int cellSysutilCheckCallback(void)
{
 checks++;
 if(active){
  if((is("quit")||is("long_ps_menu"))&&checks>8&&(!is("long_ps_menu")||clock_us>720000000u))event_cb(CELL_SYSUTIL_REQUEST_EXITGAME,0,NULL);
  if(is("long_ps_menu")){if(clock_us<720000000u){clock_us+=15000000u;event_cb(CELL_SYSUTIL_SYSTEM_MENU_OPEN,0,NULL);}else event_cb(CELL_SYSUTIL_REQUEST_EXITGAME,0,NULL);}
  if((is("system_menu")||is("resume_change")||is("refresh_query_fail"))&&!resumed&&polls==2)event_cb(CELL_SYSUTIL_SYSTEM_MENU_OPEN,0,NULL);
  if((is("system_menu")||is("resume_change")||is("refresh_query_fail"))&&!resumed&&polls==2&&checks>16){resumed=1;event_cb(CELL_SYSUTIL_SYSTEM_MENU_CLOSE,0,NULL);}
 }
 return is("callback_fail")?-1:0;
}
int cellGcmInit(unsigned a,unsigned b,void *p)
{assert(a==0x100000&&b==32*1024*1024&&p);return is("gcm_fail")?-1:0;}
void cellGcmSetWriteBackEndLabel(CellGcmContextData *c,unsigned i,uint32_t n)
{(void)c;assert(i==255);drains++;if(!is("label_timeout")&&!(active&&is("late_label_timeout")))label=n;}
void cellGcmFlush(CellGcmContextData *c){
 (void)c;flushes++;
 if(queued_flip){assert(queued_wait);queued_flip=queued_wait=0;}
}
void *cellGcmGetLabelAddress(unsigned i){assert(i==255);return &label;}
unsigned cellGcmGetFlipStatus(void){return active&&is("flip_timeout");}
void cellGcmResetFlipStatus(void){}
int cellGcmSetFlip(CellGcmContextData *c,uint8_t i){(void)c;assert(i<2);frames++;if(is("flip_submit_fail"))return -1;assert(!queued_flip);queued_flip=1;return 0;}
void cellGcmSetWaitFlip(CellGcmContextData *c){(void)c;assert(queued_flip);queued_wait=1;wait_commands++;}
static unsigned mode(void){if(is("resume_change")&&resumed)return 5;if(strstr(scenario,"480"))return 4;if(strstr(scenario,"576"))return 5;if(strstr(scenario,"1080"))return 1;return 2;}
int videoGetState(int a,int b,videoState *s)
{
 (void)a;(void)b;queries++;
 if(is("query_fallback")&&!configured)return -1;
 if(is("refresh_query_fail")&&active)return -1;
 s->state=configured&&is("video_state_timeout")?VIDEO_STATE_BUSY:0;
 s->displayMode.resolution=mode();s->displayMode.aspect=strstr(scenario,"4_3")?1:2;
 s->displayMode.scanMode=strstr(scenario,"i_")?0:1;return 0;
}
int videoGetResolution(unsigned r,videoResolution *s){assert(r==mode());s->width=r==1?1920:r==2?1280:720;s->height=r==1?1080:r==2?720:r==4?480:576;if(is("bad_resolution"))s->width=0;return 0;}
int videoConfigure(int a,videoConfiguration *s,void *p,int b)
{
 (void)a;(void)b;assert(!p);unsigned r=s->resolution;
 assert(r==mode()||(is("bad_resolution")&&r==2));
 assert(s->pitch==(r==1?1920u:r==2?1280u:720u)*4u);
 assert(s->aspect==(strstr(scenario,"4_3")?1u:2u));configured=1;configurations++;return is("configure_fail")?-1:0;
}
void cellGcmSetFlipMode(int m){assert(m==GCM_FLIP_VSYNC);}
int cellGcmAddressToOffset(void *p,uint32_t *o){assert(p);*o=4096;return 0;}
int cellGcmSetDisplayBuffer(int i,uint32_t o,unsigned p,unsigned w,unsigned h)
{registrations++;assert(i<2&&o&&p==w*4&&w<=1920&&h<=1080);return is("display_buffer_fail")?-1:0;}
int ioPadInit(int n){assert(n==7);return is("pad_fail")?-1:0;}
int ioPadEnd(void){pad_ended=1;return 0;}
int ioPadGetInfo(padInfo *i){memset(i,0,sizeof(*i));i->connected=1;i->status[0]=1;return is("menu_select_noinfo")?-1:0;}
int ioPadGetData(int p,padData *d)
{
 if(p>=0&&p<7)port_polls[p]++;
 d->len=0;if(p)return 0;unsigned n=polls++;d->len=1;active=1;
 if(!strncmp(scenario,"menu_",5)){
  if(is("menu_select")||is("menu_select_noinfo")){d->BTN_CROSS=n<4||n==9;d->BTN_DOWN=n==5||n==7;}
  if(is("menu_idle_exit")){if(n<70)clock_us+=15000000u;return 0;}
  if(is("menu_cancel"))d->BTN_CIRCLE=n==3;
  if(is("menu_timeout")){if(n<48)clock_us+=15000000u;d->BTN_CIRCLE=n==50;}return 0;
 }
 if(is("idle_stable")){
  if(n==0){assert(frames==2);}
  if(n==199){assert(frames==2&&configurations==1&&queries==2&&registrations==2);assert(clock_us>=199u*40000u);d->BTN_CIRCLE=1;}
  return 0;
 }
 if(is("idle_exit")){if(n<70)clock_us+=15000000u;return 0;}
 if(is("late_label_timeout")){d->BTN_LEFT=n==3;return 0;}
 if(is("timeout")||is("status_timeout")||is("late_answer")||is("long_ps_menu")){
  /* Advance virtual time beyond ten minutes with no input. Exercise the real
   * watchdog after each healthy pump, then answer or cancel explicitly. */
  if(n<48){clock_us+=15000000u;if(!setjmp(jump)){guard(NULL);assert(0);}assert(exit_result==-77);exit_result=-99;}
  d->BTN_CIRCLE=n==50&&(is("timeout")||is("status_timeout")||is("long_ps_menu"));
  d->BTN_LEFT=n==50&&is("late_answer");d->BTN_CROSS=n==52&&is("late_answer");return 0;
 }
 if(is("long_dialog")){
  /* Held Cross never confirms a question or skips pages. Read one page per edge,
   * then explicitly select Yes only after the final page is actually drawn. */
  d->BTN_CROSS=n<4||(n>=6&&n%4==2);d->BTN_LEFT=n>=5&&n%4==1;return 0;
 }
 d->BTN_LEFT=(is("yes")||is("system_menu"))&&n==5;
 d->BTN_CROSS=n==7&&(is("yes")||is("no")||is("system_menu"));
 d->BTN_CIRCLE=n==7&&!(is("yes")||is("no")||is("system_menu"));return 0;
}
int main(int argc,char **argv)
{
 assert(argc==2);scenario=argv[1];int init=app_ui_init();
 if(is("work_stable")){
  assert(!init);for(int i=0;i<200;i++)app_work("Same operation progress");
  assert(frames==2&&queries==2&&registrations==2&&configurations==1);
  printf("PASS work_stable: repeated progress rendered/flipped once; no video polling\n");return 0;
 }
 if(!strncmp(scenario,"menu_",5)){
  assert(!init);const char *items[]={"one","two","three"};
  int selecting=is("menu_select")||is("menu_select_noinfo");
  assert(app_menu("test list",items,3,0)==(selecting?2:-1));
  if(is("menu_idle_exit")){assert(strstr(logs,"idle exit")&&app_exiting());printf("PASS menu_idle_exit: 15 minutes without input returns cleanly\n");return 0;}
  assert(frames==(selecting?4u:2u));
  /* Only ports that report a controller are polled; with no pad info every port is polled. */
  for(int p=1;p<7;p++)assert(is("menu_select_noinfo")?port_polls[p]>0:port_polls[p]==0);
  printf("PASS %s navigation / held-button suppression / cancellation / pad polling set\n",scenario);return 0;
 }
 int init_failure=strstr(scenario,"_fail")!=NULL&&!is("refresh_query_fail");
 if(is("video_state_timeout")||is("label_timeout"))init_failure=1;
 assert((init!=0)==init_failure);
 if(!init){
  if(is("query_fallback")||is("bad_resolution"))assert(strstr(logs,"fallback 720p"));
  if(is("watchdog")||is("total_watchdog")){
   if(!setjmp(jump)){
    if(is("watchdog"))clock_us+=31000000u;
    else for(int i=0;i<25;i++){clock_us+=25000000u;app_stage("healthy progress");}
    guard(NULL);assert(0);
   }
   assert(exit_result==2);printf("PASS %s\n",scenario);return 0;
  }
  char text[3000]="Test dialog";
  if(is("long_dialog")){text[0]=0;for(int i=0;i<55;i++)strcat(text,"Read every safety warning before choosing Yes.\n");}
  int result=app_dialog(text,!is("status_timeout"));
  assert(result==(is("no")?CELL_MSGDIALOG_BUTTON_NO:is("yes")||is("system_menu")||is("long_dialog")||is("late_answer")?CELL_MSGDIALOG_BUTTON_YES:CELL_MSGDIALOG_BUTTON_ESCAPE));
  assert(frames>1&&checks>1);
  if(is("timeout")||is("status_timeout")){assert(frames==2&&queries==2&&registrations==2&&configurations==1);assert(drains==4);}
  if(is("timeout")||is("status_timeout")||is("late_answer"))assert(!strstr(logs,"response timeout")&&clock_us>720000000u&&!app_exiting());
  if(is("flip_timeout")||is("late_label_timeout"))assert(clock_us<4000000u&&app_exiting());
  if(is("quit"))assert(app_exiting());
  if(is("idle_exit"))assert(strstr(logs,"idle exit")&&app_exiting());
  if(is("long_dialog"))assert(polls>12);
  if(is("system_menu")||is("refresh_query_fail"))assert(configurations==1&&registrations==2);
  if(is("refresh_query_fail"))assert(strstr(logs,"refresh query failed"));
  if(is("resume_change"))assert(configurations==2&&strstr(logs,"720x576"));
 }
 assert(flushes>=wait_commands);
 assert(wait_commands==frames-(is("flip_submit_fail")?1u:0u));
 if(!setjmp(jump)){app_ui_exit(init?1:0);assert(0);}
 assert(exit_result==(init?1:0));
 if(!is("log_open_fail")&&!is("thread_fail")){assert(writes==syncs&&strstr(logs,"exit: returning to XMB"));if(!init)assert(pad_ended);}
 printf("PASS %s frames=%u callbacks=%u synced=%u time=%llu us\n",scenario,frames,checks,syncs,(unsigned long long)clock_us);return 0;
}

