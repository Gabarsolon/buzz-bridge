# Buzz! Bridge

Play **Buzz!** quiz games on a PlayStation 3 and in PCSX2 with the controllers you actually have.

| What you want | How | Folder | Download |
|---|---|---|---|
| **PS2 Buzz! games on a PS3 with all 4 real buzzers and working buzzer lamps, no ISO patching** (CFW only) | A modified `ps2_netemu.self` replaces the PS2 emulator file in flash. Your game images stay untouched. | [netemu/](netemu/) | `ps2_netemu.self` + install scripts (latest release) |
| **PS2 Buzz! games with DualShocks or mixed setups** (up to 4 players, or 8 in the two-buzzer-set quiz games) | Universal patcher: a virtual Buzz! dongle fed by DualShocks and/or real buzzers. Run it on a PC, or from the Buzz! Bridge app on the PS3 (CFW) | [ps2/](ps2/) | `buzz-bridge-ps2.exe` (latest release) |
| **PS3 Buzz! games** (Quiz TV, Quiz World, Brain of the UK …) with phones and DualShocks | A PS3 plugin, no game patching (up to 8 players) | [ps3/](ps3/) | `BuzzBridge.pkg` (latest release) |
| **PS2 Buzz! games in PCSX2** (PC) | Phones act as buzzers through a small PC server and a virtual gamepad | [pcsx2/](pcsx2/) | Run from source (Python), see [pcsx2/README.md](pcsx2/README.md) |

`BuzzBridge.pkg` is the PS3 app: a menu with (1) the phones/DualShock helper for PS3 games, (2) install, check and restore the modified `ps2_netemu.self` (backs up your own file first and verifies every write), and (3) patch a PS2 ISO in place (with a restore). It adapts to 480i / 576i / 720p / 1080p.

## What is tested

Tested on one CFW PS3 slim (Evilnat 4.93 + Cobra) with a wired Namtai Buzz! dongle (USB 054C:1000), Buzz!: The Schools Quiz:

- Modified `ps2_netemu.self`: all four real buzzers and the lamps (menu blink, buzz-in) work, no restart needed after installing.
- The PS3 app on the same console: menus, the ISO list (images over 2 GB), 576i text and flicker, installing and restoring the netemu.
- ISO patcher: Schools Quiz and all five Buzz! Junior games in PCSX2; real buzzers + 2 DualShocks on the PS3.

**Not tested / not supported:**

- **HEN:** the netemu route cannot work on HEN (HEN cannot replace the file in flash). The PS3 app's ISO patcher is offered for CFW only. The phones/DualShock helper (`ps3/`) is untested on HEN.
- Other firmware, consoles and dongles (a genuine Logitech dongle `054C:0002` is accepted by the netemu build but untested).
- Only one dongle (four buzzers) through the netemu route. DualShock and eight-player setups go through the ISO patcher.

Phones for PS2 games **on a PS3** are not possible without extra hardware: while a PS2 game runs, `ps2_netemu` replaces the PS3 system and has no network support.

## Linux and macOS

The netemu scripts have bash versions (`install.sh`, `restore.sh`, need `curl`). The ISO patcher is plain Python (standard library only): `python3 ps2/buzz_bridge_ps2.py`. The Linux/macOS scripts are written to the same logic as the Windows ones but have only had a syntax check.

## About `ps2_netemu.self`

The release file is a patched copy of a firmware file, built from the Evilnat 4.93 `ps2_netemu.self`. Use it only on your own console, keep the backup the installer makes, and know that replacing a system file always carries some risk. Everything else here is our own code. No game data is included; you need your own copies of the games.

## License

Our code is MIT, see [LICENSE](LICENSE). The PS2 universal module also contains ps2sdk-derived code under AFL 2.0 and an attributed OPL hook reference (AFL 3.0); see [third-party notices](ps2/universal/THIRD_PARTY.md).
