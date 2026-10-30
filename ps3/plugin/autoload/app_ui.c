/* SPDX-License-Identifier: MIT. GCM flow: PS3DK cellgcm-clear/sysmenu samples.
 * Pump callbacks while idle; the display retains unchanged frames.
 * GPU allocations remain alive until process exit; no unbounded Finish/join.
 */
#include "app_ui.h"
#include "ui_layout.h"
#include "app_health.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#ifndef BUZZ_APP_HOST_TEST
#include <malloc.h>
#include <unistd.h>
#include <sys/process.h>
#include <sys/file.h>
#include <sys/thread.h>
#include <sys/timer.h>
#include <sys/sys_time.h>
#include <sys/memory.h>
#include <sysutil/video.h>
#include <cell/gcm.h>
#include <cell/sysutil.h>
#include <io/pad.h>
#endif

#define APP_LOG "/dev_hdd0/tmp/buzzbridge_app.log"
#define HB_LOG "/dev_hdd0/tmp/buzzbridge_heartbeat.log"
#define HOST_SIZE (32u*1024*1024)
#define GPU_TIMEOUT 2000000u
static int log_fd=-1,log_ok=1,registered,pad_owned,video_ready,pending,current,refresh_video;
static volatile int exiting,menu_open;
static atomic_uint_fast64_t deadline;
static uint64_t absolute_deadline;
static uint16_t width,height;
static uint32_t offsets[2],pitch,label_value;
static uint32_t *buffers[2],*software;
static unsigned resolution,aspect,scan_mode,revision=1,presented;
static ui_layout layout;
static ui_result drawn;
static int dialog_active,dialog_question,dialog_page,dialog_yes;
static const char *dialog_text;
static int configure_video(int startup);
static const char *background="Starting...";
static const char *list_title;
static const char *const *list_items;
static int list_count,list_selected;

static uint64_t now(void) {return (uint64_t)sys_time_get_system_time();}
static void arm(void) {atomic_store(&deadline,now()+30000000u);}
/* Black-box statistics (diagnostics only; plain counters, no locks). */
static atomic_ulong stat_frames,stat_flips,stat_pad_polls,stat_pumps,flip_last_us,flip_max_us;
static uint64_t flip_submitted,last_input,log_bytes,hb_bytes,pad_next;
static unsigned pad_mask=0x7fu;
static char last_stage[96];
static int hb_fd=-1;
static void note_input(void){last_input=now();}
/* Poll only ports that report a controller (re-checked once a second). If the
 * query fails or reports none, every port is polled exactly as before. */
static int pad_present(int p)
{
 uint64_t t=now();
 if(t>=pad_next){
  padInfo info;memset(&info,0,sizeof(info));unsigned mask=0;
  if(!ioPadGetInfo(&info)&&info.connected){for(int i=0;i<7;i++)if(info.status[i])mask|=1u<<i;}
  pad_mask=mask?mask:0x7fu;pad_next=t+1000000u;
 }
 return (int)((pad_mask>>p)&1u);
}
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
 if(log_bytes>APP_LOG_LIMIT)return;
 char line[1100];va_list ap;
 int at=snprintf(line,sizeof(line),"%llu ",(unsigned long long)(now()/1000));
 va_start(ap,fmt);int n=vsnprintf(line+at,sizeof(line)-(size_t)at-2,fmt,ap);va_end(ap);
 if(n<0){log_ok=0;return;}
 size_t count=(size_t)n+at;if(count>sizeof(line)-2)count=sizeof(line)-2;line[count++]='\n';
 uint64_t wrote=0;
 if(log_fd<0||sysLv2FsWrite(log_fd,line,count,&wrote)||wrote!=count||sysLv2FsFsync(log_fd))log_ok=0;
 else log_bytes+=count;
}
void app_stage(const char *text) {arm();snprintf(last_stage,sizeof(last_stage),"%s",text);app_log("%s",text);}
static void event(uint64_t status,uint64_t param,void *arg)
{
 (void)param;(void)arg;
 if(status==CELL_SYSUTIL_REQUEST_EXITGAME)exiting=1;
 if(status==CELL_SYSUTIL_SYSTEM_MENU_OPEN){menu_open=1;note_input();}
 if(status==CELL_SYSUTIL_SYSTEM_MENU_CLOSE){menu_open=0;refresh_video=1;revision++;note_input();}
}
static int pump(void)
{
 stat_pumps++;int rc=cellSysutilCheckCallback();
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
 {uint64_t d=now()-flip_submitted;flip_last_us=d;if(d>flip_max_us)flip_max_us=d;}
 cellGcmResetFlipStatus();pending=0;return 0;
}
static int frame(void)
{
 arm();stat_frames++;
 if(!video_ready||!pump()||wait_flip())return -1;
 /* XMB owns presentation while open. Resume requests a mode query and redraw. */
 if(menu_open){usleep(APP_TICK_US);return 0;}
 if(refresh_video){refresh_video=0;if(configure_video(0))return -1;}
 if(presented==revision){usleep(APP_TICK_US);return 0;}
 /* Wait for the previous VSYNC flip AND its queued GPU fence before touching
  * another buffer or submitting a changed frame. All waits remain bounded. */
 if(wait_flip()||drain())return -1;
 CellGcmContextData *ctx=CELL_GCM_CURRENT;
 ui_view view={0};
 view.title=list_title;view.text=background;view.items=list_items;
 view.count=list_count;view.selected=list_selected;view.page=-1;
 if(dialog_active){view.title=dialog_question?"Please confirm":"Status / result";view.text=dialog_text;
  view.items=NULL;view.count=0;view.question=dialog_question;view.page=dialog_page;view.yes=dialog_yes;}
 ui_canvas canvas={software,width,NULL,NULL};
 if(ui_render(&layout,&view,&canvas,&drawn))return gpu_error("text rendering failed");
 memcpy(buffers[current],software,(size_t)pitch*height);
 atomic_thread_fence(memory_order_seq_cst);
 cellGcmResetFlipStatus();
 int rc=cellGcmSetFlip(ctx,(uint8_t)current);
 if(rc)return gpu_error("flip submission failed");
 cellGcmSetWaitFlip(ctx);cellGcmFlush(ctx);
 pending=1;current^=1;presented=revision;stat_flips++;flip_submitted=now();
 usleep(10000);return 0;
}
/* Error fallback never authorizes a choice. START/Circle/Cross acknowledge. */
static void text_error(const char *text)
{
 app_stage(text);background=text;list_items=NULL;dialog_active=0;revision++;
 uint64_t until=now()+10000000u;
 while(!exiting&&now()<until) {
  if(frame())break;
  if(pad_owned&&!menu_open)for(int p=0;p<7;p++) {
   padData pd={0};
   if(pad_present(p)&&!ioPadGetData(p,&pd)&&pd.len>0&&(pd.BTN_START||pd.BTN_CIRCLE||pd.BTN_CROSS))return;
  }
 }
}
/* Reuse maximum-size buffers across mode changes; never leak RSX allocations.
 * The firmware resolution id identifies line count; scanMode distinguishes i/p.
 * videoConfiguration has no scan-mode member: preserving the current id/aspect
 * retains the output's current scan timing, which is read back and logged. */
static int configure_video(int startup)
{
 videoState state={0};videoResolution res={0};videoConfiguration cfg={0};
 int valid=!videoGetState(0,0,&state)&&state.state!=VIDEO_STATE_BUSY&&
  !videoGetResolution(state.displayMode.resolution,&res)&&
  res.width>=640&&res.height>=480&&res.width<=1920&&res.height<=1080;
 if(!valid){
  if(!startup){app_log("video refresh query failed; retaining last valid mode");return 0;}
  app_log("video query failed; fallback 720p 16:9");
  state.displayMode.resolution=VIDEO_RESOLUTION_720;state.displayMode.aspect=VIDEO_ASPECT_16_9;
  state.displayMode.scanMode=VIDEO_SCANMODE_PROGRESSIVE;res.width=1280;res.height=720;
 }
 unsigned a=state.displayMode.aspect;
 if(a!=VIDEO_ASPECT_4_3&&a!=VIDEO_ASPECT_16_9){
  a=res.height<=576?VIDEO_ASPECT_4_3:VIDEO_ASPECT_16_9;
  app_log("video aspect unspecified; inferred %s",a==VIDEO_ASPECT_4_3?"4:3":"16:9");
 }
 if(!startup&&resolution==state.displayMode.resolution&&aspect==a&&
    width==res.width&&height==res.height&&scan_mode==state.displayMode.scanMode)return 0;
 if(wait_flip()||drain())return -1;
 width=res.width;height=res.height;pitch=width*4u;
 resolution=state.displayMode.resolution;aspect=a;scan_mode=state.displayMode.scanMode;
 cfg.resolution=resolution;cfg.format=VIDEO_BUFFER_FORMAT_XRGB;cfg.pitch=pitch;cfg.aspect=aspect;
 app_stage("video configure current output");
 int rc=videoConfigure(0,&cfg,NULL,0);
 app_log("videoConfigure result=%08x",(unsigned)rc);if(rc)return gpu_error("video configure failed");
 uint64_t until=now()+GPU_TIMEOUT;
 for(;;){
  if(videoGetState(0,0,&state))return gpu_error("video state readback failed");
  if(state.state!=VIDEO_STATE_BUSY&&state.displayMode.resolution==resolution)break;
  if(!pump()||now()>=until)return gpu_error("video configure timeout");
  usleep(1000);
 }
 scan_mode=state.displayMode.scanMode;
 cellGcmSetFlipMode(GCM_FLIP_VSYNC);
 for(int i=0;i<2;i++)if(cellGcmSetDisplayBuffer(i,offsets[i],pitch,width,height))return gpu_error("display buffer registration failed");
 ui_layout_init(&layout,width,height,aspect==VIDEO_ASPECT_16_9,scan_mode==VIDEO_SCANMODE_INTERLACE);
 presented=0;revision++;dialog_yes=0;
 app_log("video mode: id=%u %ux%u aspect=%s scan=%s pitch=%u glyph=%d safe=%d,%d-%d,%d",
  resolution,width,height,aspect==VIDEO_ASPECT_4_3?"4:3":"16:9",
  scan_mode==VIDEO_SCANMODE_INTERLACE?"interlaced":"progressive",pitch,layout.scale*7,
  layout.left,layout.top,layout.right,layout.bottom);
 return 0;
}
static void health_line(char *line,size_t cap,const char *tag)
{
 unsigned total=0,avail=0;int heap_used=0,heap_arena=0;
#ifndef BUZZ_APP_HOST_TEST
 sys_memory_info_t mi;memset(&mi,0,sizeof(mi));
 if(!sys_memory_get_user_memory_size(&mi)){total=mi.total_user_memory;avail=mi.available_user_memory;}
 struct mallinfo mm=mallinfo();heap_used=mm.uordblks;heap_arena=mm.arena;
#endif
 snprintf(line,cap,"%llu %s idle_s=%llu frames=%lu flips=%lu rev=%u flip_us=%lu/%lu pads=%lu pumps=%lu mem_free=%u/%u heap=%d/%d fd=%d dir=%d sock=%d menu=%d exiting=%d stage=%s\n",
  (unsigned long long)(now()/1000),tag,(unsigned long long)((now()-last_input)/1000000u),
  (unsigned long)atomic_load(&stat_frames),(unsigned long)atomic_load(&stat_flips),revision,
  (unsigned long)atomic_load(&flip_last_us),(unsigned long)atomic_load(&flip_max_us),
  (unsigned long)atomic_load(&stat_pad_polls),(unsigned long)atomic_load(&stat_pumps),
  avail,total,heap_used,heap_arena,(int)atomic_load(&app_files),(int)atomic_load(&app_dirs),
  (int)atomic_load(&app_sockets),(int)menu_open,(int)exiting,last_stage);
}
void app_health_snapshot(const char *label)
{
 char line[640];health_line(line,sizeof(line),label);
 size_t n=strlen(line);if(n&&line[n-1]=='\n')line[n-1]=0;
 app_log("%s",line);
}
/* Black box: its own thread and file, so a stalled main thread or log cannot hide it. */
static void heartbeat(void *arg)
{
 (void)arg;uint64_t next=now()+APP_HEARTBEAT_US;
 for(;;){
  sys_timer_usleep(500000);
  uint64_t t=now();if(t<next)continue;next=t+APP_HEARTBEAT_US;
  if(hb_fd<0||hb_bytes>APP_LOG_LIMIT)continue;
  char line[640];health_line(line,sizeof(line),"hb");
  size_t n=strlen(line);uint64_t wrote=0;
  if(!sysLv2FsWrite(hb_fd,line,n,&wrote)&&wrote==n){hb_bytes+=n;(void)sysLv2FsFsync(hb_fd);}
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
 note_input();
 if(!sysLv2FsOpen(HB_LOG,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&hb_fd,0666,NULL,0)){
  sys_ppu_thread_t hbt;if(sysThreadCreate(&hbt,heartbeat,NULL,900,0x10000,0,"buzzbridge_heartbeat"))app_log("heartbeat thread not started");
 }else app_log("heartbeat log not opened");
 int rc=cellSysutilRegisterCallback(0,event,NULL);
 if(rc){app_log("ERROR register callback=%08x",(unsigned)rc);return -1;}registered=1;
 app_stage("video init: allocate host memory");
 void *host=memalign(1024*1024,HOST_SIZE);
 if(!host){app_log("ERROR host allocation");return -1;}
 app_stage("video init: cellGcmInit enter");
 rc=cellGcmInit(0x100000,HOST_SIZE,host);
 app_log("video init: cellGcmInit result=%08x",(unsigned)rc);if(rc)return -1;
 for(int i=0;i<2;i++) {
  buffers[i]=rsxMemalign(64,1920u*1080u*4u);
  if(!buffers[i]||cellGcmAddressToOffset(buffers[i],&offsets[i])){
   app_log("ERROR display buffer=%d",i);return -1;
  }
 }
 software=malloc(1920u*1080u*4u);if(!software){app_log("ERROR software framebuffer allocation");return -1;}
 if(configure_video(1))return -1;
 video_ready=1;cellGcmResetFlipStatus();
 rc=ioPadInit(7);pad_owned=!rc;
 if(rc){text_error("Controller initialization failed. Returning to XMB.");return -1;}
 background="Preparing the installer. Please wait...";
 if(frame()||wait_flip()||drain())return -1;
 app_log("video init complete; first frame presented; %ux%u",width,height);
 if(!log_ok){text_error("App log could not be synced. Installation stopped.");return -1;}
 return 0;
}
int app_dialog(const char *text,int question)
{
 int result=CELL_MSGDIALOG_BUTTON_ESCAPE;
 if(app_exiting()||!video_ready)return result;
 if(!log_ok){text_error("App log write failed. Returning to XMB.");exiting=1;return result;}
 dialog_active=1;dialog_text=text;dialog_question=question;dialog_page=dialog_yes=0;revision++;
 unsigned previous[7]={0};int armed=0;note_input();
 app_stage("dialog open; default No; all pages precede confirmation");
 while(!exiting){
  /* User reading time is unbounded; blocked operations still have watchdogs. */
  absolute_deadline=now()+600000000u;
  if(frame())break;if(menu_open){armed=0;continue;}
  if(now()-last_input>APP_IDLE_EXIT_US){app_log("idle exit: no input for 15 minutes");exiting=1;break;}
  int held=0;
  for(int p=0;p<7;p++){
   padData pd={0};if(!pad_present(p)){held|=previous[p]!=0;continue;}stat_pad_polls++;if(ioPadGetData(p,&pd)||pd.len<=0){held|=previous[p]!=0;continue;}
   unsigned buttons=(pd.BTN_UP?1u:0u)|(pd.BTN_DOWN?2u:0u)|(pd.BTN_CROSS?4u:0u)|(pd.BTN_CIRCLE?8u:0u)|(pd.BTN_LEFT?16u:0u)|(pd.BTN_RIGHT?32u:0u);
   unsigned pressed=buttons&~previous[p];previous[p]=buttons;held|=buttons!=0;
   if(!armed||!pressed)continue;
   note_input();

   if(pressed&8u){goto close_dialog;}
   int old_page=dialog_page,old_yes=dialog_yes;
   dialog_page=drawn.page;
   if(pressed&1u){if(dialog_page>0)dialog_page--;dialog_yes=0;}
   if(pressed&2u){if(dialog_page+1<drawn.pages)dialog_page++;dialog_yes=0;}
   if(dialog_page==drawn.page&&drawn.page+1==drawn.pages){if(pressed&16u)dialog_yes=1;if(pressed&32u)dialog_yes=0;}
   if((pressed&4u)&&dialog_page==drawn.page){
    if(dialog_page+1<drawn.pages){dialog_page++;dialog_yes=0;}
    else {result=question?(dialog_yes?CELL_MSGDIALOG_BUTTON_YES:CELL_MSGDIALOG_BUTTON_NO):CELL_MSGDIALOG_BUTTON_OK;goto close_dialog;}
   }
   if(old_page!=dialog_page||old_yes!=dialog_yes)revision++;
  }
  if(!held)armed=1;
 }
close_dialog:
 absolute_deadline=now()+600000000u;
 app_log("choice button=%d exit_request=%d",result,exiting);
 dialog_active=0;dialog_text=NULL;revision++;
 return exiting?CELL_MSGDIALOG_BUTTON_ESCAPE:result;
}
void app_work(const char *stage){if(strcmp(background,stage)){background=stage;revision++;}app_stage(stage);(void)frame();}
int app_menu(const char *title,const char *const *items,int count,int selected)
{
 if(count<1||count>256||!items||!log_ok||app_exiting()||!video_ready)return -1;
 list_title=title;list_items=items;list_count=count;list_selected=selected>=0&&selected<count?selected:0;
 revision++;
 note_input();app_stage("controller list menu open");unsigned previous[7]={0};int armed=0,result=-1;
 while(!exiting){
  absolute_deadline=now()+600000000u;
  if(frame())break;if(menu_open)continue;
  if(now()-last_input>APP_IDLE_EXIT_US){app_log("idle exit: no input for 15 minutes");exiting=1;break;}
  int held=0;
  for(int p=0;p<7;p++){
   padData pd={0};if(!pad_present(p)){held|=previous[p]!=0;continue;}stat_pad_polls++;if(ioPadGetData(p,&pd)||pd.len<=0){held|=previous[p]!=0;continue;}
   unsigned buttons=(pd.BTN_UP?1u:0u)|(pd.BTN_DOWN?2u:0u)|(pd.BTN_CROSS?4u:0u)|(pd.BTN_CIRCLE?8u:0u);
   unsigned pressed=buttons&~previous[p];previous[p]=buttons;held|=buttons!=0;
   if(!armed||!pressed)continue;
   note_input();
   absolute_deadline=now()+600000000u;
   if(pressed&8u)goto finish_menu;
   int old_selected=list_selected;
   if(pressed&1u)list_selected=(list_selected+count-1)%count;
   if(pressed&2u)list_selected=(list_selected+1)%count;
   if(old_selected!=list_selected)revision++;
   if(pressed&4u){result=list_selected;goto finish_menu;}
  }
  if(!held)armed=1;
 }
finish_menu:
 app_log("controller menu result=%d",result);list_items=NULL;list_title=NULL;revision++;return result;
}
void app_ui_exit(int result)
{
 app_stage("exit: returning to XMB");
 if(registered){(void)cellSysutilUnregisterCallback(0);registered=0;}
 if(pad_owned){(void)ioPadEnd();pad_owned=0;}
 /* Keep RSX/host/software allocations alive. Process teardown reclaims them. */
 app_log("exit result=%d exit_request=%d log_ok=%d",result,exiting,log_ok);
 sysProcessExit(result);
}
