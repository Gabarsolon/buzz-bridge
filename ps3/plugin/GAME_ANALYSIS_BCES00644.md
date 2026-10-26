> Runtime 0.9 update: manual 0.8 loads work on all three titles per the user; automatic 0.8 attach hung. [New per-title mutex/NetCtl addresses, source quotes, readiness gate and limits](../build/ATTACH_SAFETY_09.md). Earlier timing/verification statements below are historical and superseded for this trial. No native thread identity or console-wide freeze cause has been proved.

# BCES00644 APP_VER 01.00: static USB evidence for runtime 0.8

Reviewed 2026-10-26. Input: the owner’s decrypted original Buzz!: Quiz World.
Full input SHA-256, rechecked this session: `6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8`. ELF size: 9,147,848 bytes.

Ghidra 12.1.3 and the already-installed pyghidra supplied selected PowerPC disassembly/decompilation in an ephemeral ProgramDB. PT_LOAD segments were mapped in memory because the decrypted section-name table is empty. No Ghidra project, game ELF/SELF, asset, full disassembly or decompilation was saved in the work folder. `research/porting/query.py` reproduces targeted read-only queries. `tools/inspect_game.py` independently decodes ELF/import metadata and validates the derived shape. PC tests compare it with the actual C resolver. No PPU/game instructions were executed; console compatibility is pending.

## Imports and registration

Import interval: `0x6c0f10..0x6c13b4`. Main TOC: `0x8759d8`. Slot addresses below are the game’s data words, replaced atomically only after the loader has installed valid compact OPDs. Stub addresses are executable trampolines, never replacement targets.

| FNID | Export | Slot | Trampoline | Direct callsites |
|---|---|---|---|---|
| `2fb08e1e` | cellUsbdScanStaticDescriptor | `85c260` | `6a55b4` | `222ae8`, `4d8a60`, `4d8acc`, `4d8b08`, `4d8b60` |
| `35f22ac3` | cellUsbdEnd | `85c264` | `6a55d4` | `223270` |
| `5de3af36` | cellUsbdGetPrivateData | `85c268` | `6a55f4` | `222db4` |
| `7fe92c54` | cellUsbdRegisterExtraLdd | `85c26c` | `6a5614` | `222cdc` |
| `90460081` | cellUsbdUnregisterExtraLdd | `85c270` | `6a5634` | `222be8` |
| `bd554bcb` | cellUsbdRegisterExtraLdd2 | `85c274` | `6a5654` | `222c70` |
| `d0e766fe` | cellUsbdInit | `85c278` | `6a5674` | `223598` |
| `db819e03` | cellUsbdGetDeviceLocation | `85c27c` | `6a5694` | `222ea8` |
| `0f411262` | cellUsbdInterruptTransfer | `85c280` | `6a6134` | `4d8628`, `4d8670` |
| `254289ac` | cellUsbdOpenPipe | `85c284` | `6a6154` | `4d8bf0`, `4d8c10`, `4d8cb8` |
| `63bfdb97` | cellUsbdSetPrivateData | `85c288` | `6a6174` | `4d8c44` |
| `9763e962` | cellUsbdClosePipe | `85c28c` | `6a6194` | `4d89c8`, `4d89d8`, `4d89f0` |
| `97cf128e` | cellUsbdControlTransfer | `85c290` | `6a61b4` | `4d871c`, `4d8970`, `4d8c94`, `4d8de8` |

The fourteenth hook is sys_io/cellPadGetData FNID `8b72cda1`, slot `0x85c444`, trampoline `0x6a4b94`. It tees the game’s successful reads; no competing pad polling was introduced. Neither executable imports cellUsbdRegisterLdd/UnregisterLdd. Both Extra registration variants occur in one table-walking registration routine.

| Record | Address | VID | Inclusive PID range |
|---|---|---|---|
| buzzer0 | `8bb168` | `054c` | `1000..1040` |
| buzzer1 | `8bb180` | `054c` | `0001..0041` |
| buzzer2 | `8bb198` | `054c` | `0002..0042` |
| buzzer9 | `8bb1b0` | `046d` | `c220..c220` |

All four records use probe/attach/detach OPDs `863060`, `863070`, `863058`. The table ends with a zero VID sentinel. Buzzer2 is the unique selected wired record, even though buzzer1’s registration range numerically overlaps PID 0002. Registration filters are not the probe’s exact predicate. The probe accepts Sony `054c:1000,1020,1040,0001,0021,0041,0002,0022,0042` and Logitech `046d:c220`; returns 0 on acceptance and -1 otherwise; refuses when two devices are already counted. No class-only match is used.

## Wired-device sequence

The missing “USB Buzzer Driver” / “BuzzerModule” diagnostics do not imply a different USB protocol. This build separates manager code from device code and uses another TOC. `0x5b63e8` saves r2, adds `0x10000-0x18`, then branches to `0x4d8a18`; device TOC is therefore `0x8859c0`. USB completion OPDs carry that TOC; the adapter invokes the original descriptors and lets the game’s bridges restore its calling convention. The generic contract fingerprints the bridge block as a fourth code unit.

1. Probe `0x223078` calls singleton getter `0x222fd8`, then tail-calls `0x222a60`; descriptor type 1 supplies VID/PID.
2. Attach `0x223500` -> `0x2233e0` -> device/location selection `0x222e20` (GetDeviceLocation at `0x222ea8`). Two persistent objects are matched by seven-byte location. Device attach crosses the TOC bridge at `0x223468` to `0x4d8a18`.
3. Device attach scans types 2, 4, 5, then 1; picks interrupt-IN and optional interrupt-OUT. OpenPipe EP0/IN occur at `0x4d8bf0/0x4d8c10`; SetPrivateData at `0x4d8c44`. IN buffer length is min(endpoint max packet,16), hence 8 with our descriptor; output length is 7.
4. Product request at `0x4d8c94`: `(80,06,0300|iProduct,0409,128)`. Completion OPD `0x869728` -> thunk `0x69c828` -> `0x4d8d80` processes the string and submits SET_CONFIGURATION `(00,09,config,0,0)` at `0x4d8de8`.
5. Configuration callback OPD `0x869720` -> `0x69c810` -> `0x4d8920` submits SET_INTERFACE `(01,0b,alt,interface,0)` at `0x4d8970`.
6. Interface callback OPD `0x869730` -> `0x69c840` enters the device’s finish-setup routine. It selects wired handling, sends initial lights off, marks object+0x10c=1 and submits IN through `0x4d8640`.
7. IN completion OPD `0x869718` -> `0x69c7f8` -> `0x4d8688`: success queues five bytes at `0x4d8408`; the tail call to `0x4d8640` rearms even after an error. OUT completion OPD `0x869710` -> `0x69c7f0` returns without work.
8. Detach `0x223040` -> `0x222d98` obtains private data (GetPrivateData callsite `0x222db4`), marks unavailable and decrements count under the manager lock. Pipe shutdown `0x4d8988` closes EP0, IN and optional OUT; manager shutdown `0x2230b0` unregisters the four LDDs and calls End only if this driver owned Init.

These are API-level static reconstructions. No GET_REPORT_DESCRIPTOR control request appears on this wired startup path. Static descriptor scanning reads the cache and does not submit a control transfer. Product completion is followed by configuration/interface, and normal deferred callbacks establish readiness; there is no presence-bypass patch.

## Input, lights, timing and presence

`0x4d8408/0x4d84d0` queue/reassemble five input bytes; manager input conversion uses the low twenty button bits. Light request construction is `0x4d86c8`, callsite `0x4d871c`; payload builders include `0x4d8758`.
Four handsets use active-high five-bit groups starting in byte 2: red bit 0, yellow bit 1, green bit 2, orange bit 3, blue bit 4. Handset 4 crosses byte 3 bit 7 / byte 4 low nibble. The two prefix bytes are not a separate readiness gate. Lights use `(21,09,0200,interface,7)` and `[00,L1,L2,L3,L4,00,00]` with 00/ff lamps. An available OUT endpoint selects interrupt output; our consistent wired descriptor has only IN, so control output is used.

The IN completion rearms immediately and the manager drains a five-report ring on updates. No fixed sleep is present in that driver loop. No evidence requires a new timing value: the adapter retains 100 us control / 6000 us IN deadlines, descriptor interval 10 ms and native minimum 2 ms USB sleep. Deadlines/descriptor bytes come from the existing RPCS3/PSL1GHT evidence in [USB_CONTRACT_07.md](game/USB_CONTRACT_07.md), not a new physical capture.

Singleton pointer slot `0x873598` contains base `0x8ce308`; getter returns base+8 (`0x8ce310`). Init completes by writing manager+0x94 (`0x8ce3a4`)=1 only after allocation/mutex/registration. Successful attach increments manager+0xa8 (`0x8ce3b8`) under the game’s mutex. Update publishes manager+0x69/+0x8d (`0x8ce379/0x8ce39d`), using ff for absent and 00/01 for the device slots. Each object’s +0x10c setup marker gates publication. The plugin observes these fields and never writes them.

Update is `0x2237b0`; count method `0x222730` is called from `0x29aff8`. It reads those same two presence bytes.
This establishes the driver route to two-dongle/eight-handset presence, not every retail screen/script policy or multiplayer mode. No separate kernel-presence query is needed by this recovered manager path. A complete console match remains necessary.

## Every observed difference from BCES00347 in the analyzed path

* Import table, slot order and addresses differ; eight cellUsbd trampolines are grouped at `0x6a55b4..0x6a5694`, five at `0x6a6134..0x6a61b4`. The API set and compact-OPD thunk shape are identical.
* Debug banner and BuzzerModule strings are absent. LDD names, four filters, exact probe whitelist, two-device capacity and callback API are retained.
* Manager code is at `0x222670..0x223a30`, device code at `0x4d83e8..0x4d9110`, callbacks at `0x69c7e8..0x69c858`. The separate device TOC and `0x5b6388..0x5b6428` bridge unit must be preserved. Stack/register allocation and instruction scheduling differ throughout.
* The initialization path omits the two preliminary external calls visible in the reference before cellUsbdInit; its USB Init / ALREADY_INITIALIZED handling, two object allocations, mutex creation, registration and final ready store are unchanged in meaning.
* Manager attach has a shared branch back to its device-attach call instead of the reference’s duplicated device-attach callsite. Success still increments +0xa8 under lock; failure releases the selected object.
* Data addresses/OPDs/TOCs differ as tabulated; object offsets, five-byte report, twenty bits, seven-byte lights, control request fields, callback rearm and presence sentinel match.
* FIXEDPADS / INPUT_MAPPING_ALGORITHM remain present, but generic mode does not change those scripting policies. No game-specific adapter change is justified by this path.

## Exact profile and evidence limits

`profile.h` / `profiles.json` pin the full source ELF SHA-256 and 47 immutable CRC32 ranges; all fourteen resolved slots are explicit. Full SHA-256 is checked on the file during generation/inspection. Live identity uses those derived immutable fingerprints because the loader changes import slots and the game changes its data. Live slots are independently discovered, their OPDs validated and original values compared atomically. CRC32 is a build/shape guard, not adversarial authentication.

The VSH helper now includes this title and its own exact readiness addresses; it retains a 15-second continuous settle, one attempt per PID, R2/disable/stop behavior. Generic mode is confined to recognized driver code shapes and does not broaden the helper’s title allowlist. The original adapter/model/network assets remain hash-identical to the frozen reference. The 0.8 runtime/profile/helper combination has no console verification. See [PORTING.md](../PORTING.md) and [console procedure](../build/CONSOLE_TEST_08.md).
