/* SPDX-License-Identifier: MIT. GCM flow: PS3DK cellgcm-clear/sysmenu samples.
 * Dialogs must pump callbacks AND present frames (PSL1GHT msgdialog sample).
 * GPU allocations remain alive until process exit; no unbounded Finish/join.
 */
#include "app_ui.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdatomic.h>
#ifndef BUZZ_APP_HOST_TEST
#include <malloc.h>
#include <unistd.h>
#include <sys/process.h>
#include <sys/file.h>
#include <sys/thread.h>
#include <sys/timer.h>
#include <sys/sys_time.h>
#include <sysutil/video.h>
#include <cell/gcm.h>
#include <cell/dbgfont.h>
#include <cell/sysutil.h>
#include <io/pad.h>
#endif

#define APP_LOG "/dev_hdd0/tmp/buzzbridge_app.log"
#define HOST_SIZE (32u*1024*1024)
#define GPU_TIMEOUT 2000000u
static int log_fd=-1,log_ok=1,registered,pad_owned,font_ready,video_ready,pending,current;
static volatile int exiting,menu_open,done,answer;
static atomic_uint_fast64_t deadline;
static uint64_t absolute_deadline;
static uint16_t width,height;
static uint32_t offsets[2],depth_offset,pitch,label_value;
static const char *background="Starting...";

static uint64_t now(void) {return (uint64_t)sys_time_get_system_time();}
static void arm(void) {atomic_store(&deadline,now()+30000000u);}
/* Independent of libc locks, disk I/O and RSX waits. Previous stage is fsynced.
 * Expiry deliberately does not attempt logging/fsync or a blocked thread join. */
static void watchdog(void *arg)
{
 (void)arg;
 for(;;) {
  sys_timer_usleep(250000);
  uint64_t t=now();
  if(t>=atomic_load(&deadline)||t>=absolute_deadline)sysProcessExit(2);
 }
}
void app_log(const char *fmt,...)
{
 char line[1100];va_list ap;
 int at=snprintf(line,sizeof(line),"%llu ",(unsigned long long)(now()/1000));
 va_start(ap,fmt);int n=vsnprintf(line+at,sizeof(line)-(size_t)at-2,fmt,ap);va_end(ap);
 if(n<0){log_ok=0;return;}
 size_t count=(size_t)n+at;if(count>sizeof(line)-2)count=sizeof(line)-2;line[count++]='\n';
 uint64_t wrote=0;
 if(log_fd<0||sysLv2FsWrite(log_fd,line,count,&wrote)||wrote!=count||sysLv2FsFsync(log_fd))log_ok=0;
}
void app_stage(const char *text) {arm();app_log("%s",text);}
static void event(uint64_t status,uint64_t param,void *arg)
{
 (void)param;(void)arg;
 if(status==CELL_SYSUTIL_REQUEST_EXITGAME){exiting=1;done=1;answer=CELL_MSGDIALOG_BUTTON_ESCAPE;}
 if(status==CELL_SYSUTIL_SYSTEM_MENU_OPEN)menu_open=1;
 if(status==CELL_SYSUTIL_SYSTEM_MENU_CLOSE)menu_open=0;
}
static int pump(void)
{
 int rc=cellSysutilCheckCallback();
 if(rc){app_log("ERROR callback pump=%08x",(unsigned)rc);exiting=1;}
 return !exiting;
}
int app_exiting(void) {if(registered)(void)pump();return exiting;}
static int gpu_error(const char *what)
{app_log("ERROR %s (2s GPU deadline)",what);video_ready=0;exiting=1;return -1;}
static int drain(void)
{
 CellGcmContextData *ctx=CELL_GCM_CURRENT;
 uint32_t expected=++label_value;
 cellGcmSetWriteBackEndLabel(ctx,255,expected);cellGcmFlush(ctx);
 volatile uint32_t *label=(volatile uint32_t *)cellGcmGetLabelAddress(255);
 if(!label)return gpu_error("missing GPU label");
 uint64_t until=now()+GPU_TIMEOUT;
 while(*label!=expected) {
  if(!pump())return -1;
  if(now()>=until)return gpu_error("GPU label timeout");
  usleep(1000);
 }
 return 0;
}
static int wait_flip(void)
{
 if(!pending)return 0;
 uint64_t until=now()+GPU_TIMEOUT;
 while(cellGcmGetFlipStatus()!=0) {
  if(!pump())return -1;
  if(now()>=until)return gpu_error("flip timeout");
  usleep(1000);
 }
 cellGcmResetFlipStatus();pending=0;return 0;
}
static int frame(void)
{
 arm();
 if(!video_ready||!pump()||wait_flip()||drain())return -1;
 CellGcmContextData *ctx=CELL_GCM_CURRENT;
 CellGcmSurface sf={0};
 sf.colorFormat=GCM_SURFACE_X8R8G8B8;sf.colorTarget=GCM_SURFACE_TARGET_0;
 sf.colorLocation[0]=GCM_LOCATION_RSX;sf.colorOffset[0]=offsets[current];sf.colorPitch[0]=pitch;
 for(int i=1;i<4;i++){sf.colorLocation[i]=GCM_LOCATION_RSX;sf.colorPitch[i]=64;}
 sf.depthFormat=GCM_SURFACE_ZETA_Z16;sf.depthLocation=GCM_LOCATION_RSX;
 sf.depthOffset=depth_offset;sf.depthPitch=pitch;sf.type=GCM_SURFACE_TYPE_LINEAR;
 sf.antiAlias=GCM_SURFACE_CENTER_1;sf.width=width;sf.height=height;
 cellGcmSetSurface(ctx,&sf);
 float scale[4]={width*0.5f,height*-0.5f,0.5f,0},offset[4]={width*0.5f,height*0.5f,0.5f,0};
 cellGcmSetViewport(ctx,0,0,width,height,0,1,scale,offset);
 cellGcmSetScissor(ctx,0,0,width,height);
 cellGcmSetColorMask(ctx,GCM_COLOR_MASK_R|GCM_COLOR_MASK_G|GCM_COLOR_MASK_B|GCM_COLOR_MASK_A);
 cellGcmSetClearColor(ctx,0xff142334);
 cellGcmSetClearSurface(ctx,GCM_CLEAR_R|GCM_CLEAR_G|GCM_CLEAR_B|GCM_CLEAR_A);
 if(font_ready) {
  if(cellDbgFontPrintf(0.07f,0.07f,1.4f,0xffffffff,"Buzz! Bridge")<0||
     cellDbgFontPrintf(0.07f,0.18f,0.8f,0xffffffff,"%s",background)<0||
     cellDbgFontDrawGcm())return gpu_error("text rendering failed");
 }
 cellGcmResetFlipStatus();
 int rc=cellGcmSetFlip(ctx,(uint8_t)current);
 if(rc)return gpu_error("flip submission failed");
 cellGcmFlush(ctx);cellGcmSetWaitFlip(ctx);pending=1;current^=1;
 usleep(1000);return 0;
}
/* Error fallback never authorizes a choice. START/Circle/Cross acknowledge. */
static void text_error(const char *text)
{
 app_stage(text);background=text;
 uint64_t until=now()+10000000u;
 while(!exiting&&now()<until) {
  if(frame())break;
  if(pad_owned&&!menu_open)for(int p=0;p<7;p++) {
   padData pd={0};
   if(!ioPadGetData(p,&pd)&&pd.len>0&&(pd.BTN_START||pd.BTN_CIRCLE||pd.BTN_CROSS))return;
  }
 }
}
int app_ui_init(void)
{
 absolute_deadline=now()+600000000u;arm();
 sys_ppu_thread_t tid;
 if(sysThreadCreate(&tid,watchdog,NULL,900,0x10000,0,"buzzbridge_guard"))return -1;
 (void)sysLv2FsMkdir("/dev_hdd0/tmp",0777);
 if(sysLv2FsOpen(APP_LOG,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&log_fd,0666,NULL,0))return -1;
 app_stage("start Buzz! Bridge BUZZB0001; operation watchdog=30s total=600s");
 int rc=cellSysutilRegisterCallback(0,event,NULL);
 if(rc){app_log("ERROR register callback=%08x",(unsigned)rc);return -1;}registered=1;
 app_stage("video init: allocate host memory");
 void *host=memalign(1024*1024,HOST_SIZE);
 if(!host){app_log("ERROR host allocation");return -1;}
 app_stage("video init: cellGcmInit enter");
 rc=cellGcmInit(0x100000,HOST_SIZE,host);
 app_log("video init: cellGcmInit result=%08x",(unsigned)rc);if(rc)return -1;
 videoState state={0};videoResolution res={0};videoConfiguration cfg={0};
 if(videoGetState(0,0,&state)||state.state||videoGetResolution(state.displayMode.resolution,&res)||
    !res.width||!res.height||res.width>1920||res.height>1080){app_log("ERROR active video mode");return -1;}
 width=res.width;height=res.height;pitch=width*4u;
 cfg.resolution=state.displayMode.resolution;cfg.format=VIDEO_BUFFER_FORMAT_XRGB;
 cfg.pitch=pitch;cfg.aspect=state.displayMode.aspect;
 if(drain())return -1;
 app_stage("video init: videoConfigure enter");
 rc=videoConfigure(0,&cfg,NULL,0);
 app_log("video init: videoConfigure result=%08x",(unsigned)rc);if(rc)return -1;
 uint64_t until=now()+GPU_TIMEOUT;
 do {
  if(videoGetState(0,0,&state)){app_log("ERROR video state");return -1;}
  if(!state.state)break;
  if(!pump()||now()>=until)return gpu_error("video configure timeout");
  usleep(1000);
 }while(1);
 cellGcmSetFlipMode(GCM_FLIP_VSYNC);
 for(int i=0;i<2;i++) {
  void *b=rsxMemalign(64,pitch*height);
  if(!b||cellGcmAddressToOffset(b,&offsets[i])||cellGcmSetDisplayBuffer(i,offsets[i],pitch,width,height)){
   app_log("ERROR display buffer=%d",i);return -1;
  }
 }
 void *depth=rsxMemalign(64,pitch*height*2u);
 if(!depth||cellGcmAddressToOffset(depth,&depth_offset)){app_log("ERROR depth allocation");return -1;}
 size_t n=CELL_DBGFONT_FRAGMENT_SIZE+CELL_DBGFONT_TEXTURE_SIZE+12000*CELL_DBGFONT_VERTEX_SIZE;
 void *font=rsxMemalign(128,(uint32_t)n);
 if(!font){app_log("ERROR font allocation");return -1;}
 CellDbgFontConfigGcm fc={0};fc.localBufAddr=(sys_addr_t)(uintptr_t)font;fc.localBufSize=(uint32_t)n;
 fc.option=CELL_DBGFONT_VERTEX_LOCAL|CELL_DBGFONT_TEXTURE_LOCAL;fc.screenWidth=width;fc.screenHeight=height;
 rc=cellDbgFontInitGcm(&fc);if(rc){app_log("ERROR font init=%08x",(unsigned)rc);return -1;}
 font_ready=1;video_ready=1;cellGcmResetFlipStatus();
 rc=ioPadInit(7);pad_owned=!rc;
 if(rc){text_error("Controller initialization failed. Returning to XMB.");return -1;}
 background="Preparing the installer. Please wait...";
 if(frame()||wait_flip()||drain())return -1;
 app_log("video init complete; first frame presented; %ux%u",width,height);
 if(!log_ok){text_error("App log could not be synced. Installation stopped.");return -1;}
 return 0;
}
static void button(int type,void *arg) {(void)arg;answer=type;done=1;}
int app_dialog(const char *text,int question)
{
 done=0;answer=CELL_MSGDIALOG_BUTTON_ESCAPE;
 if(app_exiting()||!video_ready)return answer;
 if(!log_ok){text_error("App log write failed. Returning to XMB.");exiting=1;return answer;}
 background="Use the dialog to choose. Circle cancels.\nPS button: system menu / Quit Game.\nMenus close after 120 seconds; status after 20 seconds.";
 if(frame())return answer;
 app_stage("dialog open enter");
 unsigned type=CELL_MSGDIALOG_TYPE_SE_TYPE_NORMAL|CELL_MSGDIALOG_TYPE_BG_VISIBLE|
  (question?(CELL_MSGDIALOG_TYPE_BUTTON_TYPE_YESNO|CELL_MSGDIALOG_TYPE_DEFAULT_CURSOR_NO):CELL_MSGDIALOG_TYPE_BUTTON_TYPE_OK);
 int rc=cellMsgDialogOpen2(type,text,button,NULL,NULL);
 app_log("dialog open result=%08x",(unsigned)rc);
 if(rc){text_error("System dialog could not open.\nSee /dev_hdd0/tmp/buzzbridge_app.log\nReturning to XMB.");exiting=1;return answer;}
 if(frame()||wait_flip()) {exiting=1;goto abort_dialog;}
 app_log("menu shown: first dialog frame presented; question=%d",question);
 uint64_t until=now()+(question?120000000u:20000000u);
 while(!done&&!exiting&&now()<until)if(frame())break;
 if(!done&&!exiting){app_log("ERROR dialog response timeout");exiting=1;}
 app_log("choice button=%d exit_request=%d",answer,exiting);
abort_dialog:
 arm();rc=cellMsgDialogAbort();app_log("dialog abort result=%08x",(unsigned)rc);
 /* Bounded close grace, still presenting frames and dispatching EXIT_GAME. */
 uint64_t close_until=now()+500000u;
 while(!exiting&&now()<close_until)if(frame())break;
 if(exiting)answer=CELL_MSGDIALOG_BUTTON_ESCAPE;
 return answer;
}
void app_ui_exit(int result)
{
 app_stage("exit: returning to XMB");
 if(registered){(void)cellSysutilUnregisterCallback(0);registered=0;}
 if(pad_owned){(void)ioPadEnd();pad_owned=0;}
 /* Keep all RSX/host/font allocations alive. Process teardown reclaims them. */
 app_log("exit result=%d exit_request=%d log_ok=%d",result,exiting,log_ok);
 sysProcessExit(result);
}
