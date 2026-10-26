# Current self-contained build

Run root `build_ps3.ps1` with the installed read-only PS3DK v0.26.0. It rebuilds the game, requires identical decrypted PRX versus the confirmed 0.7, restores the confirmed distribution bytes, builds the new helper/app, and packages/audits `build/BuzzPhones.pkg`. All source inputs and temporary output are inside this folder; no original game ELF or scratchpad is read. Public signing text is under `research/load10/`. Frozen digests are in `plugin/autoload/frozen_inputs.json`; `make_profile.py` verifies them instead of regenerating from game data. No pip packages were installed.

The helper is an APP with VSH authority 1070000052000001 and webMAN's plaintext control flags. The game keeps authority 1010000001000003. The actual key revision remains 1; the app refuses HEN setup because the inspected HEN plugin guidance requires a separately verified revision-0A signing path. The homebrew app is NPDRM packaged with installed make_self_npdrm/pkg/package_finalize, category HG, BUZZP0001. `autoload_audit.json` decrypts the final package and checks every member against the allowlist/staging, including EBOOT's zero padding.

See [AUTOLOAD_PATTERNS.md](../build/AUTOLOAD_PATTERNS.md), [root guide](../README.md), and `build/autoload-build.log`. Historical notes follow; references to scratchpad/game analysis are no longer build dependencies.

# Historical PS3DK v0.26.0 revision 10 build

Installed SDK: `E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64`, read only. All output/temp files stay inside ps3buzz. No executable, toolchain, firmware or pip package was downloaded/installed. Already-installed Crypto is used offline; signing constants are read from saved public source, and root APP keys from the existing scratchpad `dl/key_vault.cpp`. Keys are not written to audit reports or copied into deliverables.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File plugin/build_ps3.ps1
python -B plugin/tests/run_all.py
python -B research/verify_game.py
```

The script sets process-only PS3DK/PS3DEV/PSL1GHT/PATH/TEMP/TMP, restoring them afterward. Generated assets/profile and all outputs go in this work folder. GCC 16.1.0 uses the installed compact-OPD ILP32 PPU ABI, `-O2 -Wall -Wextra -Werror`. Native linking uses `lv2-prx.specs`, `-nostartfiles`, `-Wl,-q`, `-z,max-page-size=16`, the workspace `plugin/game/native-prx.ld`, SDK PRX CRT, and io/net_stub/runtime libraries.

`prepare_native_link.py` derives the workspace script from the installed SDK template. The primary path does not use SDK `prx-gen`: it assumes one LOAD when dropping relative relocations. `native_prx.py` reads the linked ELF's symbols/relocations, resolves descriptor-named code branches, checks TOC-relative data against the TOC's own segment, fills imports/28-byte management exports, and emits RX/RW plus PPURELA with no section table. Every unsupported allocated-relative case fails closed. Full segment flags include the control's SPU permissions. No dummy section names stand in for runtime metadata.

`sign_native_self.py` calls installed make_self, verifies its public-source scalar computation, normalizes zero control flags/ELF SHA-1/scetool APP encrypted capabilities, zeroes empty SELF section offset, then recomputes fixed-width signature scalars and re-encrypts metadata with the **same actual APP revision-1 key**. It checks the new scalars and HMACs. The fake path uses installed fself and zeroes the empty SELF SH offset. This is not a hardware/signature-public-key acceptance test. The retained make_sprx system-authority comparison is not selected for console testing.

`build_main_variants.py` also packages the flags-zero counterpart and freshly relinks a one-RWX counterpart using the SDK script/prx-gen. `build_diag.py` builds only the new E–H set; it leaves A–D intact. The underlying diagnostic C module_start is file-only, has zero imports, and returns 1; small Cobra wrappers implement thread exit and empty stop.

`verify_native_prx.py` checks pointers/OPDs/import instruction stubs at unrelated RX/RW bases and rejects bad relocation writes. `audit_format10.py` decrypts/compares 17 old/new/control containers, verifies 32 segment HMACs, confirms exact normalized ELF differences and diagnostic FS-only syscall behavior, and checks the main's first syscall/144-byte frame. Reports: `build/prx_audit.json`, `build/format10_audit.json` and `build/loader_audit.json`. Legacy `verify_prx.py`/`audit_loader.py` dispatch to the new audits when the primary has two LOADs.

The current main has 220 function imports, no unresolved symbols, no executable CRT/allocator, a 144-byte entry frame and 64 KiB initialization thread. PC tests, sanitizers and read-only game hash verification pass. Exact captured commands/results: `build/native-build10.log`, `build/pc-test10.log`, `plugin/TEST_RESULTS.md`. Console acceptance, native import/link policy, callbacks and gameplay are unverified.

Prior revision files/reports are preserved under `build/previous10/`; v1 and A–D remain intact. [Loader evidence](../build/LOAD_DIAGNOSIS.md), [console procedure](../build/CONSOLE_TEST.md), [hashes](../build/ARTIFACTS.md).
