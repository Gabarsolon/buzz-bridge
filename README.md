# Buzz! Bridge

Play **Buzz!** quiz games on a PlayStation 3 without the original buzzer setup problems.

| Platform | What it does | Folder | Download |
|---|---|---|---|
| **PS3 games** (Buzz!: Quiz TV, Quiz World, Brain of …) | Phones and DualShock controllers act as Buzz! buzzers (up to 8 players). A PS3 plugin, no game patching. CFW (HEN untested) | [ps3/](ps3/) | [v1.0.0 release](../../releases/tag/v1.0.0) (`BuzzBridge.pkg`) |
| **PS2 games on a PS3 slim/super slim** (Buzz! The Schools Quiz, Mega Quiz, …) | Real wired Buzz! buzzers work again through `ps2_netemu` (2 players). Patches your own ISO | [ps2/](ps2/) | [v1.0.0 release](../../releases/tag/v1.0.0) (`buzz-pad-bridge.exe`) |
| **PS2 games in PCSX2** (PC) | Phones act as Buzz! buzzers through a small PC server and a virtual gamepad (LAN or internet). No PCSX2 patching | [pcsx2/](pcsx2/) | Run from source (Python), see [pcsx2/README.md](pcsx2/README.md) |

Phones for PS2 games **on a PS3** are not possible without extra hardware: while a PS2 game runs, `ps2_netemu` replaces the PS3 system and has no network support. In PCSX2 they work (see `pcsx2/`).

No game data is included in this repository. You need your own copies of the games.

## License

MIT, see [LICENSE](LICENSE).
