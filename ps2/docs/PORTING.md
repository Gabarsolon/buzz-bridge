# Porting the universal PS2 bridge

The main tool is `../buzz_bridge_ps2.py` / `buzz-bridge-ps2.exe`. It replaces the game's USBD module while leaving the EE executable and BUZZER driver unchanged. The older EE signature detector, cheats and address research are archived under [../legacy/](../legacy/); its original porting notes are [../legacy/docs/PORTING.md](../legacy/docs/PORTING.md).

## Runtime contract

`../universal/bbusbd.irx` exports **usbd 1.2, 19 slots**, presents one virtual wired dongle `054C:0002` and delivers the five-byte report `7F 7F b0 b1 b2|F0`. The game's own BUZZER driver retains its RPC interface, report FIFO and consumer logic. Lights reports are accepted internally but do not reach a physical dongle. Other USB devices are not passed through.

The module observes SIO2MAN 2.x transfer exports **25/51**, relinking existing callers and replacing entries for later callers. `--poll auto` additionally performs session-gated pad reads when passive traffic is absent. This is needed for Jungle Party and intended for the other Junior games without libpad. It is not proof of compatibility with another emulator or game. Sources, import/export tables and the Makefile are in `../universal/`.

Four virtual handsets are mapped from slot 0 of controller ports 1 and 2. The five layouts live in `src/bb_layouts.h`; the patcher uses the same IDs/names and has a bundled fallback. Do not renumber them without changing the configuration ABI. See the user guide for exact button mappings and the separate 1–5 menu numbering.

## ISO patch contract

Only plain ISO9660 images with 2048-byte sectors are supported. The reader validates both-endian extent fields, record boundaries and file bounds, and refuses multi-extent/interleaved records. It searches `/IOP/USBD.IRX` and `/SYSTEM/USBD.IRX`. Multiple matches require `--path`; unsupported locations are refused.

Patching requires an **exact SHA-1 match** in `tools/bb_patch.py:KNOWN_USBD`. The three known hashes came from the universal project's research; no game modules are included:

| USBD family | SHA-1 |
|---|---|
| Sony 1.3.0, Schools/Mega | `cbae284db35baceeaaed642351df99817ec47586` |
| Sony 1.4.0, Junior | `80ebc1a552dffaf5640714b69bac4cbc173e3d22` |
| Sony 1.6.0, Monster Rumble | `4a9d0d25b523f72f1be111bf9e0a8d88b96db873` |

A familiar filename, module name or size is insufficient. There is **no force bypass**. `--info` prints unknown hashes without modifying anything. A recognised USBD is an eligibility check, not a tested-game claim. It also cannot detect an old EE patch elsewhere in the ISO: use a clean original, or restore the legacy patch before patching.

The replacement must be a little-endian ELF32 MIPS IOP module exporting the expected table with exactly one version-1 configuration block. The patcher modifies this 16-byte block:

```text
<8sBBBBHH: BBZCFG01, version=1, layout, flags, reserved=0,
           attach_delay_ms, heartbeat_ms
flags: 01 verbose, 02 no pad, 04 scripted demo, 10 poll auto, 20 poll always
```

The module is written at the original file offset and **zero-padded to the original file length**. No relocation, directory length change or EE patch is made. A larger replacement is refused before writing. Tests compare every byte before and after the USBD range, including adjacent files.

The default is a new `<stem> (Buzz Bridge).iso` beside the source. Free space is checked for the entire ISO and recovery overhead. Copying uses a temporary file on the destination filesystem; the final name is published without replacing an existing file. `--in-place` is explicit. Recovery files are flushed before its first write; every extent write is read back. Existing outputs and recovery files are refused.

Recovery v2 stores the original USBD in `<ISO>.usbd.orig` and metadata in `<ISO>.bbpatch.json`: image size, path/extent, SHA-256 checksums and expected patched bytes. Backup lookup is relative to the ISO, never an arbitrary JSON path. Restore validates the directory extent and checksums, accepts original/patched bytes (including a partially completed extent write), and refuses unrelated bytes. It changes only that extent, not the rest of the image. Recovery files remain after restore, so use a fresh output name for the next patch. The prototype's v1 JSON is not accepted by this tool; use its original patcher or `--from-iso` with the matching original image. `--from-iso` is an alternative for a complete patch matching the bundled module; sidecar recovery is needed for an interrupted patch or a different module build.

## Investigating a new version

1. Obtain `--info` output, title, serial, region and edition from a clean original. Keep its USBD/BUZZER binaries private.
2. Inspect USBD's export ABI and the BUZZER imports using `tools/check_irx.py` / `tools/irxlib.py`. Check SIO2MAN version/entries, pad availability, module load paths and replacement size. Do not treat an export match alone as sufficient evidence.
3. Keep unknown-version refusal intact until the specific build has been reviewed. Add a hash with its provenance only after confirming the replacement contract; never loosen the check to a substring or size match.
4. Build and run the synthetic/native tests. On an authorised test copy, test scripted enumeration first, then actual input and releases with `--poll auto`. Disable legacy cheats and boot without a save state.
5. Record emulator version or hardware/firmware, exact game build, layout, all players/buttons and BBUSB logs. Update the compatibility table only for the combinations actually tested.

## Evidence and checks

The user reports PCSX2 v2.8.2 success for Schools Quiz and Jungle Party (scripted demo and `two_ds_shared`; Jungle needs auto polling). The universal module on PS3 CFW/HEN, Mega Quiz and the other Junior titles remain untested. Historical [prototype notes](../universal/research/PROTOTYPE_NOTES.md) document source provenance, not current release validation.

From the release repository root, with no game data or toolchain needed:

```powershell
python -W error::ResourceWarning -m unittest discover -s ps2/tests -v
python -I -W error::ResourceWarning ps2/universal/tools/check_irx.py ps2/universal/bbusbd.irx
```

The tests use deterministic synthetic ISO contents. Their in-process allowlist includes fixture hashes only under a mock; the actual CLI must refuse those same fixtures. Native tests require an existing host C compiler, not the PS2 SDK; see [../universal/TOOLCHAIN.md](../universal/TOOLCHAIN.md). No game-dependent emulator harness or test inputs are copied into this release repository.
