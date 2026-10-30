/* Production browser + production LV2 adapter against fake directory listings.
 * No ISO image reads, network, firmware or real filesystem modifications. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dir_mock.h"
int mkdir(const char *,int);
#include "../ps3/plugin/autoload/menu.c"
int mkdir(const char *p,int mode){(void)p;(void)mode;abort();}
static int fixture,position,inspections,menus,not_supported,read_error;
typedef struct {const char *screen,*label;} step;
static const step *script;
static size_t cursor,length;
static const step *next(const char *screen){assert(cursor<length);const step *s=&script[cursor++];if(!strstr(screen,s->screen)){fprintf(stderr,"expected %s, got %s\n",s->screen,screen);abort();}return s;}
static char logs[32768];
static const char *names[]={"above2.iso","above4.ISO","broken.iSo","ULAUNCHELF.iso","folder","mystery"};
static int fixture_count(void){return fixture==1?600:fixture==2?3:6;}
static void name_at(int n,char *s){if(fixture==1)snprintf(s,256,"game-%03d.iso",n);else if(fixture==2)strcpy(s,n==0?"S4L3.json":n==1?"bad.json":"S4L3.audit.json");else strcpy(s,names[n]);}
int sysLv2FsOpenDir(const char *p,int *fd){(void)p;position=0;*fd=10;return 0;}
int sysLv2FsReadDir(int fd,sysFSDirent *de,uint64_t *got){
 assert(fd==10);if(read_error&&position==2)return (int)0x80010005u;
 if(position==fixture_count()){*got=0;return 0;}name_at(position,de->d_name);
 de->d_namlen=(unsigned char)strlen(de->d_name);de->d_type=(fixture==0||fixture==4)&&position==4?DT_DIR:(fixture==0||fixture==4)&&position==5?0:DT_REG;position++;*got=1;return 0;
}
int sysLv2FsCloseDir(int fd){assert(fd==10);return 0;}
int sysLv2FsStat(const char *p,sysFSStat *st){
 if(strstr(p,"broken")||strstr(p,"mystery")||strstr(p,"/folder"))return (int)0x8001002bu;
 st->st_mode=0100000;st->st_size=strstr(p,"above2")?3221225472ULL:strstr(p,"above4")?5046586573ULL:1048576;return 0;
}
void app_log(const char *fmt,...){va_list ap;va_start(ap,fmt);size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,fmt,ap);va_end(ap);strcat(logs,"\n");}
int app_exiting(void){return 0;}
void app_work(const char *s){(void)s;}
void app_health_snapshot(const char *s){(void)s;}
int app_dialog(const char *s,int question){assert(!question);if(strstr(s,"not a supported Buzz! game (SYSTEM.CNF/module not recognised)"))not_supported++;if(script)return next(s)->label?CELL_MSGDIALOG_BUTTON_OK:CELL_MSGDIALOG_BUTTON_ESCAPE;if(fixture==3&&inspections&&strstr(s,"Patch a PS2 ISO"))return CELL_MSGDIALOG_BUTTON_ESCAPE;return 0;}
int app_menu(const char *title,const char *const *labels,int count,int selected){
 (void)selected;menus++;
 if(script){const char *label=next(title)->label;if(!label)return -1;for(int i=0;i<count;i++)if(strstr(labels[i],label))return i;assert(0);}
 if(fixture==3){
  if(inspections)return -1;
  if(strstr(title,"Choose ISO location"))return 0;
  for(int i=0;i<count;i++)if(strstr(labels[i],"ULAUNCHELF.iso"))return i;
  assert(0);
 }
 if(fixture==1){
  assert(strstr(title,"Page "));if(menus<=2){assert(count==(menus==1?255:256));assert(strstr(labels[count-1],"Next page"));return count-1;}
  assert(count==93&&strstr(labels[92],"Previous page"));return 0;
 }
 assert(count==(read_error?2:6));
 if(!read_error){
  int big2=0,big4=0,failed=0,folder=0,mystery=0;
  for(int i=0;i<count;i++){big2+=strstr(labels[i],"[3.0 GB] above2.iso")!=NULL;big4+=strstr(labels[i],"[4.7 GB] above4.ISO")!=NULL;failed+=strstr(labels[i],"? broken.iSo")!=NULL;folder+=strstr(labels[i],"? [Folder] folder")!=NULL;mystery+=strstr(labels[i],"? mystery")!=NULL;}
  assert(big2&&big4&&failed&&folder&&mystery);
 }
 return -1;
}
int bb_iso_inspect(const char *p,bb_iso_info *i,char *msg,size_t cap){
 inspections++;memset(i,0,sizeof(*i));snprintf(msg,cap,"Unexpected ISO9660 layout");
 if(strstr(p,"ULAUNCHELF"))return -1;i->patched=1;return 0;
}
/* Every operation outside enumeration/inspection is forbidden in this fixture. */
bb_progress bb_tick;
const char *const bb_setup_names[7]={0};const char *const bb_layout_names[14]={0};
const bb_netemu_paths bb_console_netemu_paths={0};const bb_netemu_ops bb_console_netemu_ops={0};
int bb_file_hash(const char *p,char h[65]){abort();}
int bb_netemu_classify(const char *h){abort();}
int bb_netemu_known(const char *h){abort();}
const char *bb_netemu_name(const char *h){abort();}
int bb_netemu_copies(const bb_netemu_paths *p,size_t o,bb_netemu_copy *c,size_t n,size_t *t,bb_netemu_copy *old){abort();}
int bb_netemu_status(const bb_netemu_paths *p,char *e,size_t n){abort();}
int bb_netemu_replace(const bb_netemu_paths *p,const bb_netemu_ops *o,const char *s,const char *h,const char *c,int u,char *e,size_t n){abort();}
int bb_iso_patch(const char *p,const char *m,const bb_iso_options *o,int c,char *e,size_t n){abort();}
int bb_iso_restore(const char *p,int c,char *e,size_t n){abort();}
int main(void){
 char result[1024];assert(browse("/fixture",result,0)==-1&&inspections==0);
 assert(strstr(logs,"8001002b path=/fixture/broken.iSo")&&strstr(logs,"/fixture/mystery"));
 int budget=6;assert(scan_patched("/fixture",4,&budget)==3&&budget==0&&inspections==4);
 budget=2;inspections=0;assert(scan_patched("/fixture",4,&budget)==2&&budget==0&&inspections==2);
 fixture=1;menus=inspections=0;assert(!browse("/fixture",result,0));assert(strstr(result,"game-508.iso")&&menus==3&&inspections==0);
 fixture=0;read_error=1;assert(browse("/fixture",result,0)==-1);assert(strstr(logs,"sysLv2FsReadDir rc=80010005 path=/fixture"));
 fixture=3;read_error=0;inspections=0;iso_menu();assert(inspections==1&&not_supported==1);
 const step back[]={{"Patch a PS2 ISO","OK"},{"Choose ISO location","/dev_hdd0/PS2ISO"},{"Page 1: /dev_hdd0/PS2ISO","[Folder] folder"},{"Page 1: /dev_hdd0/PS2ISO/folder",NULL},{"Page 1: /dev_hdd0/PS2ISO","ULAUNCHELF.iso"},{"not a supported Buzz! game",NULL},{"Page 1: /dev_hdd0/PS2ISO",NULL},{"Choose ISO location",NULL},{"Patch a PS2 ISO",NULL}};
 fixture=4;inspections=cursor=0;script=back;length=sizeof(back)/sizeof(*back);iso_menu();assert(cursor==length&&inspections==1);script=NULL;
 puts("PASS browser back: nested folder -> parent, unsupported image -> same folder, folder -> location -> introduction -> root");
 puts("PASS browser: >2 GiB / >4 GiB / failed stat retained; 600 entries paged; lazy inspect; scan bounded; read failures logged");
 return 0;
}
