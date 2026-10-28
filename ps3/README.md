# Buzz! Bridge (PS3)

Play **PS3 Buzz! games** with **phones** and **DualShock controllers** instead of Buzz! buzzers. No extra hardware and no game patching.

A small plugin runs inside the game and presents up to **two virtual Buzz! buzzer sets (8 players)** to it. Phones join over Wi-Fi through a web page served by the PS3; DualShocks are read directly. The game sees ordinary wired buzzers, so its "no Buzz! buzzers detected" screen goes away.

## Status (PS3 v1.0.0)

| Game | Title ID | Tested on a PS3 |
|---|---|---|
| Buzz!: Brain of the UK | BCES00347 (01.00) | ✅ phones + DualShocks, full match |
| Buzz!: Quiz TV – Special Edition | BCES00303 (01.00) | ✅ automatic start through the Buzz! Bridge app |
| Buzz!: Quiz World | BCES00644 (01.00) | ✅ |
| Other PS3 Buzz! versions | — | ❓ tried in "generic" mode, refused if anything does not match. See [PORTING.md](PORTING.md) |

- **Console:** CFW with Cobra (tested: Evilnat 4.92 + webMAN MOD 1.47.48a). **PS3HEN: should work but is untested.** The plugin files are signed with key revision 0A, as HEN requires (same as webMAN MOD and DS45Pad). If you try it on HEN, please report the result with the logs listed below.
- **Not yet tested on a PS3:** "Enable at boot". Use "Start for this session" until it is.

## Install

1. Download `BuzzBridge.pkg` from the [v1.2.1 release](../../../releases/tag/v1.2.1) and install it from the XMB (**Package Manager → Install Package Files**).
2. Start **Buzz! Bridge** from the XMB and choose **Start for this session**. It installs its files to `/dev_hdd0/plugins/ps3buzz/`, starts a small background helper for this session only and shows the phone address. Nothing is added to the boot list.
3. Start your Buzz! game (Buzz! buzzers unplugged) and wait at the main menu. After about **30 seconds** the helper loads the plugin into the game and the buzzers are detected.
4. On phones on the same network open **`http://<PS3-IP>:8420/`** and pick a player. The game's buzzer lights show on the phone.
5. **Reboot before playing another game.** The helper loads the plugin once per session.

## DualShock buttons

| Controller | Buzzer |
|---|---|
| R1 or R2 | Red |
| △ | Blue |
| □ | Orange |
| ○ | Green |
| ✕ | Yellow |

Pad 1 is player 1, pad 2 is player 2; phones take the free players. The mapping is in `/dev_hdd0/plugins/ps3buzz/ps3buzz.cfg` (`players_per_pad=2` splits a pad into two players: the left half uses L1/L2 and the D-pad). Starting the app again reinstalls the default config.

## Start automatically at boot (untested)

Start **Buzz! Bridge**, answer **No** to "Start for this session", then **Yes** to "Enable at boot". The app adds exactly one line (`/dev_hdd0/plugins/ps3buzz/ps3buzz_vsh.sprx`) to `/dev_hdd0/boot_plugins.txt` and keeps a backup (`boot_plugins.txt.ps3buzz.bak`). To turn it off: **No**, **No**, **Yes** ("Disable at boot"). Only the game plugin's helper goes in the boot list, never `ps3buzz.sprx` itself.

## Safety and recovery

Nothing is written to dev_flash and no game file is changed. The plugin only lives in the running game; quitting the game removes it.

- **Skip the helper:** hold **R2** while the helper starts (the first 10 seconds after XMB start or after "Start for this session").
- **`/dev_hdd0/plugins/ps3buzz/disable`**: while this file exists, the helper loads nothing.
- **`/dev_hdd0/tmp/ps3buzz.stop`**: stops the helper and a running game plugin; reboot afterwards.
- **XMB freezes at boot** (boot option): boot holding R2. If that is not enough, put an **empty** `boot_plugins.txt` in the root of a FAT32 USB stick that the PS3 sees as `dev_usb000` before boot (which port that is varies; not tested yet); Cobra reads it instead of the HDD list (this also skips webMAN for that boot), then disable the boot option in the app.
- Logs for bug reports: `/dev_hdd0/tmp/buzzbridge_app.log`, `ps3buzz_vsh.log`, `ps3buzz.log`, `ps3buzz.heartbeat.log`.

## Uninstall

Disable the boot option (if enabled), reboot, delete **Buzz! Bridge** from the XMB, then delete `/dev_hdd0/plugins/ps3buzz/` with a file manager (webMAN, multiMAN).

If you installed the earlier test app **Buzz! Phones** (`BUZZP0001`), delete it from the XMB.

## Other versions of the games

[PORTING.md](PORTING.md) explains how to check another Buzz! version with `tools/extract_eboot.py` and `tools/inspect_game.py`, how to read the logs and how to add a profile.

## Build from source

Windows, [PS3DK](https://github.com/FirebirdTA01/PS3DK) v0.26.0, Python 3 with PyCryptodome, CMake, Ninja; the PC tests use Clang and Node.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build_ps3.ps1 -Sdk <PS3DK folder>
python -B plugin/tests/run_all.py
```

The SELF signing step reads public key files from `research/load10/` (RPCS3's `rpcs3/Crypto/key_vault.cpp` and PSL1GHT's key headers), which are not included in this repository. The build also compares against the console-verified binaries it keeps in `build/`; a fresh clone needs those steps adjusted. [DESIGN.md](DESIGN.md) describes how it works.

## Legal

No game data is included. You need your own copy of the game. The phone page comes from the PCSX2 phone controller in [../pcsx2](../pcsx2) (formerly buzz-web-controller, MIT).
