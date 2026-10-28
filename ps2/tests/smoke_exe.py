"""Check the locally built exe using synthetic data only. No PyInstaller import needed.

Run from the release repository root: python ps2/tests/smoke_exe.py
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PS2 = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PS2 / 'universal' / 'tests'))
from test_patcher import build_iso  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=PS2 / 'buzz-bridge-ps2.exe',
                        help='test a staged build when the default exe is in use')
    exe = parser.parse_args().exe.resolve()
    if not exe.is_file():
        raise SystemExit('Build ps2/buzz-bridge-ps2.exe first.')
    base = PS2 / 'build' / 'exe-smoke'
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=base) as directory:
        work = Path(directory)
        iso = work / 'Synthetic unknown.iso'
        build_iso(iso, 'IOP', 34985)
        before = iso.read_bytes()
        runtime = work / 'runtime'
        runtime.mkdir()
        env = dict(os.environ, TEMP=str(runtime), TMP=str(runtime))

        def invoke(argv, expected, fragment, stdin=None):
            proc = subprocess.run([str(exe), *map(str, argv)], input=stdin, text=True,
                                  capture_output=True, cwd=work, env=env, timeout=60,
                                  creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            assert proc.returncode == expected, (argv, proc.returncode, proc.stdout, proc.stderr)
            assert fragment in proc.stdout + proc.stderr, (argv, proc.stdout, proc.stderr)
            assert '\033' not in proc.stdout + proc.stderr, 'redirected output contains ANSI'
            assert 'Traceback' not in proc.stdout + proc.stderr
            if any(str(arg).startswith('--') for arg in argv):
                assert 'Press Enter to close.' not in proc.stdout
            assert iso.read_bytes() == before, 'synthetic input was changed'
            assert not list(work.glob('*.bbpatch.json'))
            assert not list(work.glob('*.usbd.orig'))
            assert len(list(work.glob('*.iso'))) == 1
            assert not list(runtime.iterdir()), 'runtime extraction was not cleaned up'
            print('PASS', ' '.join(map(str, argv)))

        invoke(['--help'], 0, '--whole-ds')
        invoke(['--list-layouts'], 0, 'custom')
        invoke([iso, '--info'], 0, 'UNKNOWN version')
        # Unknown refusal occurs after the bundled module's structural checks,
        # proving that frozen imports and the embedded IRX are resolved.
        invoke([iso, '--layout', '2'], 1, 'unknown USBD.IRX version')
        invoke([iso, '--restore'], 1, 'recovery JSON missing')
        invoke([iso, '--buzzer-pairs', '1', '--whole-ds', '8'], 1, 'maximum 8')
        invoke([iso, '--shared-ds', '2', '--layout', '0'], 2, 'cannot be combined with --layout')
        invoke([iso, '--shared-ds', '2'], 1, 'unknown USBD.IRX version')
        invoke([iso], 1, 'Press Enter to close.', stdin='\n')
        extraction = 'This looks like a file inside a .zip. Extract the ISO first, then drag the extracted .iso here.'
        invoke([work / 'archive.ZIP', '--layout', '2'], 1, extraction)
        invoke([str(work / 'archive.zip') + '\\Game.iso', '--info'], 1, extraction)
        invoke([work / 'Missing.iso'], 1, extraction, stdin='\n')
        invoke([], 1, 'Press Enter to close.', stdin='"%s"\n\n' % iso)
    print('13 frozen-exe smoke checks passed (synthetic ISO only).')


if __name__ == '__main__':
    main()
