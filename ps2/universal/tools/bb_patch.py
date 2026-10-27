#!/usr/bin/env python3
"""Buzz! Bridge PS2: replace USBD.IRX, preserving the ISO layout. Stdlib only."""
import argparse
import base64
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile

try:
    from . import irxlib
    from .bb_console import Console, copy_with_progress, human_size
except ImportError:  # direct invocation from tools/
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import irxlib
    from bb_console import Console, copy_with_progress, human_size

HERE = Path(__file__).resolve().parent
SECTOR = 2048
CFG_MAGIC = b'BBZCFG01'
CFG_STRUCT = '<8sBBBBHH'
FLAG_VERBOSE, FLAG_NOPAD, FLAG_SCRIPT = 0x01, 0x02, 0x04
FLAG_POLL_AUTO, FLAG_POLL_ALWAYS = 0x10, 0x20
EMBEDDED_LAYOUTS = [
    (0, 'buzzers', 'Buzz! buzzers only'),
    (1, 'buzzers_ds', 'Buzz! buzzers + 1 DualShock (4 players)'),
    (2, 'two_ds_shared', '2 DualShocks, 2 players each (4 players)'),
    (3, 'two_ds_single', '2 DualShocks, 1 player each'),
    (4, 'one_ds_shared', '1 DualShock, 2 players'),
]
KNOWN_USBD = {
    'cbae284db35baceeaaed642351df99817ec47586': 'Sony USBD 1.3.0 (Schools/Mega family)',
    '80ebc1a552dffaf5640714b69bac4cbc173e3d22': 'Sony USBD 1.4.0 (Junior family)',
    '4a9d0d25b523f72f1be111bf9e0a8d88b96db873': 'Sony USBD 1.6.0 (Monster Rumble family)',
}
SEARCH_PATHS = ['/IOP/USBD.IRX', '/SYSTEM/USBD.IRX']
ZIP_HINT = 'This looks like a file inside a .zip. Extract the ISO first, then drag the extracted .iso here.'
PORTING_URL = 'https://github.com/Gabarsolon/buzz-bridge/blob/main/ps2/docs/PORTING.md'


class PatchError(Exception):
    pass


def load_layouts():
    hdr = HERE.parent / 'src' / 'bb_layouts.h'
    if hdr.exists():
        parsed = re.findall(r'BB_LAYOUT\(\s*(\d+)\s*,\s*(\w+)\s*,\s*"([^"]*)"\s*\)',
                            hdr.read_text(encoding='utf-8'))
        if parsed:
            return [(int(i), name, desc) for i, name, desc in parsed]
    return EMBEDDED_LAYOUTS


class Iso:
    """Bounded ISO9660 reader for plain 2048-byte-sector images."""
    def __init__(self, path):
        self.path = os.fspath(path)
        self.f = open(path, 'rb')
        try:
            self.size = os.fstat(self.f.fileno()).st_size
            pvd = self.read(16 * SECTOR, SECTOR)
            if pvd[:7] != b'\x01CD001\x01':
                raise PatchError('no ISO9660 primary volume descriptor at sector 16; '
                                 'only plain 2048-byte-sector ISOs are supported')
            self.root = self._record(pvd, 156)
            if not self.root['dir']:
                raise PatchError('invalid ISO9660 root directory')
            self.volume = pvd[40:72].decode('ascii', 'replace').strip()
        except BaseException:
            self.close()
            raise

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def read(self, off, n):
        if off < 0 or n < 0 or off + n > self.size:
            raise PatchError('ISO9660 extent/read past the end of the image')
        self.f.seek(off)
        data = self.f.read(n)
        if len(data) != n:
            raise PatchError('short ISO read')
        return data

    def _record(self, buf, off):
        if off >= len(buf):
            raise PatchError('truncated ISO9660 directory record')
        ln = buf[off]
        if ln < 34 or off + ln > len(buf) or off % SECTOR + ln > SECTOR:
            raise PatchError('invalid ISO9660 directory record length')
        lba, size = struct.unpack_from('<I', buf, off + 2)[0], struct.unpack_from('<I', buf, off + 10)[0]
        if (lba != struct.unpack_from('>I', buf, off + 6)[0] or
                size != struct.unpack_from('>I', buf, off + 14)[0]):
            raise PatchError('inconsistent ISO9660 both-endian extent')
        nlen = buf[off + 32]
        if 33 + nlen > ln or buf[off + 25] & 0x80 or buf[off + 26:off + 28] != b'\0\0':
            raise PatchError('invalid, multi-extent or interleaved ISO9660 file')
        if lba * SECTOR + size > self.size:
            raise PatchError('ISO9660 extent past the end of the image')
        return dict(len=ln, lba=lba, size=size, dir=bool(buf[off + 25] & 2),
                    name=buf[off + 33:off + 33 + nlen])

    def list_dir(self, rec):
        if rec['size'] > 16 * 1024 * 1024:
            raise PatchError('ISO9660 directory is too large')
        data = self.read(rec['lba'] * SECTOR, rec['size'])
        out, pos = [], 0
        while pos < len(data):
            if data[pos] == 0:
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            r = self._record(data, pos)
            pos += r['len']
            if r['name'] in (b'\x00', b'\x01'):
                continue
            r['upper'] = r['name'].decode('ascii', 'replace').upper().split(';')[0].rstrip('.')
            out.append(r)
        return out

    def find(self, path):
        rec = self.root
        parts = [p for p in path.upper().split('/') if p]
        for i, part in enumerate(parts):
            hits = [r for r in self.list_dir(rec)
                    if r['upper'] == part and r['dir'] == (i < len(parts) - 1)]
            if len(hits) > 1:
                raise PatchError('ambiguous ISO9660 path: ' + path)
            if not hits:
                return None
            rec = hits[0]
        return rec

    def file_bytes(self, rec):
        if rec['size'] > 2 * 1024 * 1024:
            raise PatchError('USBD.IRX is unexpectedly large')
        return self.read(rec['lba'] * SECTOR, rec['size'])


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def find_usbd(iso):
    return [(p, r) for p in SEARCH_PATHS if (r := iso.find(p)) is not None]


def select_usbd(iso, path=None):
    found = find_usbd(iso)
    if path:
        found = [(p, r) for p, r in found if p == path.upper()]
    if not found:
        raise PatchError('no USBD.IRX found under ' + ' or '.join(SEARCH_PATHS))
    if len(found) != 1:
        raise PatchError('multiple USBD.IRX files found; select one with --path')
    return found[0]


def check_original(data):
    if CFG_MAGIC in data:
        raise PatchError('USBD.IRX is already patched. Use --info or --restore first.')
    digest = sha1(data)
    if digest not in KNOWN_USBD:
        raise PatchError('unknown USBD.IRX version (SHA-1 %s). Refusing to patch; '
                         'report this hash and the game/region with --info output. '
                         'There is no force override.' % digest)
    return KNOWN_USBD[digest]


def check_new_irx(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', data, 16) != (0xFF80, 8):
        raise PatchError('replacement is not a little-endian ELF32 MIPS IRX')
    exp, _ = irxlib.parse_tables(data)
    if list(exp) != ['usbd'] or exp['usbd'][0] != 0x0102 or len(exp['usbd'][1]) != 19:
        raise PatchError('replacement must export usbd 1.2 with 19 slots')
    if data.count(CFG_MAGIC) != 1:
        raise PatchError('replacement must contain exactly one configuration block')
    cfg = read_config(data)
    if cfg['version'] != 1:
        raise PatchError('unsupported module configuration version')


def write_config(data, layout_id, flags, attach_ms, heartbeat_ms):
    off = data.index(CFG_MAGIC)
    blob = struct.pack(CFG_STRUCT, CFG_MAGIC, 1, layout_id, flags, 0, attach_ms, heartbeat_ms)
    return data[:off] + blob + data[off + len(blob):]


def read_config(data):
    off = data.index(CFG_MAGIC)
    _, ver, layout, flags, _, attach, hb = struct.unpack_from(CFG_STRUCT, data, off)
    return dict(version=ver, layout=layout, flags=flags, attach_delay_ms=attach, heartbeat_ms=hb)


def resolve_layout(value):
    for i, name, _ in load_layouts():
        if value in (str(i), name):
            return i
    raise PatchError('unknown layout: %s (use --list-layouts)' % value)


def ask_layout(console=None):
    console = console or Console()
    console.section(2, 'Your setup')
    print('What do you have?')
    for i, _, desc in EMBEDDED_LAYOUTS:
        if console.enabled:
            names = ('Buzz! buzzers only', 'Buzz! buzzers + 1 DualShock',
                     '2 DualShocks, shared', '2 DualShocks, one player each', '1 DualShock, shared')
            print('  %s  %-32s %d players' %
                  (console.paint(str(i + 1) + ')', '1;96'), names[i], (2, 4, 4, 2, 2)[i]), flush=True)
        else:
            print('%d) %s' % (i + 1, desc))
    while True:
        try:
            answer = input('Choose 1-5: ').strip()
        except EOFError:
            raise PatchError('no setup selected; use --layout for non-interactive use') from None
        if answer in ('1', '2', '3', '4', '5'):
            return int(answer) - 1
        print('Please enter a number from 1 to 5.')


def same_file(a, b):
    return (Path(a).resolve() == Path(b).resolve() or
            (os.path.exists(a) and os.path.exists(b) and os.path.samefile(a, b)))


def require_space(directory, needed):
    free = shutil.disk_usage(directory).free
    if free < needed:
        raise PatchError('not enough free disk space: need %d bytes, have %d bytes in %s' %
                         (needed, free, directory))


def validate_iso_path(path):
    if re.search(r'\.zip(?:[\\/]|$)', os.fspath(path), re.IGNORECASE) or not Path(path).exists():
        raise PatchError(ZIP_HINT)


def error_hint(error):
    message = str(error)
    if message == ZIP_HINT:
        return 'Use an extracted .iso file in a normal folder.'
    if 'free disk space' in message or getattr(error, 'errno', None) == errno.ENOSPC:
        return 'Free disk space or use --out "D:\\Games\\Patched.iso" on another drive.'
    if 'USBD.IRX version' in message or 'no USBD.IRX' in message:
        return 'Check ps2/docs/PORTING.md for supported games and reporting: ' + PORTING_URL
    if getattr(error, 'winerror', None) in (32, 33) or getattr(error, 'errno', None) == errno.EBUSY:
        return 'File in use: close any app using this ISO or output, then retry.'
    if isinstance(error, PermissionError) or getattr(error, 'errno', None) == errno.EROFS:
        return 'Check for a read-only file/folder; choose a writable --out folder and close apps using the ISO.'
    return 'Check the paths and options above; keep any recovery files beside the ISO.'


def side_paths(target):
    return Path(str(target) + '.usbd.orig'), Path(str(target) + '.bbpatch.json')


def save_recovery(target, path, rec, orig, patched, image_size):
    backup, side = side_paths(target)
    # Relative backup name: moving the ISO and both sidecars together preserves restoration.
    record = dict(tool='buzz-bridge-ps2', version=2, path=path, lba=rec['lba'],
                  length=len(orig), image_size=image_size, original_file=backup.name,
                  original_sha256=sha256(orig), patched_sha256=sha256(patched),
                  patched_data=base64.b64encode(patched).decode('ascii'), config=read_config(patched))
    made = []
    try:
        for dest, data in ((backup, orig), (side, json.dumps(record, indent=2).encode('utf-8'))):
            # Track even a partially written new file, but never delete an existing file.
            with open(dest, 'xb') as f:
                made.append(dest)
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
    except BaseException:
        for dest in made:
            dest.unlink()
        raise
    return made


def verify_recovery(target, path, rec, orig, patched, image_size):
    """Read back both durable sidecars before declaring success."""
    backup, side_path = side_paths(target)
    side = json.loads(side_path.read_text(encoding='utf-8'))
    if (backup.read_bytes() != orig or side['tool'] != 'buzz-bridge-ps2' or
            side['version'] != 2 or side['path'] != path or side['lba'] != rec['lba'] or
            side['length'] != len(orig) or side['image_size'] != image_size or
            side['original_file'] != backup.name or side['original_sha256'] != sha256(orig) or
            side['patched_sha256'] != sha256(patched) or
            base64.b64decode(side['patched_data'], validate=True) != patched or
            side['config'] != read_config(patched)):
        raise PatchError('recovery verification failed; keep recovery files and use --restore')


def write_extent(target, rec, before, after):
    with open(target, 'r+b') as f:
        off = rec['lba'] * SECTOR
        f.seek(off)
        if f.read(len(before)) != before:
            raise PatchError('USBD.IRX changed during inspection; refusing to write')
        f.seek(off)
        if f.write(after) != len(after):
            raise PatchError('short write; keep recovery files and use --restore')
        f.flush()
        os.fsync(f.fileno())
        f.seek(off)
        if f.read(len(after)) != after:
            raise PatchError('write verification failed; keep recovery files and use --restore')


def cmd_info(args):
    with Iso(args.iso) as iso:
        print('%s: volume "%s", %d bytes' % (args.iso, iso.volume, iso.size))
        found = find_usbd(iso)
        if not found:
            raise PatchError('no USBD.IRX found under ' + ' or '.join(SEARCH_PATHS))
        for path, rec in found:
            data = iso.file_bytes(rec)
            sectors = (rec['size'] + SECTOR - 1) // SECTOR
            print('%s: LBA %d, %d bytes (%d sectors), SHA-1 %s' %
                  (path, rec['lba'], rec['size'], sectors, sha1(data)))
            if CFG_MAGIC in data:
                print('   Buzz! Bridge configuration:', read_config(data))
            else:
                print('   ' + KNOWN_USBD.get(sha1(data), 'UNKNOWN version; patching will be refused'))
    return 0


def cmd_patch(args):
    console = args.console
    console.banner()
    console.section(1, 'Your game')
    console.line('Inspecting ISO and checking the bundled module ...')
    target = Path(args.iso if args.in_place else args.out or
                  str(Path(args.iso).with_name(Path(args.iso).stem + ' (Buzz Bridge).iso')))
    if not args.in_place and same_file(target, args.iso):
        raise PatchError('--out must not be the input ISO; use --in-place explicitly')
    if not args.in_place and target.exists():
        raise PatchError('output already exists: %s; choose another --out' % target)
    if any(p.exists() for p in side_paths(target)):
        raise PatchError('recovery files already exist for %s; keep them and use --restore or another output' % target)
    irx_path = Path(args.irx) if args.irx else HERE.parent / 'bbusbd.irx'
    new = irx_path.read_bytes()
    check_new_irx(new)
    flags = (0 if args.quiet else FLAG_VERBOSE) | (FLAG_NOPAD if args.no_pad else 0) | (FLAG_SCRIPT if args.script_demo else 0)
    flags |= {'off': 0, 'auto': FLAG_POLL_AUTO, 'always': FLAG_POLL_ALWAYS}[args.poll]
    with Iso(args.iso) as iso:
        path, rec = select_usbd(iso, args.path)
        orig = iso.file_bytes(rec)
        label = check_original(orig)
        if len(new) > len(orig):
            raise PatchError('replacement is larger than the original USBD.IRX; no relocation is supported')
        image_size = iso.size
        if console.enabled:
            console.line('Game / volume: ' + iso.volume)
            console.line('Input: ' + str(args.iso))
            console.line('Driver: %s - %s' % (path, label))
            console.line('ISO size: %s (%s bytes)' % (human_size(image_size), format(image_size, ',')))
        else:
            print('%s: %s (%s)' % (args.iso, path, label))
    # All input/module checks precede the interactive choice and every write.
    if args.layout is not None:
        layout = resolve_layout(args.layout)
        console.section(2, 'Your setup')
    else:
        layout = ask_layout(console)
    console.setup(layout)
    console.section(3, 'Patching')
    console.line('Patching the ISO now. Please keep this window open.')
    new = write_config(new, layout, flags, args.attach_delay_ms, args.heartbeat_ms)
    patched = new.ljust(len(orig), b'\0')
    args.space_directory = target.resolve().parent
    args.space_needed = (0 if args.in_place else image_size) + len(orig) * 3 + 65536
    require_space(args.space_directory, args.space_needed)
    console.line('Output: ' + str(target.resolve()))
    recovery, tmp, writing = [], None, False
    try:
        if not args.in_place:
            console.line('Copying the ISO (%s); large games can take a few minutes ...' % human_size(image_size))
            fd, tmp = tempfile.mkstemp(prefix=target.name + '.', suffix='.partial', dir=target.resolve().parent)
            with os.fdopen(fd, 'wb') as dst, open(args.iso, 'rb') as src:
                if console.enabled:
                    copy_with_progress(src, dst, image_size, console)
                else:
                    shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
                console.start('Flushing ISO copy to disk')
                dst.flush()
                os.fsync(dst.fileno())
                console.done()
            if os.path.getsize(tmp) != image_size:
                raise PatchError('source ISO size changed while copying')
            console.start('Writing Buzz! Bridge module')
            write_extent(tmp, rec, orig, patched)
            console.done()
        console.start('Saving recovery files')
        recovery = save_recovery(target, path, rec, orig, patched, image_size)
        console.done()
        if args.in_place:
            writing = True  # recovery must survive any write/flush failure
            console.start('Writing Buzz! Bridge module')
            write_extent(target, rec, orig, patched)
            console.done()
        console.start('Verifying')
        # write_extent already reads back the module; now check both sidecars.
        verify_recovery(target, path, rec, orig, patched, image_size)
        console.done()
        if not args.in_place:
            console.start('Finishing patched ISO')
            if os.name == 'nt':
                os.rename(tmp, target)  # Windows refuses an existing destination, including on FAT/exFAT
            else:
                os.link(tmp, target)  # no-clobber publication on POSIX
                os.unlink(tmp)
            tmp = None
            writing = True
            console.done()
    except BaseException:
        if not writing:
            for dest in recovery:
                dest.unlink()
        raise
    finally:
        if tmp is not None:
            os.unlink(tmp)
    console.section(4, 'Done')
    print('Patched: %s' % (target.resolve() if console.enabled else target))
    print('Layout %d (%s), poll %s. Keep both recovery files beside the ISO:' %
          (layout, dict((i, n) for i, n, _ in load_layouts())[layout], args.poll))
    for p in side_paths(target):
        print('  ' + str(p))
    console.line('\nPCSX2: Boot the patched ISO fresh; USB ports off, DualShock 2 pads set up.')
    console.line('PS3 CFW (untested): Copy to /dev_hdd0/PS2ISO/ and mount with webMAN.')
    console.line('PS3 HEN (untested): Convert the patched ISO to a PS2 Classic first.')
    console.line('\nUndo: buzz-bridge-ps2.exe "%s" --restore' % target)
    console.line('Keep both recovery files beside the ISO for Undo.')
    return 0


def cmd_restore(args):
    console = args.console
    console.line('Inspecting ISO and recovery data before restoration ...')
    backup, side_path = side_paths(args.iso)
    with Iso(args.iso) as iso:
        if args.from_iso:
            with Iso(args.from_iso) as src:
                path, source_rec = select_usbd(src, args.path)
                orig = src.file_bytes(source_rec)
                check_original(orig)
                if iso.size != src.size:
                    raise PatchError('original and patched image sizes differ')
            rec = iso.find(path)
            if rec is None or (rec['lba'], rec['size']) != (source_rec['lba'], source_rec['size']):
                raise PatchError('image layout differs from the original')
            cur = iso.file_bytes(rec)
            if cur == orig:
                print('Already original, nothing to do.')
                return 0
            module = (Path(args.irx) if args.irx else HERE.parent / 'bbusbd.irx').read_bytes()
            check_new_irx(module)
            cfg = read_config(cur)
            expected = write_config(module, cfg['layout'], cfg['flags'], cfg['attach_delay_ms'], cfg['heartbeat_ms']).ljust(len(orig), b'\0')
            if cur != expected:
                raise PatchError('current USBD.IRX is not the bundled patched module; use matching recovery files')
        else:
            if not side_path.exists():
                raise PatchError('recovery JSON missing; keep both sidecars or use --from-iso ORIGINAL.iso')
            if side_path.stat().st_size > 8 * 1024 * 1024:
                raise PatchError('recovery JSON is too large')
            side = json.loads(side_path.read_text(encoding='utf-8'))
            if side['tool'] != 'buzz-bridge-ps2' or side['version'] != 2:
                raise PatchError('unsupported recovery record')
            # Never follow arbitrary backup paths from JSON.
            orig = backup.read_bytes()
            expected = base64.b64decode(side['patched_data'], validate=True)
            if sha256(orig) != side['original_sha256'] or sha256(expected) != side['patched_sha256']:
                raise PatchError('recovery checksum mismatch')
            check_original(orig)
            rec = iso.find(side['path']) if side['path'] in SEARCH_PATHS else None
            if (rec is None or rec['lba'] != side['lba'] or rec['size'] != side['length'] or
                    iso.size != side['image_size'] or len(orig) != rec['size'] or len(expected) != len(orig)):
                raise PatchError('image layout differs from the recorded one')
            cur = iso.file_bytes(rec)
            if cur == orig:
                print('Already original, nothing to do.')
                return 0
            # Permit an interrupted extent write, but refuse unrelated changed bytes.
            if any(c != o and c != p for c, o, p in zip(cur, orig, expected)):
                raise PatchError('USBD.IRX is neither patched nor original (or an interrupted write); refusing to restore')
    console.start('Restoring and verifying original USBD.IRX')
    write_extent(args.iso, rec, cur, orig)
    console.done()
    print('Restored original %s in %s. Recovery files retained.' % (path if args.from_iso else side['path'], args.iso))
    return 0


def main(argv=None, console=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('iso', nargs='?', help='your PS2 Buzz! ISO')
    modes = ap.add_mutually_exclusive_group()
    modes.add_argument('--info', action='store_true', help='inspect only; write nothing')
    modes.add_argument('--restore', action='store_true', help='restore this ISO in place using its recovery files')
    dest = ap.add_mutually_exclusive_group()
    dest.add_argument('--out', help='path for patched copy (default: <name> (Buzz Bridge).iso)')
    dest.add_argument('--in-place', action='store_true', help='modify input instead of creating a copy')
    ap.add_argument('--layout', help='0-4 or name; omit to choose your setup interactively')
    ap.add_argument('--poll', choices=['off', 'auto', 'always'], default='auto', help='default auto; always is diagnostic')
    ap.add_argument('--path', choices=SEARCH_PATHS, help='select if both USBD paths exist')
    ap.add_argument('--from-iso', help='with --restore: read original USBD from an unpatched ISO')
    ap.add_argument('--irx', help='alternate bbusbd.irx (developer option)')
    ap.add_argument('--quiet', action='store_true', help='disable BBUSB IOP console logging')
    ap.add_argument('--no-pad', action='store_true', help='diagnostic: virtual USB only')
    ap.add_argument('--script-demo', action='store_true', help='diagnostic: scripted button presses')
    ap.add_argument('--attach-delay-ms', type=int, default=100)
    ap.add_argument('--heartbeat-ms', type=int, default=100)
    ap.add_argument('--list-layouts', action='store_true')
    args = ap.parse_args(argv)
    args.console = console or Console()
    if args.list_layouts:
        for i, name, desc in load_layouts():
            print('%d  %-14s %s' % (i, name, desc))
        return 0
    if not args.iso:
        ap.error('give an ISO path (or drag an ISO onto buzz-bridge-ps2.exe)')
    if (args.info or args.restore) and (args.out or args.in_place):
        ap.error('--info/--restore cannot be combined with --out/--in-place; --restore modifies the named ISO')
    if args.from_iso and not args.restore:
        ap.error('--from-iso requires --restore')
    try:
        validate_iso_path(args.iso)
        if args.from_iso:
            validate_iso_path(args.from_iso)
        for name in ('attach_delay_ms', 'heartbeat_ms'):
            if not 0 <= getattr(args, name) <= 60000:
                raise PatchError('--%s must be between 0 and 60000' % name.replace('_', '-'))
        if args.info:
            return cmd_info(args)
        if args.restore:
            return cmd_restore(args)
        return cmd_patch(args)
    except (PatchError, OSError, ValueError, KeyError, TypeError, struct.error, IndexError) as e:
        message = e
        if getattr(e, 'errno', None) == errno.ENOSPC and hasattr(args, 'space_needed'):
            try:
                free = shutil.disk_usage(args.space_directory).free
                message = 'not enough free disk space: need %d bytes for this operation, have %d bytes in %s' % (
                    args.space_needed, free, args.space_directory)
            except OSError:
                pass
        args.console.error(message, error_hint(e))
        return 1
    except KeyboardInterrupt:
        args.console.end_step()
        print('\nCancelled. Keep any recovery files beside the ISO.', file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())
