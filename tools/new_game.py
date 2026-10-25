"""Inspect auto-detection and generate a PCSX2 test cheat, reading the ISO only.

usage: python tools/new_game.py <unpatched.iso> --out DIR
Manual research fallback: supply all of --trc --poll --pad1 --pad2.
An emitted GAMES candidate must only be added after hardware/PCSX2 testing.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import buzz_pad_bridge as b  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('iso')
    for name in ('trc', 'poll', 'pad1', 'pad2'):
        ap.add_argument('--' + name, type=lambda s: int(s, 0), help='manual research address')
    ap.add_argument('--title', default='Buzz! auto-detected test')
    ap.add_argument('--serial', default='')
    ap.add_argument('--version', default='auto-detected, untested')
    ap.add_argument('--out', default='.', help='folder for <CRC>.pnach')
    ap.add_argument('--sha1', action='store_true', help='hash the full ISO by streaming reads')
    a = ap.parse_args()
    manual = [getattr(a, key) for key in ('trc', 'poll', 'pad1', 'pad2')]
    if any(x is not None for x in manual) and not all(x is not None for x in manual):
        ap.error('Manual mode needs all four addresses')
    with open(a.iso, 'rb') as f:
        elf, _, data = b.read_boot_elf(f)
        image = b.ElfImage(data)
        if all(x is not None for x in manual):
            print('MANUAL RESEARCH BUILD: addresses/layout have not been structurally validated. '
                  'Review the disassembly before using the cheat. The ISO patcher still requires detection.')
            game = dict(trc=a.trc, poll=a.poll, pad1=a.pad1, pad2=a.pad2)
            new = b.trc_words() + b.bridge_words(a.pad1, a.pad2)
            addresses = [a.trc + i * 4 for i in range(3)] + [a.poll + i * 4 for i in range(len(new)-3)]
            game['orig'] = [image.words(va, 1)[0] for va in addresses]
        else:
            game = b.detect_game(data, elf)
            print(f'Auto-detected {elf}: UNTESTED until hardware/PCSX2 testing.')
            print('Layout:', game['layout'], f"singleton={game['singleton']:#x}")
            print('Evidence:', ', '.join(f'{k}={v:#x}' for k, v in game['evidence'].items()))
        table = b.patch_table(game)
        for name in ('trc', 'poll'):
            word = image.words(game[name], 1)[0]
            b.require(word >> 16 == 0x27BD and b.signed16(word) < 0,
                      f'{name} does not start with a stack-frame allocation; use an original ELF')
        end = None
        for i in range(0x100):
            if image.words(game['poll'] + i * 4, 1)[0] == 0x03E00008:
                end = game['poll'] + (i + 2) * 4
                break
        b.require(end is not None and game['poll'] + (len(table)-3) * 4 <= end,
                  'Bridge does not fit before the poll return delay slot; no code cave is used')
        sha1 = ''
        if a.sha1:
            f.seek(0)
            h = hashlib.sha1()
            for chunk in iter(lambda: f.read(16 << 20), b''):
                h.update(chunk)
            sha1 = h.hexdigest()

    crc = 0
    for (w,) in struct.iter_unpack('<I', data[:len(data)//4*4]):
        crc ^= w
    print('Candidate for GAMES ONLY AFTER a successful hardware/PCSX2 test:\n')
    print(f"    dict(title={a.title!r}, serial={a.serial or elf!r}, version={a.version!r},")
    print(f"         redump_sha1={sha1!r}, elf={elf!r},")
    print('         ' + ', '.join(f'{key}={game[key]:#08X}' for key in ('trc', 'poll', 'pad1', 'pad2')) + ',')
    if 'layout' in game:
        print(f"         counter={game['counter']:#x}, layout={game['layout']!r},")
    print('         orig=[' + ', '.join(f'0x{old:08X}' for va, old, new in table) + ']),\n')
    pnach = Path(a.out) / f'{crc:08X}.pnach'
    with pnach.open('w', newline='\n', encoding='utf-8') as p:
        p.write(f'gametitle={a.title} [{a.serial or elf}] {a.version}\n')
        p.write(f'comment=Buzz! Pad Bridge {b.VERSION}; UNTESTED test build\n')
        for va, old, new in table:
            p.write(f'patch=1,EE,{va:08X},word,{new:08X}\n')
    print(f'PCSX2 cheat file: {pnach} (PCSX2 CRC {crc:08X})')


if __name__ == '__main__':
    try:
        main()
    except (b.DetectionError, OSError, struct.error) as e:
        raise SystemExit(f'Refused: {e}') from e
