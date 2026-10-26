"""Extract only a user's EBOOT/PARAM.SFO; random-access ISO9660 + Redump CBC.

No emulator, disc-key derivation, downloads or execution of extracted code.
The existing APP SELF decoder and installed PyCryptodome are used.
"""
import argparse
import io
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'plugin/tools'))
from self10 import decode

SECTOR = 2048


def disc_key(path):
    value = pathlib.Path(path).read_text(encoding='ascii').strip()
    if not re.fullmatch(r'[0-9a-fA-F]{32}', value):
        raise ValueError('.dkey must contain exactly 32 hex characters (the AES disc key)')
    return bytes.fromhex(value)


class Iso:
    """Read bounded ISO extents, decrypting individual encrypted sectors."""
    def __init__(self, stream, key=None):
        self.stream, self.key = stream, key
        stream.seek(0, 2)
        self.size = stream.tell()
        if self.size % SECTOR or self.size < 18*SECTOR:
            raise ValueError('truncated or non-2048-byte-sector ISO')
        self.regions = []
        if key is not None:
            if len(key) != 16:
                raise ValueError('disc key must be 16 bytes')
            stream.seek(0)
            sector0 = stream.read(SECTOR)
            count = int.from_bytes(sector0[:4], 'big')
            if not 1 <= count <= 32:
                raise ValueError('invalid PS3 plaintext-region count')
            for i in range(count):
                lo, hi = struct.unpack_from('>II', sector0, 8+i*8)
                if lo > hi or (hi+1)*SECTOR > self.size or (self.regions and lo <= self.regions[-1][1]):
                    raise ValueError('invalid/overlapping PS3 plaintext regions')
                self.regions.append((lo, hi))
            if self.regions[0][0] != 0:
                raise ValueError('PS3 header must be plaintext')
        pvd = None
        for lba in range(16, min(80, self.size//SECTOR)):
            d = self.read(lba*SECTOR, SECTOR)
            if d[1:6] != b'CD001' or d[6] != 1:
                raise ValueError('invalid ISO9660 volume descriptor (wrong key or unsupported ISO)')
            if d[0] == 1:
                pvd = d
            if d[0] == 255:
                break
        if pvd is None or int.from_bytes(pvd[128:130], 'little') != SECTOR:
            raise ValueError('missing primary ISO9660 volume / unsupported block size')
        self.root = self.record(pvd[156:190])

    def read(self, offset, length):
        if offset < 0 or length < 0 or offset+length > self.size:
            raise ValueError('ISO extent outside image')
        out = bytearray()
        first, last = offset//SECTOR, (offset+length+SECTOR-1)//SECTOR
        for sector in range(first, last):
            self.stream.seek(sector*SECTOR)
            data = self.stream.read(SECTOR)
            if len(data) != SECTOR:
                raise ValueError('short ISO read')
            if self.key is not None and not any(lo <= sector <= hi for lo,hi in self.regions):
                from Crypto.Cipher import AES
                data = AES.new(self.key, AES.MODE_CBC, sector.to_bytes(16, 'big')).decrypt(data)
            out.extend(data)
        return bytes(out[offset % SECTOR:offset % SECTOR+length])

    @staticmethod
    def record(data):
        if len(data) < 34 or data[0] < 34 or data[0] > len(data):
            raise ValueError('malformed ISO directory record')
        extent, length = struct.unpack_from('<II', data, 2)[0], struct.unpack_from('<I', data, 10)[0]
        if extent != int.from_bytes(data[6:10], 'big') or length != int.from_bytes(data[14:18], 'big'):
            raise ValueError('ISO both-endian fields disagree')
        if data[1] or data[26] or data[27] or data[25] & 0x80:
            raise ValueError('extended-attribute/interleaved/multi-extent files are unsupported')
        n = data[32]
        if 33+n > data[0]:
            raise ValueError('truncated ISO filename')
        return (extent, length, data[25], data[33:33+n])

    def find(self, path):
        current = self.root
        for part in path.split('/'):
            if not current[2] & 2 or current[1] > 16*1024*1024:
                raise ValueError('not a bounded ISO directory')
            data = self.read(current[0]*SECTOR, current[1])
            found = []
            at = 0
            while at < len(data):
                n = data[at]
                if not n:
                    at = (at//SECTOR+1)*SECTOR
                    continue
                if n < 34 or at+n > len(data) or at % SECTOR+n > SECTOR:
                    raise ValueError('invalid directory record length')
                record = self.record(data[at:at+n])
                if record[3].split(b';',1)[0].decode('ascii',errors='replace').upper() == part.upper():
                    found.append(record)
                at += n
            if len(found) != 1:
                raise ValueError(f'{path}: component {part} found {len(found)} times')
            current = found[0]
        if current[2] & 2 or current[1] > 256*1024*1024:
            raise ValueError('expected bounded regular file')
        return self.read(current[0]*SECTOR, current[1])


def extract_bytes(source, key=None):
    """Return two files in memory; also accepts a mounted disc/folder dump."""
    source = pathlib.Path(source)
    if source.is_dir():
        game = source/'PS3_GAME' if (source/'PS3_GAME').is_dir() else source
        return (game.joinpath('USRDIR/EBOOT.BIN').read_bytes(), game.joinpath('PARAM.SFO').read_bytes())
    with source.open('rb') as stream:
        iso = Iso(stream, key)
        return iso.find('PS3_GAME/USRDIR/EBOOT.BIN'), iso.find('PS3_GAME/PARAM.SFO')


def decrypt_eboot(data, keys=ROOT/'research/load10/key_vault.cpp'):
    if data[:4] != b'SCE\0':
        raise ValueError('EBOOT is not a SELF: wrong ISO mode/key or unsupported input')
    result = decode(data, keys)
    if not all(result['hmac_checks']):
        raise ValueError('SELF section HMAC failed')
    return result['elf']


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=pathlib.Path, help='own ISO, mounted disc, or folder dump')
    ap.add_argument('--dkey', type=pathlib.Path, help='Redump disc key; omit for an already decrypted ISO')
    ap.add_argument('--output', required=True, type=pathlib.Path, help='private directory for game files; never the plugin checkout')
    ap.add_argument('--keys', type=pathlib.Path, default=ROOT/'research/load10/key_vault.cpp')
    args = ap.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        ap.error('game data must stay outside the plugin work folder')
    try:
        eboot, sfo = extract_bytes(args.source, disc_key(args.dkey) if args.dkey else None)
        if sfo[:4] != b'\0PSF':
            raise ValueError('PARAM.SFO magic failed: check ISO mode/key')
        elf = decrypt_eboot(eboot, args.keys)
        if any((output/name).exists() for name in ('EBOOT.BIN','PARAM.SFO','EBOOT.elf')):
            raise ValueError('output files already exist; choose an empty private directory')
        output.mkdir(parents=True, exist_ok=True)
        for name,data in [('EBOOT.BIN',eboot),('PARAM.SFO',sfo),('EBOOT.elf',elf)]:
            with (output/name).open('xb') as f:
                f.write(data)
            print(f'{name}: {len(data)} bytes')
    except (ValueError, OSError, AssertionError, StopIteration, struct.error) as exc:
        ap.exit(1, f'REFUSE: {exc}\n')


if __name__ == '__main__':
    main()
