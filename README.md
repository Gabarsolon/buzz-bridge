# Buzz! Pad Bridge

Play **Buzz! games** (PS2) with your **real Buzz! buzzers on a PS3 slim or super slim**, through the PS3's built-in PS2 emulation.

The PS2 software emulator that every PS3 slim and super slim uses (`ps2_netemu`) does not pass the Buzz! USB dongle through to the game. It turns the dongle into a DualShock controller instead, so the game says *"Buzz! buzzers are not detected. Please insert the Buzz! buzzers into a USB connector"* and never gets past that screen. This tool patches **your own copy** of the game so it reads the buzzers from that controller.

It is a game patch only. Nothing on the PS3 is modified (no `ps2_netemu.self` or dev_flash changes).

## Status (v0.2.0)

- ✅ Schools Quiz works on a real PS3 (CECH-25xx, Evilnat 4.92 CFW + Cobra, ISO mounted with webMAN) with real wired buzzers: **2 players** (handsets 1 and 2).
- ✅ Schools Quiz works in PCSX2 with a normal controller.
- ✅ Mega Quiz works in PCSX2 with a normal controller (found by auto-detection). Not yet tested on a PS3.
- ❓ PS3 HEN: untested. It should work because nothing but the game is patched. HEN cannot run PS2 ISOs directly: patch the ISO first, then convert it to a PS2 Classic (`.BIN.ENC` / `.pkg`, e.g. with PS2 Classics GUI).
- ❌ Handsets 3 and 4: `ps2_netemu` only passes 10 of the 20 buzzer buttons to the game. See [PLAN.md](PLAN.md) for how 4 players will work.
- ❌ Buzzer lights stay off.

## Verified and auto-detected versions

| Version | Status |
|---|---|
| Schools Quiz (UK), `SCES_549.41`, Original v2.00 | Verified on PS3/PCSX2; original 50 words checked against `GAMES` |
| Mega Quiz (Europe, En/Fr/Nl), `SCES_545.05` | Auto-detected; **works in PCSX2**, PS3 untested |
| Other games, regions and editions, including Classroom Edition v1.00 | Detection attempted; accepted only if every required signature is unique and all structure checks pass |

The Schools Quiz reference has Redump SHA-1 `b3862f09cf0be9caa1ccf88dae5ab8de6691b31b` and volume label `BUZZLRN`. `GAMES` remains the list of versions verified on hardware/PCSX2. Auto-detection does not add entries to it.

Give the patcher any PS2 Buzz! ISO: it reads the boot ELF, locates the observed Relentless buzzer/pad code family and derives the addresses. An unknown build prints **UNTESTED**. A missing/duplicate signature, changed instruction, inconsistent layout or short poll causes refusal before patching. Different game engines or compiler layouts may require extending the detector; this is not evidence that every Buzz! release works.

For auto-detected builds the patcher saves `<game.iso>.buzz-pad-bridge.json` beside the ISO **before writing it**. Keep this recovery sidecar with that ISO: it preserves the original words for `--check`, interrupted-patch recovery and `--restore`. A patched unknown build without its sidecar is refused. Known verified builds need no sidecar. Read-only `--check` creates no files.

[docs/PORTING.md](docs/PORTING.md) documents the detector, disassembly evidence, limits, and manual research fallback. `tools/new_game.py "My Buzz Game.iso" --out .` inspects an original ISO and generates a PCSX2 test cheat without modifying it. Bundled cheats: `cheats/7C8E9A15.pnach` (Schools Quiz) and `cheats/175AAA90.pnach` (Mega Quiz).

## How to use

Download `buzz-pad-bridge.exe` from the [latest release](../../releases/latest) (or run `buzz_pad_bridge.py` with Python 3).

### PS3

1. Drag your Buzz! ISO onto `buzz-pad-bridge.exe`. It patches the ISO in place (about 200 bytes).
2. Copy the ISO to `/dev_hdd0/PS2ISO/` on the PS3.
3. Plug in the Buzz! buzzers, mount the game with webMAN and start it with your DualShock 3.
4. Once the game is running, **turn the DualShock 3 off** (hold PS → Turn Off Controller). Its buttons would otherwise count as buzzer presses.

### PCSX2

- Either load the patched ISO, or keep it original and enable its matching cheat in your PCSX2 `cheats` folder. Bundled: `cheats/7C8E9A15.pnach` (Schools Quiz), `cheats/175AAA90.pnach` (Mega Quiz). For another auto-detected build, generate its cheat with `tools/new_game.py`.
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

- The detected "buzzers not detected" check (Schools Quiz EE `0x202A60`) always reports the buzzers as connected.
- The detected buzzer poll (Schools Quiz EE `0x275B00`), which normally asks the IOP buzzer driver for USB reports, is replaced by a bridge. It reads both controller ports, translates the buttons with `ps2_netemu`'s own Buzz!→DualShock layout back into buzzer bits, and writes them where the game expects the buzzer report. That includes the handset-present flags (bits 20–22) the menus check before reading a button.

## Legal

No game data is included. You need your own copy of the game.

## License

MIT, see [LICENSE](LICENSE).
