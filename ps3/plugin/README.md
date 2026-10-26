# PS3 virtual Buzz! — current build entry

The app is Buzz! Bridge (`BUZZB0001`, `BuzzBridge.pkg`), with fsynced `/dev_hdd0/tmp/buzzbridge_app.log`. **CFW/Cobra and PS3HEN (HEN untested); signing rev 0A.** Runtime 0.9 / helper 0.3 retain the released PRX payloads byte-for-byte, with a 15-second helper settle and 10-second runtime minimum. Wait at the game's menu for about **30 seconds**. The user reports previous rev-1 CFW success with Quiz TV autoload and manual Brain of the UK/Quiz World loads; the new signing needs CFW retesting before HEN testing. Reboot before each title; the helper parks after its first attempt. See [the user guide](../README.md) and [signing research, proof and handoff](../build/HEN_SIGNING.md). The old package is `build/BuzzBridge_rev1.pkg`.

Build with `powershell -NoProfile -ExecutionPolicy Bypass -File build_ps3.ps1` from the folder root. The build no longer needs scratchpad files or a game ELF. `python -B plugin/tests/run_all.py` runs the PC suite. The older runtime notes below are historical; use the root guide for current installation and controller mapping.

# Historical runtime 0.5 notes: revision 11

The user confirmed diag E and the previous main load on Evilnat 4.92/Cobra, but v4 stopped logging after adoption and did not answer HTTP. This revision separates USB, HTTP and diagnostics, raises their priorities, and adds bounded heartbeats and presentation call/return traces. The accepted container format is retained; the exact console-tested main is backed up as `build/ps3buzz_v4.sprx`.

Follow [CONSOLE_TEST.md](../build/CONSOLE_TEST.md) for one fresh original-game launch and one main injection. F-H are not needed. [RUNTIME_DIAGNOSIS.md](../build/RUNTIME_DIAGNOSIS.md) gives the exact old/new code paths, static game priority evidence, tests and limits. The original stall's exact instruction and this revision's console behavior remain unverified.

Supported profile remains **Buzz!: Brain of the UK, BCES00347 01.00**, original decrypted ELF SHA-256 `6f4d1f05cde3d8a6e7836b65c7103cff0ccc6a248ca0b877a7da3e3453585a19`. Keep the existing original-byte guards and use the original game. The module belongs in the running game process. Quitting the game is the main module's undo.

## Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File plugin/build_ps3.ps1
python -B plugin/tests/run_all.py
python -B research/verify_game.py
```

The SDK stays read only. [TOOLCHAIN.md](TOOLCHAIN.md) describes the workspace linker/packer/signing changes. No packages were installed. The build now runs the native/SELF audits itself. [ARTIFACTS.md](../build/ARTIFACTS.md) records exact current sizes and hashes.

The current console candidate is `build/ps3buzz.sprx`; upload it with the config. Diagnostic files and alternate containers are offline build comparisons. E already succeeded on hardware. Do not load the system-authority comparison or Sony control.

## Config, pads and phones

The config is read once from `/dev_hdd0/plugins/ps3buzz/ps3buzz.cfg`; a missing/invalid config refuses startup. Shipped values: `enabled=1`, `dongles=2`, `players_per_pad=1`, `port=8420`, `stale_ms=45000`, `pad.1.right=1`, `pad.2.right=2`. Compiled defaults remain disabled. For an optional hooks/listener-only trial set `enabled=0` before a fresh injection.

Virtual dongle 1 carries handsets 1?4; dongle 2 carries 5?8. `pad.N.right` and `pad.N.left` address one-based pad ports 1?7 and each split half: `0` = first free handset, `1`?`8` = fixed target, `off` = ignore. Targets must fit the configured dongle count. A fixed target occupied by a phone stays with the phone; the pad retries after it becomes free. Connect configured pads before joining phones to give them their targets. Unspecified pad halves use the first free slot.

| Buzz button | Right half / normal pad | Left half with players_per_pad=2 |
|---|---|---|
| Red | R1 or R2 | L1 or L2 |
| Blue | Triangle | D-pad Up |
| Orange | Square | D-pad Left |
| Green | Circle | D-pad Right |
| Yellow | Cross | D-pad Down |

Button keys `map.right.red=r1|r2` and equivalents are customizable. To use two split pads for four handsets, set `players_per_pad=2`, `pad.1.right=1`, `pad.1.left=2`, `pad.2.right=3`, `pad.2.left=4`.

Phones choose free slots through the web page. Game lights update the page's red button; rising lights also trigger a short pad rumble cue. Closing/blur releases buttons; a dead connection expires within the configured stale timeout. The native pad hook tees the game's GetData return unchanged. No competing pad initialization/poll is used.

For **one real + one virtual dongle**, quit, change `dongles=1`, plug the real dongle in before starting the original game and inject after its native attachment. This gives four virtual plus four real handsets. Real calls are forwarded unchanged. The game has only two dongle slots: two virtual dongles can leave no room for a later physical hotplug. Rejected virtual probes are not retried in this build. Physical enumeration order, replug and coexistence still require console tests.

## Log and late-load mechanism

The log is truncated at entry and capped at **32 KiB**, with 8 KiB reserved for initial heartbeats. Entry retains the small no-CRT frame and raw FS marker. Bootstrap creates USB/HTTP/diagnostic threads with priorities **100/95/90** and 64 KiB stacks. USB and HTTP each sleep 1 ms; diagnostics drains events every 100 ms and logs loop counts/current phases every ~2 s through 30 s. Callback and HTTP events are queued without HDD I/O, so a blocked USB callback can be identified while HTTP and logging continue. The native pad hook uses the game's GetData reads, releases on NO_DEVICE or 250 ms without a successful read, and leaves native return values unchanged. Rumble/socket calls execute outside the shared hub lock.

The exact existing record is `buzzer2` at **0x760174**, callbacks probe/attach/detach OPDs **0x700820/0x700870/0x700818**. After manager-ready **0x78ab14 == 1**, the adapter adopts the record without registering/unregistering real USB drivers. After 200 ms it runs the game's own callbacks with plugin-private IDs. Product string -> SET_CONFIGURATION -> SET_INTERFACE -> lights + rearmed IN then runs through the hooks. The game's attach callback increments manager count at **0x78ab28**. Update function 0xbd638 writes the normal presence bytes at **0x78aae9/0x78ab0d** after device setup; the presence queries read those bytes (`ff` = absent). No buzzer-check bypass is patched.

Before each hook plan, 33 immutable ranges (including every trampoline, import headers, callback OPDs/code, LDD table and untouched pad-route words) must match the hash-verified ELF's fingerprints. Each NID must resolve to its exact expected slot, original/replacement OPDs must point to mapped code, and atomic compare/replace checks the original pointer again before a write. A mismatch refuses installation. CRC fingerprints identify these ranges; they are not a full cryptographic hash of the live process. The build generator verifies the full input ELF SHA-256.

The native reserved-ID lookup must return DEVICE_NOT_FOUND (`8011000a`); other results refuse attachment and are logged. Actual native error semantics are unverified. Callback thread scheduling, game mutex interactions, loader/OPD behavior, network readiness, pad availability and complete gameplay remain hardware questions. A crash/hang can lose game progress. This binary installs no boot-time or kernel hook.

## Undo / unload

**Supported unload is quitting the game to XMB.** The process, hooks, callbacks and phone listener disappear together. HDD files remain inert and can be deleted by FTP later. No EBOOT/dev_flash/boot-list restoration is needed for this module. A fresh launch needs a fresh PID and manual injection.

For reference, webMAN's HTTP process-unload syntax is:

```text
http://<PS3-IP>/gameplugin.ps3mapi?proc=GAME_PID&unload_slot=MODULE_ID
```

`MODULE_ID` is the actual module ID, not a display-row number. **Do not invoke live unload on the real candidate.** Cobra joins its stop thread but ignores its exit code, so the module cannot veto unmapping while wrappers or USB callbacks may still execute. The TCP `MODULE UNLOAD` branch also reuses its command variable while parsing the PID. Quitting is the supported complete undo. The four file-only probes have empty stop shims and no published callbacks, so their explicit unload is a separate supported diagnostic step.

[DESIGN.md](../DESIGN.md), [GAME_ANALYSIS.md](GAME_ANALYSIS.md), [REVIEW.md](REVIEW.md), [TEST_RESULTS.md](TEST_RESULTS.md) contain the design, static evidence and handoff. Keep `../game`, private analysis scratch and unrelated reference builds out of any distribution.
