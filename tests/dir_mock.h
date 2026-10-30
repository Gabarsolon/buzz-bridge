/* LV2 metadata fixtures, intentionally independent of host libc stat sizes. */
#ifndef DIR_MOCK_H
#define DIR_MOCK_H
#include <stdint.h>
#include <sys/stat.h>
#define DT_DIR 1
#define DT_REG 2
#ifndef S_ISDIR
#define S_ISDIR(m) (((m)&0170000)==0040000)
#endif
typedef struct {int st_mode;uint64_t st_size;} sysFSStat;
typedef struct {unsigned char d_type,d_namlen;char d_name[1025];} sysFSDirent;
int sysLv2FsOpenDir(const char *,int *);
int sysLv2FsReadDir(int,sysFSDirent *,uint64_t *);
int sysLv2FsCloseDir(int);
int sysLv2FsStat(const char *,sysFSStat *);
#endif
