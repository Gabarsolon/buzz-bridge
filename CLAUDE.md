# CLAUDE.md: Buzz! Bridge

Public repo `Gabarsolon/buzz-bridge` (MIT). Lets PS2 Buzz! games take real buzzers, phones or DualShocks on a PS3 CFW/HEN console (and PCSX2). Latest release: **v1.3.0** (tag at `9d757e5`). Owner: Gabarsolon, own console (PS3 slim CECH-25xx, Evilnat 4.93 + Cobra, webMAN MOD, `192.168.0.169`).

## Layout

- `ps2/`: PC ISO patcher (`buzz_bridge_ps2.py`, menu entries 1-7 incl. 6 = buzzers + 2 DualShocks) and the PS2-side payloads; `buzz-bridge-ps2.exe` is built from it.
- `ps3/`: PS3 app `BuzzBridge.pkg` (PS3DK C, CMake+Ninja): `README.md`, `DESIGN.md`, `PORTING.md`, `build_ps3.ps1`; `plugin/`, `payloads/`, `assets/`.
- `netemu/`: install/restore scripts (`.ps1` + `.sh`) for the modified `ps2_netemu.self` and README.
- `pcsx2/`: PCSX2 side.
- `tests/`: `run_all.py` (14 groups: C unit tests for the app flow/browser/installer/menu, Python tests for features/UI layout/hashes/simple netemu).
- `tools/`: helpers.

## Build and test

- Tests: `python -B tests/run_all.py` (show failures + summary only).
- PS3 app (both `BuzzBridge.pkg` full and `BuzzBridge-public.pkg`): `powershell -File ps3/build_ps3.ps1`. SDK: PS3DK v0.26.0 at `E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64`.
- PC patcher: `python3 ps2/buzz_bridge_ps2.py`.
- Parse-check any script you write (`[System.Management.Automation.Language.Parser]::ParseFile`, `bash -n`).

## The modified netemu (the heavy part, lives OUTSIDE this repo)

- Final build tree: `C:\Users\remus\ps3buzz-console\raw_capture_debug11` (build.py, src, tests, REPORT.md). Variant shipped = "S4L3": SELF sha256 `74e890bf7f77edfd92d4c0a57f45e73cf7fe39aa3c21cd7587948fb3c81ade40`, built from Evilnat 4.93 stock `bad3e45c9a81f25726e414b146ea5a112f28216ab4bdd2fd9f5c81b33d04c80a`. Console-proven: four buzzers (dongle 054C:1000) and lamps.
- Full internals, addresses, memory rules, pipeline, failed ideas: `C:\Users\remus\ps2-netemu-disc\docs\NETEMU_KNOWLEDGE.md`.
- App workspace and decisions: `C:\Users\remus\ps3buzz-console\buzzbridge_app\REPORT.md`; release staging `ps3buzz-console\release_v1.3.0`; scripts `release_pc\`, `netemu_simple\`.
- The app embeds the modified and the original netemu; install = backup, hash-verified write, rollback on failure. No console restart needed after replacing the netemu.

## Hard rules

- **Never run install scripts or send files to the PS3 yourself.** Write, parse-check, give the exact command. PS3 reads (FTP/webMAN GET) only when the user asks, read-only.
- Never kill/stop agy, codex, claude processes. Never foreground/click/type/screenshot the desktop. No Claude in Chrome.
- Never modify the user's original ISOs (patched copies only). No game data in git or releases.
- Any store into the netemu's read-only memory powers the PS3 off; validate patches on the PC harness first.
- Confirm before push, tags, releases, deleting releases. v1.2.0 still exists (deletion was never asked).
- Sony-derived files stay out of git by default. `ps2_netemu.self` is in the v1.3.0 release by the owner's explicit choice (takedown risk flagged once).
- User-facing text: say "any slots", never "multitap". No `.CONFIG` files for users. No "HEN" in the netemu entry text (netemu route needs CFW).

## Working style

- Test once, then hand over a short exact test. No repeated game launches.
- Research proven open-source patterns first (PSL1GHT, Cobra, webMAN, Linux ps3rom).
- Codex for long builds (`codex exec`, tracked background task, no dangerous bypass, no Monitor watcher); agy only via `agyp gabar -p ... --dangerously-skip-permissions` because the user said so. Self-contained prompts; see `C:\Users\remus\ps3buzz-console\netemu_setups\prompt.txt` (parked: per-setup netemu builds).
- Use pyghidra, not capstone.
- Heredoc gotcha: bash heredocs and `python - <<E` collapse backslashes (`\\n` becomes a real newline). Use the Write tool for code with backslashes. Many files are CRLF: keep line endings.
- Short answers; the user types casually.
- Releases: draft first, emoji notes, honest tested/untested, `SHA256SUMS.txt`, exe + zip assets. Git identity: `Gabarsolon <Gabarsolon@users.noreply.github.com>`.

## Known limits and open items

- HEN: cannot replace the netemu; ISO patching gives two handsets only (stock emulator delivers 12 buttons per pad: usages 13..20 dropped). Ideas not done: second dongle, Pico adapter.
- Console freeze: whole PS3 froze once after ~10 min idle in the app (earlier build). Not confirmed fixed; heartbeat log `/dev_hdd0/tmp/buzzbridge_heartbeat.log` (ask the user for it if it recurs).
- libc `stat()` drops files > 2 GiB: use `sysLv2FsStat`.
- New SIO2 frame shapes froze the console twice, unexplained; only use proven shapes.
- webMAN `/remap.ps3` redirect of the netemu does not work (stock file in flash runs).
- Linux/macOS install scripts only syntax-checked.
- Parked: per-setup netemu builds matching the ISO wizard (prompt ready, not launched).
- Sibling project: PS2 disc straight from the drive, `C:\Users\remus\ps2-netemu-disc`.
