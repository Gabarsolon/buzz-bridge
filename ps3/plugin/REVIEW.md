# Historical v4 console-test handoff

Superseded by the runtime 0.5 repair and current [runtime diagnosis](../build/RUNTIME_DIAGNOSIS.md), [console procedure](../build/CONSOLE_TEST.md), and [test results](TEST_RESULTS.md). The user subsequently confirmed v4's native load and adoption, followed by a stall. The text below records the earlier candidate review.

A PPU game-process module now exists at `../build/ps3buzz.sprx`, with fake-signed fallback, PRX and config. PS3DK v0.26.0 was used read only. The build, SDK export sample, independent relocation audit and PC suite pass. The console was never contacted; **loadability on this PS3 and working virtual attachment are still unverified**.

Read [README.md](README.md) for the exact webMAN 1.47.48a load URL, PID discovery, phone URL and step-by-step test; [TOOLCHAIN.md](TOOLCHAIN.md) for exact build commands/runtime fixes; [GAME_ANALYSIS.md](GAME_ANALYSIS.md) for static evidence. Final hashes/sizes are in `../build/manifest.json`.

The USB test requires original BCES00347 01.00. Claude must clear its temporary pad-EBOOT remap and relaunch first. The supplied four pad-route changes are intentionally included in the refusal fingerprint. The separately observed match freeze was not investigated, and no cause is asserted.

The actual existing wired record is buzzer2 **0x760174** (correcting the older candidate's use of buzzer1 at 0x76015c, whose range happened to include PID 2). It adopts after manager-ready 0x78ab14 and invokes the game's own probe/attach without native re-registration. Probe accepts 054c:0002 with return 0; attach increments the manager count. Update 0xbd638 sets the presence flags consumed by the game's buzzer checks after setup. No game code or buzzer-check bypass is written. Therefore there is no statically identified need for an earlier injection point; the runtime behavior still needs the requested test.

Runtime guards cover 33 immutable ranges, every expected import-slot address and atomic original-pointer checks. The build generator verifies the complete input ELF SHA-256; the running module uses CRC fingerprints of the selected immutable ranges, not a full live-image cryptographic hash. OPDs/code pages are checked with the process page-attribute query. Hooks touch only fourteen game import data slots and need data-memory barriers, not instruction-cache patching.

The module avoids PS3DK's uninitialized process CRT. Config/log use LV2 filesystem calls; formatting/HTTP/config number parsing need no allocator; sockets use raw PRX imports. Final binary auditing finds no local newlib allocator or syscall table. A tiny compatibility heap-ready definition is never set and is discarded when unused. All game callbacks cross compact OPDs, preserving each callee's TOC.

The log `/dev_hdd0/tmp/ps3buzz.log` is truncated at load and capped at 128 KiB. It includes hook plans, adoption, native reserved-ID rejection, probe/attach, control and sampled IN completion, pad mask changes, and game count/presence changes. Callback trace collection does not perform HDD I/O. Fetch the log before another load truncates it.

First-test questions the log should resolve:

1. Does the encrypted container start, and does the log show all fourteen hook plans/install success? If no startup log exists, investigate loader/container/HDD access; use the fake fallback only after a fresh game launch.
2. Does the native lookup reject each reserved ID with `8011000a`? The guard intentionally refuses other statuses. Do not weaken it without the trace and native API evidence.
3. Do probe/attach return 0 and control/IN callbacks complete? The adapter worker is not Sony's USB callback thread; game locks and timing remain untested.
4. Does game count become 2 and presence change from ff,ff to 00,01 with no real dongle? This is the evidence needed for the separate retail buzzer gate. A module entry/web page alone proves neither attachment nor gameplay.
5. Do pad mask traces and phone presses change reports, and do game lights reach the page? The pad tee preserves the original return; actual game pad polling and LAN initialization remain untested.

Default config enables two virtual dongles, normal pad ports 1/2 targeting virtual handsets 1/2. Phones take free handsets. Fixed targets and split halves are documented. One real + one virtual requires dongles=1 and the real dongle attached before injection. Real calls forward to native libusbd, but ordering/replug/coexistence require later tests; a game already filled by two dongles rejects a third.

Supported unload is **quit the game**. HTTP process-unload syntax is documented for reference, but active live-unload is unsafe because an in-flight wrapper or callback can still use this code. Stop refuses; do not rely on every loader honoring that refusal. This architecture installs no boot-time payload, dev_flash change or permanent game patch.

All writes during this run stayed inside the requested ps3buzz work folder. SDK/game/reference input files were read only. No emulator, screen/input automation, console request, executable download, installation or state-changing git command was used. Temporary full decompilation for this run is removed at handoff; only derived facts and short excerpts remain.
