/* SPDX-License-Identifier: MIT. Explicit counters only: no link wrappers, no
 * locks, no allocation. They feed the heartbeat and the install leak check. */
#include "app_health.h"
atomic_int app_files,app_dirs,app_sockets;
