"""Buzz! Pad Bridge - patch your own copy of a PS2 Buzz! game so it reads the
buzzers from the controller port.

The PS3's PS2 software emulator (ps2_netemu, used by every slim and super slim)
does not hand the Buzz! USB dongle to the game; it turns it into a DualShock.
This patch makes the game accept that controller as buzzers.

usage:
  buzz_pad_bridge.py <game.iso>             patch the ISO in place
  buzz_pad_bridge.py <game.iso> --check     only report whether it is patched
  buzz_pad_bridge.py <game.iso> --restore   undo the patch
"""
import argparse
import re
import struct
import sys

VERSION = '0.1.0'
SECTOR = 2048

# One entry per supported game version. See docs/PORTING.md to add one.
#   trc   : the "are the buzzers connected" check behind the "buzzers not detected" screen
#   poll  : the buzzer poll that asks the IOP buzzer driver for reports; replaced by the bridge
#   pad1/2: scePadRead data of controller ports 1 and 2
#   orig  : original words at trc (3) and poll (len(bridge_words)), used to verify the version
GAMES = [
    dict(title='Buzz! The Schools Quiz (UK)', serial='SCES-54941', version='Original v2.00',
         redump_sha1='b3862f09cf0be9caa1ccf88dae5ab8de6691b31b', elf='SCES_549.41',
         trc=0x202A60, poll=0x275B00, pad1=0x4D5F20, pad2=0x4D5FB4,
         orig=[0x27BDFF70, 0x3C02004B, 0x7FB40040,
               0x27BDFFC0, 0x7FB00030, 0x0080802D, 0x7FB10020, 0x7FB20010, 0xFFBF0000, 0x0C09D664, 0x24110001,
               0x0000902D, 0x26030024, 0x24040001, 0x00000000, 0x80620000, 0x2484FFFF, 0x2463002C, 0x00021027,
               0x0481FFFB, 0x0002880B, 0x16200016, 0x0000102D, 0x26030020, 0x26040078, 0x8C620000, 0x00000000,
               0x50400004, 0x2463002C, 0xAC600000, 0x24120001, 0x2463002C, 0x0064102A, 0x5440FFF9, 0x8C620000,
               0x12400005, 0x00000000, 0x0C09D78C, 0x0200202D, 0x10000004, 0x24020001, 0x0C09D724, 0x0200202D,
               0x24020001, 0x7BB00030, 0x7BB10020, 0x7BB20010, 0xDFBF0000, 0x03E00008, 0x27BD0040]),
]


def trc_words():
    """sw zero,0x30(a0) / jr ra / addiu v0,zero,1: report the buzzers as connected."""
    return [0xAC800030, 0x03E00008, 0x24020001]


def bridge_words(pad1, pad2):
    """MIPS code replacing the buzzer poll (a0 = buzzer state).

    Marks device 0 connected, reads both controller ports, maps the buttons with
    ps2_netemu's Buzz! layout back to buzzer bits (handsets 1-2, handset 3 red/yellow),
    adds the handset-present flags (bits 20-22) and stores the report.
    """
    hi = (pad1 + 0x8000) >> 16

    def lo(addr):
        off = addr - (hi << 16)
        if not -0x8000 <= off < 0x8000:
            raise ValueError('pad1 and pad2 must lie within 32 KB of each other')
        return off & 0xFFFF

    return [0xA0800024, 0x24020001, 0xAC800058, 0xAC820000,            # +0x24=0, +0x58=0 (device 0), +0x00=1
            0x3C080000 | hi, 0x0000482D,                                # lui t0,hi ; t1 = 0
            0x910C0000 | lo(pad1 + 1), 0x11800002,                     # pad 1 present?
            0x950A0000 | lo(pad1 + 2), 0x3949FFFF,                     # t1 = ~buttons
            0x910C0000 | lo(pad2 + 1), 0x11800003,                     # pad 2 present?
            0x950A0000 | lo(pad2 + 2), 0x394AFFFF, 0x012A4825,         # t1 |= ~buttons
            0x000953C2, 0x31430001,                                     # SQUARE -> bit 0
            0x00095342, 0x314A0002, 0x006A1825,                         # CROSS -> 1
            0x000952C2, 0x314A0004, 0x006A1825,                         # CIRCLE -> 2
            0x00095242, 0x314A0008, 0x006A1825,                         # TRIANGLE -> 3
            0x00095182, 0x314A0030, 0x006A1825,                         # L1, R1 -> 4, 5
            0x00095082, 0x314A00C0, 0x006A1825,                         # L2, R2 -> 6, 7
            0x00095200, 0x314A0100, 0x006A1825,                         # SELECT -> 8
            0x00095180, 0x314A0200, 0x006A1825,                         # START -> 9
            0x00095240, 0x314A0C00, 0x006A1825,                         # L3, R3 -> 10, 11
            0x3C0A0070, 0x006A1825,                                     # handsets 1-3 present
            0xAC830004, 0xAC830018,                                     # report slot and latest report
            0x03E00008, 0x24020001]                                     # return 1


def patch_table(game):
    new = trc_words() + bridge_words(game['pad1'], game['pad2'])
    addrs = [game['trc'] + 4 * i for i in range(3)] + [game['poll'] + 4 * i for i in range(len(new) - 3)]
    return list(zip(addrs, game['orig'], new))


def read_dir(f, lba, size):
    f.seek(lba * SECTOR)
    data = f.read(size)
    i = 0
    while i < len(data):
        ln = data[i]
        if ln == 0:
            i = (i // SECTOR + 1) * SECTOR
            continue
        rec = data[i:i + ln]
        nl = rec[32]
        name = rec[33:33 + nl].split(b';')[0].decode('latin-1').upper()
        yield name, struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        i += ln


def find_file(f, name):
    """Return (byte offset, size) of a file in the ISO9660 root directory."""
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    if pvd[1:6] != b'CD001':
        raise SystemExit('Not an ISO9660 image (no CD001 volume descriptor).')
    root = pvd[156:156 + 34]
    for n, lba, size in read_dir(f, struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]):
        if n == name.upper():
            return lba * SECTOR, size
    raise SystemExit(f'{name} not found in the ISO.')


def boot_elf_name(f):
    off, size = find_file(f, 'SYSTEM.CNF')
    f.seek(off)
    m = re.search(rb'BOOT2\s*=\s*cdrom0:\\?([^;\r\n]+)', f.read(size))
    if not m:
        raise SystemExit('SYSTEM.CNF has no BOOT2 line; not a PS2 game ISO.')
    return m.group(1).decode('latin-1').strip().upper()


def elf_load_segment(f, elf_off):
    """Return (vaddr, file offset inside the ELF, file size) of the first PT_LOAD segment."""
    f.seek(elf_off)
    hdr = f.read(0x34)
    if hdr[:4] != b'\x7fELF':
        raise SystemExit('The boot file is not an ELF.')
    phoff, = struct.unpack_from('<I', hdr, 0x1C)
    phnum, = struct.unpack_from('<H', hdr, 0x2C)
    for i in range(phnum):
        f.seek(elf_off + phoff + i * 32)
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack('<IIIII', f.read(20))
        if p_type == 1:
            return p_vaddr, p_offset, p_filesz
    raise SystemExit('The boot ELF has no loadable segment.')


def open_game(f):
    """Locate the boot ELF; return (elf name, function mapping an EE address to an ISO offset)."""
    elf = boot_elf_name(f)
    elf_off, _ = find_file(f, elf)
    vaddr, seg_off, seg_size = elf_load_segment(f, elf_off)

    def iso_offset(va):
        if not vaddr <= va < vaddr + seg_size:
            raise SystemExit(f'Address {va:#x} is outside {elf}.')
        return elf_off + seg_off + (va - vaddr)
    return elf, iso_offset


def word_states(f, iso_offset, table):
    states = []
    for va, orig, new in table:
        f.seek(iso_offset(va))
        cur, = struct.unpack('<I', f.read(4))
        states.append('both' if orig == new == cur else 'orig' if cur == orig else 'new' if cur == new else 'other')
    return states


def main():
    ap = argparse.ArgumentParser(description=f'Buzz! Pad Bridge {VERSION}')
    ap.add_argument('iso', nargs='?', help='your Buzz! game ISO')
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='only report the patch state')
    mode.add_argument('--restore', action='store_true', help='undo the patch')
    args = ap.parse_args()
    iso = args.iso or input('Drag your Buzz! game ISO here and press Enter: ').strip().strip('"')

    try:
        f = open(iso, 'rb' if args.check else 'r+b')
    except PermissionError:
        raise SystemExit('Cannot open the ISO for writing. Close PCSX2 (or anything else using it) and try again.')
    with f:
        elf, iso_offset = open_game(f)
        match = None
        for game in (g for g in GAMES if g['elf'] == elf):
            table = patch_table(game)
            states = word_states(f, iso_offset, table)
            if 'other' not in states:
                match = game, table, states
                break
        if not match:
            supported = ', '.join(f"{g['title']} {g['version']}" for g in GAMES)
            raise SystemExit(f'Unsupported game or version ({elf}). Nothing was changed.\n'
                             f'Supported: {supported}. See docs/PORTING.md to add yours.')
        game, table, states = match
        print(f"{game['title']} ({game['serial']}, {game['version']})")
        patched = all(s in ('new', 'both') for s in states)
        clean = all(s in ('orig', 'both') for s in states)

        if args.check:
            print('Patched.' if patched else 'Not patched.' if clean else 'Partly patched; run again to finish.')
            return
        if args.restore:
            if clean:
                print('Already original. Nothing to do.')
                return
            for va, orig, _ in table:
                f.seek(iso_offset(va))
                f.write(struct.pack('<I', orig))
            print('Restored the original game code.')
            return
        if patched:
            print('Already patched. Nothing to do.')
            return
        for va, _, new in table:
            f.seek(iso_offset(va))
            f.write(struct.pack('<I', new))
        f.flush()
        if word_states(f, iso_offset, table).count('other'):
            raise SystemExit('Verification failed. Run with --restore and try again.')
        print(f'Patched {len(table)} words. Copy the ISO to your PS3 (/dev_hdd0/PS2ISO/) or load it in PCSX2.')


if __name__ == '__main__':
    try:
        main()
    finally:
        if getattr(sys, 'frozen', False):
            input('\nPress Enter to close.')
