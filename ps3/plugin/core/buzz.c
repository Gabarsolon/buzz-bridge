/* SPDX-License-Identifier: MIT */
#include "buzz.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "format.h"

void buzz_defaults(buzz_config *c)
{
 memset(c,0,sizeof(*c)); c->dongles=2; c->players_per_pad=1; c->port=8420; c->stale_ms=45000; c->generic=1;
 /* Avoid the ambiguous X/cross alias: cross is ALWAYS yellow; red is R1/R2. */
 c->map[0][BUZZ_RED]=BP_R1|BP_R2; c->map[0][BUZZ_YELLOW]=BP_CROSS;
 c->map[0][BUZZ_GREEN]=BP_CIRCLE; c->map[0][BUZZ_ORANGE]=BP_SQUARE; c->map[0][BUZZ_BLUE]=BP_TRIANGLE;
 c->map[1][BUZZ_RED]=BP_L1|BP_L2; c->map[1][BUZZ_YELLOW]=BP_DOWN;
 c->map[1][BUZZ_GREEN]=BP_RIGHT; c->map[1][BUZZ_ORANGE]=BP_LEFT; c->map[1][BUZZ_BLUE]=BP_UP;
}
static char *trim(char *s)
{
 char *e; while(isspace((unsigned char)*s)) ++s; e=s+strlen(s);
 while(e>s && isspace((unsigned char)e[-1])) --e; *e=0; return s;
}
int buzz_button_index(const char *name)
{
 static const char *const names[]={"red","yellow","green","orange","blue"}; unsigned i;
 for(i=0;i<5;i++) if(!strcmp(name,names[i])) return (int)i; return -1;
}
static int mask_parse(char *s, uint16_t *mask)
{
 static const struct { const char *name; unsigned bit; } names[]={
  {"select",BP_SELECT},{"l3",BP_L3},{"r3",BP_R3},{"start",BP_START},{"up",BP_UP},
  {"right",BP_RIGHT},{"down",BP_DOWN},{"left",BP_LEFT},{"l2",BP_L2},{"r2",BP_R2},
  {"l1",BP_L1},{"r1",BP_R1},{"triangle",BP_TRIANGLE},{"circle",BP_CIRCLE},
  {"cross",BP_CROSS},{"square",BP_SQUARE}};
 unsigned value=0,i; char *part=s,*sep;
 do {
  sep=strchr(part,'|'); if(sep) *sep=0; part=trim(part);
  for(i=0;i<sizeof(names)/sizeof(names[0]);i++) if(!strcmp(part,names[i].name)) break;
  if(i==sizeof(names)/sizeof(names[0])) return 0;
  value|=names[i].bit; part=sep ? sep+1 : NULL;
 } while(part);
 *mask=(uint16_t)value; return 1;
}
unsigned buzz_config_parse(buzz_config *c, const char *text)
{
 buzz_config next=*c; unsigned line=0; const char *p=text;
 while(*p) {
  char buf[256], *key,*val,*eq,*end,*comment; size_t n=0; unsigned long number;
  ++line;
  while(p[n] && p[n]!='\n') ++n;
  if(n>=sizeof(buf)) return line;
  memcpy(buf,p,n); buf[n]=0; p+=n; if(*p) ++p;
  comment=strchr(buf,'#'); if(comment) *comment=0;
  key=trim(buf); if(!*key) continue;
  eq=strchr(key,'='); if(!eq) return line; *eq=0;
  key=trim(key); val=trim(eq+1);
  if(!strncmp(key,"map.",4)) {
   unsigned half; int b; char *dot=strchr(key+4,'.');
   if(!dot) return line; *dot=0;
   if(!strcmp(key+4,"right")) half=0;
   else if(!strcmp(key+4,"left")) half=1; else return line;
   b=buzz_button_index(dot+1);
   if(b<0 || !mask_parse(val,&next.map[half][b])) return line;
   continue;
  }
  if(!strncmp(key,"pad.",4)) {
   unsigned port,half;
   if(key[4]<'1' || key[4]>'7' || key[5]!='.') return line;
   port=(unsigned)(key[4]-'1');
   if(!strcmp(key+6,"right")) half=0;
   else if(!strcmp(key+6,"left")) half=1; else return line;
   if(!strcmp(val,"off")) next.pad_slot[port][half]=255;
   else if(val[0]>='0' && val[0]<='8' && !val[1]) next.pad_slot[port][half]=(uint8_t)(val[0]-'0');
   else return line;
   continue;
  }
  if(!*val || !isdigit((unsigned char)*val)) return line;
  number=0; end=val;
  while(*end>='0' && *end<='9') {
   number=number*10+(unsigned)(*end++-'0'); if(number>65535) return line;
  }
  if(*end) return line;
  if(!strcmp(key,"enabled") && number<=1) next.enabled=(unsigned)number;
  else if(!strcmp(key,"generic") && number<=1) next.generic=(unsigned)number;
  else if(!strcmp(key,"dongles") && number>=1 && number<=2) next.dongles=(unsigned)number;
  else if(!strcmp(key,"players_per_pad") && number>=1 && number<=2) next.players_per_pad=(unsigned)number;
  else if(!strcmp(key,"port") && number>=1024) next.port=(unsigned)number;
  else if(!strcmp(key,"stale_ms") && number>=5000 && number<=60000) next.stale_ms=(unsigned)number;
  else return line;
 }
 for(unsigned port=0;port<BUZZ_PADS;port++) for(unsigned half=0;half<2;half++) {
  unsigned target=next.pad_slot[port][half];
  if(target!=255 && target>next.dongles*4) return line;
 }
 *c=next; return 0;
}
void buzz_hub_init(buzz_hub *h,const buzz_config *c) { memset(h,0,sizeof(*h)); h->config=*c; }
unsigned buzz_capacity(const buzz_hub *h) { return h->config.dongles*4; }
int buzz_claim(buzz_hub *h,unsigned kind,unsigned id,unsigned requested)
{
 unsigned i,cap=buzz_capacity(h); int free_slot=-1;
 if((kind!=BUZZ_PAD && kind!=BUZZ_PHONE) || id>65535 || requested>cap) return -1;
 for(i=0;i<cap;i++) {
  if(h->slots[i].kind==kind && h->slots[i].id==id)
   return !requested || requested==i+1 ? (int)i+1 : -1;
  if(h->slots[i].kind==BUZZ_FREE && free_slot<0) free_slot=(int)i;
 }
 if(requested) free_slot=h->slots[requested-1].kind==BUZZ_FREE ? (int)requested-1 : -1;
 if(free_slot<0) return -1;
 h->slots[free_slot].kind=(uint8_t)kind; h->slots[free_slot].id=(uint16_t)id;
 h->slots[free_slot].buttons=0; ++h->generation; return free_slot+1;
}
void buzz_release(buzz_hub *h,unsigned kind,unsigned id)
{
 unsigned i; for(i=0;i<BUZZ_PLAYERS;i++) if(h->slots[i].kind==kind && h->slots[i].id==id) {
  h->slots[i].kind=BUZZ_FREE; h->slots[i].id=0; h->slots[i].buttons=0;
  /* Keep physical game LED state across a slot reassignment. */
  ++h->generation;
 }
}
int buzz_press(buzz_hub *h,unsigned kind,unsigned id,unsigned b,int down)
{
 unsigned i; if(b>=5) return 0;
 for(i=0;i<buzz_capacity(h);i++) if(h->slots[i].kind==kind && h->slots[i].id==id) {
  uint8_t old=h->slots[i].buttons;
  if(down) h->slots[i].buttons|=(uint8_t)(1u<<b); else h->slots[i].buttons&=(uint8_t)~(1u<<b);
  if(old!=h->slots[i].buttons) ++h->generation; return 1;
 }
 return 0;
}
void buzz_pad_update(buzz_hub *h,unsigned port,int connected,int new_data,uint16_t mask)
{
 unsigned half,b; if(port>=BUZZ_PADS) return;
 if(!connected) {
  buzz_release(h,BUZZ_PAD,port*2); buzz_release(h,BUZZ_PAD,port*2+1); h->pad_connected[port]=0; return;
 }
 /* Retry an unassigned pad when slots free up. Existing assignments never move. */
 for(half=0;half<h->config.players_per_pad;half++) {
  unsigned target=h->config.pad_slot[port][half];
  if(target==255) continue;
  int slot=buzz_claim(h,BUZZ_PAD,port*2+half,target);
  if(slot>0 && new_data) {
   for(b=0;b<5;b++) buzz_press(h,BUZZ_PAD,port*2+half,b,(mask & h->config.map[half][b])!=0);
  }
 } h->pad_connected[port]=1;
}
void buzz_encode(const uint8_t buttons[4],uint8_t out[5])
{
 uint32_t bits=0; unsigned i; for(i=0;i<4;i++) bits|=(uint32_t)(buttons[i]&31)<<(5*i);
 out[0]=out[1]=0x7f; out[2]=(uint8_t)bits; out[3]=(uint8_t)(bits>>8); out[4]=(uint8_t)(0xf0 | (bits>>16));
}
void buzz_report(const buzz_hub *h,unsigned dongle,uint8_t out[5])
{
 uint8_t b[4]={0}; unsigned i;
 if(dongle<h->config.dongles) for(i=0;i<4;i++) b[i]=h->slots[dongle*4+i].buttons;
 buzz_encode(b,out);
}
int buzz_lights(buzz_hub *h,unsigned dongle,const uint8_t *payload,size_t len)
{
 unsigned i; if(dongle>=h->config.dongles || len<5 || len>8 || !payload) return 0;
 for(i=0;i<4;i++) h->slots[dongle*4+i].light=(uint8_t)(payload[i+1]==0xff);
 ++h->generation; return 1;
}
unsigned buzz_pad_light(const buzz_hub *h,unsigned port)
{
 unsigned i,on=0; for(i=0;i<buzz_capacity(h);i++)
  if(h->slots[i].kind==BUZZ_PAD && h->slots[i].id/2==port) on|=h->slots[i].light;
 return on;
}
/* Deliberately small JSON grammar: flat object, ASCII strings without escapes,
   unsigned decimal slot. Reject duplicates, trailing data and nested input. */
static void space(const char **p,const char *e)
{
 while(*p<e && (**p==' ' || **p=='\t' || **p=='\r' || **p=='\n')) ++*p;
}
static int string(const char **p,const char *e,char *out,size_t cap)
{
 size_t n=0; if(*p==e || *(*p)++!='"') return 0;
 while(*p<e && **p!='"') {
  unsigned char ch=(unsigned char)*(*p)++; if(ch<32 || ch>=127 || ch=='\\' || n+1>=cap) return 0;
  out[n++]=(char)ch;
 } if(*p==e) return 0; ++*p; out[n]=0; return 1;
}
int buzz_message_parse(const char *data,size_t len,buzz_message *msg)
{
 const char *p=data,*e=data+len; char type[16]="",button[16]=""; unsigned seen=0,slot=0;
 if(len>256) return 0; space(&p,e); if(p==e || *p++!='{') return 0;
 for(unsigned fields=0;fields<3;fields++) {
  char key[16],value[16]; unsigned field; space(&p,e);
  if(p<e && *p=='}') { ++p; break; }
  if(!string(&p,e,key,sizeof(key))) return 0; space(&p,e);
  if(p==e || *p++!=':') return 0; space(&p,e);
  field=!strcmp(key,"type")?1:!strcmp(key,"button")?2:!strcmp(key,"slot")?4:0;
  if(!field || (seen&field)) return 0; seen|=field;
  if(field==4) {
   unsigned digits=0; if(p==e || !isdigit((unsigned char)*p)) return 0;
   if(*p=='0' && p+1<e && isdigit((unsigned char)p[1])) return 0;
   while(p<e && isdigit((unsigned char)*p)) { if(++digits>2) return 0; slot=slot*10+(unsigned)(*p++-'0'); }
  } else {
   if(!string(&p,e,value,sizeof(value))) return 0;
   if(field==1) memcpy(type,value,strlen(value)+1); else memcpy(button,value,strlen(value)+1);
  }
  space(&p,e); if(p==e) return 0;
  if(*p=='}') { ++p; break; }
  if(*p++!=',') return 0; space(&p,e); if(p==e || *p=='}') return 0;
 }
 space(&p,e); if(p!=e || !(seen&1)) return 0;
 memset(msg,0,sizeof(*msg)); msg->slot=slot;
 if(!strcmp(type,"join") && !(seen&2) && slot<=8) msg->type=BUZZ_JOIN;
 else if((!strcmp(type,"down") || !strcmp(type,"up")) && seen==3) {
  int b=buzz_button_index(button); if(b<0) return 0;
  msg->type=!strcmp(type,"down")?BUZZ_DOWN:BUZZ_UP; msg->button=(unsigned)b;
 } else return 0;
 return 1;
}
int buzz_phone_message(buzz_hub *h,unsigned client,const char *data,size_t len,char *reply,size_t cap)
{
 buzz_message msg; int slot,n; if(!buzz_message_parse(data,len,&msg)) return -1;
 if(msg.type!=BUZZ_JOIN) return buzz_press(h,BUZZ_PHONE,client,msg.button,msg.type==BUZZ_DOWN)?0:-1;
 slot=buzz_claim(h,BUZZ_PHONE,client,msg.slot);
 n=snprintf(reply,cap,"{\"type\":\"%s\",\"slot\":%u}",slot>0?"joined":"join_rejected",slot>0?(unsigned)slot:msg.slot);
 return n>=0 && (size_t)n<cap ? n : -1;
}
int buzz_status_json(const buzz_hub *h,char *out,size_t cap)
{
 unsigned i; size_t used=0; int n;
 n=snprintf(out,cap,"{\"type\":\"status\",\"players\":%u,\"slots\":{",buzz_capacity(h));
 if(n<0 || (size_t)n>=cap) return -1; used=(size_t)n;
 for(i=0;i<buzz_capacity(h);i++) {
  n=snprintf(out+used,cap-used,"%s\"%u\":%s",i?",":"",i+1,h->slots[i].kind?"true":"false");
  if(n<0 || (size_t)n>=cap-used) return -1; used+=(size_t)n;
 }
 n=snprintf(out+used,cap-used,"}}"); if(n<0 || (size_t)n>=cap-used) return -1; return (int)(used+(size_t)n);
}
