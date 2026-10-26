/* SPDX-License-Identifier: MIT. Minimal XMB system-dialog menu, based on PS3DK samples. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/process.h>
#include "app_ui.h"
#include <cell/msg_dialog.h>
#include <cell/sysmodule.h>
#include <net/netctl.h>
#include "logic.h"
#include "files.h"
#include "ps3_api.h"
/* GCM renderer and independent watchdog need an ordinary application stack. */
SYS_PROCESS_PARAM(1100,0x100000);
static char url[64]="http://<ps3-ip>:8420/";
static void info(const char *status)
{char msg[900];app_log("status: %s",status);snprintf(msg,sizeof(msg),"Buzz! Bridge\n%s\n\nPhone URL (available in supported game):\n%s",status,url);(void)app_dialog(msg,0);}
static int present(const char *path) {struct stat st;return !stat(path,&st);}
static int touch_disable(void)
{
 FILE *f=fopen(BUZZ_DISABLE,"wb");if(!f)return -1;int rc=fflush(f);if(!rc)rc=fsync(fileno(f));if(fclose(f))rc=-1;return rc;
}
static int session(char *message,size_t cap)
{
 if(present(BUZZ_STOP)) {snprintf(message,cap,"Safety stop file exists: " BUZZ_STOP "\nRemove it deliberately, then reboot to reset latched helpers.");return -1;}
 if(present(BUZZ_DISABLE) && unlink(BUZZ_DISABLE)) {snprintf(message,cap,"Cannot remove disable file; check filesystem permissions.");return -1;}
 unsigned free_slot=0,found=0;
 for(unsigned slot=1;slot<=6;slot++) {
  char name[64]={0},path[512]={0};
  int rc=buzz_mapi(0x47,slot,buzz_ptr(name),buzz_ptr(path),0);
  app_log("inspect VSH slot=%u result=%08x name=%s path=%s",slot,(unsigned)rc,name,path);
  if(rc && (unsigned)rc!=0x80010006u) {snprintf(message,cap,"Cannot inspect VSH slots: %08x. Cobra/PS3MAPI must be enabled.",(unsigned)rc);return -1;}
  if(!strcmp(name,"ps3buzz_vsh")||!strcmp(path,BUZZ_HELPER))found++;
  if(!name[0]&&!path[0]&&!free_slot)free_slot=slot;
 }
 if(found>1) {snprintf(message,cap,"Multiple helpers loaded. Disable at boot and reboot before retrying.");return -1;}
 if(found) {snprintf(message,cap,"Helper already resident. No second load.\nIf R2/stop was latched, reboot to reset it.");return 0;}
 if(!free_slot) {snprintf(message,cap,"No free VSH plugin slot. Other plugins were left alone.");return -1;}
 app_stage("load session helper enter");
 int rc=buzz_load_vsh(free_slot,BUZZ_HELPER);
 app_log("load session helper slot=%u result=%08x",free_slot,(unsigned)rc);
 snprintf(message,cap,"Session helper slot %u: result %08x.\n%s",free_slot,(unsigned)rc,rc?"No retry in this session; reboot before retrying.":"Return to XMB, launch a supported original Buzz! title and wait at its menu for about two minutes. Restart the PS3 before launching another game in this trial.");
 return rc? -1:0;
}
int main(void)
{
 if(app_ui_init()){app_ui_exit(1);return 1;}
 app_stage("platform check: CFW/Cobra");
 if(buzz_is_hen()) {
  info("CFW/Cobra build only. This helper uses APP key revision 1.\nPublished HEN guidance requires revision 0A; session/boot setup is refused.\nNo plugin files or boot list changed.");
  app_ui_exit(1);return 1;
 }
 app_stage("network init enter (IP lookup only)");
 int net_module=cellSysmoduleLoadModule(CELL_SYSMODULE_NET)==0;
 int ctl_module=cellSysmoduleLoadModule(CELL_SYSMODULE_NETCTL)==0;
 int ctl_available=ctl_module || cellSysmoduleIsLoaded(CELL_SYSMODULE_NETCTL)==0;
 int net_owned=ctl_available && netCtlInit()==0;union net_ctl_info net;memset(&net,0,sizeof(net));
 if(ctl_available&&!netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS,&net)&&net.ip_address[0]){net.ip_address[15]=0;snprintf(url,sizeof(url),"http://%s:8420/",net.ip_address);}
 app_log("network init complete net=%d ctl=%d owned=%d url=%s",net_module,ctl_module,net_owned,url);
 char message[640]="";
 (void)mkdir("/dev_hdd0/plugins",0777);(void)mkdir(BUZZ_DIR,0777);(void)mkdir("/dev_hdd0/tmp",0777);
 app_stage("install begin: safety disable then packaged payloads");
 if(app_exiting())goto finish;
 int was_disabled=present(BUZZ_DISABLE);
 if(touch_disable()) {info("Cannot create safety disable file. Installation stopped.");goto finish;}
 /* Payloads from our installed USRDIR only, never from mounted game paths. */
 if(buzz_install("/dev_hdd0/game/BUZZB0001/USRDIR",BUZZ_DIR,message,sizeof(message))) {info(message);goto finish;}
 app_stage("install complete: all three payloads verified and activated");
 if(app_exiting())goto finish;
 if(!was_disabled && unlink(BUZZ_DISABLE)) {info("Installed, but disable file could not be cleared.");goto finish;}
 snprintf(message,sizeof(message),"Buzz! Bridge\nPhone URL (in supported game): %s\n\nStart for this session?\nYes: start helper, then exit to XMB.\nNo: boot options. Circle: exit.\n\nBoot files are unchanged by session start.",url);
 int choice=app_dialog(message,1);
 app_log("choice session=%d",choice);
 if(choice==CELL_MSGDIALOG_BUTTON_YES) {
  app_stage("session helper start enter");
  int rc=session(message,sizeof(message));app_log("session result=%d %s",rc,message);info(message);goto finish;
 }
 if(app_exiting() || choice==CELL_MSGDIALOG_BUTTON_ESCAPE)goto finish;
 choice=app_dialog("Enable Buzz! Bridge at boot?\nYes: add exactly one helper line with a backup.\nNo: Disable at boot option. Circle: exit.\n\nTest session mode successfully first.\nHEN loads after activation; USB boot lists can override HDD.",1);
 app_log("choice enable_boot=%d",choice);
 if(choice==CELL_MSGDIALOG_BUTTON_YES) {
  if(present("/dev_usb000/boot_plugins.txt")||present("/dev_usb001/boot_plugins.txt")) {info("USB boot list detected. HDD boot changes refused; unplug that USB and retry.");goto finish;}
  app_stage("boot edit enter");
  int rc=buzz_edit_boot_file(BUZZ_BOOT,1,message,sizeof(message));
  app_log("boot edit result=%d %s",rc,message);
  if(rc>=0) {
   if(present(BUZZ_DISABLE)&&unlink(BUZZ_DISABLE))snprintf(message,sizeof(message),"Boot line installed, but disable file remains. Start session after fixing permissions.");
   else snprintf(message,sizeof(message),"Boot helper enabled%s.\nRestart after your session test. Hold R2 through XMB startup to skip.\nBackup: boot_plugins.txt.ps3buzz.bak",rc?"":" (already configured)");
  }
  info(message);goto finish;
 }
 if(app_exiting() || choice==CELL_MSGDIALOG_BUTTON_ESCAPE)goto finish;
 choice=app_dialog("Disable Buzz! Bridge at boot?\nYes: remove only this helper's lines, keep a backup, and create its disable file.\nNo / Circle: leave boot settings unchanged and exit.",1);
 app_log("choice disable_boot=%d",choice);
 if(choice==CELL_MSGDIALOG_BUTTON_YES) {
  if(touch_disable()) {info("Cannot create disable file. Boot list left unchanged.");goto finish;}
  app_stage("boot edit enter");
  int rc=buzz_edit_boot_file(BUZZ_BOOT,0,message,sizeof(message));
  app_log("boot edit result=%d %s",rc,message);
  if(rc>=0)snprintf(message,sizeof(message),"Disabled. Other boot entries preserved.\nResident helper becomes inert; restart to remove it.\nQuit any running game to remove the game plugin.");
  info(message);
 }
finish:
 app_stage("cleanup enter");
 if(net_owned)netCtlTerm();
 if(ctl_module)(void)cellSysmoduleUnloadModule(CELL_SYSMODULE_NETCTL);
 if(net_module)(void)cellSysmoduleUnloadModule(CELL_SYSMODULE_NET);
 app_ui_exit(0);return 0;
}
