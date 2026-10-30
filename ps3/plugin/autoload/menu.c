/* SPDX-License-Identifier: MIT. New routes are isolated from the PS3 helper. */
#include "menu.h"
#include "app_ui.h"
#include "app_health.h"
#include "iso_patch.h"
#include "netemu_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bb_dir.h"
#include <sys/stat.h>
#include <time.h>
#define USR "/dev_hdd0/game/BUZZB0001/USRDIR"
#define MAX_ITEMS 254 /* plus Previous/Next: UI limit 256; every page accessible */
static int yes(const char *text){return !app_exiting()&&app_dialog(text,1)==CELL_MSGDIALOG_BUTTON_YES&&!app_exiting();}
static int tell(const char *text){return !app_exiting()&&app_dialog(text,0)!=CELL_MSGDIALOG_BUTTON_ESCAPE&&!app_exiting();}
static int exists(const char *path){return bb_path_exists(path);}
static int netemu_state(void){char hash[65];return bb_file_hash(bb_console_netemu_paths.current,hash)?2:bb_netemu_classify(hash);}
static int choose_number(const char *question,int low,int high,int def){char storage[10][48];const char *items[10];int n=0;for(int i=low;i<=high;i++){snprintf(storage[n],sizeof(storage[n]),"%d%s",i,i==def?" (default)":"");items[n]=storage[n];n++;}int k=app_menu(question,items,n,def-low);return k<0?-1:low+k;}
typedef struct {int step,k,pairs,ds;} setup_cursor;
static int setup(bb_iso_options *o,setup_cursor *c){
 int k,pairs,ds;
 if(c->step==4)goto sharing;
choices:
 if(app_exiting())return -1;
 memset(o,0,sizeof(*o));o->flags=0x11;o->attach_ms=100;o->heartbeat_ms=100;
 c->step=0;
 k=app_menu("Your setup: What do you have?",bb_setup_names,7,c->k);if(k<0)return -1;c->k=k;
 if(k<5){o->layout=(unsigned)k;return 0;}o->layout=13;
 if(k==5){o->pairs=1;o->whole=2;return 0;}
explanation:
 if(!tell("A Buzz! set (4 buzzers + 1 receiver) counts as 1 pair: on a PS3 only 2 of its 4 buzzers work.\n\nThis ISO route uses the original netemu. With modified netemu, keep the ISO original; no ISO patch is needed."))goto choices;
pairs:
 if(app_exiting())return -1;
 pairs=choose_number("How many pairs of Buzz! buzzers do you have? (1 pair = 2 players)",0,2,0);if(pairs<0)goto explanation;c->pairs=pairs;
dualshocks:
 if(app_exiting())return -1;
 ds=choose_number("How many DualShocks do you have?",0,8,4-2*c->pairs);if(ds<0)goto pairs;c->ds=ds;
 if(!c->pairs&&!ds){tell("You need at least one pair of Buzz! buzzers or one DualShock.");goto dualshocks;}
sharing:
 if(app_exiting())return -1;
 pairs=c->pairs;ds=c->ds;
 int usable=ds<8-2*pairs?ds:8-2*pairs;char names[5][100];const char *items[5];unsigned wh[5],sh[5];int n=0;
 for(int shared=0;shared<=usable&&shared<=4;shared++){
  int whole=usable-shared,players=2*pairs+whole+2*shared;if(players<1||players>8)continue;
  snprintf(names[n],sizeof(names[n]),"%d players: %d pairs + %d whole DualShocks + %d shared DualShocks",players,pairs,whole,shared);items[n]=names[n];wh[n]=(unsigned)whole;sh[n]=(unsigned)shared;n++;
 }
 int option=app_menu("You can play with: Which one?",items,n,0);if(option<0)goto dualshocks;c->step=4;o->pairs=(unsigned)pairs;o->whole=wh[option];o->shared=sh[option];return 0;
}
typedef struct {bb_dir_entry entry;char label[352];} item;
static int item_cmp(const void *a,const void *b){return strcmp(((const item*)a)->entry.path,((const item*)b)->entry.path);}
static void iso_actions(const char *path);
static int browse(const char *root,char result[1024],int depth){
 if(depth>8){tell("Subfolder limit (8 levels) reached.");return -1;}
 item *entries=calloc(MAX_ITEMS,sizeof(*entries));if(!entries){tell("Cannot allocate ISO browser page.");return -1;}
 size_t page=0;int rc=-1;
 while(!app_exiting()){
  bb_dir dir;if(bb_dir_open(&dir,root)){tell("Folder unavailable. See /dev_hdd0/tmp/buzzbridge_app.log. USB support depends on the PS3 filesystem driver. Use /dev_hdd0/PS2ISO for plain .iso images.");break;}
  int n=0,more=0,status;size_t skipped=0;bb_dir_entry e;
  while((status=bb_dir_read(&dir,&e))>0){
   if(!bb_dir_visible(&e))continue;
   if(skipped<page*MAX_ITEMS){skipped++;continue;}
   if(n==MAX_ITEMS){more=1;break;}
   entries[n].entry=e;bb_dir_label(&e,entries[n].label,sizeof(entries[n].label));n++;
  }
  bb_dir_close(&dir);
  if(status<0)tell("Folder read failed; this page may be incomplete. See /dev_hdd0/tmp/buzzbridge_app.log.");
  qsort(entries,(size_t)n,sizeof(*entries),item_cmp);
  const char *labels[MAX_ITEMS+2];for(int i=0;i<n;i++)labels[i]=entries[i].label;
  int previous=-1,next=-1,total=n;
  if(page){previous=total;labels[total++]="Previous page";}
  if(more){next=total;labels[total++]="Next page (more than 254 entries)";}
  if(!total){tell("No .iso files or subfolders in this folder. Split, encrypted and non-ISO images are unsupported.");break;}
  char title[1200];snprintf(title,sizeof(title),"Page %llu%s: %s",(unsigned long long)page+1,more?" (more entries on next page)":"",root);
  int k=app_menu(title,labels,total,0);if(k<0)break;
  if(k==previous){page--;continue;}if(k==next){page++;continue;}
  if(!entries[k].entry.path_known){tell("? Path is too long to open. See app log; shorten the folder path.");continue;}
  if(entries[k].entry.directory==1||(entries[k].entry.directory<0&&!bb_iso_name(entries[k].entry.name))){
   if(!browse(entries[k].entry.path,result,depth+1)){rc=0;break;}
  }else if(result){snprintf(result,1024,"%s",entries[k].entry.path);rc=0;break;}
  else iso_actions(entries[k].entry.path); /* Keep this folder/page on back. */
 }
 free(entries);return rc;
}
static int pick_iso(char path[1024]){
 char locations[10][64];const char *items[10];int n=2;snprintf(locations[1],64,"/dev_hdd0");items[1]=locations[1];snprintf(locations[0],64,"/dev_hdd0/PS2ISO");items[0]=locations[0];
 for(int i=0;i<8;i++){char usb[64];snprintf(usb,sizeof(usb),"/dev_usb%03d",i);if(exists(usb)){snprintf(locations[n],64,"%s",usb);items[n]=locations[n];n++;}}
 while(!app_exiting()){
  int k=app_menu("Choose ISO location",items,n,0);if(k<0)return -1;
  if(!browse(locations[k],path,0))return 0;
 }
 return -1;
}
#if defined(BB_BUNDLED_NETEMU) || defined(BUZZ_APP_HOST_TEST)
static int scan_patched(const char *root,int depth,int *budget){
 if(depth>4||*budget<=0)return 0;bb_dir dir;if(bb_dir_open(&dir,root))return 0;
 int count=0;bb_dir_entry e;
 while(*budget>0&&bb_dir_read(&dir,&e)>0){
  --*budget; /* Failed metadata also consumes budget: scan stays bounded. */
  if(!e.path_known)continue;
  if(e.directory==1)count+=scan_patched(e.path,depth+1,budget);
  else if(bb_iso_name(e.name)){bb_iso_info i;char msg[600];if(!bb_iso_inspect(e.path,&i,msg,sizeof(msg))&&i.patched)count++;}
 }
 bb_dir_close(&dir);return count;
}
#endif
static void iso_actions(const char *path){
 char msg[1800];bb_iso_info info;
details:
 if(app_exiting())return;
 if(bb_iso_inspect(path,&info,msg,sizeof(msg))){
  char detail[2200];snprintf(detail,sizeof(detail),"This image is not a supported Buzz! game (SYSTEM.CNF/module not recognised).\nSupported module families: Schools/Mega, Junior, Monster Rumble. See README for ISO layout limits.\n\nDetails: %s",msg);tell(detail);return;
 }
 if(!info.known&&!info.patched){char detail[2200];snprintf(detail,sizeof(detail),"This image is not a supported Buzz! game (SYSTEM.CNF/module not recognised).\nSupported module families: Schools/Mega, Junior, Monster Rumble. See README for ISO layout limits.\n\n%s",msg);if(!tell(detail))return;}else if(!tell(msg))return;
 const char *const actions[]={"Check only (no ISO writes)","Patch in place (backup first)","Restore original ISO module", "Check restore only"};
actions:
 if(app_exiting())return;
 int k=app_menu("PS2 ISO actions",actions,4,0);if(k<0)goto details;
 if(k==2||k==3){
  if(k==2&&!yes("Restore this ISO's original module from its recovery sidecars?\n\nOnly the saved extent will be overwritten. The backups are verified first.\nYes: restore. No: cancel. Circle: back."))goto actions;
  (void)bb_iso_restore(path,k==3,msg,sizeof(msg));tell(msg);goto actions;
 }
 int state=netemu_state();
route_guard:
 if(app_exiting())return;
 if(state==1){
  tell("WARNING: a known modified Buzz! netemu is installed. An ISO patch must not be combined with that route.\n\nRestore stock netemu first. For real Buzz! handsets with modified netemu, use an original unpatched ISO.");goto actions;
 }
 if(state==2&&!yes("The netemu fingerprint is unrecognized or unreadable. It may be original firmware from another revision.\n\nContinue only if this console keeps the ORIGINAL netemu for its firmware. A modified Buzz! netemu must not be combined with an ISO patch.\n\nYes: I keep original netemu; continue. No: cancel. Circle: back."))goto actions;
 bb_iso_options o;setup_cursor cursor={0};
wizard:
 if(setup(&o,&cursor)){if(state==2)goto route_guard;goto actions;}
 if(k==1){
  snprintf(msg,sizeof(msg),"Patch this ISO in place?\n%.600s\n\n%s (layout %u)\nPairs: %u; whole DualShocks: %u; shared: %u\nOriginal bytes and a small sidecar are saved beside the ISO first.\nYes: write only the module extent. No: cancel. Circle: back.",path,bb_layout_names[o.layout],o.layout,o.pairs,o.whole,o.shared);
  int choice=app_dialog(msg,1);if(app_exiting())return;if(choice==CELL_MSGDIALOG_BUTTON_ESCAPE)goto wizard;if(choice!=CELL_MSGDIALOG_BUTTON_YES)goto actions;
 }
 (void)bb_iso_patch(path,USR "/bbusbd.irx",&o,k==0,msg,sizeof(msg));tell(msg);goto actions;
}
static void iso_menu(void){
 while(!app_exiting()&&tell("Patch a PS2 ISO on this PS3\n\nCFW only, with original netemu and plain .iso images through Cobra. With modified netemu, use an original ISO: no ISO patch is needed. Do not combine the two routes.\n\nOnly a small module extent is overwritten. Keep recovery files beside your ISO.")){
  (void)pick_iso(NULL);
 }
}
static int confirm_current(char hash[65]){
 char msg[1100];app_work("Hashing installed netemu");
 if(bb_file_hash(bb_console_netemu_paths.current,hash)){tell("Cannot read installed netemu; operation refused.");return -1;}
 if(!bb_netemu_known(hash)){
  snprintf(msg,sizeof(msg),"UNKNOWN FILE - extra OVERWRITE confirmation\nInstalled SHA256: %s\n\nThis is outside the original/known-build allow-list. It may belong to another firmware revision, or be an interrupted partial file. The current file will be backed up and verified first.\n\nExplicitly accept overwriting this unknown file?\nYes: OVERWRITE. No: cancel. Circle: back.",hash);
  int choice=app_dialog(msg,1);if(app_exiting())return -1;
  if(choice==CELL_MSGDIALOG_BUTTON_ESCAPE)return -2;
  if(choice!=CELL_MSGDIALOG_BUTTON_YES)return -1;
 }
 return 0;
}
/* 1: back to the preceding wizard screen; 0: done, refused or No. */
static int replace_netemu(const char *source,const char *wanted,const char *description){
 char current[65],msg[2400];
 if(bb_console_netemu_ops.is_hen()){tell("CFW only. HEN cannot replace netemu.");return 0;}
 app_work("Checking installed netemu");
 if(bb_file_hash(bb_console_netemu_paths.current,current)){tell("Cannot read installed netemu; operation refused.");return 0;}
 if(!strcmp(current,wanted)){tell("That exact netemu hash is already installed. Nothing to do.");return 0;}
confirmation:
 if(app_exiting())return 0;
 snprintf(msg,sizeof(msg),"%s\nSource SHA256: %s\nCurrent: %s\nSHA256: %s\n\nBack up current file, verify the backup, mount dev_blind, overwrite netemu, read back and verify SHA256, then unmount. A replacement failure triggers automatic restoration of the just-saved backup.\n\nYes: replace netemu. No: cancel. Circle: back.",description,wanted,bb_netemu_name(current),current);
 int choice=app_dialog(msg,1);if(app_exiting())return 0;
 if(choice==CELL_MSGDIALOG_BUTTON_ESCAPE)return 1;
 if(choice!=CELL_MSGDIALOG_BUTTON_YES)return 0;
 char confirmed[65];int rc=confirm_current(confirmed);if(rc==-2)goto confirmation;if(rc)return 0;
 if(strcmp(current,confirmed)){tell("Installed file changed after confirmation. Nothing installed.");return 0;}
 app_health_snapshot("netemu replace: before");
 (void)bb_netemu_replace(&bb_console_netemu_paths,&bb_console_netemu_ops,source,wanted,current,!bb_netemu_known(current),msg,sizeof(msg));
 app_health_snapshot("netemu replace: after");
 tell(msg);return 0;
}
/* All backups are reachable in pages. Metadata/hash failures remain visible;
 * invalid files cannot authorize a restore. No backup is ever removed. */
static void backup_list(int restore){
 size_t page=0;bb_netemu_copy *copies=calloc(16,sizeof(*copies));if(!copies){tell("Cannot allocate backup list.");return;}
 while(!app_exiting()){
  size_t total=0;int n=bb_netemu_copies(&bb_console_netemu_paths,page*16,copies,16,&total,NULL);
  if(n<0){tell("Cannot read the backup folder. See app log. No files changed.");break;}
  if(!total){tell("No saved netemu backups exist in /dev_hdd0/buzzbridge/netemu_backup/.");break;}
  const char *labels[18];char names[16][320];int prev=-1,next=-1,count=n;
  for(int i=0;i<n;i++){snprintf(names[i],sizeof(names[i]),"%s - %s",copies[i].name,copies[i].valid?bb_netemu_name(copies[i].hash):"INVALID / unreadable / partial");labels[i]=names[i];}
  if(page){prev=count;labels[count++]="Previous backups";}
  if((page+1)*16<total){next=count;labels[count++]="Next backups";}
  int k=app_menu(restore?"Choose a specific backup to restore":"STATUS: saved netemu backups",labels,count,0);if(k<0)break;
  if(k==prev){page--;continue;}if(k==next){page++;continue;}
  char msg[1800];snprintf(msg,sizeof(msg),"Backup: %s\n%s\nSHA256: %s\n%s",copies[k].path,bb_netemu_name(copies[k].hash),copies[k].hash,copies[k].valid?"Backup hash matches its saved filename.":"INVALID: unreadable or hash does not match filename. Restore refused.");
detail:
  if(tell(msg)&&restore&&copies[k].valid&&replace_netemu(copies[k].path,copies[k].hash,"RESTORE selected user backup"))goto detail;
 }
 free(copies);
}
static void restore_netemu(void){
 bb_netemu_copy oldest;size_t total;
 if(bb_netemu_copies(&bb_console_netemu_paths,0,NULL,0,&total,&oldest)<0){tell("Cannot read backup folder; automatic restore refused.");return;}
 if(oldest.valid){replace_netemu(oldest.path,oldest.hash,"RESTORE your own original: oldest verified backup that is not one of our builds");return;}
#ifdef BB_BUNDLED_NETEMU
 replace_netemu(USR "/netemu/ps2_netemu_original.self",BB_STOCK_SELF,"RESTORE fallback: bundled original Evilnat 4.93 netemu. No user original backup was found. Confirm only if this firmware file is appropriate for your console.");
#else
 tell("No user original backup found. netemu files not included in this build. You can restore a specific saved user backup from the list.");
#endif
}
#ifdef BB_BUNDLED_NETEMU
static void install_netemu(void){
 if(app_exiting())return;
 if(bb_console_netemu_ops.is_hen()){tell("CFW only. HEN cannot install netemu.");return;}
warning:
 if(app_exiting())return;
 int choice=app_dialog("Modified netemu needs original, unpatched ISOs. Restore any already-patched ISO before playing with this route. Do not combine the two routes.\n\nYes: continue. No: cancel. Circle: back.",1);
 if(app_exiting())return;if(choice!=CELL_MSGDIALOG_BUTTON_YES)return;
 app_work("Checking ISO folders for existing Buzz! Bridge patches");
 int budget=256,patched=scan_patched("/dev_hdd0/PS2ISO",0,&budget);
 for(int usb=0;usb<8&&budget>0;usb++){char path[64];snprintf(path,sizeof(path),"/dev_usb%03d",usb);patched+=scan_patched(path,0,&budget);}
patched_warning:
 if(app_exiting())return;
 if(patched){
  choice=app_dialog("WARNING: already-patched ISO(s) were found. Restore them before playing with modified netemu. The scan is bounded to 256 entries and four subfolder levels; other patches may exist.\n\nYes: continue to install confirmation. No: cancel. Circle: back.",1);
  if(app_exiting())return;if(choice==CELL_MSGDIALOG_BUTTON_ESCAPE)goto warning;if(choice!=CELL_MSGDIALOG_BUTTON_YES)return;
 }
 if(replace_netemu(USR "/netemu/ps2_netemu.self",BB_MODIFIED_SELF,"Install the modified ps2_netemu.self (four handsets + lamps)")){
  if(patched)goto patched_warning;goto warning;
 }
}
#endif
static void netemu_menu(void){
 (void)mkdir("/dev_hdd0/buzzbridge",0777);(void)mkdir(bb_console_netemu_paths.directory,0777);
#ifdef BB_BUNDLED_NETEMU
 const char *const actions[]={"STATUS: installed netemu and backups","Install the modified ps2_netemu.self (four handsets + lamps)","RESTORE your own original (oldest backup)","RESTORE a specific user backup","About this full build"};
#else
 const char *const actions[]={"STATUS: installed netemu and backups","RESTORE your own original (oldest backup)","RESTORE a specific user backup","About this public build"};
 if(!tell("netemu files not included in this build\n\nSTATUS and restoring user backups are available. CFW is required for replacement."))return;
#endif
 while(!app_exiting()){
  int k=app_menu("Real Buzz! handsets in PS2 games - no ISO patching",actions,(int)(sizeof(actions)/sizeof(actions[0])),0);if(k<0)return;
  if(k==0){char msg[1800];(void)bb_netemu_status(&bb_console_netemu_paths,msg,sizeof(msg));while(tell(msg))backup_list(0);continue;}
#ifdef BB_BUNDLED_NETEMU
  if(k==1){install_netemu();continue;}k--;
#endif
  if(k==1)restore_netemu();
  if(k==2)backup_list(1);
  if(k==3){
#ifdef BB_BUNDLED_NETEMU
   tell("About Buzz! Bridge - FULL BUILD\n\nIncludes the modified ps2_netemu.self (four handsets + lamps) and ps2_netemu_original.self (Evilnat 4.93 restore fallback). The owner plans to publish this package and the standalone ps2_netemu.self together in a GitHub release.\n\nThe owner tested the modified file with four handsets and game lamps on a real console. This app's install/restore and rollback paths have only PC tests; they have not been verified on a PS3.");
#else
   tell("About Buzz! Bridge - PUBLIC BUILD\n\nnetemu files not included in this build. Entry 2 offers STATUS and restoring user backups only.\n\nThe new install/restore and rollback paths have only PC tests; they have not been verified on a PS3.");
#endif
  }
 }
}
int bb_main_menu(void){
 const char *const items[]={"1. Phones and DualShock for PS3 Buzz! games","2. Real Buzz! handsets in PS2 games - no ISO patching (CFW only)","3. Patch a PS2 ISO (CFW, original netemu)","Exit to XMB"};
 bb_tick=app_work;
 while(!app_exiting()){
  int k=app_menu("Choose how to play",items,4,0);if(k<0)continue;if(k==3)return -1;if(!k)return 0;if(k==1)netemu_menu();if(k==2)iso_menu();
 }
 return -1;
}
