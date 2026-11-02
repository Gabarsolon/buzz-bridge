"""Decrypt an LV2-type PS3 SELF (e.g. ps2_netemu.self) to ELF using RPCS3's key_vault.cpp keys.
usage: python -I selfdec.py <key_vault.cpp> <in.self> <out.elf>
"""
import re, struct, sys, zlib
from Crypto.Cipher import AES
from Crypto.Util import Counter

def lv2_keys(path):
    src = open(path, encoding='utf-8').read()
    body = src[src.index('void KeyVault::LoadSelfLV2Keys()'):]
    body = body[:body.index('\n}')]
    out = []
    for m in re.finditer(r'emplace_back\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*KEY_LV2,\s*"([0-9A-F]+)",\s*"([0-9A-F]+)"', body):
        out.append((int(m[1], 16), int(m[2], 16), int(m[3], 16), bytes.fromhex(m[4]), bytes.fromhex(m[5])))
    return out

def ctr(key, iv, data):
    c = AES.new(key, AES.MODE_CTR, counter=Counter.new(128, initial_value=int.from_bytes(iv, 'big')))
    return c.decrypt(data)

def main(kv, inp, outp):
    d = open(inp, 'rb').read()
    magic, ver, keyrev, htype, meta_off, hlen, dlen = struct.unpack_from('>4sIHHIQQ', d, 0)
    assert magic == b'SCE\0' and htype == 1, 'not a SELF'
    (_, appinfo_off, elf_off, phdr_off, shdr_off, sinfo_off, sver_off, ctrl_off, ctrl_size) = struct.unpack_from('>9Q', d, 0x20)
    auth_id, vendor, self_type, app_ver = struct.unpack_from('>QIIQ', d, appinfo_off)
    print(f'keyrev={keyrev:#x} self_type={self_type} auth={auth_id:#x} version={app_ver:#018x}')
    assert self_type == 3, 'only LV2 type handled'
    key = next(k for k in lv2_keys(kv) if k[0] <= app_ver <= k[1] and k[2] == keyrev)
    mi_off = 0x20 + meta_off
    mi = AES.new(key[3], AES.MODE_CBC, key[4]).decrypt(d[mi_off:mi_off + 0x40])
    mkey, mkpad, miv, mivpad = mi[0:16], mi[16:32], mi[32:48], mi[48:64]
    assert mkpad[0] == 0 and mivpad[0] == 0, 'metadata info decrypt failed (wrong key)'
    hdrs = ctr(mkey, miv, d[mi_off + 0x40:hlen])
    _, _, nsec, nkey, _, _, _ = struct.unpack_from('>QIIIIII', hdrs, 0)
    secs = [struct.unpack_from('>QQIIIIIIII', hdrs, 0x20 + i * 0x30) for i in range(nsec)]
    kbase = 0x20 + nsec * 0x30
    keys = [hdrs[kbase + i * 16:kbase + i * 16 + 16] for i in range(nkey)]

    e = d[elf_off:elf_off + 0x40]
    assert e[:4] == b'\x7fELF' and e[4] == 2, 'expected ELF64'
    e_phoff, e_shoff = struct.unpack_from('>QQ', e, 0x20)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from('>HHHH', e, 0x36)
    phdrs = [struct.unpack_from('>IIQQQQQQ', d, phdr_off + i * 0x38) for i in range(e_phnum)]
    size = max([e_shoff + e_shnum * e_shentsize if e_shnum else 0] + [p[2] + p[5] for p in phdrs])
    out = bytearray(size)
    out[0:0x40] = e
    out[e_phoff:e_phoff + e_phnum * 0x38] = d[phdr_off:phdr_off + e_phnum * 0x38]
    if e_shnum and shdr_off:
        out[e_shoff:e_shoff + e_shnum * e_shentsize] = d[shdr_off:shdr_off + e_shnum * e_shentsize]
    for (off, sz, typ, pidx, hashed, sha, enc, kidx, ividx, comp) in secs:
        if typ != 2:
            continue
        blob = d[off:off + sz]
        if enc == 3:
            blob = ctr(keys[kidx], keys[ividx], blob)
        if comp == 2:
            blob = zlib.decompress(blob)
        p = phdrs[pidx]
        out[p[2]:p[2] + len(blob)] = blob
        print(f'seg {pidx}: type={p[0]:#x} vaddr={p[3]:#x} filesz={p[5]:#x} memsz={p[6]:#x} enc={enc} comp={comp} -> {len(blob):#x}')
    open(outp, 'wb').write(out)
    print('wrote', outp, len(out))

if __name__ == '__main__':
    main(*sys.argv[1:4])
