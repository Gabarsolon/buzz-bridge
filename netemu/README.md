# Modified `ps2_netemu.self` for Buzz! (CFW only)

Lets PS2 Buzz! games on a PS3 see **all four real handsets** of a wired Buzz! dongle, with working **buzzer lamps**, using **no ISO patching** (untouched game images). Tested on a CFW PS3 slim (Evilnat 4.93 + Cobra) with the Namtai "Buzz" dongle (USB 054C:1000), Buzz!: The Schools Quiz.

**CFW only.** It replaces the PS2 emulator file in flash, which HEN cannot do. On HEN use the Buzz! Bridge app's ISO patcher instead.

## What is in this folder

| File | What |
|---|---|
| `ps2_netemu.self` | the modified emulator (sha256 `74e890bf7f77edfd92d4c0a57f45e73cf7fe39aa3c21cd7587948fb3c81ade40`) |
| `install.ps1` / `restore.ps1` | Windows (PowerShell) |
| `install.sh` / `restore.sh` | Linux, macOS, Git Bash (bash + curl) |

It is a patched copy of a firmware file. Use it only on your own console.

## Requirements
- A CFW PS3 with webMAN MOD running (its FTP and web server), at the XMB, on the same network as your computer.
- Your PS3's IP address (default in the scripts: `192.168.0.169`: pass yours).
- `curl` (built into Windows 10/11, Linux and macOS).

## Install
Windows:
```
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Ps3Host 192.168.0.XXX
```
Linux / macOS:
```
chmod +x install.sh restore.sh
./install.sh 192.168.0.XXX
```
Wait for `OK installed; file read back and verified`, then plug the dongle in and start your PS2 game. No restart is needed.

## What the script does
1. Reads the netemu currently on your console and saves it in `backups/` (and on the console in `/dev_hdd0/buzzbridge/netemu_backup/`). **Keep these.**
2. Overwrites `/dev_flash/ps2emu/ps2_netemu.self` with `ps2_netemu.self`. Nothing is patched on your console.
3. Reads the file back, compares the hash, and always makes flash read-only again.
4. If your current netemu is not the Evilnat 4.93 one, it asks you to type `OVERWRITE` first (`-Force` / `FORCE=1` skips that). Other firmware is not tested.

## Restore
```
powershell -NoProfile -ExecutionPolicy Bypass -File .\restore.ps1 -Ps3Host 192.168.0.XXX
./restore.sh 192.168.0.XXX
```
It puts back your oldest saved copy that is not this modified one (from `backups/`, else from the console disk), saves what it replaces, and verifies. If something fails after an upload, run restore before starting any PS2 game. A bad PS2 emulator only affects PS2 games; reinstalling the firmware also replaces it.

## Using it
Start your PS2 Buzz! game with the original ISO. Press the PS button in-game: the "Debug Information" row of the emulator menu shows the status line of this build. Lamps follow the game (menu blink, buzz-in).

## Known limits
- Tested: four handsets and lamps with one wired Namtai dongle on one console. Other dongles (genuine Logitech `054C:0002` is also accepted), other consoles and firmware are untested.
- One dongle only (four handsets). DualShock and eight-player setups through this route are not available yet; the Buzz! Bridge app's ISO patcher covers those.
- Linux/macOS scripts are written to the same logic as the Windows ones but have only had a syntax check, not a run against a console.
