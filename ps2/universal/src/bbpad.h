#ifndef BBPAD_H
#define BBPAD_H

#include <stdint.h>
#include "bb_core.h"

/* Returns 1 hooked, 0 SIO2MAN not loaded yet (try again later), -1 unsupported (never retry). */
int bbpad_install(void);
void bbpad_tick(void);                        /* every worker tick: ages the snapshots */
void bbpad_sample(uint16_t masks[BB_NSRC]);   /* writes the current buttons of ports that answered recently */
int bbpad_state(void);
/* own polling (BB_FLAG_POLL_*). bbpad_poll_loop is the body of a separate thread. */
void bbpad_poll_loop(unsigned flags);
unsigned bbpad_poll_count(void);
int bbpad_polling(void);
unsigned bbpad_hook_calls(void);
unsigned bbpad_pad_events(void);

#endif
