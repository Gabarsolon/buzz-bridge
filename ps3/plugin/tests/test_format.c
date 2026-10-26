/* SPDX-License-Identifier: MIT. Compare the native PRX formatter with libc. */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
int buzz_snprintf(char *,size_t,const char *,...);
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"format line %d\n",__LINE__); exit(1); } } while(0)
int main(void)
{
 char expected[128],actual[128];
 unsigned values[]={0,1,15,16,255,65535,0xffffffffu};
 for(unsigned v=0;v<sizeof(values)/sizeof(values[0]);v++) for(size_t cap=0;cap<=128;cap++) {
  memset(expected,0x55,sizeof(expected)); memset(actual,0x55,sizeof(actual));
  int a=snprintf(expected,cap,"%s %08x %u %02x %% %c","abcdef",values[v],values[v],values[v],'Z');
  int b=buzz_snprintf(actual,cap,"%s %08x %u %02x %% %c","abcdef",values[v],values[v],values[v],'Z');
  CHECK(a==b && !memcmp(expected,actual,sizeof(actual)));
 }
 for(int v=-9;v<10;v++) {
  snprintf(expected,sizeof(expected),"%04d %d",v,INT_MIN);
  buzz_snprintf(actual,sizeof(actual),"%04d %d",v,INT_MIN); CHECK(!strcmp(actual,expected));
 }
 CHECK(buzz_snprintf(NULL,0,"%s","abc")==3);
 CHECK(buzz_snprintf(actual,sizeof(actual),"%q",1)==-1);
 puts("PRX formatter: 903 truncation/numeric cases, signed limits, invalid format passed");
 return 0;
}
