# Adding another Buzz! game or version

Every Buzz! game, region and edition has its own executable, so the patch has to be located again for each one. You need to find **four addresses**. The tooling then generates the patch, a PCSX2 cheat file for testing, and the entry to add to `buzz_pad_bridge.py`.

This assumes the game uses the same Relentless buzzer code as Buzz! The Schools Quiz (the IOP module `IOP/BUZZER.IRX` on the disc, RPC server `0x52533030`). If it does not, the bridge will not fit and deeper work is needed.

## What you need

- Your own ISO of the game, **unpatched**.
- [PCSX2](https://pcsx2.net), ideally the [PCSX2-MCP](https://github.com/hkmodd/PCSX2-MCP) build, whose debug server makes watchpoints scriptable.
- A disassembler that handles PS2 (MIPS R5900) code, e.g. Ghidra. R5900-only instructions (MMI) confuse Ghidra's MIPS decompiler, so read the disassembly when the decompiler gives up.
- The boot ELF: `SYSTEM.CNF` on the disc names it (`BOOT2 = cdrom0:\SXXX_XXX.XX;1`). It usually loads at `0x100000`.

## The four addresses

Reference values are from Buzz! The Schools Quiz (UK) Original v2.00.

### 1. `pad1` and `pad2`: controller data (Schools Quiz: `0x4D5F20`, `0x4D5FB4`)

The game keeps a copy of each port's `scePadRead` data: byte 1 = mode (`0x41` digital, `0x73` analog), bytes 2–3 = buttons (active low, `ff ff` when nothing is pressed). Follow the destination buffer of the game's `scePadRead` calls for port 0 and port 1, or search RAM in the PCSX2 debugger while holding and releasing a button. In Schools Quiz the two copies are `0x94` bytes apart. Both must lie within 32 KB of each other.

### 2. The buzzer state object

Not needed as an input, but you will meet it. It is a singleton returned by a small lazy-init getter (Schools Quiz: `0x4B9D10`). Layout as used by the bridge:

| Offset | Meaning |
|---|---|
| `+0x00` | report count |
| `+0x04` | report slot |
| `+0x18` | latest report: 20 button bits (5 per handset: red, yellow, green, orange, blue) + handset-present flags in bits 20–23 |
| `+0x24` | device 0 status (`0` = connected, `0xFF` = unplugged; device 1 at `+0x50`) |
| `+0x58` | current device index (must be 0) |

The game reads the report at `state + index * 0x2C + 0x18`.

### 3. `poll`: the buzzer poll (Schools Quiz: `0x275B00`)

This function receives the buzzer state in `a0` and does, in order: call the USB status update, loop over the two devices (`+0x24`, `+0x50`, stride `0x2C`), optionally set up the lights, and call the report fetch. The report fetch calls `sceSifCallRpc` with command 3 on the RPC client bound to server id `0x52533030`, so a good way in is to search the code for `lui rX,0x5253 ; ori rX,rX,0x3030` (the RPC bind), find the function that calls RPC command 3, and take its caller.

The bridge is 47 words and must fit before the original function returns: its `jr ra` delay slot must be at `poll + 0xB8` or later. `tools/new_game.py` checks this.

### 4. `trc`: the "buzzers connected" check (Schools Quiz: `0x202A60`)

This is the function the "buzzers not detected / please insert the Buzz! buzzers" monitor calls every frame. It returns 1 (connected) or 0. The easiest way to find it is dynamically:

1. Boot the game in PCSX2 with USB set to None, so the "not detected" screen appears.
2. Put a **read** watchpoint on `state + 0x24`.
3. Follow the backtrace up from the read to the function that returns the boolean. Its caller branches to the "not detected" screen when it returns 0.

The patch replaces its first three words with `sw zero,0x30(a0) / jr ra / addiu v0,zero,1`. In Schools Quiz `a0 + 0x30` is a counter the original function clears; **check that the same holds in your game** before you test.

## Generate, test, submit

1. Generate the entry and a cheat file:

   ```
   python tools/new_game.py "My Buzz Game.iso" --trc 0x... --poll 0x... --pad1 0x... --pad2 0x... ^
       --title "Buzz! ... (Region)" --serial SLES-xxxxx --version "..." --sha1 --out .
   ```

   It refuses an already-patched ISO and warns when an address does not look like a function start.
2. Test in PCSX2: copy the generated `<CRC>.pnach` into the PCSX2 `cheats` folder, enable cheats, set both USB ports to None and use a controller as pad 1. The game must skip "buzzers not detected", and □ must act as player 1's red button in the menus (see the mapping in the README).
3. If you can, test on a PS3 with real buzzers: add the printed entry to `GAMES`, run `python buzz_pad_bridge.py "My Buzz Game.iso"`, copy the ISO to `/dev_hdd0/PS2ISO/`.
4. Open a pull request with the entry, or an issue with the four addresses, the Redump SHA-1, and what you tested.
