/* SPDX-License-Identifier: MIT. Host CLI shares exact app C implementation. */
#include "bb.h"
#include "iso_patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void progress(const char *s){fprintf(stderr,"%s\n",s);}
int main(int argc,char **argv){
 char error[2048]="";int rc=-1;bb_tick=progress;
 if(argc==3&&!strcmp(argv[1],"info")){bb_iso_info i;rc=bb_iso_inspect(argv[2],&i,error,sizeof(error));}
 else if(argc==4&&!strcmp(argv[1],"restore"))rc=bb_iso_restore(argv[2],atoi(argv[3]),error,sizeof(error));
 else if(argc==12&&!strcmp(argv[1],"patch")){
  bb_iso_options o={(unsigned)atoi(argv[4]),(unsigned)atoi(argv[5]),(unsigned)atoi(argv[6]),(unsigned)atoi(argv[7]),(unsigned)atoi(argv[8]),(unsigned)atoi(argv[9]),(unsigned)atoi(argv[10])};rc=bb_iso_patch(argv[2],argv[3],&o,atoi(argv[11]),error,sizeof(error));
 }
 else if(argc==2&&!strcmp(argv[1],"crypto")){
  unsigned char hash[32];char hex[129];rc=0;
  bb_sha256("abc",3,hash);bb_hex(hash,32,hex);if(strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))rc=1;
  bb_sha1("abc",3,hash);bb_hex(hash,20,hex);if(strcmp(hex,"a9993e364706816aba3e25717850c26c9cd0d89d"))rc=1;
  printf("crypto vectors: %s\n",rc?"FAIL":"PASS");
 }else snprintf(error,sizeof(error),"Usage: crypto | info ISO | restore ISO check | patch ISO module layout flags pairs whole shared attach heartbeat check");
 puts(error);return rc?1:0;
}
