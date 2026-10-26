/* SPDX-License-Identifier: MIT. Allocation-free formatter for the PRX.
 * Only the formats used by this project: %s, %u, %d, %x, %c, %% and 0 width.
 * PRX startup does not initialize PS3DK's process-owned newlib runtime. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
int buzz_vsnprintf(char *out,size_t cap,const char *fmt,va_list ap)
{
 size_t n=0;
#define PUT(ch) do { char output_ch=(char)(ch); if(cap && n<cap-1) out[n]=output_ch; ++n; } while(0)
 while(*fmt) {
  char ch=*fmt++,pad=' '; unsigned width=0;
  if(ch!='%') { PUT(ch); continue; }
  if(*fmt=='0') { pad='0'; ++fmt; }
  while(*fmt>='0' && *fmt<='9') { if(width>32) return -1; width=width*10+(unsigned)(*fmt++-'0'); }
  ch=*fmt++;
  if(ch=='s') { const char *p=va_arg(ap,const char *); if(!p) p="(null)"; while(*p) PUT(*p++); }
  else if(ch=='c') { int c=va_arg(ap,int); PUT(c); }
  else if(ch=='%') { PUT('%'); }
  else if(ch=='u' || ch=='d' || ch=='x') {
   char digits[32]; unsigned used=0,base=ch=='x'?16:10,negative=0; uint32_t value;
   if(ch=='d') { int v=va_arg(ap,int); negative=v<0; value=negative?0u-(uint32_t)v:(uint32_t)v; }
   else value=va_arg(ap,unsigned);
   do { digits[used++]="0123456789abcdef"[value%base]; value/=base; } while(value);
   if(negative && pad=='0') { PUT('-'); negative=0; if(width) --width; }
   while(width>used+negative) { PUT(pad); --width; }
   if(negative) { PUT('-'); }
   while(used) { PUT(digits[--used]); }
  } else { if(cap) out[n<cap?n:cap-1]=0; return -1; }
 }
 if(cap) out[n<cap?n:cap-1]=0;
#undef PUT
 return n>INT_MAX?-1:(int)n;
}
int buzz_snprintf(char *out,size_t cap,const char *fmt,...)
{
 int n; va_list ap; va_start(ap,fmt); n=buzz_vsnprintf(out,cap,fmt,ap); va_end(ap); return n;
}
