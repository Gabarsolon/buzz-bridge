# PC test results

Executed 2026-10-26T17:57:12.604545+00:00. Working directory: workspace root.

These checks compile/run Windows code only. No PS3, emulator, compiler installation or executable download was used. No pip packages were installed.

## Compiler

```text
clang version 23.1.3 (https://github.com/llvm/llvm-project 0d261d1ca552c95a8f007e061c787ac7132fbcbc)
Target: x86_64-pc-windows-msvc
Thread model: posix
InstalledDir: C:\Program Files\LLVM\bin
Build config: +alloc:rpmalloc
```

Existing MSVC/Windows SDK headers and libraries were discovered automatically by Clang. MSVC cl.exe was found but not used.

## Commands and captured results

```text
python -B tools/test_porting.py
exit code: 0
BCES00303 PASS: Python + actual C resolver; generic relocation; refusals; SELF + plain/encrypted RAM ISO
BCES00347 PASS: Python + actual C resolver; generic relocation; refusals; SELF + plain/encrypted RAM ISO
BCES00644 PASS: Python + actual C resolver; generic relocation; refusals; SELF + plain/encrypted RAM ISO
test_bad_elf (__main__.ToolTests.test_bad_elf) ... ok
test_iso_modes_and_bad_keys (__main__.ToolTests.test_iso_modes_and_bad_keys) ... ok
test_own_BCES00303 (__main__.ToolTests.test_own_BCES00303) ... ok
test_own_BCES00347 (__main__.ToolTests.test_own_BCES00347) ... ok
test_own_BCES00644 (__main__.ToolTests.test_own_BCES00644) ... ok

----------------------------------------------------------------------
Ran 5 tests in 24.855s

OK
```

```text
python -B plugin/tests/run_autoload.py
exit code: 0
PASS: 63 autoload assertions (matching, continuous settle, no retry, boot bytes/backups/idempotency/errors)
ps3buzz_vsh 0.3 startup; runtime=0.9; priority=1100 sleep=1s; R2 skip window=10s
VSH APIs find=1 interface=1 mode=1 pid=1; notification disabled
injection disabled=0
candidate pid=01050200 name=01050200_main_EBOOT.BIN title=BCES00303 profile=01.00
pid=01050200 libusbd + exact profile + LDD ready; settle=15s (guard band, not menu proof)
waiting pid=01050200 ready=1 stable=0s/15; runtime will independently gate presentation
pid=01050200 readiness lost; settle reset
pid=01050200 libusbd + exact profile + LDD ready; settle=15s (guard band, not menu proof)
waiting pid=01050200 ready=1 stable=0s/15; runtime will independently gate presentation
waiting pid=01050200 ready=1 stable=5s/15; runtime will independently gate presentation
waiting pid=01050200 ready=1 stable=10s/15; runtime will independently gate presentation
waiting pid=01050200 ready=1 stable=15s/15; runtime will independently gate presentation
ATTEMPT pid=01050200 path=/dev_hdd0/plugins/ps3buzz/ps3buzz.sprx (never retry this pid)
LOAD result=00000000 pid=01050200
post-load passive entered; no further process/VSH/network/UI calls; loader acceptance only
post-load passive pid=01050200; log only; reboot/helper restart for another game
post-load passive pid=01050200; log only; reboot/helper restart for another game
post-load passive pid=01050200; log only; reboot/helper restart for another game
helper thread stopped
PASS: actual VSH worker: readiness reset/15s/progress; one attempt or resident reuse; 16s passive after success/failure/residency with ZERO process/VSH/pad/network/sentinel/clock API calls
ps3buzz_vsh 0.3 startup; runtime=0.9; priority=1100 sleep=1s; R2 skip window=10s
VSH APIs find=1 interface=1 mode=1 pid=1; notification disabled
injection disabled=0
candidate pid=01050200 name=01050200_main_EBOOT.BIN title=BCES00303 profile=01.00
pid=01050200 libusbd + exact profile + LDD ready; settle=15s (guard band, not menu proof)
waiting pid=01050200 ready=1 stable=0s/15; runtime will independently gate presentation
pid=01050200 readiness lost; settle reset
pid=01050200 libusbd + exact profile + LDD ready; settle=15s (guard band, not menu proof)
waiting pid=01050200 ready=1 stable=0s/15; runtime will independently gate presentation
waiting pid=01050200 ready=1 stable=5s/15; runtime will independently gate presentation
waiting pid=01050200 ready=1 stable=10s/15; runtime will independently gate presentation
waiting pid=01050200 ready=1 stable=15s/15; runtime will independently gate presentation
ATTEMPT pid=01050200 path=/dev_hdd0/plugins/ps3buzz/ps3buzz.sprx (never retry this pid)
LOAD result=00000001 pid=01050200
load failed; passive entered; no further process/VSH/network/UI calls; preserve logs and reboot
post-load passive pid=01050200; log only; reboot/helper restart for another game
post-load passive pid=01050200; log only; reboot/helper restart for another game
post-load passive pid=01050200; log only; reboot/helper restart for another game
helper thread stopped
PASS: actual VSH worker: readiness reset/15s/progress; one attempt or resident reuse; 16s passive after success/failure/residency with ZERO process/VSH/pad/network/sentinel/clock API calls
ps3buzz_vsh 0.3 startup; runtime=0.9; priority=1100 sleep=1s; R2 skip window=10s
VSH APIs find=1 interface=1 mode=1 pid=1; notification disabled
injection disabled=0
candidate pid=01050200 name=01050200_main_EBOOT.BIN title=BCES00303 profile=01.00
pid=01050200 already loaded; no attempt; post-load passive entered
post-load passive pid=01050200; log only; reboot/helper restart for another game
post-load passive pid=01050200; log only; reboot/helper restart for another game
post-load passive pid=01050200; log only; reboot/helper restart for another game
helper thread stopped
PASS: actual VSH worker: readiness reset/15s/progress; one attempt or resident reuse; 16s passive after success/failure/residency with ZERO process/VSH/pad/network/sentinel/clock API calls

```

```text
python -B plugin/tests/run_app_ui.py
exit code: 0
PASS yes frames=368 callbacks=369 fsynced entries=14 time=517700 us
PASS no frames=368 callbacks=369 fsynced entries=14 time=517700 us
PASS cancel frames=368 callbacks=369 fsynced entries=14 time=517700 us
PASS quit frames=6 callbacks=9 fsynced entries=14 time=11000 us
PASS system_menu frames=368 callbacks=369 fsynced entries=14 time=517700 us
PASS timeout frames=85718 callbacks=85719 fsynced entries=15 time=120007800 us
PASS status_timeout frames=14289 callbacks=14290 fsynced entries=15 time=20007200 us
PASS dialog_fail frames=7145 callbacks=7146 fsynced entries=12 time=10005300 us
PASS flip_timeout frames=2 callbacks=1825 fsynced entries=13 time=2006300 us
PASS late_label_timeout frames=2 callbacks=1825 fsynced entries=13 time=2006400 us
PASS thread_fail frames=0 callbacks=0 fsynced entries=0 time=500 us
PASS log_open_fail frames=0 callbacks=0 fsynced entries=2 time=500 us
PASS log_sync_fail frames=7144 callbacks=7144 fsynced entries=10 time=10003700 us
PASS host_alloc_fail frames=0 callbacks=0 fsynced entries=5 time=1000 us
PASS callback_register_fail frames=0 callbacks=0 fsynced entries=4 time=800 us
PASS callback_fail frames=0 callbacks=1 fsynced entries=9 time=1900 us
PASS gcm_fail frames=0 callbacks=0 fsynced entries=6 time=1200 us
PASS rsx_alloc_fail frames=0 callbacks=0 fsynced entries=9 time=1800 us
PASS configure_fail frames=0 callbacks=0 fsynced entries=8 time=1600 us
PASS video_state_timeout frames=0 callbacks=1820 fsynced entries=9 time=2002800 us
PASS font_fail frames=0 callbacks=0 fsynced entries=9 time=1800 us
PASS font_draw_fail frames=0 callbacks=1 fsynced entries=9 time=2000 us
PASS pad_fail frames=7143 callbacks=7143 fsynced entries=9 time=10002200 us
PASS flip_submit_fail frames=1 callbacks=1 fsynced entries=9 time=2000 us
PASS label_timeout frames=0 callbacks=1820 fsynced entries=7 time=2002400 us
PASS watchdog
PASS total_watchdog

```

```text
python -B plugin/tools/audit_loops12.py
exit code: 0
Loop audit PASS: 152 reviewed loops; three paced services, sleeping lock; eight unsafe mutations rejected.

```

```text
powershell -NoProfile -ExecutionPolicy Bypass -File plugin/build_pc.ps1
exit code: 0
report: all 1,048,576 states agree with RPCS3 expression
hub: mixed ownership, exhaustion, disconnect, stable order, split pad, player 8, lights pass
config/JSON: strict parsing, transactional failures and protocol compatibility pass
WebSocket: RFC6455 accept vector, split input, lengths, mask/control bounds pass
USB model: descriptors, async completion, 6ms/100us deadlines, LEDs, limits, reset pass
mailbox/engine: byte ABI, validation, unsupported backend, LEDs and watchdog pass
PASS: 1049006 checks
Console 0.6.1 replay PASS: adopt -> local lookup -> probe/attach x2; no real virtual-ID export; deferred setup/IN; pad 32768/4096/release; detach clears readiness.
Game sequence/import adapter: 3428 checks passed; 2 dongles, 40 button bits, callback rearm, real pass-through, cancellation, late adoption, rollback.
PRX formatter: 903 truncation/numeric cases, signed limits, invalid format passed

```

```text
python plugin/tests/test_transport.py
exit code: 0
PASS: C HTTP/WS loopback integration: idle/partial clients, lock/observer contract, copied assets, split/coalesced input, 8 phones, full reports, ownership, disconnect, origin, close handling

```

```text
python plugin/tests/test_worker_liveness.py
exit code: 0
PASS: real native service loops: USB blocked at lookup-call; HTTP 200 + independent 2 s heartbeat
PASS: real native service loops: USB blocked at probe-call; HTTP 200 + independent 2 s heartbeat
PASS: real native service loops: USB blocked at attach-call; HTTP 200 + independent 2 s heartbeat
PASS: first attach blocks >3s; once-only independent timeout; HTTP 200; late return quarantined; no second dongle
PASS: heartbeat while adapter mutex, trace lock and general-log writer lock ALL held
PASS: 1 Hz safety stop, frozen work counters, hook bypass and persistent latch
PASS: preexisting stop sentinel prevents all USB/HTTP work from first iteration
PASS: console replay through actual native hooks + services: two attached, callback IN, HTTP usb_backend_ready=true

```

```text
python -B plugin/tests/replay_autoload09.py
exit code: 0
PC CPU guard: >1000/s throttles repeatedly, once-only report bit, window reset passed
PC presentation: no polls/bursts/stalls refuse; sustained frames+minimum age; failed mutex resets; late return/clock rollover quarantines
historical replay: attach-timeout once at observation t=6; no second dongle
PASS: supplied stuck-worker/advancing-HTTP observations replayed through actual watchdog; unknown polling signal earns no presentation

```

```text
powershell -NoProfile -ExecutionPolicy Bypass -File plugin/test_sanitize.ps1
exit code: 0
PASS: 200,000 deterministic mutated/random parser inputs
Console 0.6.1 replay PASS: adopt -> local lookup -> probe/attach x2; no real virtual-ID export; deferred setup/IN; pad 32768/4096/release; detach clears readiness.
Game sequence/import adapter: 3428 checks passed; 2 dongles, 40 button bits, callback rearm, real pass-through, cancellation, late adoption, rollback.

```

```text
node --check plugin/web/app.js
exit code: 0


```

Phone JS/CSS hashes still match frozen 0.7 (HTML title/heading intentionally renamed): **True**.

The unit test has 1,049,006 assertions, including all 1,048,576 four-handset button states against a separate transcription of RPCS3's bit expression. Fixed pad targets and phone ownership conflicts are included. USB tests are a model of asynchronous completion, not native syscalls.

The allocation-free PRX formatter passes 903 numeric/truncation cases plus signed-limit/invalid-format checks against libc. The loopback phone server uses this formatter too. Native build results are recorded separately in ../build/prx_audit.json and TOOLCHAIN.md.

The additional game-sequence fixture uses the reconstructed BCES00347 1.00 wired startup: four extra-LDD registrations, probe/location/static descriptors, default/interrupt pipes, product string read, SET_CONFIGURATION, SET_INTERFACE, seven-byte lights and continuously rearmed interrupt IN. It tests both dongles and all forty button bits, real-call forwarding, pending cancellation, late adoption, import-slot rollback and hook ownership. It runs again under ASan/UBSan. No copyrighted game code is linked or executed.

game/native.c also passes a strict Windows Clang syntax check with BUZZ_HOST_CHECK and declaration-only PS3 stubs. This does not check PPU code generation, linking, compact OPDs or SDK layouts.

The transport test launches its own C server bound only to 127.0.0.1 on an ephemeral port. It checks copied assets, fixed routes, split/coalesced TCP input, independent handshake accept values, eight phone claims, full reports for two dongles, ownership rejection, player-eight disconnect cleanup, ping/pong, invalid-frame close and cross-origin refusal. It terminates only its own test child.

The worker-liveness harness includes the actual native service loops with PC mocks. Separate runs deliberately block USB lookup, probe or attach; HTTP still returns 200 and the independent two-second heartbeat reports that phase. Holding all three shared locks cannot stop that heartbeat. Stop tests cover the 1 Hz check, frozen work counters, hook bypass, persistent latch and a preexisting sentinel. The CPU guard is forced above 1000 calls in a clock window and checked for repeated 100 ms throttling. Pad held/no-change, stale-read release and NO_DEVICE also pass. This verifies host behavior, not PS3 priorities, syscalls or game code.

The source audit inventories 152 loops in nine native translation units. Three indefinite service loops and the lock wait must sleep first; every finite loop has a reviewed bound/progress contract and immutable token hash. Eight deliberate unsafe source mutations are rejected. This is a source contract, not a termination proof for external firmware/game calls. The native object audit separately verifies the correct clock import because host mocks bypass that branch.

The sanitizer test compiles with `-fsanitize=address,undefined -fno-sanitize-recover=all` and exercises 200,000 deterministic random/mutated JSON, config, HTTP, WebSocket and mailbox inputs. No sanitizer finding occurred in the successful run. This is bounded stress testing, not a proof of protocol compliance or memory safety.

JS validation is syntax-only. Actual mobile rendering, console endianness, game-process SPRX imports/OPDs, loader behavior, threading, USB scheduling, real-dongle coexistence, LEDs/rumble and game compatibility remain untested. The old VSH/kernel model remains regression-tested but is no longer the selected architecture.

Runtime 0.9 adds actual C readiness-policy checks (minimum age, sustained polling, bursts, stalls and mutex failure), three-second attach timeout/late-return quarantine with HTTP remaining available, and replay of the supplied 0.8 stuck-worker sequence. The real VSH worker is mocked for success/failure/already-resident outcomes and checked for zero process/VSH/network/pad/sentinel calls after parking. See ../build/ATTACH_SAFETY_09.md for source URLs, timing policy and limits.

Raw command outputs and compiler identity are also saved in `build/test_runs.json`. Re-run with `python plugin/tests/run_all.py`.
