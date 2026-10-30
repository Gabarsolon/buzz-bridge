/* Actual entry-2 menu with deterministic user/filesystem/transaction boundaries. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
int mkdir(const char *,int);
#include "../ps3/plugin/autoload/menu.c"
static int mode,unknown,questions,replacements,cancel_at,tells;
static char last_message[3000];
typedef struct {const char *screen;int choice;} step;
static const step *script;
static size_t cursor,length;
static int iso_calls,dir_position;
#define BACK CELL_MSGDIALOG_BUTTON_ESCAPE
#define YES CELL_MSGDIALOG_BUTTON_YES
#define NO CELL_MSGDIALOG_BUTTON_NO
#define OK CELL_MSGDIALOG_BUTTON_OK
static int reply(const char *s){
 assert(cursor<length);
 if(!strstr(s,script[cursor].screen)){fprintf(stderr,"step %zu expected %s, got %s\n",cursor,script[cursor].screen,s);abort();}
 return script[cursor++].choice;
}
int mkdir(const char *p,int m){(void)p;(void)m;return 0;}
void app_log(const char *fmt,...){(void)fmt;}
void app_work(const char *s){(void)s;}
void app_health_snapshot(const char *s){(void)s;}
int app_exiting(void){return 0;}
int app_dialog(const char *s,int q){
 if(script)return reply(s);
 if(q){questions++;if(strstr(s,"fallback"))assert(strstr(s,"Evilnat 4.93"));return questions==cancel_at?CELL_MSGDIALOG_BUTTON_NO:CELL_MSGDIALOG_BUTTON_YES;}
 tells++;snprintf(last_message,sizeof(last_message),"%s",s);return 0;
}
int app_menu(const char *title,const char *const *items,int count,int selected){
 if(script)return reply(title);
 (void)selected;assert(strcmp(title,"Install the finished netemu"));
 if(!strcmp(title,"Real Buzz! handsets in PS2 games - no ISO patching")){
#ifdef BB_BUNDLED_NETEMU
  assert(count==5&&!strcmp(items[1],"Install the modified ps2_netemu.self (four handsets + lamps)"));
#else
  assert(count==4);for(int i=0;i<count;i++)assert(!strstr(items[i],"INSTALL")&&!strstr(items[i],"Install"));
#endif
 }
 return -1;
}
static int not_hen(void){return mode==4;}
const bb_netemu_paths bb_console_netemu_paths={"/installed","/writable","/backups"};
const bb_netemu_ops bb_console_netemu_ops={not_hen,NULL,NULL};
bb_progress bb_tick;
const char *const bb_setup_names[7]={0};const char *const bb_layout_names[14]={0};
int bb_file_hash(const char *p,char h[65]){assert(!strcmp(p,"/installed"));strcpy(h,unknown?"1111111111111111111111111111111111111111111111111111111111111111":BB_STOCK_SELF);return 0;}
int bb_netemu_classify(const char *h){return !strcmp(h,BB_STOCK_SELF)?0:2;}
int bb_netemu_known(const char *h){return !strcmp(h,BB_STOCK_SELF);}
const char *bb_netemu_name(const char *h){(void)h;return "fixture";}
int bb_netemu_copies(const bb_netemu_paths *p,size_t o,bb_netemu_copy *c,size_t n,size_t *t,bb_netemu_copy *old){
 (void)p;(void)o;(void)n;*t=0;if(mode==7&&c){memset(c,0,sizeof(*c));c->valid=1;strcpy(c->name,"saved.self");strcpy(c->path,"/backups/own.self");strcpy(c->hash,BB_STOCK_SELF);*t=1;return 1;}if(old){memset(old,0,sizeof(*old));if(mode==1){old->valid=1;strcpy(old->path,"/backups/own.self");strcpy(old->hash,BB_STOCK_SELF);*t=1;}}return mode==2?-1:0;
}
int bb_netemu_status(const bb_netemu_paths *p,char *s,size_t n){(void)p;snprintf(s,n,"status");return 0;}
int bb_netemu_replace(const bb_netemu_paths *p,const bb_netemu_ops *o,const char *s,const char *h,const char *c,int u,char *msg,size_t n){
 (void)p;(void)o;(void)c;replacements++;assert(u==unknown);
 if(mode==1){assert(!strcmp(s,"/backups/own.self")&&!strcmp(h,BB_STOCK_SELF));}
#ifdef BB_BUNDLED_NETEMU
 else if(mode==3){assert(strstr(s,"ps2_netemu.self")&&!strcmp(h,BB_MODIFIED_SELF));}
 else {assert(strstr(s,"ps2_netemu_original.self")&&!strcmp(h,BB_STOCK_SELF));}
#else
 else assert(0);
#endif
 snprintf(msg,n,"transaction fixture result");return 0;
}
int bb_dir_open(bb_dir *d,const char *p){(void)d;dir_position=0;return mode==6&&!strcmp(p,"/dev_hdd0/PS2ISO")?0:-1;}
int bb_dir_read(bb_dir *d,bb_dir_entry *e){(void)d;assert(mode==6);if(dir_position++)return 0;memset(e,0,sizeof(*e));e->path_known=1;strcpy(e->name,"fixture.iso");strcpy(e->path,"/fixture.iso");return 1;}
void bb_dir_close(bb_dir *d){(void)d;assert(mode==6);}
int bb_path_exists(const char *p){(void)p;return 0;}
int bb_iso_name(const char *p){(void)p;assert(mode==6);return 1;}
int bb_dir_visible(const bb_dir_entry *e){(void)e;abort();}
void bb_dir_label(const bb_dir_entry *e,char *s,unsigned n){(void)e;(void)s;(void)n;abort();}
int bb_iso_inspect(const char *p,bb_iso_info *i,char *e,size_t n){(void)p;assert(mode==5||mode==6);memset(i,0,sizeof(*i));i->known=1;i->patched=mode==6;snprintf(e,n,"ISO details");return 0;}
int bb_iso_patch(const char *p,const char *m,const bb_iso_options *o,int c,char *e,size_t n){(void)p;(void)m;(void)o;assert(mode==5&&c);iso_calls++;snprintf(e,n,"ISO check result");return 0;}
int bb_iso_restore(const char *p,int c,char *e,size_t n){(void)p;assert(mode==5&&c);iso_calls++;snprintf(e,n,"ISO restore check result");return 0;}
static void reset(int m){mode=m;unknown=questions=replacements=cancel_at=tells=iso_calls=0;last_message[0]=0;script=NULL;}
static void scripted(const step *s,size_t n){script=s;cursor=0;length=n;}
#define SCRIPT(s) scripted(s,sizeof(s)/sizeof(*(s)))
static void done(void){assert(cursor==length&&!replacements);script=NULL;}
static void navigation(void){
 const step root_back[]={{"Choose how to play",BACK},{"Choose how to play",0}};
 reset(0);SCRIPT(root_back);assert(bb_main_menu()==0);done();
 const step root_exit[]={{"Choose how to play",BACK},{"Choose how to play",3}};
 reset(0);SCRIPT(root_exit);assert(bb_main_menu()==-1);done();
 const step unknown_back[]={{"RESTORE your own",YES},{"UNKNOWN FILE",BACK},{"RESTORE your own",NO}};
 reset(1);unknown=1;SCRIPT(unknown_back);restore_netemu();done();
 const step backup_back[]={{"Choose a specific",0},{"Backup:",BACK},{"Choose a specific",0},{"Backup:",OK},{"RESTORE selected",BACK},{"Backup:",BACK},{"Choose a specific",BACK}};
 reset(7);unknown=1;SCRIPT(backup_back);backup_list(1);done();
 const step status_back[]={
#ifndef BB_BUNDLED_NETEMU
  {"netemu files not included",OK},
#endif
  {"Real Buzz!",0},{"status",BACK},{"Real Buzz!",0},{"status",OK},{"No saved netemu",BACK},{"status",BACK},{"Real Buzz!",BACK}};
 reset(0);SCRIPT(status_back);netemu_menu();done();
#ifdef BB_BUNDLED_NETEMU
 const step install_back[]={{"Modified netemu needs",YES},{"Install the modified ps2_netemu.self",BACK},{"Modified netemu needs",BACK}};
 reset(3);SCRIPT(install_back);install_netemu();done();
 const step patched_back[]={{"Modified netemu needs",YES},{"WARNING: already-patched",BACK},{"Modified netemu needs",YES},{"WARNING: already-patched",YES},{"Install the modified ps2_netemu.self",BACK},{"WARNING: already-patched",BACK},{"Modified netemu needs",BACK}};
 reset(6);SCRIPT(patched_back);install_netemu();done();
#else
 const step intro_back[]={{"netemu files not included",BACK}};
 reset(0);SCRIPT(intro_back);netemu_menu();done();
#endif
 /* Walk backward through every custom wizard screen, then the action menu. */
 const step wizard_back[]={{"ISO details",OK},{"PS2 ISO actions",1},{"Your setup",6},{"A Buzz! set",OK},{"How many pairs",0},{"How many DualShocks",4},{"You can play",BACK},{"How many DualShocks",BACK},{"How many pairs",BACK},{"A Buzz! set",BACK},{"Your setup",BACK},{"PS2 ISO actions",BACK},{"ISO details",BACK}};
 reset(5);SCRIPT(wizard_back);iso_actions("/fixture.iso");done();assert(!iso_calls);
 const step confirmation_back[]={{"ISO details",OK},{"PS2 ISO actions",1},{"Your setup",6},{"A Buzz! set",OK},{"How many pairs",0},{"How many DualShocks",4},{"You can play",0},{"Patch this ISO",BACK},{"You can play",BACK},{"How many DualShocks",BACK},{"How many pairs",BACK},{"A Buzz! set",BACK},{"Your setup",BACK},{"PS2 ISO actions",2},{"Restore this ISO",BACK},{"PS2 ISO actions",3},{"ISO restore check result",BACK},{"PS2 ISO actions",BACK},{"ISO details",BACK}};
 reset(5);SCRIPT(confirmation_back);iso_actions("/fixture.iso");done();assert(iso_calls==1);
 const step check_back[]={{"ISO details",OK},{"PS2 ISO actions",0},{"Your setup",0},{"ISO check result",BACK},{"PS2 ISO actions",BACK},{"ISO details",BACK}};
 reset(5);SCRIPT(check_back);iso_actions("/fixture.iso");done();assert(iso_calls==1);
 const step details_back[]={{"ISO details",BACK}};
 reset(5);SCRIPT(details_back);iso_actions("/fixture.iso");done();
 const step guard_back[]={{"ISO details",OK},{"PS2 ISO actions",1},{"netemu fingerprint",YES},{"Your setup",BACK},{"netemu fingerprint",BACK},{"PS2 ISO actions",BACK},{"ISO details",BACK}};
 reset(5);unknown=1;SCRIPT(guard_back);iso_actions("/fixture.iso");done();assert(!iso_calls);
 puts("PASS navigation: root Circle ignored, explicit Exit retained, wizard steps, ISO details/confirmations/results, status/backups and netemu confirmations back without writes");
}
int main(void){
 navigation();
 int budget=256;assert(scan_patched("/no-iso-folder",0,&budget)==0);
 reset(0);netemu_menu(); /* Inspect full/public root action availability. */
#ifndef BB_BUNDLED_NETEMU
 assert(strstr(last_message,"netemu files not included in this build"));
#endif
 reset(0);restore_netemu();
#ifdef BB_BUNDLED_NETEMU
 assert(questions==0&&replacements==0); /* original already installed: no-op */
 /* Fallback confirmation must be exercised with a different installed hash. */
 reset(0);unknown=1;restore_netemu();assert(questions==2&&replacements==1);
#else
 assert(!questions&&!replacements&&strstr(last_message,"not included"));
#endif
 reset(1);unknown=1;restore_netemu();assert(questions==2&&replacements==1);
 reset(2);restore_netemu();assert(!questions&&!replacements&&strstr(last_message,"refused"));
 reset(1);unknown=1;cancel_at=1;restore_netemu();assert(questions==1&&!replacements);
 reset(1);unknown=1;cancel_at=2;restore_netemu();assert(questions==2&&!replacements);
 reset(4);restore_netemu();assert(!replacements);
#ifdef BB_BUNDLED_NETEMU
 reset(3);install_netemu();assert(questions==2&&replacements==1);
 reset(3);unknown=1;install_netemu();assert(questions==3&&replacements==1);
 reset(3);unknown=1;cancel_at=3;install_netemu();assert(questions==3&&!replacements);
 reset(4);install_netemu();assert(!questions&&!replacements&&strstr(last_message,"HEN"));
 puts("PASS full entry 2: single modified ps2_netemu.self without chooser, confirmations/cancellation, extra unknown warning, HEN, own backup and named Evilnat fallback");
#else
 puts("PASS public entry 2: no install action/fallback, missing-files text, own-backup restore, extra unknown confirmation and cancellation");
#endif
 return 0;
}
