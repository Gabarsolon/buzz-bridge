/* Host-only app platform boundary. No console calls or filesystem writes. */
#ifndef APP_FLOW_MOCK_H
#define APP_FLOW_MOCK_H
#include <stdint.h>
#define SYS_PROCESS_PARAM(a,b)
#define PS3TC_HEAP_SIZE(n)
#define CELL_SYSMODULE_NET 1
#define CELL_SYSMODULE_NETCTL 2
#define NET_CTL_INFO_IP_ADDRESS 1
union net_ctl_info {char ip_address[16];};
int cellSysmoduleLoadModule(int);
int cellSysmoduleIsLoaded(int);
int cellSysmoduleUnloadModule(int);
int netCtlInit(void);
int netCtlTerm(void);
int netCtlGetInfo(int,union net_ctl_info *);
int buzz_is_hen(void);
int buzz_mapi(unsigned,uint64_t,uint64_t,uint64_t,uint64_t);
int buzz_load_vsh(unsigned,const char *);
static inline uint64_t buzz_ptr(const void *p){return (uint64_t)(uintptr_t)p;}
int flow_stat(const char *,struct stat *);
int flow_mkdir(const char *,int);
int flow_unlink(const char *);
FILE *flow_fopen(const char *,const char *);
int flow_fflush(FILE *);
int flow_fileno(FILE *);
int flow_fsync(int);
int flow_fclose(FILE *);
#define stat(p,s) flow_stat(p,s)
#define mkdir(p,m) flow_mkdir(p,m)
#define unlink(p) flow_unlink(p)
#define fopen(p,m) flow_fopen(p,m)
#define fflush(f) flow_fflush(f)
#define fileno(f) flow_fileno(f)
#define fsync(f) flow_fsync(f)
#define fclose(f) flow_fclose(f)
#endif
