/* Single list of the input layouts. Included with different BB_LAYOUT macros by
 * bb_core.h (enum), bb_core.c (names) and tools/bb_patch.py (it parses this file).
 * Keep one entry per line: BB_LAYOUT(id, name, "description") */
BB_LAYOUT(0, buzzers,       "buzzers only: port 1 (netemu dongle pad) = handsets 1-2")
BB_LAYOUT(1, buzzers_ds,    "buzzers + 1 DualShock: port 1 = handsets 1-2, port 2 split = handsets 3-4")
BB_LAYOUT(2, two_ds_shared, "2 DualShocks shared: port 1 split = handsets 1-2, port 2 split = handsets 3-4")
BB_LAYOUT(3, two_ds_single, "2 DualShocks, one player each: port 1 = handset 1, port 2 = handset 2 (whole pad)")
BB_LAYOUT(4, one_ds_shared, "1 DualShock shared: port 1 split = handsets 1-2")
