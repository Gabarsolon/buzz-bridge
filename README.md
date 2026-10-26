# Buzz! Bridge

Play **Buzz!** quiz games on a PlayStation 3 without the original buzzer setup problems.

| Platform | What it does | Folder | Download |
|---|---|---|---|
| **PS3 games** (Buzz!: Quiz TV, Quiz World, Brain of …) | Phones and DualShock controllers act as Buzz! buzzers (up to 8 players). A PS3 plugin, no game patching, CFW only | [ps3/](ps3/) | [PS3 release](../../releases/tag/ps3-v1.0.0) (`BuzzBridge.pkg`) |
| **PS2 games on a PS3 slim/super slim** (Buzz! The Schools Quiz, Mega Quiz, …) | Real wired Buzz! buzzers work again through `ps2_netemu` (2 players). Patches your own ISO | [ps2/](ps2/) | [PS2 release](../../releases/tag/v0.2.0) (`buzz-pad-bridge.exe`) |

Phones for PS2 games are not possible without extra hardware: while a PS2 game runs, `ps2_netemu` replaces the PS3 system and has no network support.

No game data is included in this repository. You need your own copies of the games.

## License

MIT, see [LICENSE](LICENSE).
