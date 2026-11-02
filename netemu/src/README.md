# Modified ps2_netemu: source

Sources and tools that built the v1.3.0 `ps2_netemu.self` (variant "S4L3", sha256 `74e890bf7f77edfd92d4c0a57f45e73cf7fe39aa3c21cd7587948fb3c81ade40`) from the Evilnat 4.93 stock file (sha256 `bad3e45c9a81f25726e414b146ea5a112f28216ab4bdd2fd9f5c81b33d04c80a`).

This is a snapshot of the working build tree, not a one-command build: no firmware file is included, and some inputs are not in the repo (the stock SELF and its decrypted ELF, the public `key_vault.cpp`, the PS2 IOP module `bbusbd_raw.irx`, built in the original workspace; `ps2/universal/bbusbd.irx` is the related ISO-patcher module). Paths inside the scripts point at the original workspace; adapt them.

## Contents

- `src/lights.c`, `src/overlay.c`, `src/raw.c`: the native PPC64 payload (lamp output, menu label overlay, raw dongle path).
- `build.py`: compiles the payload (`clang --target=powerpc64-unknown-linux-gnu -mcpu=970 -mabi=elfv2 -Oz -ffreestanding`), links it into the code slot, patches the hooks, repacks the SELF. Expects the decrypted stock ELF at `dependencies/stock.elf`.
- `selfdec.py`, `resign_self_installed.py`, `ppcasm.py`: SELF decrypt / repack and a tiny PPC assembler.
- `gh_run.py`, `gh_harness.py`, `gh_raw_tests.py`, `lamp_tests_l3.py`, `gh_store_scan.py`: PC validation. A pyghidra `EmulatorHelper` PPC64 harness runs the real final code with the original handlers, plus an audit that no store lands outside the proven writable memory.
- `PROTOCOL.md`: the private command-0x42 channel between the native code and the PS2-side module.

## Rules that matter

- Any store into the emulator's read-only memory powers the PS3 off. Run the store audit before shipping a build.
- The code slot (`0x4800..0x7FF0`) is almost full (14,304 of 14,320 bytes).
- The ECDSA field of the repacked SELF is zero: this works on CFW only.
- Internals, addresses and what does not work: see <https://github.com/Gabarsolon/ps2-netemu-disc> (`docs/NETEMU_KNOWLEDGE.md`).

The stock firmware file is Sony's. Do not commit it or its decrypted ELF.
