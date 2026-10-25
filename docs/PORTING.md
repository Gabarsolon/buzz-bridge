# Detecting and porting Buzz! games

Start with an **original ISO**:

```
python buzz_pad_bridge.py "My Buzz Game.iso" --check
python tools/new_game.py "My Buzz Game.iso" --title "Buzz! ..." --out .
```

The second command reads the ISO only, prints detected addresses, layout and evidence addresses, and emits a `<CRC>.pnach` test cheat. CRC here means XOR of all complete 32-bit little-endian words of the original boot ELF. An emitted `GAMES` candidate is for submission **after a successful hardware/PCSX2 test**. It is not added automatically. `VERSION` remains `0.1.0`.

## Detection contract

`SYSTEM.CNF` names the boot ELF. The patcher validates ELF32/LE/MIPS program headers and maps every file-backed PT_LOAD segment. Signatures search aligned instructions in executable segments; buffer references must fall in writable loaded memory, including BSS.

Verified `GAMES` entries retain their exact original/replacement word checks and interrupted-patch support. Other builds use `detect_game()` and are labelled **UNTESTED**. Detection supports the two observed shapes described below, without consulting the ELF name or a per-game address list:

1. Match the unique buzzer RPC bind (`lui a1,0x5253`, `ori a1,a1,0x3030`), including call/argument structure.
2. Match the poll prologue and complete 47-word function. Its three JAL targets and six device-loop immediates are the only variable fields. Follow the calls to the status updater, report fetch (RPC command 3) and lights path; check the device strides, loop bounds and shared RPC client/call target. Validate the singleton initializer/getter and its boundary with the bind/status functions.
3. Match the device-connected and handset-present queries, tied to that singleton getter. Derive status, stride, current device index, report slot/count/latest-report offsets and flag shift. Cross-check the repeated offsets in initialization, status updates and consumers. The observed RPC client offsets remain `+0x60/+0x64`; the report ABI must still contain 20 button bits and flags at bits 20–23. Other ABIs are refused.
4. Match the unique shared TRC body that queries device 0 and device 1 through that connected query. Validate one of the two complete prologues, including all three overwritten words, its global reference, the counter clear/set and return. Derive the counter offset.
5. Match pad setup for ports 0/1 and the per-frame read loop. Derive the buffer base and stride, cross-check both paths, then validate the port-open/read wrappers and SDK destination-copy shape. These are the **read destinations**, distinct from the aligned DMA buffers passed to port-open.

Instruction masks keep opcodes/registers fixed. `?` permits immediate nibbles; a JAL mask permits only its 26-bit target. Each searched signature must match exactly once; linked targets must match their expected structure. Every overwritten poll word and all three TRC words are covered, including delay slots. The bridge is generated from the captured fields. No code cave is allocated: the first original `jr ra` delay slot must leave room for all 47 words. Unrecognised shapes are refused even when some anchors match.

The singleton-getter signature includes the call to the uniquely matched initializer. A generic lazy-initialization shape alone is ambiguous (nine matches in Mega Quiz).

## Restoring an auto-detected build

Patching destroys address-bearing instructions, so the original ELF cannot reliably be rediscovered from a patched unknown version alone. Before its first ISO write the patcher persists `<game.iso>.buzz-pad-bridge.json` containing the 50 original/new words and original boot-ELF SHA-256. Keep it with the ISO; it is recovery data, not a `GAMES` verification entry.

For `--check`, continued patching or `--restore`, the patcher permits only original/new words at the recorded positions, reconstructs the original ELF in memory, checks its SHA-256, re-runs detection, and requires the entire generated table to equal the sidecar. A wrong/stale/corrupt sidecar or unrelated ELF change is refused. Losing the sidecar means an auto-detected patched image needs an original copy; the patcher will not guess original instructions. Verified table versions retain restoration without a sidecar. `--check` opens the ISO read-only and creates no sidecar.

## Evidence: Schools Quiz versus Mega Quiz

These facts were read from the supplied **original** `SCES_549.41` and `SCES_545.05` ELFs. Mega Quiz remains untested on hardware/PCSX2. Addresses are EE virtual addresses.

| Item | Schools Quiz | Mega Quiz |
|---|---|---|
| TRC | `0x202A60` | `0x1F4D18` |
| Poll | `0x275B00` | `0x151AF8` |
| Pad 1 / pad 2 | `0x4D5F20 / 0x4D5FB4` | `0x487070 / 0x487104` |
| Pad stride | `0x94` | `0x94` |
| Pad DMA base | `0x4D63C0` | `0x487540` |
| scePadRead SDK target | `0x34B590` | `0x2BCEA8` |
| Singleton | `0x4B9D10` | `0x45FF70` |
| Singleton getter | `0x2758A0` | `0x151898` |
| RPC bind anchor | `0x275928` | `0x151920` |
| SIF bind / call target | `0x37E810 / 0x37E9F0` | `0x30BC90 / 0x30BE70` |
| Status updater / report fetch | `0x275990 / 0x275C90` | `0x151988 / 0x151C88` |
| Connected / handset-present query | `0x260FA8 / 0x261000` | `0x13E548 / 0x13E5A0` |
| Button query | `0x261150` | `0x13E6F0` |
| Poll return / delay slot | `0x275BB4 / 0x275BB8` | `0x151BAC / 0x151BB0` |
| TRC game-mode global | `0x4B3274` | `0x46D7FC` |
| TRC controller-manager reference | `0x4B4CD0` | `0x46C690` |

### RPC and poll

Mega Quiz's RPC binding retains the Schools Quiz server and client layout:

```text
151920  3C055253  lui   a1,0x5253
151924  0040202D  daddu a0,v0,zero
151928  34A53030  ori   a1,a1,0x3030
15192C  0C0C2F24  jal   0x30BC90       ; bind
151CA4  24050003  addiu a1,zero,3      ; report fetch command
151CBC  0C0C2F9C  jal   0x30BE70       ; SIF RPC call
```

The poll itself differs in **only three words**, its JAL targets: status `0x275990 → 0x151988`, lights `0x275E30 → 0x151D98`, fetch `0x275C90 → 0x151C88`. All other 44 words are identical. Both functions occupy exactly 47 words through the original return delay slot, so the bridge fits exactly.

```text
151AF8  27BDFFC0  addiu sp,sp,-0x40   ; same prologue as Schools 0x275B00
151B10  0C054662  jal   0x151988
151B1C  26030024  addiu v1,s0,0x24
151B20  24040001  addiu a0,zero,1      ; two iterations (1,0)
151B28  80620000  lb    v0,0(v1)
151B30  2463002C  addiu v1,v1,0x2C    ; statuses +0x24, +0x50
151BAC  03E00008  jr    ra
151BB0  27BD0040  addiu sp,sp,0x40
```

The lights implementations differ: Schools Quiz's `0x275E30` calls report fetch and loops through per-device RPC command 4; Mega Quiz's `0x151D98` packs both devices and calls RPC command 5 (`0x151E1C`). The bridge bypasses both paths, so lights remain outside its functionality.

### Singleton layout and flags

Both games use report count `+0x00`, report slot `+0x04`, latest report `+0x18`, byte statuses `+0x24/+0x50`, current device index `+0x58`, and stride `0x2C`:

```text
151A04  AE000018  sw    zero,0x18(s0) ; reset latest report on connect
151A34  AE320058  sw    s2,0x58(s1)   ; selected device index
151CCC  2419002C  addiu t9,zero,0x2C
151CD4  260E0018  addiu t6,s0,0x18   ; latest-report destination
151CDC  260D0004  addiu t5,s0,4      ; report slots
151D10  AC830000  sw    v1,0(a0)     ; report count
151D48  AC820000  sw    v0,0(a0)     ; slot
151D4C  ACA20000  sw    v0,0(a1)     ; latest report
```

The consumer confirms indexed report access and handset flags:

```text
13E5C8  24020004  addiu v0,zero,4
13E5CC  3C030080  lui   v1,0x80      ; bit 23
13E5D0  00441023  subu  v0,v0,a0     ; a0 = handset 1..4
13E5D8  00431817  dsrav v1,v1,v0     ; masks bits 20..23
13E5F0  8CC20058  lw    v0,0x58(a2)
13E608  8C430018  lw    v1,0x18(v0) ; state + device*0x2C + 0x18
```

The bridge preserves the verified mapping and sets `0x00700000` (handsets 1–3). This does not provide all five buttons for handset 3 or any for handset 4.

### TRC differences

The first three original words differ only in the LUI immediate (`0x4B → 0x47`), but the following game-mode logic differs. Schools Quiz reads a global into `a0`, compares mode 2, then tests ranges 2/4/6; Mega Quiz reads into `v1` and tests ranges 2/5. Both move the incoming object to `s4`, clear `s4+0x30`, query both buzzer devices, and share the same later boolean logic.

```text
1F4D18  27BDFF70  addiu sp,sp,-0x90
1F4D1C  3C020047  lui   v0,0x47
1F4D20  7FB40040  sq    s4,0x40(sp)
1F4D28  0080A02D  daddu s4,a0,zero
1F4D54  AE800030  sw    zero,0x30(s4) ; Schools: 0x202A9C
1F4D90  0C04F952  jal   0x13E548     ; a1=0 in delay slot
1F4DA4  0C04F952  jal   0x13E548     ; a1=1 in delay slot
1F4E44  AE820030  sw    v0,0x30(s4)  ; same counter
```

The shared TRC core starts at `0x202AD0` versus `0x1F4D70`; the function is `0x1B8` versus `0x1A0` bytes through the return delay slot. The bypass remains `sw zero,0x30(a0); jr ra; addiu v0,zero,1`.

### Pad buffers

Mega Quiz setup and its SDK read path independently identify the destinations:

```text
13ECDC  3C100048  lui   s0,0x48
13ECE4  26107070  addiu s0,s0,0x7070 ; pad 1
13ECF8  0C04FD60  jal   0x13F580     ; open wrapper, port 0
13ED00  26120094  addiu s2,s0,0x94   ; pad 2 = 0x487104
13ED10  0C04FD60  jal   0x13F580     ; open wrapper, port 1
13F67C  0200302D  daddu a2,s0,zero   ; read destination is struct start
13F680  0C0AF3AA  jal   0x2BCEA8     ; scePadRead
2BCF18  0260202D  daddu a0,s3,zero   ; SDK copies data to caller destination
```

Byte 1 is pad mode; halfword `+2` contains active-low buttons. The five generated patch words that differ from Schools Quiz are the pad-base LUI and four pad load immediates (`7071/7072/7105/7106`). The remaining 45 replacement words are identical.
The DMA base is `0x4A0` bytes after pad 1 in Schools Quiz versus `0x4D0` in Mega Quiz; neither gap is used to infer the read buffers.

## Checks completed offline

`tests/test_tables.py` retains the verified Schools Quiz output. `tests/test_detect.py` uses no committed game data; supply `BUZZ_SCHOOLS_ELF` and `BUZZ_MEGA_ELF` for integration checks, otherwise those checks skip:

```
python tests/test_tables.py
python -m unittest discover -s tests -v
```

With both original ELFs supplied: nine tests pass, including exact reference/Mega output, changed opcodes at every overwritten location, ambiguous signatures, inconsistent fields, broken call targets, short functions, malformed ELF mappings and recovery-sidecar validation.

Additional scratchpad verification executed the generated Mega Quiz `175AAA90.pnach` bridge in Unicorn MIPS64 for all 65,536 button halfwords on each port (131,072 cases), two-port OR, mode-zero exclusion, register/stack preservation, exact singleton stores and the TRC bypass. The original `0x13E5A0`/`0x13E6F0` consumers accepted eligibility and mapped buttons using a fixture singleton. That consumer check stubbed the getter and handled R5900 `lq`, `sq`, and `mult rd` explicitly; the exhaustive bridge check used no instruction hooks. This is not a game boot, USB, menu-timing or hardware test.

Patch → `--check` → `--restore` reproduced every byte of 5,242,880-byte Schools Quiz and 4,272,128-byte Mega Quiz slice copies. No full ISO was copied. Both supplied Romanian/original comparison ISOs contain an ELF byte-identical to the Schools reference (SHA-256 `1a8096eb5ef93e88308740872165ce84019a673145a7e6f752004c1ce507897d`), so both match the verified table. Their translated assets were not play-tested.

## Manual research fallback

If detection refuses a new build, retain that refusal and inspect its boot ELF. A disassembler must handle MIPS R5900; ordinary MIPS decoders may not understand MMI instructions. Follow the RPC bind and command-3 fetch to the poll, and prove the initializer/consumer layout. Trace both pad read destinations, not just port-open DMA memory. Find the boolean TRC monitor and its cleared counter. Confirm the entire replacement range is inside the poll through its return delay slot.

After proving those addresses manually, the research tool can emit a candidate using the reference layout:

```
python tools/new_game.py "My Buzz Game.iso" --trc 0x... --poll 0x... --pad1 0x... --pad2 0x... --out .
```

Manual mode is explicitly unvalidated and does not bypass the ISO patcher's detector. It still requires original function starts and adequate poll space; a different singleton layout requires code changes and evidence. Extend narrowly anchored masks/cross-checks and add refusal tests before offering automatic patching. Never infer a code cave from zero bytes alone.

Test the original ISO with the matching cheat in PCSX2: enable cheats, set both USB ports to None and map controller 1. Square must pass the buzzer prompt as player 1 red; Cross/Circle/Triangle/L1 are that player's colour buttons. R1 is player 2 red, followed by L2/R2/Select/Start. Check press, hold, release and repeated presses in menus and a quiz; also test controller port 2. Only then report the exact build/CRC and observed result for a verified `GAMES` entry.
