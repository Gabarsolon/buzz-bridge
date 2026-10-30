/* SPDX-License-Identifier: MIT. SHA algorithms implemented from FIPS 180-4. */
#include "bb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#define fsync _commit
#define close _close
#define write _write
#define open _open
#define O_BINARY _O_BINARY
#else
#include <unistd.h>
#define O_BINARY 0
#endif
bb_progress bb_tick;
uint32_t bb_be32(const void *v){const unsigned char *p=v;return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
void bb_put32(void *v,uint32_t x){unsigned char *p=v;for(int i=3;i>=0;i--){p[i]=(unsigned char)x;x>>=8;}}
void bb_put64(void *v,uint64_t x){unsigned char *p=v;bb_put32(p,(uint32_t)(x>>32));bb_put32(p+4,(uint32_t)x);}
static uint32_t rol(uint32_t x,unsigned n){return x<<n|x>>(32-n);}
static uint32_t ror(uint32_t x,unsigned n){return x>>n|x<<(32-n);}
static const uint32_t k256[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void digest(const void *v,size_t n,unsigned char *out,int sha256){
 const unsigned char *p=v;uint32_t h[8]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0,0,0,0};
 if(sha256){const uint32_t init[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memcpy(h,init,32);}
 size_t blocks=n/64+1+(n%64>=56);unsigned char tail[64];
 for(size_t block=0;block<blocks;block++){
  size_t off=block*64;const unsigned char *q;
  if(off+64<=n)q=p+off;else{memset(tail,0,64);if(off<n)memcpy(tail,p+off,n-off);if(off<=n&&n-off<64)tail[n-off]=128;if(block+1==blocks)bb_put64(tail+56,(uint64_t)n*8);q=tail;}
  uint32_t w[80];for(int i=0;i<16;i++)w[i]=bb_be32(q+i*4);
  if(sha256)for(int i=16;i<64;i++){uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(ror(x,7)^ror(x,18)^(x>>3))+w[i-7]+(ror(y,17)^ror(y,19)^(y>>10));}
  else for(int i=16;i<80;i++)w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
  uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
  for(int i=0;i<(sha256?64:80);i++){
   if(sha256){uint32_t t=hh+(ror(e,6)^ror(e,11)^ror(e,25))+((e&f)^(~e&g))+k256[i]+w[i];uint32_t u=(ror(a,2)^ror(a,13)^ror(a,22))+((a&b)^(a&c)^(b&c));hh=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
   else{uint32_t x=i<20?((b&c)|(~b&d)):i<40?(b^c^d):i<60?((b&c)|(b&d)|(c&d)):(b^c^d);uint32_t k=i<20?0x5a827999:i<40?0x6ed9eba1:i<60?0x8f1bbcdc:0xca62c1d6;uint32_t t=rol(a,5)+x+e+k+w[i];e=d;d=c;c=rol(b,30);b=a;a=t;}
  }
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;if(sha256){h[5]+=f;h[6]+=g;h[7]+=hh;}
 }
 for(int i=0;i<(sha256?8:5);i++)bb_put32(out+4*i,h[i]);
}
void bb_sha1(const void *p,size_t n,unsigned char out[20]){digest(p,n,out,0);}
void bb_sha256(const void *p,size_t n,unsigned char out[32]){digest(p,n,out,1);}
void bb_hex(const void *v,size_t n,char *out){const unsigned char *p=v;const char *h="0123456789abcdef";for(size_t i=0;i<n;i++){out[2*i]=h[p[i]>>4];out[2*i+1]=h[p[i]&15];}out[2*n]=0;}
int bb_hash_is(const void *p,size_t n,const char *hex){unsigned char h[32];char s[65];bb_sha256(p,n,h);bb_hex(h,32,s);return !strcmp(hex,s);}
int bb_read(const char *path,unsigned char **data,size_t *n,size_t max){
 FILE *f=fopen(path,"rb");*data=NULL;*n=0;if(!f)return -1;
 if(fseek(f,0,SEEK_END)){fclose(f);return -1;}long len=ftell(f);if(len<0||(size_t)len>max||fseek(f,0,SEEK_SET)){fclose(f);return -1;}
 unsigned char *p=malloc((size_t)len+1);if(!p){fclose(f);return -1;}int ok=fread(p,1,(size_t)len,f)==(size_t)len&&!ferror(f);if(fclose(f))ok=0;
 if(!ok){free(p);return -1;}p[len]=0;*data=p;*n=(size_t)len;return 0;
}
int bb_save(const char *path,const void *data,size_t n,int exclusive){
 int fd=open(path,O_WRONLY|O_CREAT|(exclusive?O_EXCL:O_TRUNC)|O_BINARY,0666);if(fd<0)return -1;const unsigned char *p=data;size_t at=0;int rc=0;
 while(at<n){unsigned block=(unsigned)(n-at>65536?65536:n-at);int got=write(fd,p+at,block);if(got<=0){rc=-1;break;}at+=(size_t)got;}
 if(fsync(fd))rc=-1;if(close(fd))rc=-1;return rc;
}
int bb_file_hash(const char *path,char out[65]){unsigned char *p,h[32];size_t n;if(bb_read(path,&p,&n,BB_MAX_FILE))return -1;bb_sha256(p,n,h);bb_hex(h,32,out);free(p);return 0;}
int bb_error(char *e,size_t cap,const char *text){snprintf(e,cap,"%s",text);return -1;}
