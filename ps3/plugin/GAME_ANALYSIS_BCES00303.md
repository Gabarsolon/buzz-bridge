> Runtime 0.9 update: manual 0.8 loads work on all three titles per the user; automatic 0.8 attach hung. [New per-title mutex/NetCtl addresses, source quotes, readiness gate and limits](../build/ATTACH_SAFETY_09.md). Earlier timing/verification statements below are historical and superseded for this trial. No native thread identity or console-wide freeze cause has been proved.

# BCES00303 APP_VER 01.00: static USB evidence for runtime 0.8

Reviewed 2026-10-26. Input: the owner’s decrypted original Buzz!: Quiz TV Special Edition.
Full input SHA-256, rechecked this session: `11848045c3884d68b250ab849dcd3d0e0e071e50327246a4f8936e972d70de6d`. ELF size: 7,694,392 bytes.

Ghidra 12.1.3 and the already-installed pyghidra supplied selected PowerPC disassembly/decompilation in an ephemeral ProgramDB. PT_LOAD segments were mapped in memory because the decrypted section-name table is empty. No Ghidra project, game ELF/SELF, asset, full disassembly or decompilation was saved in the work folder. `research/porting/query.py` reproduces targeted read-only queries. `tools/inspect_game.py` independently decodes ELF/import metadata and validates the derived shape. PC tests compare it with the actual C resolver. No PPU/game instructions were executed; console compatibility is pending.

## Imports and registration

Import interval: `0x5e0084..0x5e044c`. Main TOC: `0x730890`. Slot addresses below are the game’s data words, replaced atomically only after the loader has installed valid compact OPDs. Stub addresses are executable trampolines, never replacement targets.

| FNID | Export | Slot | Trampoline | Direct callsites |
|---|---|---|---|---|
| `0f411262` | cellUsbdInterruptTransfer | `6fa3a4` | `5c5a2c` | `baf48`, `bafa0` |
| `254289ac` | cellUsbdOpenPipe | `6fa3a8` | `5c5a4c` | `bb600`, `bb620`, `bb6c8` |
| `2fb08e1e` | cellUsbdScanStaticDescriptor | `6fa3ac` | `5c5a6c` | `bb468`, `bb4dc`, `bb518`, `bb570`, `bc4e8` |
| `35f22ac3` | cellUsbdEnd | `6fa3b0` | `5c5a8c` | `bcd60` |
| `5de3af36` | cellUsbdGetPrivateData | `6fa3b4` | `5c5aac` | `bc818` |
| `63bfdb97` | cellUsbdSetPrivateData | `6fa3b8` | `5c5acc` | `bb654` |
| `7fe92c54` | cellUsbdRegisterExtraLdd | `6fa3bc` | `5c5aec` | `bc71c` |
| `90460081` | cellUsbdUnregisterExtraLdd | `6fa3c0` | `5c5b0c` | `bc618` |
| `9763e962` | cellUsbdClosePipe | `6fa3c4` | `5c5b2c` | `bb3c0`, `bb3d0`, `bb3e8` |
| `97cf128e` | cellUsbdControlTransfer | `6fa3c8` | `5c5b4c` | `bb06c`, `bb350`, `bb6a4`, `bb810` |
| `bd554bcb` | cellUsbdRegisterExtraLdd2 | `6fa3cc` | `5c5b6c` | `bc6b0` |
| `d0e766fe` | cellUsbdInit | `6fa3d0` | `5c5b8c` | `bd1f8` |
| `db819e03` | cellUsbdGetDeviceLocation | `6fa3d4` | `5c5bac` | `bc940` |

The fourteenth hook is sys_io/cellPadGetData FNID `8b72cda1`, slot `0x6fa528`, trampoline `0x5c5dec`. It tees the game’s successful reads; no competing pad polling was introduced. Neither executable imports cellUsbdRegisterLdd/UnregisterLdd. Both Extra registration variants occur in one table-walking registration routine.

| Record | Address | VID | Inclusive PID range |
|---|---|---|---|
| buzzer0 | `75fb44` | `054c` | `1000..1040` |
| buzzer1 | `75fb5c` | `054c` | `0001..0041` |
| buzzer2 | `75fb74` | `054c` | `0002..0042` |
| buzzer9 | `75fb8c` | `046d` | `c220..c220` |

All four records use probe/attach/detach OPDs `700750`, `7007a0`, `700748`. The table ends with a zero VID sentinel. Buzzer2 is the unique selected wired record, even though buzzer1’s registration range numerically overlaps PID 0002. Registration filters are not the probe’s exact predicate. The probe accepts Sony `054c:1000,1020,1040,0001,0021,0041,0002,0022,0042` and Logitech `046d:c220`; returns 0 on acceptance and -1 otherwise; refuses when two devices are already counted. No class-only match is used.

## Wired-device sequence

1. Probe `0xbcb30` -> singleton getter `0xbca70` -> predicate `0xbc448`; ScanStaticDescriptor(type 1) is at `0xbc4e8`.
2. Attach `0xbd120` -> `0xbcf88` -> location/object allocation `0xbc8a8` -> device attach `0xbb418`. GetDeviceLocation callsite is `0xbc940`; matching saved location chooses one of two persistent objects.
3. Device attach scans descriptor types 2, 4, 5, then 1; opens EP0 and IN at `0xbb600/0xbb620`, optionally OUT at `0xbb6c8`, and stores private data at `0xbb654`. IN size=min(max packet,16)=8; output size=7.
4. Product request at `0xbb6a4`: `(80,06,0300|iProduct,0409,128)`. Product completion `0x54bf90` -> `0xbb7a0` processes the returned string and submits SET_CONFIGURATION `(00,09,config,0,0)` at `0xbb810`.
5. Configuration completion `0x54bf60` -> `0xbb2f8` submits SET_INTERFACE `(01,0b,alt,interface,0)` at `0xbb350`.
6. Interface completion `0x54bfc0` -> `0xbb8b0` selects wired handling, sends initial lights off, sets object+0x10c=1 and submits IN through `0xbaf68` (actual callsite `0xbafa0`).
7. IN completion `0x54bf30` -> `0xbafc0` queues five bytes at `0xbacc8` on result 0 and rearms IN even on error. The optional OUT submission is `0xbaf48`; its completion `0x54bf10` is a no-op.
8. Detach `0xbcae8` -> `0xbc7f0` obtains private data (GetPrivateData callsite `0xbc818`), marks unavailable and decrements count. Pipe shutdown `0xbb370` closes EP0, IN and optional OUT; manager shutdown `0xbcbe8` unregisters all four LDDs and ends only an owned libusbd initialization.

These are API-level static reconstructions. No GET_REPORT_DESCRIPTOR control request appears on this wired startup path. Static descriptor scanning reads the cache and does not submit a control transfer. Product completion is followed by configuration/interface, and normal deferred callbacks establish readiness; there is no presence-bypass patch.

## Input, lights, timing and presence

`0xbacc8/0xbadb0` queue/reassemble five input bytes; `0xbc238` selects the low twenty bits. Light request construction is `0xbb010/0xbb090`, callsite `0xbb06c`; payload builders `0xbb0d0/0xbb278`.
Four handsets use active-high five-bit groups starting in byte 2: red bit 0, yellow bit 1, green bit 2, orange bit 3, blue bit 4. Handset 4 crosses byte 3 bit 7 / byte 4 low nibble. The two prefix bytes are not a separate readiness gate. Lights use `(21,09,0200,interface,7)` and `[00,L1,L2,L3,L4,00,00]` with 00/ff lamps. An available OUT endpoint selects interrupt output; our consistent wired descriptor has only IN, so control output is used.

The IN completion rearms immediately and the manager drains a five-report ring on updates. No fixed sleep is present in that driver loop. No evidence requires a new timing value: the adapter retains 100 us control / 6000 us IN deadlines, descriptor interval 10 ms and native minimum 2 ms USB sleep. Deadlines/descriptor bytes come from the existing RPCS3/PSL1GHT evidence in [USB_CONTRACT_07.md](game/USB_CONTRACT_07.md), not a new physical capture.

Singleton pointer slot `0x729d0c` contains base `0x78a178`; getter returns base+8 (`0x78a180`). Init completes by writing manager+0x94 (`0x78a214`)=1 only after allocation/mutex/registration. Successful attach increments manager+0xa8 (`0x78a228`) under the game’s mutex. Update publishes manager+0x69/+0x8d (`0x78a1e9/0x78a20d`), using ff for absent and 00/01 for the device slots. Each object’s +0x10c setup marker gates publication. The plugin observes these fields and never writes them.

Update is `0xbd458`; count method `0xbbec8` is called from `0x97118`. It reads those same two presence bytes.
This establishes the driver route to two-dongle/eight-handset presence, not every retail screen/script policy or multiplayer mode. No separate kernel-presence query is needed by this recovered manager path. A complete console match remains necessary.

## Every observed difference from BCES00347 in the analyzed path

* All USB-manager/device-code addresses in the reviewed units move by -0x1e0; main TOC, import-table bounds, slots, OPDs, LDD and singleton data have their own relocations.
* USB device-core and callback units have identical canonical instruction streams/CRCs to the reference (`9336827c`, `920de19d`). Canonicalization changes external addresses/TOC displacements only; constants, local control flow and cross-unit/import identities remain checked.
* The manager canonical comparison differs only in the connected-device count function: TV at `0xbbec8` uses an explicit compare with ff for its first byte, where the reference at `0xbc0a8` uses arithmetic. Both count the two non-ff presence bytes; the compiler changes instruction order/return position within the same 0x58-byte region. This produces a separate manager-shape CRC (`498e699a` versus `8c5ffd2a`), not a protocol variation.
* Driver diagnostic strings, LDD table/whitelist, setup, report use, lights, capacity, timing and presence offsets have no observed semantic difference. FIXEDPADS / INPUT_MAPPING_ALGORITHM remain present. No adapter change is justified.

## Exact profile and evidence limits

`profile.h` / `profiles.json` pin the full source ELF SHA-256 and 47 immutable CRC32 ranges; all fourteen resolved slots are explicit. Full SHA-256 is checked on the file during generation/inspection. Live identity uses those derived immutable fingerprints because the loader changes import slots and the game changes its data. Live slots are independently discovered, their OPDs validated and original values compared atomically. CRC32 is a build/shape guard, not adversarial authentication.

The VSH helper now includes this title and its own exact readiness addresses; it retains a 15-second continuous settle, one attempt per PID, R2/disable/stop behavior. Generic mode is confined to recognized driver code shapes and does not broaden the helper’s title allowlist. The original adapter/model/network assets remain hash-identical to the frozen reference. The 0.8 runtime/profile/helper combination has no console verification. See [PORTING.md](../PORTING.md) and [console procedure](../build/CONSOLE_TEST_08.md).
