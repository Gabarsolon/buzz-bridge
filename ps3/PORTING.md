# Porting Buzz! Bridge 0.8 to another PS3 Buzz! release

Use your own disc or disc dump. Keep EBOOT.BIN, EBOOT.elf, PARAM.SFO, ISOs and keys in a private directory **outside this checkout**. The distributed plugin contains derived addresses/hashes and public source only. Regions, updates, Quiz TV/World variants and Brain of other countries are separate builds until inspected; a similar title/name is insufficient.

The reference is Brain of the UK BCES00347 01.00 with the owner-confirmed 0.7 full match. Runtime 0.8 and the new Quiz TV/World profiles need console tests. See [supported releases](README.md) and [test order](build/CONSOLE_TEST_08.md).

The current installer is **Buzz! Bridge**, `BUZZB0001`, `build/BuzzBridge.pkg`. Delete the previous Buzz! Phones (`BUZZP0001`) app from XMB; the new app has its own USRDIR and no dependency on the old installation. Keep the existing `ps3buzz.sprx`, `ps3buzz_vsh.sprx`, `ps3buzz.cfg` and external paths. App startup/render/choice/exit failures are recorded in `/dev_hdd0/tmp/buzzbridge_app.log`; collect it before relaunch. [App research/diagnosis](build/APP_PATTERNS.md), [helper-first test procedure](build/CONSOLE_TEST_AUTOLOAD.md).

## What the game must provide

The game must use the reviewed CellPort wired Buzz driver contract: compact 32-bit PPU OPDs, the thirteen handled cellUsbd exports and sys_io/cellPadGetData, the four-record extra-LDD table, wired Sony 054c:0002 accepted by probe, two device objects, the recovered setup/callback/report/light behavior and manager presence fields. It must already have initialized the driver when the plugin starts. Virtual devices stay inside the game-process adapter; physical devices use the saved original libusbd calls. No game file or input-policy patch is applied.

Generic mode is enabled by `generic=1` in `/dev_hdd0/plugins/ps3buzz/ps3buzz.cfg` (default). `generic=0` permits exact profiles only. `enabled=0` refuses startup entirely. The XMB app reinstalls the supplied default config each launch, so apply custom settings afterward.

Both exact and generic paths:

1. Read the game ELF at its normal 0x10000 mapping, validate bounded ELF64/PowerPC program headers and the unique process PRX parameter, then obtain its import-table interval. No title-specific import addresses are used for discovery.
2. Match library name + FNID, require each of the thirteen USB imports and pad read exactly once, reject additional USB functions/variable or TLS imports, reconstruct each slot from its unique standard 32-byte import thunk, and require unambiguous Init/RegisterExtraLdd2/interrupt callsites.
3. Find exactly one wired buzzer2 record, validate all four shared-callback LDD/filter records and zero sentinel, link the registration routine’s TOC load to that exact table, and validate callback OPDs/TOCs. The ranges for 054c:1000 and 0002 are checked; the virtual device remains wired 0002. A title with only the wireless path is refused.
4. Recognize exactly one reviewed driver code shape. The contract covers the manager, device code, completion thunks, and Quiz World’s extra TOC bridges. It retains instruction constants/local branches and canonicalizes inter-unit/import addresses and TOC displacements. Manager singleton/ready/count/presence addresses are recovered from the matched getter, rather than guessed from the title.
5. Prefer an exact profile’s immutable CRC ranges and expected slots. Profiles are generated from SHA-256-checked ELF files. A known slot/LDD layout with changed fingerprints is refused, with **no generic fallback**. The reference’s four original pad-route words remain guarded. Full file SHA-256 cannot be recomputed from a live image whose imports/data the loader/game have changed; runtime guards use the derived immutable fingerprints.
6. Require constructed/ready bytes, valid resolved original/replacement OPDs, and compare-and-swap each selected import word. An installation failure rolls back only owned slots; any published wrapper keeps its module resident until game exit. The worker rechecks the ready byte and callback descriptors before adopting the registered LDD. It never repeats native registration for late injection.

Unknown file hashes that pass the complete structural contract are **UNVERIFIED**, not confirmed compatible. Runtime logs `UNVERIFIED title <id>`; the label comes from mounted PARAM.SFO (or UNKNOWN if unavailable), and never authorizes a hook. The tool detects RegisterLdd/UnregisterLdd imports too, but a different RegisterLdd-only driver, table shape, device count, unsupported USB API, altered callback code, ambiguity or unreadable pointer is refused until separately analyzed. Generic does not mean all Buzz titles work.

The VSH helper deliberately autoloads only its explicit title list and exact immutable profiles. Generic ports use a deliberate game-process injection after inspection, at the main menu; add an exact profile/title entry before enabling automatic loading for a new release. Never put the game SPRX in boot_plugins.txt; only ps3buzz_vsh.sprx belongs there. Existing R2/disable/stop and one-attempt-per-PID rules remain in force.

## Extract and inspect your own release

Use the existing Python plus PyCryptodome installation. The tool calls the included APP SELF decoder (`plugin/tools/self10.py`) with the existing RPCS3 key source; it downloads/installs nothing. It handles APP SELF, not arbitrary NPDRM/debug/firmware containers. Unsupported input fails before an ELF is saved. It never runs an extracted executable.

1. Create a private directory such as `D:\BuzzPrivate\MyRelease`. Do not put it under this checkout. Keep your original dump read-only. A mounted disc or folder dump can be supplied instead of an ISO.
2. For an ordinary/decrypted ISO, **omit --dkey**, even if sector 0 still declares encrypted regions:

   ```powershell
   python -B tools/extract_eboot.py "D:\OwnDiscs\Buzz-decrypted.iso" --output "D:\BuzzPrivate\MyRelease"
   ```

   For a Redump-encrypted ISO, provide its matching `.dkey`:

   ```powershell
   python -B tools/extract_eboot.py "D:\OwnDiscs\Buzz.iso" --dkey "D:\OwnDiscs\Buzz.dkey" --output "D:\BuzzPrivate\MyRelease"
   ```

   The file’s 32 hexadecimal characters **are the AES-128 disc key**. No D1 derivation is applied. Sector 0 supplies inclusive plaintext ranges; other sectors use independent AES-CBC with IV = sector number encoded as 16-byte big-endian. Only requested file/directory sectors are read/decrypted; the entire ISO is not copied. The implementation also follows the plaintext-map pattern in [ps3netsrv File.cpp](https://github.com/aldostools/ps3netsrv/blob/ad0ccc31df157d402520bdaf090bfde26c8eac65/src/File.cpp).

   For a mounted/folder disc use:

   ```powershell
   python -B tools/extract_eboot.py "D:\OwnDiscs\BuzzFolder" --output "D:\BuzzPrivate\MyRelease"
   ```

   It finds `PS3_GAME/USRDIR/EBOOT.BIN` and `PS3_GAME/PARAM.SFO`, then calls the SELF decoder and creates EBOOT.elf. Existing output files are never overwritten. ISO9660 uses 2048-byte sectors; malformed extents, interleaving and multi-extent files are refused. Wrong ISO mode/key normally fails the SFO/SELF magic or metadata/HMAC check. Preserve the error; do not rename encrypted bytes to `.elf`.
3. Record TITLE_ID and APP_VER from your own PARAM.SFO with an SFO viewer. The ELF alone does not reliably identify a retail title/version. Hash/inspect the **original**, unpatched EBOOT:

   ```powershell
   python -B tools/inspect_game.py "D:\BuzzPrivate\MyRelease\EBOOT.elf"
   python -B tools/inspect_game.py "D:\BuzzPrivate\MyRelease\EBOOT.elf" --json
   ```

   Output includes full SHA-256, every cellUsbd FNID/name/slot, registration callsites, LDD candidates with filters, decision/reason, and a JSON profile entry containing slots, recovered addresses and immutable fingerprints. Replace YOURTITLE/APP_VER in a generic entry with your SFO values before review. `--no-generic` previews the exact-only policy.
4. Read the decision:

   | Result | Meaning / next action |
   |---|---|
   | EXACT PROFILE | Source file hash and structural checks match a packaged release. Its hardware status still comes from the supported-games table. |
   | GENERIC | Unknown hash, all reviewed structural pieces found uniquely. Console behavior remains unverified; preserve a full first-attempt log. |
   | REFUSE | A piece is absent, ambiguous, unreadable or has an unsupported shape. Do not patch guards or force a different profile. Report the reason and metadata for analysis. |

5. For a deliberate generic trial, follow the recovery/session guidance in the README and inject only `ps3buzz.sprx` into the running game process, once, after the main menu. Use your existing PS3MAPI game-plugin workflow; the helper does not discover new title IDs automatically. Keep real dongles unplugged for the first baseline. Preserve both runtime logs and restart the game before any second attempt. Never load Sony system SPRXs to work around a refusal.

## Read the logs after a failed attempt

Collect `/dev_hdd0/tmp/ps3buzz.log` **and** `/dev_hdd0/tmp/ps3buzz.heartbeat.log`; automatic loading also requires `ps3buzz_vsh.log`. A fresh successful module entry truncates ps3buzz.log; diagnostics truncates its heartbeat file only after hook installation, so an old heartbeat beside a refused load can be stale. Save all files before relaunching/reinstalling. Capture at roughly 5 seconds and 40 seconds, plus the menu/injection times.

| Runtime line | Meaning |
|---|---|
| `ps3buzz 0.8 ENTRY` | Container reached module_start and wrote the first fsynced breadcrumb, before formatting/config/hooks. |
| `bootstrap` | Dedicated 64 KiB initializer thread is running. |
| `config ... generic=...` | Parsed configuration; dongle count, pad split and HTTP port actually selected. |
| `exact profile ... source-sha256=...` | Immutable live fingerprints match metadata generated from that source file hash. |
| `UNVERIFIED title ... structural family=...` | Generic structural match; mounted title text is only a log label. |
| `resolved imports=... ldd=... ready=...` | Discovered import interval and adoption addresses. Compare with inspect output; never substitute reference addresses. |
| `load REFUSED ...` | No new hooks installed: unsupported structure/profile/config/readiness. The reason names the failing stage. |
| `hook plan nid=... slot=... original=... replacement=...` | FNID-to-slot plan; values are descriptor pointers, not raw code addresses. |
| `import plan REFUSED` | Loader imports or descriptors are unresolved/invalid or disagree with discovery. |
| `hook install failed; published=...` | CAS/ownership changed during installation. Rollback was attempted; quit the game before retrying. |
| `phone listener failed step=... errno=...` | 1=socket, 2=bind, 3=listen, 4=nonblocking setup; readiness was not established. |
| `service create failed result=...` | A thread failed to start after publication. Resident wrappers must survive until process exit. |
| `14 hooks installed ...` / `clock=...` | Services created; logs print priorities, sleeps, clock, heartbeat path and stop policy. This alone does not mean two buzzers are configured. |
| `adopt buzzer2 ... ready=1 result=00000000` | Remembers the game’s already-registered wired callbacks; native registration is not repeated. |
| `lookup-call/lookup-return` | Adapter-local seven-byte location lookup for 425a0000/425a0001. Success is 0. No virtual ID may reach real libusbd. |
| `probe-call/probe-return` | Game’s probe callback. Acceptance=0; -1 rejects. |
| `attach-call/attach-return` | Game’s attach callback. Nonnegative result must be followed by its deferred setup requests. |
| `open-pipe ... endpoint=...` | Private EP0 or interrupt-IN pipe allocated. Original physical handles remain separate. |
| `control-submit` / `control` | Submission request fields, then deferred result/count. Expect product -> configuration -> interface -> lights. Public submission status is 0, not a transfer ID. |
| `first-IN-submit` / `first-IN-complete` / `input` | IN armed, then callbacks receiving five bytes with result 0. Periodic input trace is sampled. |
| `game state count=2 presence=00,01` | Game’s own manager now reports both dongles; ff marks absent. Plugin observes only. |
| `pad` | Successful game pad-read tee and connection/button mask changes. |
| `http ... fd=... result=...` | Listener/client/request/WebSocket/join/close events; URL acceptance is not USB readiness. |
| `detach` | Owned virtual devices detached and pending state cancelled. |
| `event log capped` | 32 KiB event cap reached; use the independent heartbeat file. |

Heartbeat fields:

| Field | Interpretation |
|---|---|
| `t` | Seconds since diagnostic service started; normally a new line every two seconds, up to its 32 KiB cap. |
| `worker loops / last` | USB loop progress and last phase, e.g. adopt-check, usb-poll, lookup-call, probe-call, attach-call, pad-snapshot, rumble-call or sleep. A fixed foreign-call phase with fresh heartbeat localizes a stalled call. |
| `http loops / last` | Independent network progress; it can continue when USB is blocked. |
| `lock-waits` | Cooperative lock contention counter; rising waits without work help identify lock trouble. |
| `dropped` | Trace events lost to contention/full queues. It is not a lost-input count. |
| `stop` | 1 means the safety sentinel latched. Removing the file does not resume this process. |
| `CPU GUARD mask` | 1=USB, 2=HTTP, 4=diagnostics exceeded 1000 loops/s; repeated 100 ms throttling, one report per thread. |
| `SAFETY STOP` | `/dev_hdd0/tmp/ps3buzz.stop` observed by the 1 Hz stop poll; work quiesces at a safe boundary. Quit/restart before retry. |

Typical failures already recorded in the handoff:

* **ENOEXEC / 80010007:** loader/container/signing/relocation trouble, commonly before ENTRY. Changing a VID/PID/profile cannot fix that. Compare the installed bytes with the final package manifest; preserve the exact loader result. The included offline audits check APP authority, HMACs, module metadata and independent RX/RW relocation bases. HEN remains excluded from this signer path.
* **801100ff:** CELL_USBD_ERROR_FATAL. The earlier 0.6.1 trial passed adapter-only device IDs to real GetDeviceLocation; both devices stopped before probe. 0.7/0.8 perform that lookup locally and never reclassify FATAL as success. A recurrence requires the call/return log and original installed hash; do not ignore the code.
* **No advancing heartbeat:** if ENTRY exists but bootstrap/services did not finish, inspect their last line first. If hooks/services were created, thread creation, priority or a spin can be involved; this project previously needed lower-priority paced services. USB=1000/2 ms, HTTP=1010/10 ms, diagnostics=990/2 s remain selected. A fresh heartbeat with a frozen worker instead points to that worker’s last phase. PC tests do not prove PS3 scheduling or foreign-call termination.
* **Ready false with HTTP working:** inspect both dongles’ attach/control/first-IN sequence and manager presence. Health requires every configured dongle. The HTTP listener can answer before USB is ready.

## Add an exact profile and rebuild

For a generic result already passing the reviewed driver contract:

```powershell
python -B tools/make_profiles.py --add-exact "BCES12345=D:\BuzzPrivate\MyRelease\EBOOT.elf" --app-ver 01.00
```

Use your actual TITLE_ID, not the placeholder. This reads the ELF, computes its full SHA-256 and all immutable ranges/slots, appends derived metadata to `plugin/game/profiles.json`, and regenerates `profile.h`/`driver_shapes.h`. It refuses an existing title/version entry and does not save game bytes. The inspector’s complete JSON `profile_entry` is also ready to paste into the catalog’s profiles array after replacing its label/version, then regenerate headers with `python -B tools/make_profiles.py`.

Add that title/version to the explicit list at the top of `plugin/autoload/logic.c` and add a process-match assertion. The helper obtains fingerprints, LDD, ready byte and OPDs from the generated profile header. A different APP_VER or driver shape needs a fresh review. If generic refused, do **not** manufacture a matching CRC: first reconstruct the USB path with Ghidra, establish the callbacks/filter/TOCs/protocol/presence contract and add a reviewed structural family plus negative tests. Every generic family is intentionally bounded; this tool cannot infer an arbitrary new USB driver from its strings.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build_ps3.ps1
python -B plugin/tests/run_all.py
```

The installed PS3DK remains read-only; builds use only this work folder. A rebuild requires no game inputs once derived metadata is checked in. `tools/test_porting.py` optionally reads the owner’s private inputs, compares the Python and production C resolvers, exercises relocation/refusals and plain/encrypted ISO extraction entirely in RAM, and skips absent inputs cleanly. No tests embed retail bytes. The final PKG contains only the app, default config and two plugins; `build/release08_manifest.json` records sizes/SHA-256.

## Bug report

Include TITLE_ID, APP_VER, disc region/edition, original ELF SHA-256, full inspect output/decision, plugin/helper/PKG hashes, CFW/Cobra version, configuration, injection/menu times, exact loader result, and **both runtime logs**. Include the VSH log for automatic loading. State whether both dongles attached, health reported ready, each button released, lights worked, and a full match completed. State whether real dongles/other input plugins were connected. Describe the recovery/stop action taken and use a fresh game process for another attempt.

Do not attach game executables, ISOs, assets, save data containing private information, disc keys or complete decompilations. Derived facts and short relevant instruction excerpts are enough to begin analysis.
