"""Small helpers to read IOP IRX import/export tables and IOPRP (ROMDIR) images.
Pure Python, no dependencies. Used by check_irx.py and the research scripts."""
import struct

EXPORT_MAGIC = 0x41C00000
IMPORT_MAGIC = 0x41E00000


def romdir_modules(img: bytes):
    """Yield (name, bytes) for every file in an IOPRP/ROMDIR image."""
    # ROMDIR starts with an entry named RESET
    pos = img.find(b'RESET\0')
    if pos < 0:
        raise ValueError('no ROMDIR')
    ents = []
    off = pos
    while img[off] != 0:
        name = img[off:off + 10].split(b'\0')[0].decode('ascii', 'replace')
        ext, size = struct.unpack_from('<HI', img, off + 10)
        ents.append((name, ext, size))
        off += 16
    # data follows the romdir (terminator entry included) and the extinfo area
    data_off = off + 16
    data_off += sum(e[1] for e in ents)
    data_off = (data_off + 15) & ~15
    # EXTINFO bytes belong to the "EXTINFO" pseudo file in the dir, so data_off is
    # computed by walking the directory instead (below).
    out = []
    cur = pos
    # first file (RESET) starts at 0 inside the data area that begins at file start
    cur = 0
    for name, ext, size in ents:
        out.append((name, img[cur:cur + size]))
        cur += (size + 15) & ~15
    return out


def _sections(elf: bytes):
    shoff, = struct.unpack_from('<I', elf, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', elf, 0x2E)
    secs = []
    for i in range(shnum):
        sh = struct.unpack_from('<IIIIIIIIII', elf, shoff + i * shentsize)
        secs.append(dict(type=sh[1], addr=sh[3], off=sh[4], size=sh[5]))
    return secs


def parse_tables(elf: bytes):
    """Return (exports, imports). exports: {name: (version, [offsets])};
    imports: {name: (version, [fno, ...])}. Scans for the table magics inside
    PROGBITS sections (tables live in .text)."""
    exports, imports = {}, {}
    secs = [s for s in _sections(elf) if s['type'] == 1 and s['size']]
    for s in secs:
        blob = elf[s['off']:s['off'] + s['size']]
        for i in range(0, len(blob) - 16, 4):
            magic, = struct.unpack_from('<I', blob, i)
            if magic == EXPORT_MAGIC:
                ver, mode = struct.unpack_from('<HH', blob, i + 8)
                name = blob[i + 12:i + 20].split(b'\0')[0].decode('ascii', 'replace')
                fp = []
                j = i + 20
                limit = len(blob)
                # entries are section-relative offsets (or 0 for an empty slot).
                # The table ends at the first word that cannot be an offset.
                while j + 4 <= len(blob) and len(fp) < 256:
                    v, = struct.unpack_from('<I', blob, j)
                    if v >= limit or (v == 0 and fp and
                                      struct.unpack_from('<I', blob, j + 4)[0] >= limit):
                        break
                    fp.append(v)
                    j += 4
                while fp and fp[-1] == 0:  # terminator word(s)
                    fp.pop()
                exports[name] = (ver, fp)
            elif magic == IMPORT_MAGIC:
                ver, mode = struct.unpack_from('<HH', blob, i + 8)
                name = blob[i + 12:i + 20].split(b'\0')[0].decode('ascii', 'replace')
                fnos = []
                j = i + 20
                while j + 8 <= len(blob):
                    jr, fn = struct.unpack_from('<II', blob, j)
                    if jr == 0 and fn == 0:
                        break
                    if jr != 0x03E00008 or (fn >> 16) != 0x2400:
                        break
                    fnos.append(fn & 0xFFFF)
                    j += 8
                imports.setdefault(name, (ver, []))[1].extend(fnos)
    return exports, imports
