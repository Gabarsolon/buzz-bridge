# Mode C pad bridge — 2026-11-07

Implemented experimental **layout 14 / `mode_c`**, PC menu choice **8**.
One clean Mode C Pico, assigned to PS2 controller **port 1, slot A**, supplies
four handsets and receives the game's four lamp bits. IDs 0–13 and the 16-byte
`BBZCFG01` version-1 configuration remain stable. No netemu patch, console
connection, upload, firmware edit, commit or push was performed.

## Evidence and remaining console gate

The user reports these real-PS3 results on CFW 4.93 / stock ps2_netemu:

- A clean 054C:0268 Mode C Pico registers as a pad.
- Gran Turismo vibration reaches the Pico and lights all four physical lamps
  with **rumble probe on**.
- Mode B plus layout 13 / `--buzzer-pairs 2` carries four handsets' buttons.

Those establish pad recognition and a vibration transport. They do **not**
establish all 20 Mode C input bits, opposite digital directions, all sixteen
rumble amplitude levels, PREPARE/COMMIT timing, or independent game lamp forwarding.
Those are the next console test, with **rumble probe OFF**. HEN needs no emulator
modification in this design, but this new module has not been tested on HEN or
on the user's CFW console by this task.

The read-only firmware contract was
`pico/docs/MODE_C_PROTOCOL.md`, with
`REPORT_LAMPS.md` and `REPORT_MODEC.md` as context. Their older “untested” claims
about enumeration/vibration are superseded only by the user's evidence above.

## Input and pad polling

The existing implementation hooks SIO2MAN 2.x transfer exports **25 / 51** and
relinks existing callers. It observes the game's completed PADMAN transfers.
Legacy `--poll auto` waits one second after hook installation and starts its
own digital reads after 500 ms without game pad traffic. `--poll always`
reads continually; `--poll off` normally leaves passive observation. Layouts
using additional slots already have their own session-gated service. These
legacy mappings and polling rules remain intact.

Legacy own polls request only five bytes; even if netemu's pad is analog,
that request cannot carry the four axes. Mode C instead runs the existing
lower-priority poll thread every **16 ms**, including when the game polls.
The patcher promotes `--poll off` to `auto` for this layout; the module itself
also starts the Mode C service regardless of poll flags. Explicit `--no-pad`
and `--script-demo` still disable physical pad servicing.

Mode C only accepts port 1/A, address 01, command 42, with a bounded SIO2 FIFO
entry. Its decoder checks transfer-status bit 13, at least nine response
bytes, byte 0 = FF, ID **73 or 79**, and byte 2 = 5A. It implements exactly:

```c
bits = ((~reply[3]) & 255) | (((~reply[4]) & 255) << 8);
bits |= (reply[7] >= 0xB0) << 16;  /* LX */
bits |= (reply[8] >= 0xB0) << 17;  /* LY */
bits |= (reply[5] >= 0xB0) << 18;  /* RX */
bits |= (reply[6] >= 0xB0) << 19;  /* RY */
```

The first nine bytes of a 79 pressure reply are sufficient. Digital 41,
flight-stick 53, configuration F3, malformed headers and failed reads do not
become partial handsets. Mode C bypasses the legacy 16-bit button bindings and
packs this 20-bit word directly with `bb_report_pack`. A failed own read clears
input and triggers reconfiguration. Missing snapshots expire after the existing
approximately one-second freshness limit. A short, otherwise valid analog
discovery poll from the game is ignored without refreshing or erasing the last
complete snapshot; it cannot manufacture stick data.

**Analog mode is not assumed.** A DS3 being analog-capable does not prove the
guest pad is currently in analog mode. With no game enabling it, the module
enters configuration using 43, uses **44 / analog=1 / unlocked=0** if the
previous mode was digital, sends **4D / 00 01 FF FF FF FF**, and exits using 43.
An already analog/pressure pad skips 44 so its pressure settings survive. It
then verifies an analog 42 response. Failed setup always attempts exit and
retries at most once per second.

This follows the command layouts and port timing in
[PS2SDK PADMAN's sio2Cmds.c](https://github.com/ps2dev/ps2sdk/blob/master/iop/input/padman/src/sio2Cmds.c).
Setup uses four configuration transfers at most plus one poll, all inside a
single SIO2MAN session, so the game cannot read an intermediate F3 reply.
Nine-byte polls/configuration are standard PADMAN frame shapes; no new custom
SIO2 framing was introduced. Actual stock-netemu mode transitions remain a
hardware validation item.

## Lamp route, scheduling and PADMAN sharing

The game's unchanged BUZZER.IRX submits HID **SET_REPORT** on the virtual
dongle's default control pipe: request type 21, request 09, value 0200,
interface 0, seven bytes **00 L1 L2 L3 L4 00 00**. In `vusb.c`,
`control_request()` already copies these bytes to `vu.st.lights[d]`.
Mode C additionally publishes the four nonzero lamp bytes as mask bits 0–3.
Malformed/short/wrong-type reports do not reach its lamp mailbox. Identical
valid reports are still published. Detach/unregister requests mask zero.

The USB callback only stores a small mailbox value, even while interrupts are
suspended. It never waits for a pad or sleeps. The existing **4 ms USB worker**
(priority 0x24) continues delivering game callbacks and input reports.

The **16 ms poll thread** (priority 0x28, below the USB worker) owns setup and
the lamp state machine. It acquires the same SIO2MAN **46 / 51 / 52** pad-session
route already used for polling. The session acquisition can wait for PADMAN;
this wait is confined to the poll thread. There are no added delays inside a
session. After setup, each session normally contains one nine-byte poll.
The handshake matches
[PS2SDK SIO2MAN](https://github.com/ps2dev/ps2sdk/blob/master/iop/sio/sio2man/src/sio2man.c).

For each desired mask it sends, in 42 poll bytes 3 and 4:

| Successful poll | Small motor | Large motor |
|---|---:|---:|
| PREPARE 1 | 0 | 17 × mask |
| PREPARE 2 | 0 | 17 × mask |
| COMMIT 1 | 1 | 17 × mask |
| COMMIT 2 | 1 | 17 × mask |
| Idle | 1 | 17 × mask |

A new mask restarts PREPARE, including mask **0** to clear. Failed polls do not
advance a phase. Every mask starts another complete pair every **250 ms**,
even unchanged, including **zero**. The Pico consumes the first matching COMMIT;
repeated COMMITs alone do nothing. Idle keeps that consumed state until refresh.
Refreshing zero also recovers from stock netemu suppressing an initial redundant
both-zero PREPARE: the first COMMIT changes the native motor state, so the next
zero PREPARE can reach USB and its matching COMMIT can clear/claim the lamps.
The firmware's **1500 ms lease** clears
lamps when updates cease; there is no acknowledgement of physical completion.

Timing uses IOP **GetSystemTime / SysClock2USec**, sampled after acquiring the
session, with wrap-safe millisecond subtraction. An unfinished sequence older
than **400 ms** restarts PREPARE, below the firmware's 500 ms matching deadline.
Normal phase dwell is about 32 ms each, subject to scheduling. Neither IOP nor
stock netemu is a real-time guarantee; USB/provider coalescing may still lose
the strobe. This cannot be established by host tests.

When the game's PADMAN issues its own 42 poll on the reserved port, the hook
temporarily overlays **only motor bytes 3/4**, calls the original transfer,
then restores the caller's request bytes. It preserves descriptors, sizes,
button/axis response data, other ports and other slots. This prevents game
polls from inserting zero rumble between protocol phases. The motor mapping
is established before overlay is enabled.

Game configuration commands pass through; they invalidate the module's map
and defer its setup until there have been **500 ms without configuration**.
The next bounded setup establishes the dedicated mapping again. This avoids
interleaving configuration command sequences. A game continuously changing
configuration or keeping another slot selected can starve the channel; lamp
leases then expire. The port must remain assigned to the Pico.

The module serializes access; it does not make two independent owners' pad
settings compatible. **Port 1/A is reserved for Mode C.** Game rumble on that
port is intentionally replaced. A title depending on its own digital-mode
setting, actuator mapping or vibration on that same pad is outside the
supported setup. Buzz titles do not need that pad as a gameplay controller.
An ordinary real DualShock must not be assigned as this source: it would
receive protocol vibration. Other layouts never run this protocol.

## Build, packaging and validation

Built using the installed `C:\ps2dev` SDK and MSYS2 MINGW32 at `C:\msys64`, with
explicit PS2DEV/PS2SDK/PATH and workspace-local temporary files. The SDK path
contains no spaces. The source patcher reads `universal/bbusbd.irx`; there is
no separate root `tools/bb_patch.py` in this checkout—the actual implementation
is `ps2/universal/tools/bb_patch.py`.

- IRX: **29,505 bytes**, SHA-256
  `5a6fed024a6bba041e24b5e389ed3acd57127b6d5a157b48c1be6b47b4ec6afc`.
- Rebuilt `ps2/buzz-bridge-ps2.exe`: SHA-256
  `86aa72b7b02c31640e17b0bfc34da21622e51e91f97602bf7674145a4c1e3338`.
- Extracted the embedded IRX from the PyInstaller archive and compared it
  byte-for-byte with the rebuilt module. Python/frozen layout lists agree.
- Updated the shared C ISO patcher's module hash, layout-name/configuration
  bounds and restore bounds for ID 14. Existing C menu choices remain stable.
- Native pure-core/USB tests: **5,392 checks, zero failures**. Actual `bbpad.c`
  with a fake IOP/SIO2 bus: **657 checks, zero failures** (GCC 32-bit and Clang
  64-bit). These include every
  bit/axis threshold, analog and pressure headers, status/length/source refusal,
  every mask amplitude, PREPARE/COMMIT dwell, repeats, zero, wrap/expiry,
  configuration cleanup/retry, suppressed initial zero recovery, buffer
  restoration and legacy isolation.
- `ps2/tests`: **50 tests passed**; frozen-exe smoke: **14 checks passed**.
- `check_irx.py`: passed, additionally using this Schools Quiz ISO's original
  USBD and IOPRP300 image. All imports, including thbase 34/40 for time, are
  present; the usbd 1.2 table remains 19 slots.
- Full repository runner: **14/14 groups passed**, recorded in
  `build/test-results.json`. After the final zero-refresh refinement, the
  affected native, PS2 and C/Python byte-comparison/recovery checks were rerun
  successfully. The C/Python tests cover 90 layout/family patch-and-restore
  comparisons, 95 flag combinations and all 49 supported custom count triples.
- Shell/PowerShell parse checks passed. Test logs remain in
  `ps2/universal/build/`; UI previews are ignored generated artifacts.

Initial full-suite attempts exposed absent ignored signing/netemu fixtures in
this public checkout. Existing local copies from
a local copy of the app workspace were copied into ignored
workspace fixture paths, without modifying that source tree. No replacement
fixtures or weakened hash checks were invented. Updating the C patcher's IRX
pin and ID-14 recovery bound resolved the integration failures. The existing
runner excludes its socket/worker-liveness process-stop runners; those remain
outside its advertised 14 groups.

## Local test ISO and requested D: command

The only writable area for this task is the repository, so the final ISO is:

`build/bb_modeC_final/Buzz! The Schools Quiz (Europe) (Buzz Bridge).iso` (ignored by git)

Its `.usbd.orig` and `.bbpatch.json` recovery files are beside it. Keep them
with the ISO. Earlier copies under `build\modeC` and `build\bb_modeC` are
intermediate builds; use `build\bb_modeC_final`.

- Original input (read only):
  `E:\Downloads\Buzz The Schools Quiz (Europe)\Buzz! The Schools Quiz (Europe).iso`.
- ISO size: **3,007,021,056 bytes**. Only the original **34,985-byte USBD extent
  at LBA 2542** changes; size, directory records and every other byte compare
  identical to the input. Patched configuration is layout 14, flags 11 hex,
  attach/heartbeat 100 ms, reserved byte zero.
- Input SHA-256:
  `eda08ad786125f1a0aaf6b8440db5a6b705700e0b4d472f7e1b164bfae357246`.
- Patched ISO SHA-256:
  `3e01f714cefece2f70bf9dbaf31d81b712478a86330904015773e8da57313d47`.
- Verification metadata: `build\bb_modeC_final\verification.json`.

To produce exactly the requested D: output yourself, run in PowerShell:

```powershell
New-Item -ItemType Directory -Force -Path 'D:\bb_modeC' | Out-Null
& .\ps2\buzz-bridge-ps2.exe 'Buzz! The Schools Quiz (Europe).iso' --layout mode_c --poll auto --out 'Buzz! The Schools Quiz (Europe) (Buzz Bridge).iso'
```

This refuses an existing output. Alternatively copy the final workspace ISO
**and both recovery files** into `D:\bb_modeC`; no repatching is needed.

## Exact next hardware test — user-operated only

1. Connect the Pico's native USB to the PC with CDC available. If it is already
   clean C, its documented P1 red+blue three-second gesture switches to C/debug
   so these commands can run. In PowerShell, turn the diagnostic off **before**
   selecting clean Mode C:

   ```powershell
   python pico/tools/bridge_cli.py rumble probe off
   # Wait for CDC to return after any settings reboot, then:
   python pico/tools/bridge_cli.py config c 0
   ```

   Clean C has no CDC. Keep the real Buzz dongle attached to the Pico's host
   USB. Do not use Mode B, debug C's alternate identity, or rumble probe on.
2. Use stock ps2_netemu, connect the Pico's native USB to the PS3 and assign it
   controller **1**. Keep other controllers from occupying that source. If
   necessary use the documented four-red hold for the PS activation gesture.
3. Launch the final **layout-14** Schools Quiz ISO. Join all four handsets with
   their blue buttons. Test each handset's red/yellow/green/orange/blue once,
   especially **P4 yellow, green, orange, blue** (the four axes). Check that
   no handset/color activates another.
4. Check simultaneous input: P1 blue + P2 yellow (Up+Down), and P2 red + P2 green
   (Right+Left). These are independent Buzz buttons despite their DS3 fields.
   Missing one direction indicates a host normalization limit, not a different
   handset mapping to paper over in the guest.
5. Play a question with each player buzzing first in turn. Confirm the game's
   lamp states reach the correct individual physical lamps, not invariably all
   four. Check a lamp state that lasts **more than two seconds** to exercise
   lease refresh, then check the game's all-off transition.
6. Report four results: all 20 buttons; simultaneous opposite directions;
   individual lamp identities; sustained/cleared lamps. A pad accepted with no
   P4 colors suggests analog/axis transport. All lamps together suggests probe
   mode still enabled. Buttons working with no lamps or lamps dying after
   1.5 seconds suggests map/amplitude/strobe/refresh transport and needs a saved
   Pico DS3 log before unplugging, following its existing diagnostic procedure.

No Pico firmware change is required by the documented contract. Its current
`firmware_source/ds3.c` explicitly rejects small-motor values above **1**. If
netemu emits FF instead of 01 for COMMIT, the Pico would need a documented
boolean-normalization change. Likewise, transformed large-motor amplitudes or
coalesced phases require captured motor values/timing before changing tolerance,
dwell or encoding. Those firmware changes belong in the read-only project's
next task; this task did not edit it.
