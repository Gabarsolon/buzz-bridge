/* Minimal host types for executing the actual bbpad.c with a fake SIO2 bus. */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef struct { u32 lo, hi; } iop_sys_clock_t;
struct irx_import_stub { u32 jump, function; };
struct irx_import_table { struct irx_import_table *next; void *stubs; };
typedef struct iop_library {
    struct iop_library *prev;
    char name[8];
    unsigned version;
    struct irx_import_table *caller;
    void *exports[64];
} iop_library_t;
typedef struct { iop_library_t *let_next; } mock_loadcore_t;
typedef struct {
    u32 stat6c, port_ctrl1[4], port_ctrl2[4], regdata[16];
    unsigned in_size, out_size;
    u8 *in, *out;
    struct { void *addr; } in_dma, out_dma;
} sio2_transfer_data_t;
static mock_loadcore_t mock_loadcore;
static mock_loadcore_t *GetLoadcoreInternalData(void) { return &mock_loadcore; }
static void CpuSuspendIntr(int *s) { *s = 0; }
static void CpuResumeIntr(int s) { (void)s; }
static void FlushIcache(void) {}
static int DelayThread(int usec);
static int GetSystemTime(iop_sys_clock_t *t);
static void SysClock2USec(iop_sys_clock_t *t, u32 *sec, u32 *usec);
