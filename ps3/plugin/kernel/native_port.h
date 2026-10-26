/* SPDX-License-Identifier: MIT */
#ifndef BUZZ_NATIVE_PORT_H
#define BUZZ_NATIVE_PORT_H
#include <stdint.h>
/* REQUIRED, UNIMPLEMENTED hardware adapter contract. Do not fill with guessed offsets.
 * Return false until ALL of the following have been verified on the exact LV2:
 *  - firmware fingerprint, PPU ABI/TOC and all original syscall OPDs;
 *  - copy_from_user/copy_to_user, caller process identity, process-exit cleanup;
 *  - LDD matching, real-device enumeration and collision-free native IDs;
 *  - per-(PID,USB handle) sessions and independent dongle pipes;
 *  - native 540 wait queue integration (549 is only a hypothesis to test);
 *  - 543 async buffer lifetime, 545 result/count and transfer ID bounds;
 *  - no real USB request or error changed; compare physical traffic;
 *  - transactional install/rollback, synchronization, cache barriers;
 *  - in-flight request draining and game-exit detach before any uninstall.
 *
 * These operations need kernel-only code and MUST NOT use VSH libnet/libio.
 * See DESIGN.md's adapter acceptance checklist and syscall table.
 */
int buzz_native_install_verified(void);
#endif
