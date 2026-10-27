# Third-party notices for the universal PS2 module

New Buzz! Bridge code and integration changes are Copyright (c) 2026 Gabarsolon, under the [MIT license](../../LICENSE). Existing third-party portions retain their licenses below. The bundled `bbusbd.irx` includes those derived portions; distribute these notices, license texts and the corresponding source with it (including when embedded in the exe).

## ps2sdk — Academic Free License 2.0

**Copyright ps2dev — http://www.ps2dev.org**  
**Licensed under the Academic Free License version 2.0.**

The complete, unmodified license is [licenses/AFL-2.0.txt](licenses/AFL-2.0.txt). The source used by the prototype was ps2sdk commit `329e2bb43287e16af16c4dde1c614e2662c24468`. This integration preserves the copyright/license notice beside the affected source and identifies modifications:

| Derived/reference material | Source in this folder | Modifications |
|---|---|---|
| USBD module entry, registration, worker/callback patterns | `src/main.c` | Virtual dongle, fixed worker, GP-safe callbacks |
| Library lookup, export-hook and import-stub relinking from mx4sio_bd `ioplib.c` (credits maximus32) | `src/bbpad.c` | Entries 25/51 only, passive snapshots, session-gated optional polling |
| USBD descriptor walk (the prototype notes identify a line-for-line port), LDD validation and API behavior | `src/vusb.c` | Fixed virtual device/endpoints, queued completions |
| Public USB ABI structures | `src/vusb.h`, ABI checks in `src/main.c` | Host-testable mirror declarations |
| USBD 1.2 export order, imports and build conventions | `src/exports.tab`, `src/imports.lst`, `src/irx_imports.h`, `Makefile` | Virtual implementations and reduced import set |

The pad command framing, timing constants and public result codes also reference ps2sdk. The build uses installed ps2sdk headers and Makefile fragments. Corresponding modified module source is included in `src/` with `Makefile` and [TOOLCHAIN.md](TOOLCHAIN.md). Upstream source references:

- [ps2sdk at the reference commit](https://github.com/ps2dev/ps2sdk/tree/329e2bb43287e16af16c4dde1c614e2662c24468)
- [USBD](https://github.com/ps2dev/ps2sdk/tree/329e2bb43287e16af16c4dde1c614e2662c24468/iop/usb/usbd/src)
- [mx4sio_bd ioplib.c](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/sio/mx4sio_bd/src/ioplib.c)

## Open PS2 Loader — hook reference, AFL 3.0

**Copyright 2006–2008 Romz; Copyright 2010 Polo.**  
**Licensed under the Academic Free License version 3.0.**

The prototype consulted the SIO2MAN wrapper/hook pattern in OPL's `pademu.c` / `mcemu_sys.c`, commit `b3a2199795fe89ba7e2dd7b843354472dff2c3eb`. Attribution is retained in `src/bbpad.c`; the complete upstream license is [licenses/AFL-3.0.txt](licenses/AFL-3.0.txt). The implementation also derives its relinking from ps2sdk as detailed above; no OPL runtime, Bluetooth/USB controller stack or pad emulation is bundled.

- [OPL pademu reference](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/modules/pademu/pademu.c)

## PCSX2 — hardware facts used as reference

The prototype cross-checked the dongle's USB/HID descriptor values, VID/PID and report bit order against PCSX2 `usb-buzz.cpp/.h` at commit `f0523f8acb1d3a765ee73b74cffec63093569199`. These are hardware protocol facts; the provenance notes state that **no PCSX2 implementation code was copied**. PCSX2 is GPL-3.0; its implementation is not included here. UI/logging instructions were checked in the prototype against v2.8.2 source.

- [PCSX2 Buzz! device reference](https://github.com/PCSX2/pcsx2/blob/f0523f8acb1d3a765ee73b74cffec63093569199/pcsx2/USB/usb-pad/usb-buzz.cpp)

[research/PROTOTYPE_NOTES.md](research/PROTOTYPE_NOTES.md) preserves the detailed source mapping. It is historical research, not a fresh hardware verification. No Sony/game binaries, ISO inputs or upstream build outputs are included, other than the project's own `bbusbd.irx`.
