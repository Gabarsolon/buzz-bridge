"""Generate a GAMES entry and a PCSX2 cheat file for another Buzz! game version.

usage: python tools/new_game.py <unpatched.iso> --trc 0x... --poll 0x... --pad1 0x... --pad2 0x...
                                [--title "..."] [--serial SLES-xxxxx] [--version "..."] [--out DIR] [--sha1]

Finding the four addresses is described in docs/PORTING.md.
"""
import argparse
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import buzz_pad_bridge as b  # noqa: E402

JR_RA = 0x03E00008


def num(s):
    return int(s, 0)


def main():
    ap = argparse.ArgumentParser(description='Generate a Buzz! Pad Bridge entry for a new game version')
    ap.add_argument('iso')
    ap.add_argument('--trc', type=num, required=True, help='buzzers-connected check function')
    ap.add_argument('--poll', type=num, required=True, help='buzzer poll function')
    ap.add_argument('--pad1', type=num, required=True, help='scePadRead data of port 1')
    ap.add_argument('--pad2', type=num, required=True, help='scePadRead data of port 2')
    ap.add_argument('--title', default='Buzz! <title> (<region>)')
    ap.add_argument('--serial', default='')
    ap.add_argument('--version', default='')
    ap.add_argument('--out', default='.', help='folder for the <CRC>.pnach file')
    ap.add_argument('--sha1', action='store_true', help='also hash the whole ISO (slow) for the Redump check')
    a = ap.parse_args()

    new = b.trc_words() + b.bridge_words(a.pad1, a.pad2)
    addrs = [a.trc + 4 * i for i in range(3)] + [a.poll + 4 * i for i in range(len(new) - 3)]

    with open(a.iso, 'rb') as f:
        elf, iso_offset = b.open_game(f)
        elf_off, elf_size = b.find_file(f, elf)
        f.seek(elf_off)
        elf_data = f.read(elf_size)

        def word(va):
            f.seek(iso_offset(va))
            return struct.unpack('<I', f.read(4))[0]

        orig = [word(va) for va in addrs]
        if orig == new:
            raise SystemExit('This ISO is already patched at these addresses. Use an unpatched ISO.')
        for name, va in (('trc', a.trc), ('poll', a.poll)):
            if word(va) >> 16 != 0x27BD:
                print(f'warning: --{name} {va:#x} does not start with a stack-frame setup (addiu sp,sp,-N); '
                      'is it really the start of the function?')
        # The bridge must fit inside the original poll function (up to its jr ra delay slot).
        end = None
        for va in range(a.poll, a.poll + 0x400, 4):
            if word(va) == JR_RA:
                end = va + 4
                break
        last = a.poll + 4 * (len(new) - 4)
        if end is None or last > end:
            raise SystemExit(f'The bridge ({len(new) - 3} words) does not fit before the poll function returns '
                             f'(jr ra delay slot at {end and hex(end)}). This game needs a code cave; see PORTING.md.')

        sha1 = ''
        if a.sha1:
            f.seek(0)
            h = hashlib.sha1()
            for chunk in iter(lambda: f.read(16 << 20), b''):
                h.update(chunk)
            sha1 = h.hexdigest()

    crc = 0
    for (w,) in struct.iter_unpack('<I', elf_data[:len(elf_data) // 4 * 4]):
        crc ^= w

    print('Add this to GAMES in buzz_pad_bridge.py:\n')
    print(f"    dict(title={a.title!r}, serial={a.serial!r}, version={a.version!r},")
    print(f"         redump_sha1={sha1!r}, elf={elf!r},")
    print(f"         trc={a.trc:#08X}, poll={a.poll:#08X}, pad1={a.pad1:#08X}, pad2={a.pad2:#08X},")
    words = ', '.join(f'0x{w:08X}' for w in orig)
    print(f"         orig=[{words}]),\n")

    pnach = os.path.join(a.out, f'{crc:08X}.pnach')
    with open(pnach, 'w', newline='\r\n') as p:
        p.write(f'gametitle={a.title} [{a.serial or elf}] {a.version}\n')
        p.write(f'comment=Buzz! Pad Bridge {b.VERSION} test build\n')
        for va, w in zip(addrs, new):
            p.write(f'patch=1,EE,{va:08X},word,{w:08X}\n')
    print(f'PCSX2 cheat file for testing: {pnach} (PCSX2 CRC {crc:08X})')


if __name__ == '__main__':
    main()
