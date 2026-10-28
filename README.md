# Buzz! Bridge

Play **Buzz!** quiz games on a PlayStation 3 without the original buzzer setup problems.

| Platform | What it does | Folder | Download |
|---|---|---|---|
| **PS3 games** (Buzz!: Quiz TV, Quiz World, Brain of …) | Phones and DualShock controllers act as Buzz! buzzers (up to 8 players). A PS3 plugin, no game patching. CFW (HEN untested) | [ps3/](ps3/) | [v1.2.1 release](../../releases/tag/v1.2.1) (`BuzzBridge.pkg`) |
| **PS2 games** (Buzz! Schools Quiz, Mega Quiz, Buzz! Junior, …) on a PS3 slim/super slim or in PCSX2 | Universal patcher: a virtual Buzz! dongle fed by DualShocks and/or real buzzers, no per-game code. Up to 4 players, or 8 in the two-buzzer-set quiz games, with more controllers on any slot (no physical multitap needed). Tested: Schools Quiz and all 5 Buzz! Junior games in PCSX2; real buzzers + 2 DualShocks on a PS3 (CFW) in Schools Quiz. HEN untested | [ps2/](ps2/) | [v1.2.1 release](../../releases/tag/v1.2.1) (`buzz-bridge-ps2.exe`) |
| **PS2 games in PCSX2** (PC) | Phones act as Buzz! buzzers through a small PC server and a virtual gamepad (LAN or internet). No PCSX2 patching | [pcsx2/](pcsx2/) | Run from source (Python), see [pcsx2/README.md](pcsx2/README.md) |

Phones for PS2 games **on a PS3** are not possible without extra hardware: while a PS2 game runs, `ps2_netemu` replaces the PS3 system and has no network support. In PCSX2 they work (see `pcsx2/`).

No game data is included in this repository. You need your own copies of the games.

## License

Our code is MIT, see [LICENSE](LICENSE). The PS2 universal module also contains ps2sdk-derived code under AFL 2.0 and an attributed OPL hook reference (AFL 3.0); see [third-party notices](ps2/universal/THIRD_PARTY.md).
