"""Static checks of the built IRX (no emulator needed).

  python -I tools/check_irx.py bbusbd.irx [--sony inputs/schools_quiz/USBD.IRX] [--ioprp inputs/schools_quiz/IOPRP300.IMG]

Checks:
  * ELF header: little-endian 32-bit MIPS, type 0xFF80 (IOP module)
  * exactly one export table "usbd" version 1.2 with 19 slots, same slot count as Sony's USBD
  * the nine functions BUZZER.IRX imports (4-11, 13) are non-empty slots
  * every import table names a library+version that exists in the game's IOP image (IOPRP image,
    or the BIOS libraries intrman/loadcore) and every imported index is a non-empty slot there
  * the patch-time configuration magic occurs exactly once and parses
"""
import argparse
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import irxlib  # noqa: E402

BUZZER_IMPORTS = [4, 5, 6, 7, 8, 9, 10, 11, 13]
CFG_MAGIC = b'BBZCFG01'

# libraries that live in the BIOS ROM, not in the IOPRP image (index tables of ps2sdk headers)
BIOS_LIBS = {
    'intrman': (0x0102, {17, 18}),   # CpuSuspendIntr, CpuResumeIntr (BUZZER imports the same)
}


def fail(msg):
    print('FAIL:', msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('irx')
    ap.add_argument('--sony')
    ap.add_argument('--ioprp', action='append', default=[])
    a = ap.parse_args()

    data = Path(a.irx).read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        fail('not a little-endian ELF32')
    etype, machine = struct.unpack_from('<HH', data, 0x10)
    if etype != 0xFF80:
        fail('ELF type %#x, expected IOP module 0xFF80' % etype)
    if machine != 8:
        fail('machine %d, expected MIPS' % machine)
    print('ELF ok, %d bytes, type %#x' % (len(data), etype))

    exp, imp = irxlib.parse_tables(data)
    if list(exp) != ['usbd']:
        fail('export tables: %r' % list(exp))
    ver, slots = exp['usbd']
    if ver != 0x0102:
        fail('usbd export version %#x' % ver)
    print('export usbd %#x, %d slots' % (ver, len(slots)))
    if len(slots) != 19:
        fail('expected 19 slots, got %d' % len(slots))
    for i in BUZZER_IMPORTS:
        if slots[i] == 0:
            fail('slot %d empty' % i)
    if a.sony:
        s_exp, _ = irxlib.parse_tables(Path(a.sony).read_bytes())
        s_ver, s_slots = s_exp['usbd']
        print('Sony', os.path.basename(a.sony), '%#x' % s_ver, len(s_slots), 'slots')
        if (s_ver, len(s_slots)) != (ver, len(slots)):
            fail('export table differs from Sony USBD in version/size')

    lib_exports = dict(BIOS_LIBS)
    for img in a.ioprp:
        for name, blob in irxlib.romdir_modules(Path(img).read_bytes()):
            if blob[:4] == b'\x7fELF':
                e, _ = irxlib.parse_tables(blob)
                for lname, (lver, lslots) in e.items():
                    lib_exports[lname] = (lver, {i for i, x in enumerate(lslots) if x})
    for lname, (lver, fnos) in imp.items():
        fnos = sorted(set(fnos))
        print('import %-9s v%#06x idx %s' % (lname, lver, fnos))
        if lname not in lib_exports:
            if a.ioprp:
                fail('library %s not provided by the game IOP image' % lname)
            continue
        ever, eslots = lib_exports[lname]
        if (ever >> 8) != (lver >> 8) or (ever & 0xFF) < (lver & 0xFF):
            fail('%s: exporter %#x cannot serve importer %#x' % (lname, ever, lver))
        missing = [f for f in fnos if f not in eslots]
        if missing:
            fail('%s: imported slots %s are empty/absent in the exporter' % (lname, missing))
        print('   provided by ROM/API or supplied image: v%#06x, all slots present' % ever)

    n = data.count(CFG_MAGIC)
    if n != 1:
        fail('config magic occurs %d times' % n)
    off = data.find(CFG_MAGIC)
    ver_, layout, flags, _res, delay, hb = struct.unpack_from('<BBBBHH', data, off + 8)
    print('config @%#x: version %d layout %d flags %#x attach %d ms heartbeat %d ms' %
          (off, ver_, layout, flags, delay, hb))
    print('ALL OK')


if __name__ == '__main__':
    main()
