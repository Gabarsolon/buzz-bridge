> Runtime 0.9 update: manual 0.8 loads work on all three titles per the user; automatic 0.8 attach hung. [New per-title mutex/NetCtl addresses, source quotes, readiness gate and limits](../build/ATTACH_SAFETY_09.md). Earlier timing/verification statements below are historical and superseded for this trial. No native thread identity or console-wide freeze cause has been proved.

# BCES00347 01.00: static evidence and limits

Analysis date: 2026-10-26. The supplied decrypted `game/EBOOT.elf` is the user's own Buzz!: Brain of the UK. SHA-256: `6f4d1f05cde3d8a6e7836b65c7103cff0ccc6a248ca0b877a7da3e3453585a19`. Nothing in this report is a console trace or a game compatibility test. Game files were read only. The deliverables contain derived addresses and short instruction excerpts, not an EBOOT, asset, full decompilation or patch distribution.

Ghidra 12.1.3 / installed pyghidra supplied PowerPC disassembly/decompilation. No Capstone, emulator or new package was used. The ELF's section-name string table is zero-filled in this decrypted file, so the successful analysis mapped PT_LOAD segments directly: file offset 0 -> VA 0x10000; file offset 0x6e0000 -> VA 0x6f0000. TOC is 0x730ca8, established by the entry OPD at 0x6fa5f8. Function descriptors are two 32-bit words, code and TOC. Only temporary analysis scratch held full decompiler output; that scratch is removed at handoff.

Reproduce the static checks with `python research/verify_game.py`. It reads the game and saved public source, verifies the ELF hash, FNIDs, import trampoline slots, LDD records and patch fingerprints, then writes derived metadata to `research/game_audit.json`. `research/game_elf.py` provides additional read-only queries. `research/gh_game.py fast` is the analysis helper; its address queries live in disposable `analysis_private`, not deliverables.

## Quick win: a built-in route exists, a turnkey pad-only fix is not verified

These are **input-assignment policy enum names in the scripting framework**, not discovered command-line arguments, PARAM.SFO fields, save settings or retail menu labels. At 0xcab08 and the expanded registry constructor 0xcb2e0, the game registers enum category 6, named `INPUT_MAPPING_ALGORITHM` (string VA 0x5ee178), with these values:

| Name | Value | Dispatch at 0xa1520 |
|---|---:|---|
| FIXEDPADS | 1 | 0xa159c -> 0x9f000; assigns physical channels 0,1,2,4 |
| FIXEDBUZZERS | 2 | 0xa15ec -> 0x9eef0; assigns channels starting at 8 |
| FIXED2PADS | 3 | 0xa15d8 -> 0x9ef88; assigns channels 0 and 4 |
| FIRSTSET | 4 | generic activation path 0x9ed20, with policy-specific handling at 0x96730 |
| FIRSTDYNAMIC | 5 | generic activation path; the default chosen at startup |
| DYNAMIC / FIRSTINSET / ONESWAPONACTIVATION | 6 / 7 / 8 | related activation policies |
| BYNETWORKCLIENT | 9 | 0xa12a0 |
| ONEPLAYERALLBUZZERS | 10 | 0xa15b0 -> 0x9ee90; assigns aggregate physical channel 24 |

The names describe mapping policies, not USB driver modes. The physical-device setup at 0xa23e8 creates eight pad channels 0-7; 0xa1fb0 creates eight buzzer channels 8-15; 0xa0e00 creates channels 16-23. The aggregate route 24 is explicit in 0x9ee90. Do not infer eight working pads from the eight allocated pad-channel objects.

FIRSTSET's case at 0x967c4 calls 0x95468 -> 0x952f8, which assigns the activating channel's quartet (0-3, 4-7, 8-11 or 12-15) and stops further assignment. Thus it selects a device set based on the first activation. FIRSTDYNAMIC's case at 0x96854 -> 0x95558 instead compares later joiners' device-family/type with the first assigned device. These policies affect logical assignment after input activation; they do not change USB VID/PID filtering.

The Lua binding **`ClearAndRestartMappingDevices`** is registered at 0x11f564 (within the binding registry at 0x11f350); its handler at 0x134128 takes a numeric first argument and an optional enum string second argument. It resolves the string through enum category 6 and calls 0x9f940 -> 0x9f8e0. A missing/unrecognized second argument selects 5. The handler therefore supports the setting:

```lua
ClearAndRestartMappingDevices(4, "FIXEDPADS")
```

This is a script API call, **not a launch argument or an HDD configuration file**. No USRDIR scripts/containers were supplied, so their actual filenames, call order and retail overrides cannot be identified here. There is no evidence in this session of a retail menu exposing this enum. Finding the script binding does not prove a save/XML switch exists.

The default game-specific initializer at 0xa28b0 ends with:

```text
000a2928  li r4,0x8
000a292c  li r5,0x5
000a2944  b  0x0009f940
```

**Smallest startup selector change:** in the decrypted ELF, **file offset 0x9292f: `05 -> 01`**, changing `li r5,5` to `li r5,1` at VA 0xa292c. This selects FIXEDPADS at that initialization. Later Lua calls can override it. No file was patched. This ELF offset must never be applied directly to the encrypted EBOOT.BIN/SELF.

**One-instruction override that also covers subsequent calls:** change **VA 0x9f904 / decrypted ELF offset 0x8f904**, `7c bc 2b 78 -> 3b 80 00 01` (`or r28,r5,r5` -> `li r28,1`). The relevant setter is:

```text
0009f904  or  r28,r5,r5
0009f90c  bl  0x0009f810
0009f910  stw r28,0xa4(r29)
0009f914  stw r27,0x9c(r29)
```

For the no-permanent-modification requirement, a future console experiment would apply the replacement **only to the running game process**, with expected-byte comparison and instruction-cache maintenance, then quit the game to undo. The USB SPRX in this tree does not apply either pad patch. A process-memory writer alone is not proof of correct PPU instruction-cache invalidation; that remains a console-test requirement.

Two limitations prevent advertising a working pad-only quick win:

1. **Legacy fourth-player mapping:** 0x9f000 assigns 0,1,2,4. The pad's channel getter 0x5486d0 returns its stored index unchanged; 0x9b070 only fills its five-button mask for indices **less than 4**. Thus the hard-coded fourth assignment is inconsistent with the active PS3 pad conversion. A separate one-byte correction is VA **0x9f043 / ELF offset 0x8f043: `04 -> 03`**, changing `li r4,4` at 0x9f040 to `li r4,3`. FIXED2PADS has the same index-4 issue. These are static findings; corrected four-pad gameplay was not executed.
2. **Buzzer-presence policy remains separate:** `GetNumberOfConnectedBuzzers` -> 0x1345c0 -> 0x9bd68 reads the buzzer module. `AreAnyBuzzersConnected` -> 0x12b918 -> 0x2d0fb8 ultimately checks physical buzzer state at 0x97748/0x97818. `DisableTRCBuzzerChecker` is a separate Lua binding (handler 0x122440), not an effect of selecting FIXEDPADS. Its name and call exist, but disabling the complete retail gate flow has not been verified. Do not claim the four-byte selector alone removes every “connect buzzers” screen.

The pad conversion's mask table at 0x75c590 is `0x0f00,0x1000,0x4000,0x8000,0x2000`: shoulders plus the four face buttons. This is a developer/testing route retained in the executable, with a visible legacy mapping defect; it is not evidence of advertised retail DualShock support.

**Other titles:** localized national “Brain of …” builds are plausible candidates for this same route, inferred from the binary's `BOB_BrainOf` source-path strings and this shared registry/driver structure. Quiz TV and Quiz World are reasonable future comparison targets, but neither executable was supplied. No cross-title mode, address, patch, import set or compatibility is verified. Never reuse these offsets on another title/update.

## All cellUsbd imports

The 44-byte import records span VA 0x5e3c98-0x5e4060. The library name is `cellUsbd`; its record points to function FNIDs and a 32-bit import-address table. Names were resolved by SHA-1(name + suffix `6759659904250490566427499489741A`), taking the first four digest bytes as a little-endian FNID, against RPCS3's saved registration names. Recomputed hashes match all thirteen imports.

| FNID | Name | Trampoline VA | Import slot VA |
|---|---|---:|---:|
| 0f411262 | cellUsbdInterruptTransfer | 5c966c | 6fa3f4 |
| 254289ac | cellUsbdOpenPipe | 5c968c | 6fa3f8 |
| 2fb08e1e | cellUsbdScanStaticDescriptor | 5c96ac | 6fa3fc |
| 35f22ac3 | cellUsbdEnd | 5c96cc | 6fa400 |
| 5de3af36 | cellUsbdGetPrivateData | 5c96ec | 6fa404 |
| 63bfdb97 | cellUsbdSetPrivateData | 5c970c | 6fa408 |
| 7fe92c54 | cellUsbdRegisterExtraLdd | 5c972c | 6fa40c |
| 90460081 | cellUsbdUnregisterExtraLdd | 5c974c | 6fa410 |
| 9763e962 | cellUsbdClosePipe | 5c976c | 6fa414 |
| 97cf128e | cellUsbdControlTransfer | 5c978c | 6fa418 |
| bd554bcb | cellUsbdRegisterExtraLdd2 | 5c97ac | 6fa41c |
| d0e766fe | cellUsbdInit | 5c97cc | 6fa420 |
| db819e03 | cellUsbdGetDeviceLocation | 5c97ec | 6fa424 |

This game does **not** import RegisterLdd, bulk/isochronous transfers, or a HID report parser through cellUsbd. The public API names/prototypes are independently readable in [RPCS3 cellUsbd.cpp](https://github.com/RPCS3/rpcs3/blob/39fe6c2599f3f03b68940df1cad22305e78106b9/rpcs3/Emu/Cell/Modules/cellUsbd.cpp) and [PSL1GHT usb.h](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/ppu/include/usb/usb.h). RPCS3's HLE implementations are largely stubs; their presence does not establish Sony behavior.

## LDD registration and accepted identities

Initialization 0xbd348 calls Init, tolerating `0x80110002` (already initialized), allocates two device objects, and calls the registration loop 0xbc838. It only calls End during cleanup if it owned successful initialization. LDD table VA **0x760144**, four 24-byte records followed by a zero record:

| Name | API | VID | PID range |
|---|---|---|---|
| buzzer0 | RegisterExtraLdd2 | 054c | 1000-1040 |
| buzzer1 | RegisterExtraLdd2 | 054c | 0001-0041 |
| buzzer2 | RegisterExtraLdd2 | 054c | **0002-0042** |
| buzzer9 | RegisterExtraLdd | 046d | c220 |

All share probe OPD 0x700820 -> **0xbcd10**, attach OPD 0x700870 -> **0xbd300**, detach OPD 0x700818 -> **0xbccc8**. The probe implementation is also visible at 0xbc628. It rejects when the manager count is >=2, scans static descriptor type 1 and accepts only:

* Sony 054c: **1000,1020,1040,0001,0021,0041,0002,0022,0042**;
* Logitech 046d:**c220**.

The ranges are registration filters; the exact probe whitelist is narrower. No USB-class match was found in the probe. This corrects the previous assumption that RPCS3's broad passthrough ranges were the game's predicate. RPCS3's `buzzer2` passthrough entry is 0042 alone, whereas this game's registration range starts at 0002. The selected virtual 054c:0002 passes both the game's range and exact probe check; the portable descriptor identity therefore remains correct.

## Reconstructed wired-device call sequence

The attachment path is 0xbd300 -> 0xbd168 -> 0xbb5f8; device allocation/location matching is 0xbca88. The following is an API-level reconstruction, not executed game code:

1. Probe: ScanStaticDescriptor(dev,NULL,1); inspect VID/PID.
2. Attach: GetDeviceLocation(dev,seven-byte buffer); choose one of two stable device objects, preferring a matching saved location.
3. ScanStaticDescriptor(dev,NULL,2), then scan type 4 after the configuration; require at least one interface/endpoint. Walk type 5 descriptors after the interface. Keep interrupt-IN, optionally interrupt-OUT. Re-read type 1 device descriptor.
4. OpenPipe(dev,NULL) for EP0; OpenPipe(dev,IN descriptor); open optional OUT pipe if present. Store device-private data. Requested IN size is **min(endpoint wMaxPacketSize,16)**; the RPCS3 wired profile gives 8. Output size is set to **7**.
5. ControlTransfer EP0: **type=0x80, request=6, value=0x0300 | iProduct, index=0x0409, length=128**. Product buffer is persistent in the device object. Completion 0x54e470 processes the product string and submits SET_CONFIGURATION using the selected descriptor's configuration value.
6. Configuration completion **0x54e440** submits **SET_INTERFACE**, type=1, request=11, value=alternate setting, index=interface number, length=0. The wired profile has alternate/interface zero.
7. Interface completion **0x54e4a0** selects wired/wireless handling, submits all-lights-off output, marks setup ready, and submits interrupt-IN. Wireless handling can additionally send channel commands; virtual wireless radio support is not implemented.
8. IN completion **0x54e410** checks result==0, queues the first **five bytes**, then immediately calls InterruptTransfer again, **even after an error result**. Submission call sites 0xbb128 / 0xbb180 use callbacks 0x54e3f0 (OUT no-op) / 0x54e410 (IN rearm). It does not wait for an event syscall or return a transfer ID to the game.
9. Detach 0xbccc8 gets private data, marks the device unavailable and decrements the manager count. Shutdown 0xbb550 closes EP0, IN and optional OUT; 0xbcdc8 unregisters all four extra LDDs and conditionally ends libusbd.

There is no GET_REPORT_DESCRIPTOR request in this attachment sequence. Static descriptor scanning reads the descriptor cache; it is not itself a ControlTransfer. Supplying the reconstructed HID report descriptor remains useful for other requests, but this game's wired startup does not depend on parsing its 78 bytes.

### Input, lights and timing

0xbaea8 copies five input bytes into a five-report ring; 0xbaf90 rearranges the last three bytes into a word, and 0xbc418 takes its low twenty button bits. Combined with the explicit per-handset mappings at 0xa1fb0, this agrees with the portable encoder and [RPCS3 Buzz.cpp](https://github.com/RPCS3/rpcs3/blob/39fe6c2599f3f03b68940df1cad22305e78106b9/rpcs3/Emu/Io/Buzz.cpp): four groups of five active-high bits beginning at byte2. Per group: red bit0, yellow bit1, green bit2, orange bit3, blue bit4. Handset 4 crosses byte3 bit7 / byte4 low nibble. The model's idle `7f 7f 00 00 f0` comes from RPCS3, not a new physical capture. The game consumes the button bits without requiring that axis/vendor prefix.

Light output at 0xbb1f0/0xbb270 uses **type=0x21, request=9, value=0x0200, index=interface, length=7**. The wired profile's wire setup is `21 09 00 02 00 00 07 00`. 0xbb2b0 and 0xbb458 fill `[00,L1,L2,L3,L4,00,00]`, with 00/ff values. This now verifies the setup and seven-byte output for this title instead of merely inferring them from Linux. If an interrupt-OUT pipe exists, the driver can use that instead of the control fallback; the selected wired descriptor has only IN, so the adapter need not fabricate an OUT endpoint.

The game continually rearms IN in the callback and drains its queue on updates; no fixed sleep was found in that loop. The endpoint's interval comes from the actual device descriptor. Our profile advertises 10ms, taken from RPCS3/the saved physical descriptor reference. The portable model retains RPCS3's **6000us input / 100us control** deadlines; RPCS3 labels the input delay “TODO accurate measurement.” These are simulation choices, not measured Sony or hardware timing. The native candidate pumps at roughly 1ms, with ordinary thread scheduling jitter.

Both the probe count and two-element manager/device loops establish a **two-dongle / eight-handset driver capacity** in this executable. The available player count still depends on the game's script/mode. This is stronger evidence than RPCS3's seven-pad emulation limit, and does not establish every other Buzz game's capacity.

## Native hook evidence

The interrupt trampoline at 0x5c966c computes import slot 0x6fa3f4, loads its descriptor pointer, saves r2, loads code+TOC, and branches through CTR. Representative tail:

```text
005c9674  lwz r12,-0x5c0c(r12)
005c9678  std r2,0x28(r1)
005c967c  lwz r0,0(r12)
005c9680  lwz r2,4(r12)
005c9688  bctr
```

The native design replaces the **resolved descriptor pointer in the import data slot**, preserving the original OPD for real calls. It does not replace these instructions or globally patch libusbd's exports. The ELF initially contains unresolved slot values; `valid_opd` refuses them until the game loader has resolved imports. Correct runtime compact-OPD layout, mappings and cache/barrier behavior still require console verification.

The current native build uses the validated wired buzzer2 LDD record at 0x760174 (the earlier candidate adopted the overlapping buzzer1 range at 0x76015c) after the manager-ready byte at **0x78ab14** becomes 1. That byte derives from singleton base 0x78aa78, manager offset 8, ready offset 0x94, in 0xbd348. The adapter adopts those callbacks without repeating the four real registrations. This addresses the startup race for this build; it is not a generic late-injection solution for unknown titles.

The adapter reserves process-private device/pipe namespaces, returns public submission status 0, and supplies callbacks itself. It does not inject events into native libusbd or allocate native transfer IDs. Runtime 0.7 handles all operations on its two virtual IDs locally, including the diagnostic location lookup before probe; the former real-location collision preflight was invalid and removed. See [USB_CONTRACT_07.md](game/USB_CONTRACT_07.md) for the supplied 0.6.1 FATAL result and per-export source evidence. Known real pipes in the reserved range are tracked and forwarded; a later native allocation colliding with a live virtual pipe is closed and fails with an allocation error. This rare-case policy avoids misrouting but cannot promise transparent coexistence for every possible native handle. Native callback execution and namespace coexistence remain hardware questions.

## Native-build follow-up: late adoption and the presence gate

Rechecked in this run against the unchanged SHA-256 above. The existing static singleton pointer at VA **0x72a164** is **0x78aa78**. Getter **0xbcc50** returns base+8, hence manager **0x78aa80**. Initialization **0xbd348** sets manager+0x94 only after the device objects/mutex/LDD registrations are ready: absolute ready **0x78ab14**. The current module adopts `buzzer2` at **0x760174**, VID/PID range 054c:0002?0042, with the same known callback OPDs. It does not call native registration again.

Probe 0xbcd10 tail-calls 0xbc628. The accepted wired identity returns **0**, rejection returns -1; this agrees with the adapter. Attach 0xbd300 obtains a device object, runs 0xbb5f8, and on success increments manager+0xa8 (**0x78ab28**) under the game's own mutex. The callback itself manages this lock; the adapter does not acquire it recursively. Real libusbd callbacks can still run concurrently, so full hardware scheduling/coexistence remains untested.

The retail presence checks read the manager's cached state rather than requiring an independent kernel probe. Manager update **0xbd638** examines both device objects; once the device's setup marker +0x10c is set, it writes the slot number to manager+0x69/+0x8d (**0x78aae9/0x78ab0d**). The constructor sets these bytes to ff; update keeps ff for absent devices. `GetNumberOfConnectedBuzzers` reaches **0xbc0a8**, which reads both bytes, and `AreAnyBuzzersConnected` reaches **0x97748/0x97818**, which tests the same two records. Short instruction evidence at 0xbc0a8:

```text
000bc0b0  lbz r0,0x8d(r3)
000bc0b8  lbz r3,0x69(r3)
```

Thus successful late probe/attach and the normal configuration/interface sequence have a static route to the game's existing ?connected buzzers? checks. No presence-bypass patch or earlier injection point is indicated by this path. This is **not console verification** that every retail screen/gameplay policy accepts the virtual devices. The log records count and these presence bytes to resolve that in the first test.

The native build now fingerprints 33 immutable ranges (including every hooked trampoline/import header, static LDD/callback data and relevant code) and checks fourteen exact slot addresses. CRC fingerprints are per-range identity checks, not a full cryptographic hash of the live image. The build generator verifies the full original input ELF SHA-256; each live slot is rechecked by atomic compare/replace. The four pad-route words must remain original, so the separate patched EBOOT is refused.

User-supplied console context reports the pad route also needs VA **0xa2904: 38800018 -> 38800000**, enabling family 0 instead of family 0x18, in addition to the three previously recorded words at 0xa292c, 0x9f040, 0x9f904. Two pads worked, but the physical-buzzer gate and a match-start freeze remain. This run did not execute that patch or investigate the freeze. The USB trial deliberately uses the original game so the existing buzzer-family mapping is enabled.
