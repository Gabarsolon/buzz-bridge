# Buzz! Bridge PS2

Play PS2 **Buzz!** games using DualShock controllers, or try real wired Buzz! buzzers through the PS3's `ps2_netemu` controller mapping. Up to four players can share two controllers.

The universal patcher replaces the game's `USBD.IRX` with our **`bbusbd.irx` module**. It presents a virtual wired Buzz! dongle (`054C:0002`) to the game's own, unmodified `BUZZER.IRX`. Controller ports 1 and 2 supply the buttons. No game executable addresses or per-game cheats are needed.

## Status

These are **user-reported tests of the universal module in PCSX2 v2.8.2**, supplied during integration. The integration checks use synthetic ISOs and native unit tests; no emulator or console was run during integration. Recognising a USBD version does not establish that a game works.

| Game | Universal module in PCSX2 | Universal module on PS3 CFW / HEN |
|---|---|---|
| Buzz! The Schools Quiz (Relentless driver) | Working: scripted demo and `two_ds_shared` | Untested / untested |
| Buzz! Junior: Jungle Party (Junior driver) | Working: scripted demo and `two_ds_shared` with `--poll auto` | Untested / untested |
| Buzz! The Mega Quiz | Untested | Untested / untested |
| Buzz! Junior: Ace Racers | Working: `two_ds_shared` with `--poll auto` (the game reads no controllers itself) | Untested / untested |
| Buzz! Junior: Dino Den | Working: `two_ds_shared` with `--poll auto` (the game reads no controllers itself) | Untested / untested |
| Buzz! Junior: Monster Rumble | Working: `two_ds_shared` with `--poll auto` (the game reads no controllers itself) | Untested / untested |
| Buzz! Junior: RoboJam | Working: `two_ds_shared` with `--poll auto` (the game reads no controllers itself) | Untested / untested |
| Other games, regions and editions | Untested; unknown USBD versions are refused | Untested / untested |

The older **EE game-code patcher** is preserved under [legacy/](legacy/). Its earlier Schools Quiz PS3 result and Mega Quiz PCSX2 result do **not** validate the universal module on those platforms. See [legacy/README.md](legacy/README.md) for that tool's mappings, limits and in-place restore.

## Patch your ISO

Use your own **unpatched, plain ISO** (2048-byte sectors). If you previously used the legacy tool, restore its EE changes with that tool first, or start from an original ISO.

1. Drag one ISO onto **`buzz-bridge-ps2.exe`**, or double-click the exe and drag the ISO into its prompt. **Extract ZIP archives first**: dragging a file from Explorer's ZIP view can pass an invalid temporary path. The exe embeds the module; no Python or separate IRX is needed.
2. Check **Your game** (volume, USBD driver version and ISO size), then answer **What do you have?** in **Your setup** using the setups below. The patcher shows the selected setup's controller button table.
3. **Patching** starts immediately after your choice, with a live copy bar showing percent, MB/s and ETA, followed by module writing and verification. Wait for **Done** / `Patched:`. The default output is **`<name> (Buzz Bridge).iso` beside the input**. The input stays unchanged. Existing output and recovery files are never overwritten.
4. Keep **`<patched ISO>.usbd.orig`** and **`<patched ISO>.bbpatch.json`** beside that patched ISO for restoration. They contain a local backup of your original USBD and checksums. Do not upload them with a bug report.

The patcher checks free disk space before copying. Allow space for a complete ISO plus small recovery files. `--poll auto` is the default: the module observes the game's pad reads and reads the pads itself when the game does not. BBUSB logging is on by default.

The console uses buzzer colours when the terminal supports them. Set `NO_COLOR` to disable colour; redirected output stays plain and omits the live bar. Double-click and drag-drop launches pause at the end so you can read the result. **Done** includes short PCSX2 / PS3 CFW / HEN instructions and an Undo command. The patcher reads back the written module and both recovery files before reporting success.

Example progress (the copy bar refreshes on one line):

```text
3 / Patching
------------------------------------------------------------
Patching the ISO now. Please keep this window open.
Output: C:\Games\My Game (Buzz Bridge).iso
Copying the ISO (3.80 GB); large games can take a few minutes ...
Copying ISO [############------------]  50%    95.0 MB/s  ETA 00:20
Flushing ISO copy to disk ... done
Writing Buzz! Bridge module ... done
Saving recovery files ... done
Verifying ... done
Finishing patched ISO ... done

4 / Done
```

To run from source, install nothing: Python 3.9+ and the files in this folder are enough. Run commands from `ps2/`:

```powershell
python buzz_bridge_ps2.py "My Game.iso"
python buzz_bridge_ps2.py "My Game.iso" --layout two_ds_shared
python buzz_bridge_ps2.py "My Game.iso" --layout 1 --out "My custom copy.iso"
python buzz_bridge_ps2.py "My Game.iso" --layout buzzers --in-place
python buzz_bridge_ps2.py "My Game (Buzz Bridge).iso" --info
python buzz_bridge_ps2.py "My Game (Buzz Bridge).iso" --restore
```

Use the same arguments with `buzz-bridge-ps2.exe`. `--layout` avoids the setup prompt; invocations with flags do not pause on exit. `--in-place` explicitly modifies the input and writes its recovery files first. **`--restore` modifies the ISO you name in place**, restores only its USBD extent, and retains recovery files. Move the ISO and both recovery files together; their backup paths are relative. For a different layout after restoration, keep an original ISO and choose a new output name.

If recovery files are lost, an unchanged original of the same ISO can supply the original USBD:

```powershell
python buzz_bridge_ps2.py "My Game (Buzz Bridge).iso" --restore --from-iso "My Game.iso"
```

Restoration refuses wrong checksums, changed extents and unrelated USBD edits. Recovery files also allow an interrupted extent write to be restored. Do not use another patcher on the same USBD. An unknown USBD hash is refused with a reportable SHA-1; there is no force option. `--info` is read-only and can inspect unknown versions.

## Setups and controller buttons

Menu choices are **1–13**. CLI layout IDs are **0–12**:

| Menu | CLI ID / name | Controller port 1 | Controller port 2 | Players |
|---|---|---|---|---|
| 1. Buzz! buzzers only | `0` / `buzzers` | PS3-mapped wired dongle: handsets 1–2 | Unused | 2 |
| 2. Buzz! buzzers + 1 DualShock (4 players) | `1` / `buzzers_ds` | PS3-mapped wired dongle: handsets 1–2 | Split: players 3–4 | 4 |
| 3. 2 DualShocks, 2 players each (4 players) | `2` / `two_ds_shared` | Split: players 1–2 | Split: players 3–4 | 4 |
| 4. 2 DualShocks, 1 player each | `3` / `two_ds_single` | Whole controller: player 1 | Whole controller: player 2 | 2 |
| 5. 1 DualShock, 2 players | `4` / `one_ds_shared` | Split: players 1–2 | Unused | 2 |
| 6. Buzz! buzzers + 2 DualShocks, multitap port 1 (beta) | `5` / `buzzers_ds2_tap1` | Multitap: 1-A buzzers (handsets 1–2), 1-B player 3, 1-C player 4 | Unused | 4 |
| 7. 4 DualShocks, multitap port 1 (beta) | `6` / `four_ds_tap1` | Multitap: 1-A to 1-D = players 1–4 | Unused | 4 |
| 8. Buzz! buzzers + 2 DualShocks, multitap port 2 (beta) | `7` / `buzzers_ds2_tap2` | PS3-mapped wired dongle: handsets 1–2 | Multitap: 2-A player 3, 2-B player 4 | 4 |
| 9. 8 DualShocks, multitaps on both ports (beta) | `8` / `eight_ds_tap12` | Multitap: 1-A to 1-D = players 1–4 | Multitap: 2-A to 2-D = players 5–8 (second virtual buzzer set) | 8 |
| 10. Buzz! buzzers + DualShocks on any slots (4 players) | `9` / `any_buzzers_ds` | Both ports scanned; lowest occupied slot = buzzer handsets 1–2 | Up to 2 whole DualShocks on any other slots = handsets 3–4 | 4 |
| 11. DualShocks on any slots (up to 4 players) | `10` / `any_ds4` | Both ports scanned | Up to 4 whole DualShocks on any slots | 4 |
| 12. 2 pairs of Buzz! buzzers + DualShocks on any slots (8 players) | `11` / `any_2buzzers_ds` | Lowest 2 occupied slots = buzzer pads A/B, handsets 1–2 / 5–6 | Up to 4 whole DualShocks on other slots = handsets 3,4,7,8 | 8 |
| 13. DualShocks on any slots (up to 8 players) | `12` / `any_ds8` | Both ports scanned | Up to 8 whole DualShocks on any slots; two virtual dongles | 8 |

**Multitap setups (beta):** each DualShock on a multitap is one whole player (either half of the controller works, buttons as in the table below). In PCSX2, tick **Settings → Controllers → Global Settings → Multitap on Console Port 1/2** and set the slots (Controller Port 1A–1D / 2A–2D) to DualShock 2. Tested in PCSX2 with Buzz! The Schools Quiz (4 and 8 controllers). Eight players need a game with two-buzzer-set support: the quiz games (Big, Sports, Mega, Hollywood, Pop, Schools Quiz, Brain of…), not Buzz! Junior or The Music Quiz (4 players).

**Any-slot setups on PS3 (IDs 9–12): no CONFIG is needed.** The bridge reads both ports and all available slots. Netemu enables taps by default (CONFIG 1C mask 3); assigning a controller to B/C/D makes that port's tap present. Controllers can stay wherever netemu assigns them. An existing custom CONFIG that disables a port's multitap must be removed or corrected.

**Older fixed-slot setups on PS3 (IDs 5–8): a CONFIG is needed unless controllers happen to land on the required slots.** These setups read only their named slots. Netemu's default order is 1-A, 2-A, 1-B, 2-B, and so on, so it commonly differs from the fixed setup. Each controller can only reassign itself using its own PS button; that does not work on every controller. A CONFIG can enable/force the intended tap, but cannot assign controllers or make a fixed layout read an unrelated slot. Prefer an any-slot setup when controllers cannot be reassigned.

For a fixed setup, put a `.CONFIG` next to the ISO with the same name plus `.CONFIG` (for example `My Game.iso.CONFIG`). This example enables and forces the tap on port 2 (use `01` instead of `02` in both places for port 1, or `03` for both ports):

```text
3D 00 00 00 57 44 00 00 1C 00 00 00 02 00 00 00 1D 00 00 00 02 00 00 00
```

The first PS3 build only read slot A: netemu answers multitap commands in a different form than PCSX2. Since v1.2.0 the module accepts both forms.

**Multitap check (fixed-slot setups 5–8 only):** the patcher asks whether to turn it on for multitap setups (`--diag-tap` on the command line). In the game, hold **Select+Start** on the controller in the multitap's slot A: the last multitap player presses red and joins. Keep holding **Select** and watch that player for 4 seconds: it flashes **blue** = no multitap answer, **orange** = slot change refused, **yellow** = no controller data, **green** = working. Release Select to play normally. Use a copy without the check for normal play. The patcher explicitly refuses `--diag-tap` with IDs 9–12 because its synthetic target assumes fixed slot bindings; answer No to the check prompt for those setups.

**Any-slot assignment:** a valid idle pad counts as present. On each complete scan, new pads are considered in order 1-A, 1-B, 1-C, 1-D, 2-A, 2-B, 2-C, 2-D. There is no reliable Buzz-pad identifier in the normal pad replies: for ID 9 the first pad gets the buzzer role; for ID 11 the first two do. **Connect the buzzer pads before launch and keep them in the lowest occupied slot(s).** Their two mapped handsets become 1–2 and, for the second pair, 5–6. Handsets 3 and 4 of each physical dongle remain unavailable through netemu's pad mapping.

Each discovered pad keeps its handset(s). A missing pad stops sending buttons immediately and reserves its assignment for about 3 seconds; returning inside that time keeps the assignment. Other players never move. After the timeout a waiting extra pad can take the free assignment; reconnect a buzzer before other new pads if its reservation expired. Excess pads are ignored. Game seat order follows who presses blue first, independently of handset numbers. Select, Start, L3 and R3 also buzz red on whole DualShocks in IDs 9–12; the usual two halves supply the five colours.

**Shared DualShock:** the left half belongs to the first player on that port, the right half to the second. Shapes below refer to the buttons printed on the controller:

| Buzz! button | Left half | Right half |
|---|---|---|
| 🔴 Red (buzz) | **L1 or L2** | **R1 or R2** |
| 🔵 Blue | **↑ Up** | **△ Triangle** |
| 🟠 Orange | **← Left** | **□ Square** |
| 🟢 Green | **→ Right** | **○ Circle** |
| 🟡 Yellow | **↓ Down** | **× Cross** |

For `two_ds_single`, each player can use either half of their own controller: both columns trigger the same player. Analog sticks, L3 and R3 are unused.

**Real wired buzzers on PS3:** `ps2_netemu` turns one dongle into one DualShock. Only handsets **1 and 2** reach this bridge through that mapping; handsets 3 and 4 on that dongle remain unavailable. Layout 1 adds players 3 and 4 using a separate controller. For reference, the dongle-to-pad translation is:

| Buzz! button | Handset 1 on port 1 | Handset 2 on port 1 |
|---|---|---|
| Red | □ Square | R1 |
| Blue | L1 | Start |
| Orange | △ Triangle | Select |
| Green | ○ Circle | R2 |
| Yellow | × Cross | L2 |

Layouts 0, 1, 5, 7, 9 and 11 are intended for the PS3 mapping; universal-module PS3 testing is still pending. On PCSX2, use a DualShock layout for ordinary controllers.

## PCSX2

1. Patch a clean ISO and select a controller layout, usually menu choice 3 (`two_ds_shared`).
2. In **Settings → Controllers**, set **both USB ports to Not Connected**. The patched game gets its dongle from the module inside the ISO.
3. Configure **controller port 1** as a DualShock 2. Configure **controller port 2** as a second DualShock 2 for layouts 1–3 (CLI IDs). Bind the buttons in the table above. For one shared controller, only port 1 is needed.
4. **Disable old Buzz! cheats**, including legacy `7C8E9A15.pnach` (Schools Quiz) and `175AAA90.pnach` (Mega Quiz). Remove/disable any other EE patch for this game.
5. Boot the **patched copy** from a fresh start. Do not resume a save state created with the old ISO/module.

For Jungle Party, keep the default `--poll auto`. If an older prototype ISO used polling off, patch a fresh original with this tool.

## PS3 with CFW

This is a test procedure; the universal module has **not yet been verified on ps2_netemu**.

1. Patch the ISO on the PC. Choose the setup matching your buzzers/controllers.
2. Copy the **patched ISO** to **`/dev_hdd0/PS2ISO/`** on the PS3.
3. Mount it with **webMAN** and start the mounted game through your usual PS2 launcher.
4. For IDs 9–12, leave controllers on any assigned slots and keep buzzer pads in the lowest occupied slots; no CONFIG is needed. For older fixed setups, assign controllers to ports and slots as the layout requires. For wired buzzers, the dongle's mapped controller must be port 1; the additional shared DualShock must be port 2 for layout 1. Controller assignment is part of the pending PS3 test.

No PS3 system or emulator files are patched. The virtual USBD does not pass through other USB devices; buzzer lights and EyeToy are not provided.

## PS3 with HEN

HEN is **untested** with the universal module. Patch the ISO **first on the PC**, then convert that patched ISO to a **PS2 Classic** (`.BIN.ENC` / `.pkg`) using your existing conversion workflow. Install/start it using your usual PS2 Classics launcher. Keep the patched ISO and recovery files on the PC; the patcher cannot patch or restore the encrypted Classic directly.

## Report a problem

Include the game title, serial, region/edition, platform (PCSX2 version or PS3 model/CFW/HEN), selected layout, polling mode, full patcher output and `--info` output. Describe what worked: startup, virtual dongle detection, each controller port, each player's five buttons, and releases after a held button.

For PCSX2 **v2.8.2**:

1. Before booting, enable **Debug → Enable IOP Console Logging**.
2. Ensure file logging is enabled, boot the patched ISO and reproduce the problem.
3. In PCSX2's data folder, open **`logs\emulog.txt`**. Include the **`BBUSB:`** startup/status lines and nearby errors. If the file has no BBUSB lines, report that too and confirm you booted the patched copy.

Logging is enabled in the module unless you patched with `--quiet`. Diagnostic flags `--script-demo`, `--no-pad` and `--poll always` are for investigation; report them if used. Do not attach ISOs, game ELFs/IRXs, recovery backups or other game data.

## Development and licensing

[docs/PORTING.md](docs/PORTING.md) explains the module approach and version checks. [universal/TOOLCHAIN.md](universal/TOOLCHAIN.md) documents source/IRX builds, tests and the PyInstaller exe build.

If the exe is open, the build script reports the lock without stopping any process. You can build a separate review copy with `ps2\build_exe.ps1 -DistPath build\console-preview` (the output path is relative to `ps2/`) and test it with `python ps2/tests/smoke_exe.py --exe ps2/build/console-preview/buzz-bridge-ps2.exe` from the repository root.

Our code is MIT ([root license](../LICENSE)). ps2sdk-derived code carries **Academic Free License 2.0** notices; the OPL hook reference carries AFL 3.0 attribution. See [universal/THIRD_PARTY.md](universal/THIRD_PARTY.md) and the included license texts for scope and source references. No Sony modules or game data are distributed.
