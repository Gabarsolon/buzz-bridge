/* Host-only declarations for compiling the production UI state machine. */
#ifndef APP_MOCK_H
#define APP_MOCK_H
#include <stdint.h>
#include <stddef.h>
#define CELL_MSGDIALOG_BUTTON_ESCAPE -1
#define CELL_MSGDIALOG_BUTTON_YES 1
#define CELL_MSGDIALOG_BUTTON_NO 2
#define CELL_MSGDIALOG_BUTTON_OK 1
#define VIDEO_STATE_BUSY 3
#define VIDEO_RESOLUTION_720 2
#define VIDEO_ASPECT_4_3 1
#define VIDEO_ASPECT_16_9 2
#define VIDEO_SCANMODE_INTERLACE 0
#define VIDEO_SCANMODE_PROGRESSIVE 1
#define CELL_MSGDIALOG_TYPE_SE_TYPE_NORMAL 0
#define CELL_MSGDIALOG_TYPE_BG_VISIBLE 0
#define CELL_MSGDIALOG_TYPE_BUTTON_TYPE_YESNO 1
#define CELL_MSGDIALOG_TYPE_BUTTON_TYPE_OK 2
#define CELL_MSGDIALOG_TYPE_DEFAULT_CURSOR_NO 4
#define CELL_SYSUTIL_REQUEST_EXITGAME 1
#define CELL_SYSUTIL_SYSTEM_MENU_OPEN 2
#define CELL_SYSUTIL_SYSTEM_MENU_CLOSE 3
#define SYS_O_WRONLY 1
#define SYS_O_CREAT 2
#define SYS_O_TRUNC 4
#define GCM_SURFACE_X8R8G8B8 0
#define GCM_SURFACE_TARGET_0 0
#define GCM_LOCATION_RSX 0
#define GCM_SURFACE_ZETA_Z16 0
#define GCM_SURFACE_TYPE_LINEAR 0
#define GCM_SURFACE_CENTER_1 0
#define GCM_COLOR_MASK_R 1
#define GCM_COLOR_MASK_G 2
#define GCM_COLOR_MASK_B 4
#define GCM_COLOR_MASK_A 8
#define GCM_CLEAR_R 1
#define GCM_CLEAR_G 2
#define GCM_CLEAR_B 4
#define GCM_CLEAR_A 8
#define VIDEO_BUFFER_FORMAT_XRGB 0
#define GCM_FLIP_VSYNC 0
#define CELL_DBGFONT_FRAGMENT_SIZE 1024
#define CELL_DBGFONT_TEXTURE_SIZE 1024
#define CELL_DBGFONT_VERTEX_SIZE 36
#define CELL_DBGFONT_VERTEX_LOCAL 1
#define CELL_DBGFONT_TEXTURE_LOCAL 2
typedef uintptr_t sys_addr_t;
typedef uint64_t sys_ppu_thread_t;
typedef struct {int unused;} CellGcmContextData;
extern CellGcmContextData mock_context;
#define CELL_GCM_CURRENT (&mock_context)
typedef struct {
 int colorFormat,colorTarget,colorLocation[4],depthFormat,depthLocation,type,antiAlias;
 uint32_t colorOffset[4],colorPitch[4],depthOffset,depthPitch,width,height;
} CellGcmSurface;
typedef struct {unsigned state;struct {unsigned resolution,aspect,scanMode;} displayMode;} videoState;
typedef struct {uint16_t width,height;} videoResolution;
typedef struct {unsigned resolution,format,pitch,aspect;} videoConfiguration;
typedef struct {sys_addr_t localBufAddr;uint32_t localBufSize,option;uint16_t screenWidth,screenHeight;} CellDbgFontConfigGcm;
typedef struct {int len,BTN_START,BTN_CIRCLE,BTN_CROSS,BTN_UP,BTN_DOWN,BTN_LEFT,BTN_RIGHT;} padData;
typedef struct {unsigned connected;unsigned char status[8];} padInfo;
int ioPadGetInfo(padInfo *);
uint64_t sys_time_get_system_time(void);
void sys_timer_usleep(unsigned);
void usleep(unsigned);
void sysProcessExit(int);
int sysThreadCreate(sys_ppu_thread_t *,void (*)(void *),void *,int,uint64_t,uint64_t,char *);
int sysLv2FsMkdir(const char *,unsigned);
int sysLv2FsOpen(const char *,int,int *,unsigned,void *,unsigned);
int sysLv2FsWrite(int,const void *,uint64_t,uint64_t *);
int sysLv2FsFsync(int);
void *memalign(size_t,size_t);
void *rsxMemalign(size_t,size_t);
int cellSysutilRegisterCallback(int,void (*)(uint64_t,uint64_t,void *),void *);
int cellSysutilUnregisterCallback(int);
int cellSysutilCheckCallback(void);
int cellGcmInit(unsigned,unsigned,void *);
void cellGcmSetWriteBackEndLabel(CellGcmContextData *,unsigned,uint32_t);
void cellGcmFlush(CellGcmContextData *);
void *cellGcmGetLabelAddress(unsigned);
unsigned cellGcmGetFlipStatus(void);
void cellGcmResetFlipStatus(void);
void cellGcmSetSurface(CellGcmContextData *,CellGcmSurface *);
void cellGcmSetViewport(CellGcmContextData *,int,int,unsigned,unsigned,float,float,float *,float *);
void cellGcmSetScissor(CellGcmContextData *,int,int,unsigned,unsigned);
void cellGcmSetColorMask(CellGcmContextData *,unsigned);
void cellGcmSetClearColor(CellGcmContextData *,uint32_t);
void cellGcmSetClearSurface(CellGcmContextData *,unsigned);
int cellGcmSetFlip(CellGcmContextData *,uint8_t);
void cellGcmSetWaitFlip(CellGcmContextData *);
int videoGetState(int,int,videoState *);
int videoGetResolution(unsigned,videoResolution *);
int videoConfigure(int,videoConfiguration *,void *,int);
void cellGcmSetFlipMode(int);
int cellGcmAddressToOffset(void *,uint32_t *);
int cellGcmSetDisplayBuffer(int,uint32_t,unsigned,unsigned,unsigned);
int cellDbgFontInitGcm(CellDbgFontConfigGcm *);
int cellDbgFontPrintf(float,float,float,uint32_t,const char *,...);
int cellDbgFontDrawGcm(void);
int ioPadInit(int);
int ioPadEnd(void);
int ioPadGetData(int,padData *);
int cellMsgDialogOpen2(unsigned,const char *,void (*)(int,void *),void *,void *);
int cellMsgDialogAbort(void);
#endif
