# Buzz! Bridge: PS3 app with three play routes

This workspace adds a controller menu to the existing homebrew app. D-pad moves,
Cross selects, Circle goes back. Every ISO or emulator replacement asks for an
explicit confirmation. The owner has tested the ISO browser fix (all ISOs now
appear), the 576i UI fix (flicker gone), and the finished modified
`ps2_netemu.self` with four handsets and game lamps on a real console.
The app's entry-2 write/restore/rollback integration remains PC-tested only.
PC equality checks and the PS3DK package build are recorded in
[REPORT.md](../REPORT.md#wording-fixes).

At the root, Circle is ignored. Choose **Exit to XMB** or use PS-button Quit
Game to quit. Submenus, ISO folders, wizard steps, status pages and confirmations
return to their preceding screen with Circle; it never confirms a write.

## 1. Phones and DualShock for PS3 Buzz! games

Select the first menu entry, then use the existing **Start for this session**,
**Enable at boot**, or **Disable at boot** dialogs. Circle backs from Disable to
Enable, then Start, then the root. Returning to entry 1 does not repeat the
payload installation: it runs once per app launch, with the same safety-disable
ordering. Yes at Start starts the helper, shows its result and exits to XMB.
Completed boot edits also exit after their result; No at Disable leaves boot
settings unchanged and exits. No at Start/Enable opens the next boot question.
The signed game/helper payloads and default settings are byte-identical to the
owner's `bridge_rev0a` copies. Session start does not change the boot list.
Reboot before another game. Phones open `http://<PS3-IP>:8420/` during a supported
game. R1/R2 = red, Triangle = blue, Square = orange, Circle = green, Cross = yellow.

The owner's prior console results remain: BCES00347 01.00 full match,
BCES00303 01.00 automatic app start, and BCES00644. Enable at boot and HEN
remain unproven. The recent browser and 576i UI fixes have owner console
verification; entry-1 Circle/menu-flow changes have PC verification only.

## 2. Real Buzz! handsets in PS2 games, no ISO patching

CFW only. The full `BuzzBridge.pkg` bundles one modified `ps2_netemu.self`
(four handsets + lamps) and `ps2_netemu_original.self` (Evilnat 4.93 restore
fallback). The modified file is owner-tested and stable on a real console.
There is one install action, **Install the modified ps2_netemu.self (four
handsets + lamps)**, with no build chooser. The owner plans to publish the full
package and the standalone `ps2_netemu.self` together in a GitHub release.
`BuzzBridge-public.pkg` is the second variant: it contains no netemu files and
offers STATUS and restoring user backups only. No separate patch-data
provisioning is used. Nothing was published by this work.

STATUS hashes `/dev_flash/ps2emu/ps2_netemu.self`: original Evilnat 4.93,
**modified ps2_netemu (this build)**, **older modified build**, or unknown.
All 45 historical allow-list hashes remain recognized, so earlier modified
files can be upgraded or restored. Its backup list shows every saved file in
pages, including invalid/partial backups, with individual full hashes.

INSTALL asks for confirmation, default No. Unknown current hashes need an extra
OVERWRITE confirmation. An identical target is a no-op. Current bytes are saved
to `/dev_hdd0/buzzbridge/netemu_backup/ps2_netemu_<hash8>_<UTC timestamp>_<serial>.self`
using exclusive creation and verified by full SHA256 before any writable mount.
Existing backups are never overwritten or deleted, even on errors.

RESTORE chooses the oldest timestamped backup whose verified hash is not one of
our builds, including an unknown original from another firmware revision. It
refuses a failed/incomplete directory scan. If none exists, the full build
can restore the bundled original after a confirmation naming **Evilnat 4.93**.
The public build has no fallback file. A separate list allows a specific user
backup to be selected. Saved names carry hash8; every list/restore rehashes the
file and rejects accidental damage that changes that prefix. The full selected
hash is pinned across confirmation and transaction source validation.

Replacement keeps the established webMAN **127.0.0.1** `/dev_blind?1` / `?0`
mount route and writes `/dev_blind/ps2emu/ps2_netemu.self` directly. It verifies
SHA256 by reading back, and attempts unmount on every mount-attempt path.
HEN or no writable path is refused. Any returned write/read-back/unmount failure
attempts automatic verified rollback from the just-saved HDD backup. Persistent
storage or mount failures are reported with the recovery path. Successful
installation says "Done. You can start your PS2 game now." Restoring the bundled
original says "Done. The original is back."

Modified netemu needs original, unpatched ISOs; never combine it with ISO
patching. The previous bounded warning scan remains. A power cut or watchdog
exit during blocked I/O cannot execute live rollback; the verified HDD backup
remains for restore on the next app run or with a file manager. A bad
netemu affects PS2 emulation; manual backup recovery or firmware reinstall may
be needed. No in-app install/restore, writable-flash I/O, local mount/unmount,
backup enumeration, HEN detection, or rollback has been verified on a console. The
finished modified file's handset/lamp test does not verify those app operations.
No live firmware failure injection was performed.

## 3. Patch a PS2 ISO (CFW, original netemu)

CFW only, keeping original netemu, with plain `.iso` images through Cobra.
Browse `/dev_hdd0/PS2ISO` or
accessible `/dev_usb000` through `/dev_usb007` folders. Choose `/dev_hdd0` to
browse other HDD folders. Each page contains up to 254 files/subfolders;
Previous/Next page controls provide access to the rest. Entries are sorted
within each page (pages follow filesystem enumeration order); keep the folder
unchanged while paging. Eight subfolder levels are permitted. File sizes use
64-bit metadata and are shown in binary MB/GB units before the name so even
long names retain a visible size. A `?` means details could not be read; the
entry remains selectable and its path/return code is logged to
`/dev_hdd0/tmp/buzzbridge_app.log`. ISO contents are inspected only on selection. Only plain extracted
2048-byte-sector `.iso` files are supported; USB format support depends on the
console's filesystem driver. Detected volume and a root executable title ID are
shown where available. Check only performs no writes.

Recognised standalone USBD module families are Schools/Mega (1.3.0), Junior
(1.4.0), and Monster Rumble (1.6.0), using the same three module fingerprints
as the PC patcher. Recognition requires one unambiguous `/IOP/USBD.IRX` or
`/SYSTEM/USBD.IRX` in a supported ISO layout; a title/filename alone does not
establish compatibility. Embedded-only modules and other fingerprints remain
unsupported. Homebrew images remain visible and selecting one reports that
it is not a supported Buzz! game; the underlying diagnostic is shown too.

The short setup list uses the PC wizard's exact seven numbered choices:

1. Buzz! buzzers only
2. Buzz! buzzers + 1 DualShock
3. 2 DualShocks, shared
4. 2 DualShocks, one player each
5. 1 DualShock, shared
6. Buzz! buzzers + 2 DualShocks (any slots)
7. More than 2 controllers (any slots)

Choice 6 encodes custom layout 13 with `(pairs, whole, shared) = (1, 2, 0)`.
Choice 7 retains the pairs question, DualShock count, and valid sharing options.
All 14 machine layout names/numbers are retained in C. No user configuration
file is requested. A known modified netemu blocks ISO patching and check-patch.
An unrecognized fingerprint (including other stock firmware revisions) requires
explicit confirmation that the console keeps its original netemu. ISO restoration
remains available. An already patched ISO is identified and
warned about during inspection.

The C patcher follows the supplied `bb_patch.py`: one unambiguous
`/IOP/USBD.IRX` or `/SYSTEM/USBD.IRX`, three known original SHA1 fingerprints,
unchanged ISO directory entries, replacement module plus zero fill to the
original file length. The supplied universal patcher does **not** edit
SYSTEM.CNF or IOPRP images; neither does this port. Embedded-only/unknown
layouts are refused. Firmware runtime substitution has separate IOPRP logic.

Only the module extent (normally about 35 KB) is changed. Its original bytes
are saved in `<ISO>.usbd.orig`, expected patched bytes in `<ISO>.usbd.buzz`,
and size, mtime, offset, length, layout, SHA1 and SHA256 checks in
`<ISO>.bbps3.json`. All three are synced and read back before the ISO write.
Restore verifies image geometry and both recovery blobs, accepts an exact or
interrupted write, refuses unrelated extent bytes, and writes/verifies the
original bytes. After a clean restore a new layout may reuse the verified
original backup; generated expected bytes/metadata are refreshed while the ISO
is still original. The original backup is never overwritten. Recovery files
remain after restoration and should stay beside the ISO when moved.

## PC alternatives on Linux and macOS

The PC distribution provides `install.sh` and `restore.sh` for the CFW netemu
route. For ISO patching, the repository also provides the standard-library-only
Python patcher: from its `ps2/` directory run `python3 buzz_bridge_ps2.py` with
the ISO path and desired options. It needs Python 3 and no pip dependencies.
Keep original netemu when using the ISO route; the two routes must not be combined.
These PC scripts were not executed in this work.

## Build and offline tests

From the workspace root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ps3/build_ps3.ps1
python -B tests/run_all.py
```

The SDK remains read only. Build outputs are `deliverables/BuzzBridge.pkg` (full) and
`deliverables/BuzzBridge-public.pkg`, with actual finalized-package member hashes
in `package_manifest.json` and `package_manifest-public.json`.
The installed host PyCryptodome package supplies AES for read-only PKG acceptance. Both app variants are rebuilt;
the existing signed phone/helper binaries are retained and hash-pinned in
`ps3/payloads/`. No SDK install, PS3 connection, game launch, flash write or
installer execution is performed by these commands. Exact manual console
test steps and remaining limits are in [REPORT.md](../REPORT.md).

## Diagnostics

- The app keeps a log at `/dev_hdd0/tmp/buzzbridge_app.log` and a heartbeat (one line every 5 seconds: memory, frame and flip counts, open files) at `/dev_hdd0/tmp/buzzbridge_heartbeat.log`. Send both if something goes wrong.
- After 15 minutes without any button press at a menu or dialog, the app returns to the XMB by itself.
