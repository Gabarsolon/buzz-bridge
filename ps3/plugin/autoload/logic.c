/* SPDX-License-Identifier: MIT. Pure logic shared by native code and PC tests. */
#include "logic.h"
#include <string.h>
static const struct { const char *title,*version; } profiles[]={
 {"BCES00347","01.00"},{"BCES00303","01.00"},{"BCES00644","01.00"}};
int buzz_process_match(uint32_t pid,const char *name,const char *title)
{
 const char *p=name;
 if(!pid || pid==0x1000300 || !name || !title) return 0;
 /* PS3MAPI returns main_EBOOT.BIN; webMAN displays an optional PID prefix. */
 if(strlen(p)==23 && p[8]=='_') {
  uint32_t named_pid=0;
  for(unsigned i=0;i<8;i++) {
   unsigned digit;
   if(p[i]>='0'&&p[i]<='9')digit=(unsigned)(p[i]-'0');
   else if(p[i]>='a'&&p[i]<='f')digit=(unsigned)(p[i]-'a'+10);
   else if(p[i]>='A'&&p[i]<='F')digit=(unsigned)(p[i]-'A'+10);
   else return 0;
   named_pid=(named_pid<<4)|digit;
  }
  if(named_pid!=pid)return 0;
  p+=9;
 }
 if(strcmp(p,"main_EBOOT.BIN")) return 0;
 for(unsigned i=0;i<sizeof(profiles)/sizeof(profiles[0]);i++)
  if(!strcmp(title,profiles[i].title) && !strcmp(profiles[i].version,"01.00")) return 1;
 return 0;
}
int buzz_should_attempt(buzz_attempt *s,int ready,uint64_t now)
{
 if(s->attempted) return 0;
 if(!ready) { s->waiting=0; return 0; }
 if(!s->waiting || now<s->ready_since) { s->waiting=1;s->ready_since=now;return 0; }
 return now-s->ready_since>=BUZZ_SETTLE_US;
}
void buzz_mark_attempt(buzz_attempt *s) { s->attempted=1;s->waiting=0; }
uint32_t buzz_crc32(const unsigned char *p,size_t n)
{
 uint32_t crc=UINT32_MAX;
 for(size_t i=0;i<n;i++) { crc^=p[i];for(unsigned j=0;j<8;j++) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u); }
 return ~crc;
}
static int own_line(const char *s,size_t n)
{
 while(n && (*s==' '||*s=='\t')) {s++;n--;}
 while(n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r')) n--;
 return n==sizeof(BUZZ_HELPER)-1 && !memcmp(s,BUZZ_HELPER,n);
}
int buzz_boot_has(const char *s,size_t n)
{
 for(size_t at=0;at<n;) {size_t end=at;while(end<n && s[end]!='\n')end++;if(own_line(s+at,end-at))return 1;at=end+(end<n);}
 return 0;
}
int buzz_boot_edit(const char *s,size_t n,int enable,char *d,size_t cap,size_t *used)
{
 size_t out=0;int crlf=0;
 if(n>BUZZ_BOOT_MAX || memchr(s,0,n)) return -1;
 for(size_t i=0;i<n;i++) if(s[i]=='\r' && (i+1==n || s[i+1]!='\n')) return -1;
 for(size_t at=0;at<n;) {
  size_t end=at;while(end<n && s[end]!='\n')end++;
  size_t count=end-at+(end<n);
  if(end>at && s[end-1]=='\r')crlf=1;
  if(!own_line(s+at,end-at)) {if(count>cap-out)return -1;memcpy(d+out,s+at,count);out+=count;}
  at+=count;
 }
 if(enable) {
  size_t extra=sizeof(BUZZ_HELPER)-1+(crlf?2:1)+(out && d[out-1]!='\n'?(crlf?2:1):0);
  if(extra>cap-out)return -1;
  if(out && d[out-1]!='\n') {if(crlf)d[out++]='\r';d[out++]='\n';}
  memcpy(d+out,BUZZ_HELPER,sizeof(BUZZ_HELPER)-1);out+=sizeof(BUZZ_HELPER)-1;
  if(crlf)d[out++]='\r';d[out++]='\n';
 }
 if(out>BUZZ_BOOT_MAX)return -1;
 *used=out;
 return out!=n || memcmp(s,d,n)!=0;
}
