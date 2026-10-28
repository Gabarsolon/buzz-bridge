/* What vusb.c needs from its host: the IOP glue (iop_os.c) or the unit-test mock. */
#ifndef VUSB_OS_H
#define VUSB_OS_H

#include <stdio.h>
#include "vusb.h"

/* log line to the IOP console (stdio printf) or the test console */
#define vos_log(...) printf("BBUSB: " __VA_ARGS__)

int vos_lock(void);                /* short critical section; returns state for vos_unlock */
void vos_unlock(int state);
/* Run client code with the client's $gp, on the calling (worker) thread, never with the lock held. */
void vos_call_done(vu_done_t cb, int result, int count, void *arg, void *gp);
int vos_call_ldd(int (*fn)(int), int devId, void *gp);
unsigned vos_sample(uint16_t src_masks[BB_NSRC]); /* buttons + valid source bit mask (idle pads included) */
unsigned vos_flags(void);          /* BB_FLAG_* from the patch-time configuration */

#endif
