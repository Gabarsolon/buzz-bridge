# Pico adapter: four real Buzz! buzzers with working lamps on a stock `ps2_netemu`

A Raspberry Pi **Pico 2** (RP2350) sits between a wired Buzz! dongle and the PS3. The PS3 sees the Pico, not the buzzers. Because the PS3's own PS2 emulator file stays **stock** (no flash write), this route does not need the modified `ps2_netemu.self`.

## The three modes

| Mode | PS3 sees | Result |
|---|---|---|
| **A** | a copy of the Buzz dongle (054C:1000) | 2 handsets (stock netemu keeps 12 buttons per device). Buzzer lamps are lit by the Pico itself (first press), not by the game. |
| **B** | one device with two pads (054C:1000, two HID interfaces) | 4 handsets (buttons only, no game lamps) with a PS2 ISO patched as `--buzzer-pairs 2` |
| **C** | **one DualShock 3** (054C:0268) | **4 handsets and game-driven lamps** through one pad, with an ISO patched as layout 14 (`mode_c`). The game's lamp reports go back to the Pico as pad vibration. |

Mode C is the one to use. Modes A and B are kept for testing.

**Idle:** on the PS3, a Mode C session was disabled by the console about 14 minutes after it started (the saved request log shows the PS3's disable command), and the pad stayed dead until the Pico was reflashed. Version 1.4.1 therefore adds small bounded noise to the DualShock's motion-sensor bytes, like a real pad (`jitter on|off` over the debug port, default on), and up to three delayed automatic PS-button presses after a PS3 disable. **This is not yet tested on a PS3**: whether it keeps the pad alive over a long idle is unproven.

## Tested

One CFW PS3 slim (Evilnat 4.93, **stock** `ps2_netemu.self`), Pico 2 H, wired Namtai dongle (054C:1000), *Buzz! The Schools Quiz*:
- Mode B: all four handsets.
- Mode C (the 1.4.0 firmware): all four handsets and the game's lamps work (owner-confirmed). The 1.4.1 firmware has the same Mode C plus the idle changes and is untested on the PS3. The PS3 enumerated the Pico as a DualShock 3, enabled it, and the saved request log shows the normal startup (8 feature reads, PS pulse, enable) and 80k+ input reports without a failure.
- Rumble reaching the Pico: Gran Turismo collision lights the lamps.

**Not tested:** HEN (the design needs nothing on the console, but nobody has run it), other games and PS3 models, other dongles, the Pico 1 / RP2040.

## What you need

- Raspberry Pi **Pico 2** (RP2350), ideally the "H" version with pins soldered
- A USB-A **female** socket on a breakout (screw terminals) and four wires
- A micro-USB cable to the PS3

Wiring (the Pico's pin numbers; GPIO is 3.3 V only):

| Socket | Pico |
|---|---|
| `+` (5 V) | pin 40, `VBUS` |
| `-` (GND) | pin 38, `GND` |
| `D+` | pin 21, `GP16` |
| `D-` | pin 22, `GP17` |
| `S` (shield) | not connected |

Power comes from the PS3 over the micro-USB cable. Plug the wired Buzz! dongle into the socket.

## Flash

1. Hold **BOOTSEL**, plug the Pico into your PC, release the button. A drive named `RP2350` appears.
2. Copy `firmware/buzz_bridge_modec2.uf2` onto that drive. It reboots by itself.

The first boot is Mode A with a debug COM port.

## Set Mode C for the PS3

You need Python 3 with `pyserial` (`pip install pyserial`). From `pico/tools`:

```
python bridge_cli.py config c 0
```

`config <a|b|c> <0|1>`: mode, then whether the debug COM port is on (1) or off (0). For the PS3 use debug off (`c 0`): the Pico then shows only as the DualShock. The setting is saved in flash and survives reflashing the normal image. Other commands: `status`, `ds3log` (the PS3's startup requests, read after a session), `jitter on|off`, `rumble probe on|off` (diagnostic: any rumble lights all four lamps, leave it **off** for games), `local ...` (Mode A lamp options).

**Getting the debug port back** from a clean mode: hold **P1 red + P1 blue together for 5 seconds**. The Pico saves its log and reboots with the debug port (`CAFE:B003`). Or hold BOOTSEL while plugging in and flash `firmware/buzz_bridge_modec2_recovery.uf2`, which forces the port on; then run `bridge_cli.py config c 1` and flash the normal image again.

## Patch the game and play

1. Patch your own, unpatched PS2 ISO with `buzz-bridge-ps2.exe`: choose **8 (Mode C pad)**, or
   `python ps2/buzz_bridge_ps2.py "Game.iso" --layout mode_c`. See [../ps2/README.md](../ps2/README.md).
2. Put the ISO on the PS3 as usual.
3. Plug the Pico (Mode C, debug off) into the PS3 and wait about 5 seconds at the XMB. The Pico presses the PS button by itself. You can also hold all four red buzzers for 2 seconds.
4. In **Settings > Accessory Settings > Reassign Controllers** make the Pico **controller 1**, then start the game. Keep this pad dedicated to the Pico.

## Buzzer buttons as DualShock buttons (Mode C)

This is how the PS3 sees the buzzers; the patched game reads it back. It is also handy for the XMB.

| Handset | Red | Yellow | Green | Orange | Blue |
|---|---|---|---|---|---|
| P1 | Select | L3 | R3 | Start | Up |
| P2 | Right | Down | Left | L2 | R2 |
| P3 | L1 | R1 | Triangle | Circle | Cross |
| P4 | Square | Left stick right | Left stick down | Right stick right | Right stick down |

Protocol details (report bytes, the lamp code carried in the motor values): [docs/MODE_C_PROTOCOL.md](docs/MODE_C_PROTOCOL.md).

## Limits

- Mode C needs a PS2 ISO patched with layout 14. The game must be one the universal patcher supports.
- Games that rumble the first pad themselves would collide with the lamp code; keep it for Buzz! titles.
- The Pico drives the dongle with Pico-PIO-USB at low speed. Only the dongle's 054C:1000 / 054C:0002 identity has been tried.

## Source and building

`src/` is the firmware (C, pico-sdk + TinyUSB + [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), all MIT/BSD). `tools/` has the PC helpers and a reference Windows build script (`build_modec.py`, needs `PICO_SDK_PATH`, the Arm GNU toolchain, Ninja, MSVC Build Tools; it expects this folder at `<Pico-PIO-USB>/buzz-pico-adapter` with `src/` renamed `firmware_source/`). The prebuilt UF2s are what was tested; hashes are in `firmware/SHA256_MODEC2.json`. Third-party notices: [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md), [docs/REFERENCES.md](docs/REFERENCES.md).

Helper environment variables: `BUZZ_PICO_SERIAL` (pick one board if several are attached), `LIBUSB_DLL` (path of `libusb-1.0.dll` for `flash.py`).
