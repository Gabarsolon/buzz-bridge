/* Single list of the input layouts. Included with different BB_LAYOUT macros by
 * bb_core.h (enum), bb_core.c (names) and tools/bb_patch.py (it parses this file).
 * Keep one entry per line: BB_LAYOUT(id, name, "description") */
BB_LAYOUT(0, buzzers,       "buzzers only: port 1 (netemu dongle pad) = handsets 1-2")
BB_LAYOUT(1, buzzers_ds,    "buzzers + 1 DualShock: port 1 = handsets 1-2, port 2 split = handsets 3-4")
BB_LAYOUT(2, two_ds_shared, "2 DualShocks shared: port 1 split = handsets 1-2, port 2 split = handsets 3-4")
BB_LAYOUT(3, two_ds_single, "2 DualShocks, one player each: port 1 = handset 1, port 2 = handset 2 (whole pad)")
BB_LAYOUT(4, one_ds_shared, "1 DualShock shared: port 1 split = handsets 1-2")
BB_LAYOUT(5, buzzers_ds2_tap1, "MULTITAP on port 1: buzzers (slot A) = handsets 1-2, DualShocks in slots B and C = handsets 3 and 4 (whole pad each)")
BB_LAYOUT(6, four_ds_tap1,  "MULTITAP on port 1: DualShocks in slots A-D = handsets 1-4, one player each (whole pad)")
BB_LAYOUT(7, buzzers_ds2_tap2, "MULTITAP on port 2: buzzers on port 1 = handsets 1-2, DualShocks in port 2 slots A and B = handsets 3 and 4 (whole pad each)")
BB_LAYOUT(8, eight_ds_tap12, "MULTITAP on both ports, TWO virtual dongles: port 1 slots A-D = handsets 1-4, port 2 slots A-D = handsets 5-8 (whole pad each)")
BB_LAYOUT(9, any_buzzers_ds, "Buzz! buzzers + DualShocks on any slots (4 players)")
BB_LAYOUT(10, any_ds4, "DualShocks on any slots (up to 4 players)")
BB_LAYOUT(11, any_2buzzers_ds, "2 pairs of Buzz! buzzers + DualShocks on any slots (8 players)")
BB_LAYOUT(12, any_ds8, "DualShocks on any slots (up to 8 players)")
