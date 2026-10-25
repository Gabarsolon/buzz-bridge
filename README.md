# Buzz! Pad Bridge

Play **Buzz! The Schools Quiz** (PS2) with your **real Buzz! buzzers on a PS3 slim or super slim**, through the PS3's built-in PS2 emulation.

The PS2 software emulator that every PS3 slim and super slim uses (`ps2_netemu`) does not pass the Buzz! USB dongle through to the game. It turns the dongle into a DualShock controller instead, so the game says *"Buzz! buzzers are not detected. Please insert the Buzz! buzzers into a USB connector"* and never gets past that screen. This tool patches **your own copy** of the game so it reads the buzzers from that controller.

It is a game patch only. Nothing on the PS3 is modified (no `ps2_netemu.self` or dev_flash changes).

## Status (v0.1.0)

- ✅ Works on a real PS3 (CECH-25xx, Evilnat 4.92 CFW + Cobra, ISO mounted with webMAN) with real wired buzzers: **2 players** (handsets 1 and 2).
- ✅ Works in PCSX2 with a normal controller.
- ❓ PS3 HEN: untested. It should work because nothing but the game is patched.
- ❌ Handsets 3 and 4: `ps2_netemu` only passes 10 of the 20 buzzer buttons to the game. See [PLAN.md](PLAN.md) for how 4 players will work.
- ❌ Buzzer lights stay off.

## Supported game version

Only **Buzz! The Schools Quiz (UK), SCES-54941, Original v2.00** (Redump SHA-1 `b3862f09cf0be9caa1ccf88dae5ab8de6691b31b`, volume label `BUZZLRN`).

The **Classroom Edition v1.00** (`SCES-54941/9473657`) is a different build and is **not supported yet**. The patcher checks every byte it changes and refuses any other version without touching it.

**Other Buzz! games:** each game, region and edition needs its own addresses. [docs/PORTING.md](docs/PORTING.md) explains how to find them, and `tools/new_game.py` turns them into a patch and a PCSX2 cheat file for testing. Contributions welcome.

## How to use

Download `buzz-pad-bridge.exe` from the [latest release](../../releases/latest) (or run `buzz_pad_bridge.py` with Python 3).

### PS3

1. Drag your `Buzz! The Schools Quiz` ISO onto `buzz-pad-bridge.exe`. It patches the ISO in place (about 200 bytes).
2. Copy the ISO to `/dev_hdd0/PS2ISO/` on the PS3.
3. Plug in the Buzz! buzzers, mount the game with webMAN and start it with your DualShock 3.
4. Once the game is running, **turn the DualShock 3 off** (hold PS → Turn Off Controller). Its buttons would otherwise count as buzzer presses.

### PCSX2

- Either load the patched ISO, or keep the ISO original and use the cheat file: copy `cheats/7C8E9A15.pnach` into your PCSX2 `cheats` folder and enable cheats.
- In Settings → Controllers, set both **USB ports to None** and use a normal controller as pad 1, with the mapping below.

### Controller mapping

On the PS3 this happens automatically for the real buzzers. With a controller (PCSX2, or a DualShock on the PS3) the buttons are:

| Controller | Buzzer |
|---|---|
| □ / ✕ / ○ / △ / L1 | Player 1: red / yellow / green / orange / blue |
| R1 / L2 / R2 / Select / Start | Player 2: red / yellow / green / orange / blue |

### Undo

```
buzz-pad-bridge.exe "Buzz! The Schools Quiz (Europe).iso" --restore
```

`--check` reports whether an ISO is patched without changing it.

## How it works

- The "buzzers not detected" check (EE `0x202A60`) always reports the buzzers as connected.
- The game's buzzer poll (EE `0x275B00`), which normally asks the IOP buzzer driver for USB reports, is replaced by a bridge. It reads both controller ports, translates the buttons with `ps2_netemu`'s own Buzz!→DualShock layout back into buzzer bits, and writes them where the game expects the buzzer report. That includes the handset-present flags (bits 20–22) the menus check before reading a button.

## Legal

No game data is included. You need your own copy of the game.

## License

MIT, see [LICENSE](LICENSE).
