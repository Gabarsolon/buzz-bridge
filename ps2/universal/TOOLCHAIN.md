# Building and checking Buzz! Bridge PS2

The Python patcher uses **Python 3.9+ standard library only**. Running it requires no PS2 SDK, C compiler or package install. The supplied `bbusbd.irx` is the 15,457-byte module copied from the universal project. Relative resource paths work both from source and inside the PyInstaller exe.

## Python and module checks

From the release repository root:

```powershell
python -W error::ResourceWarning -m unittest discover -s ps2/tests -v
python -I -W error::ResourceWarning ps2/universal/tools/check_irx.py ps2/universal/bbusbd.irx
```

Or, from `ps2/universal/`, run `python -I -W error::ResourceWarning tests/test_patcher.py`. All ISO data is synthetic. Tests temporarily allow synthetic hashes only within the test process and check that the real CLI refuses those hashes. No game files, environment-supplied game inputs, emulator or console are needed.

The static check covers ELF type/machine, the 19-slot `usbd` 1.2 export table, required non-empty slots, import names and the configuration marker. Without an explicitly supplied IOP image it does not establish that all imports exist in a game; do not confuse this with runtime compatibility.

## Native host tests (no PS2 toolchain)

Use an **already installed** C compiler. From `ps2/universal/` in a shell with GCC on PATH:

```sh
sh tests/run_native.sh
```

PowerShell, from the repository root:

```powershell
./ps2/universal/tests/run_native.ps1
# If gcc is not on PATH, select your existing compiler:
./ps2/universal/tests/run_native.ps1 -Compiler 'path/to/gcc.exe'
```

This compiles `tests/test_native.c`, `src/bb_core.c` and `src/vusb.c` with `-std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter`. It checks layouts, USB descriptors/state, SIO2 parsing and API fuzzing; outputs stay in `universal/build/`. It does not execute IOP code or start an emulator.

## Rebuild the IOP module

Use an existing **ps2dev** installation with the IOP compiler, ps2sdk and GNU Make. The prototype's recorded versions were ps2dev bundle **v2.0.0**, IOP GCC **15.2.0**, and ps2sdk source reference **329e2bb43287e16af16c4dde1c614e2662c24468**. The recorded Windows environment used MSYS2 MINGW32. No toolchain is installed by any script here.

In that shell, set paths for **your own existing installation** (the SDK path must not contain spaces), then change to this `ps2/universal/` folder:

```sh
export PS2DEV=/path/to/your/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export GSKIT="$PS2DEV/gsKit"
export PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin"
make
python -I tools/check_irx.py bbusbd.irx
```

`Makefile` builds exports/imports, `main`, `gpcall`, `vusb`, `bb_core` and `bbpad`, using the installed `samples/Makefile.pref` and `samples/Makefile.iopglobal`. `PS2SDK` must be set explicitly; no developer-specific path or private `build/env.sh` is required. Intermediate objects go in `obj/`; ELF intermediates are ignored. Keep the source, third-party notices and licenses with any distributed module.

## Build the Windows exe

The previous `buzz-pad-bridge.exe` was inspected locally: a PyInstaller single-file archive with a console PE subsystem. The new tool uses that same console/single-file form, with the IRX embedded as data.

From the repository root:

```powershell
./ps2/build_exe.ps1
# If local script execution is disabled, use a process-only policy:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ps2/build_exe.ps1
```

The script first checks `python -m PyInstaller --version`. If unavailable, it stops with a message; **it does not install anything**. It invokes `--onefile --console --name buzz-bridge-ps2`, embeds `universal/bbusbd.irx` plus license notices, and writes **`ps2/buzz-bridge-ps2.exe`**. The spec and intermediate files stay in `ps2/build/`. Python code is collected through normal imports; the frozen patcher uses its built-in layout list.

The exe prompts when given only an eligible ISO path, pauses at the end of a drag/drop invocation, and never pauses when flags such as `--layout`, `--info` or `--restore` are present. Test a build with `ps2/buzz-bridge-ps2.exe --list-layouts` or `--help`. Run `python ps2/tests/smoke_exe.py` from the repository root for six frozen-exe checks using only synthetic data. That script also keeps extraction temporary files inside `ps2/build/`. Synthetic unknown ISOs must be refused; no real ISO is needed to check packaging.

Build output is local and ignored by Git. There is no upload, commit or publishing step. Distribute the corresponding `universal/src/`, Makefile, this document, [THIRD_PARTY.md](THIRD_PARTY.md), both AFL license texts and the root MIT license with the tool.
