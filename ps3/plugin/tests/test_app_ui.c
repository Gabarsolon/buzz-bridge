/* Run actual app_ui.c with deterministic firmware/RSX/time mocks, no hardware. */
#include "app_mock.h"
#include "../autoload/app_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

CellGcmContextData mock_context;
static const char *scenario;
static uint64_t clock_us;
static uint32_t label;
static unsigned frames,checks,writes,syncs,opens,aborts,callbacks_after_open;
static int active,exit_result=-99,pad_ended,configured;
static char logs[65536];static size_t used;
static void (*event_cb)(uint64_t,uint64_t,void *);
static void (*button_cb)(int,void *);
static void (*guard)(void *);
static jmp_buf jump;
static int is(const char *s){return !strcmp(scenario,s);}
uint64_t sys_time_get_system_time(void){clock_us+=100;return clock_us;}
void sys_timer_usleep(unsigned n){clock_us+=n;}
void usleep(unsigned n){clock_us+=n;}
void sysProcessExit(int rc){exit_result=rc;longjmp(jump,1);}
int sysThreadCreate(sys_ppu_thread_t *t,void (*fn)(void *),void *a,int p,uint64_t s,uint64_t f,char *n)
{(void)t;(void)a;(void)s;(void)f;(void)n;assert(p==900);guard=fn;return is("thread_fail")?-1:0;}
int sysLv2FsMkdir(const char *p,unsigned m){(void)p;(void)m;return 0;}
int sysLv2FsOpen(const char *p,int f,int *fd,unsigned m,void *a,unsigned s)
{(void)f;(void)m;(void)a;(void)s;assert(!strcmp(p,"/dev_hdd0/tmp/buzzbridge_app.log"));*fd=3;return is("log_open_fail")?-1:0;}
int sysLv2FsWrite(int fd,const void *p,uint64_t n,uint64_t *got)
{assert(fd==3&&used+n<sizeof(logs));memcpy(logs+used,p,(size_t)n);used+=(size_t)n;logs[used]=0;*got=n;writes++;return 0;}
int sysLv2FsFsync(int fd){assert(fd==3);syncs++;return is("log_sync_fail")?-1:0;}
void *memalign(size_t a,size_t n){(void)a;(void)n;static char host;return is("host_alloc_fail")?NULL:&host;}
void *rsxMemalign(size_t a,size_t n){(void)a;(void)n;static char local;return is("rsx_alloc_fail")?NULL:&local;}
int cellSysutilRegisterCallback(int i,void (*cb)(uint64_t,uint64_t,void *),void *a)
{(void)i;(void)a;event_cb=cb;return is("callback_register_fail")?-1:0;}
int cellSysutilUnregisterCallback(int i){(void)i;event_cb=NULL;return 0;}
int cellSysutilCheckCallback(void)
{
 checks++;
 if(active) {
  callbacks_after_open++;
  if(is("quit")&&callbacks_after_open==5)event_cb(CELL_SYSUTIL_REQUEST_EXITGAME,0,NULL);
  if(is("system_menu")&&callbacks_after_open==2)event_cb(CELL_SYSUTIL_SYSTEM_MENU_OPEN,0,NULL);
  if(is("system_menu")&&callbacks_after_open==4)event_cb(CELL_SYSUTIL_SYSTEM_MENU_CLOSE,0,NULL);
  if(callbacks_after_open==8&&!is("timeout")&&!is("status_timeout")&&!is("quit")&&!is("flip_timeout")&&!is("late_label_timeout"))
   button_cb(is("no")?CELL_MSGDIALOG_BUTTON_NO:is("cancel")?CELL_MSGDIALOG_BUTTON_ESCAPE:CELL_MSGDIALOG_BUTTON_YES,NULL);
 }
 return is("callback_fail")?-1:0;
}
int cellGcmInit(unsigned a,unsigned b,void *p)
{assert(a==0x100000&&b==32*1024*1024&&p);assert(strstr(logs,"start Buzz! Bridge"));return is("gcm_fail")?-1:0;}
void cellGcmSetWriteBackEndLabel(CellGcmContextData *c,unsigned i,uint32_t n)
{(void)c;assert(i==255);if(!is("label_timeout")&&!(active&&is("late_label_timeout")))label=n;}
void cellGcmFlush(CellGcmContextData *c){(void)c;}
void *cellGcmGetLabelAddress(unsigned i){assert(i==255);return &label;}
unsigned cellGcmGetFlipStatus(void){return active&&is("flip_timeout");}
void cellGcmResetFlipStatus(void){}
void cellGcmSetSurface(CellGcmContextData *c,CellGcmSurface *s){(void)c;assert(s->width==1280&&s->height==720&&s->colorPitch[0]==5120);}
void cellGcmSetViewport(CellGcmContextData *c,int a,int b,unsigned w,unsigned h,float x,float y,float *s,float *o)
{(void)c;(void)a;(void)b;(void)w;(void)h;(void)x;(void)y;(void)s;(void)o;}
void cellGcmSetScissor(CellGcmContextData *c,int a,int b,unsigned w,unsigned h){(void)c;(void)a;(void)b;(void)w;(void)h;}
void cellGcmSetColorMask(CellGcmContextData *c,unsigned m){(void)c;(void)m;}
void cellGcmSetClearColor(CellGcmContextData *c,uint32_t m){(void)c;(void)m;}
void cellGcmSetClearSurface(CellGcmContextData *c,unsigned m){(void)c;(void)m;}
int cellGcmSetFlip(CellGcmContextData *c,uint8_t i){(void)c;assert(i<2);frames++;return is("flip_submit_fail")?-1:0;}
void cellGcmSetWaitFlip(CellGcmContextData *c){(void)c;}
int videoGetState(int a,int b,videoState *s)
{(void)a;(void)b;s->state=configured&&is("video_state_timeout");s->displayMode.resolution=4;s->displayMode.aspect=2;return 0;}
int videoGetResolution(unsigned r,videoResolution *s){assert(r==4);s->width=1280;s->height=720;return 0;}
int videoConfigure(int a,videoConfiguration *s,void *p,int b)
{(void)a;(void)b;assert(!p&&s->pitch==5120&&s->aspect==2);configured=1;return is("configure_fail")?-1:0;}
void cellGcmSetFlipMode(int m){(void)m;}
int cellGcmAddressToOffset(void *p,uint32_t *o){assert(p);*o=4096;return 0;}
int cellGcmSetDisplayBuffer(int i,uint32_t o,unsigned p,unsigned w,unsigned h){assert(i<2&&o&&p==5120&&w==1280&&h==720);return 0;}
int cellDbgFontInitGcm(CellDbgFontConfigGcm *c){assert(c->localBufAddr&&c->localBufSize>400000);return is("font_fail")?-1:0;}
int cellDbgFontPrintf(float x,float y,float s,uint32_t c,const char *fmt,...)
{(void)x;(void)y;(void)s;(void)c;(void)fmt;return 0;}
int cellDbgFontDrawGcm(void){return is("font_draw_fail")?-1:0;}
int ioPadInit(int n){assert(n==7);return is("pad_fail")?-1:0;}
int ioPadEnd(void){pad_ended=1;return 0;}
int ioPadGetData(int p,padData *d){(void)p;d->len=0;return 0;}
int cellMsgDialogOpen2(unsigned t,const char *s,void (*cb)(int,void *),void *a,void *b)
{(void)t;assert(s&&frames>0&&!a&&!b);button_cb=cb;opens++;active=!is("dialog_fail");return is("dialog_fail")?-1:0;}
int cellMsgDialogAbort(void){aborts++;active=0;return 0;}
int main(int argc,char **argv)
{
 assert(argc==2);scenario=argv[1];int init=app_ui_init();
 int init_failure=strstr(scenario,"_fail")!=NULL&& !is("dialog_fail");
 if(is("video_state_timeout")||is("label_timeout"))init_failure=1;
 assert((init!=0)==init_failure);
 if(!init) {
  if(is("watchdog")||is("total_watchdog")) {
   if(!setjmp(jump)) {
    if(is("watchdog"))clock_us+=31000000u;
    else for(int i=0;i<25;i++){clock_us+=25000000u;app_stage("healthy progress");}
    guard(NULL);assert(0);
   }
   assert(exit_result==2);printf("PASS %s\n",scenario);return 0;
  }
  int a=app_dialog("Test menu",!is("status_timeout"));
  assert(a==(is("no")?CELL_MSGDIALOG_BUTTON_NO:
   is("yes")||is("system_menu")?CELL_MSGDIALOG_BUTTON_YES:CELL_MSGDIALOG_BUTTON_ESCAPE));
  assert(opens==1&&frames>1&&checks>1);
  if(!is("dialog_fail"))assert(aborts==1);
  if(is("timeout"))assert(strstr(logs,"response timeout")&&clock_us<125000000u);
  if(is("status_timeout"))assert(strstr(logs,"response timeout")&&clock_us>=20000000u&&clock_us<22000000u);
  if(is("flip_timeout")||is("late_label_timeout"))assert(clock_us<4000000u&&app_exiting());
  if(is("quit"))assert(app_exiting()&&strstr(logs,"exit_request=1"));
  assert(strstr(logs,"menu shown")||is("dialog_fail")||is("flip_timeout")||is("late_label_timeout"));
 }
 if(!setjmp(jump)){app_ui_exit(init?1:0);assert(0);}
 assert(exit_result==(init?1:0));
 if(!is("log_open_fail")&&!is("thread_fail")) {
  assert(writes==syncs&&strstr(logs,"exit: returning to XMB"));
  if(!init)assert(pad_ended);
 }
 printf("PASS %s frames=%u callbacks=%u fsynced entries=%u time=%llu us\n",scenario,frames,checks,syncs,(unsigned long long)clock_us);
 return 0;
}
