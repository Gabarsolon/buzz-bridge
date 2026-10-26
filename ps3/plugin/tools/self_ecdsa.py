"""SELF loader-curve proof and public-key verification. Never emits key material."""
import hashlib
import json
import pathlib
import re
import secrets
from Crypto.Cipher import AES
from self10 import app_key, read

ROOT = pathlib.Path(__file__).resolve().parents[2]
KEYS = ROOT / 'research/load10/key_vault.cpp'

def keypair(revision):
    body = KEYS.read_text().split('void KeyVault::LoadSelfAPPKeys()', 1)[1].split('\n}', 1)[0]
    for m in re.finditer(r'emplace_back\((.*?)\);', body, re.S):
        fields = re.findall(r'0x[0-9a-fA-F]+', m[1].split('KEY_APP')[0])
        if int(fields[-1], 16) == revision:
            values = re.findall(r'"([0-9A-Fa-f]*)"', m[1])
            public = bytes.fromhex(values[2])
            private = int(values[3] or '0', 16)
            ctype = int(m[1].rsplit('"', 1)[-1].strip().strip(','), 16)
            return ctype, (int.from_bytes(public[:20], 'big'), int.from_bytes(public[20:], 'big')), private
    raise ValueError('APP revision missing')

def curve(ctype):
    values = json.loads((ROOT / 'research/hen/self-curves.json').read_text())['curves'][f'{ctype:02x}']
    return {k: int(v, 16) for k, v in values.items()}

def holds(c, point):
    if point is None:
        return True
    x, y = point
    return 0 <= x < c['p'] and 0 <= y < c['p'] and (y*y-x*x*x-c['a']*x-c['b']) % c['p'] == 0

def add(c, left, right):
    if left is None: return right
    if right is None: return left
    x, y = left; xx, yy = right; p = c['p']
    if x == xx and (y+yy) % p == 0: return None
    slope = ((3*x*x+c['a'])*pow(2*y, -1, p) if left == right else (yy-y)*pow(xx-x, -1, p)) % p
    nx = (slope*slope-x-xx) % p
    return nx, (slope*(x-nx)-y) % p

def multiply(c, scalar, point):
    assert scalar >= 0
    result = None
    while scalar:
        if scalar & 1: result = add(c, result, point)
        point = add(c, point, point)
        scalar >>= 1
    return result

def prove(revision=10):
    ct, public, private = keypair(revision)
    c = curve(ct); g = c['gx'], c['gy']
    assert (4*c['a']**3+27*c['b']**2) % c['p'] != 0
    assert holds(c, g) and holds(c, public), 'Invalid public curve/point'
    assert multiply(c, c['n'], g) is None and multiply(c, c['n'], public) is None
    assert 0 < private < c['n'], 'No usable private key; stop before signing'
    assert multiply(c, private, g) == public, 'Private key does not match APP public key; stop'
    return dict(revision=f'{revision:02X}', curve=f'{ct:02X}', private_times_G_equals_public=True,
                generator_and_public_on_curve=True, generator_and_public_order_check=True,
                public_sha256=hashlib.sha256(b''.join(v.to_bytes(20,'big') for v in public)).hexdigest())

def verify_digest(revision, digest, signature):
    ct, public, _ = keypair(revision); c = curve(ct); n = c['n']
    r, s = signature
    if not (0 < r < n and 0 < s < n and holds(c, public)): return False
    w = pow(s, -1, n); z = int.from_bytes(digest, 'big')
    found = add(c, multiply(c, z*w % n, (c['gx'], c['gy'])), multiply(c, r*w % n, public))
    return found is not None and found[0] % n == r

def sign_digest(revision, digest):
    prove(revision)  # Mandatory before any use of the private scalar.
    ct, _, private = keypair(revision); c = curve(ct); n = c['n']
    while True:
        k = secrets.randbelow(n-1)+1
        r = multiply(c, k, (c['gx'], c['gy']))[0] % n
        s = (int.from_bytes(digest,'big')+r*private)*pow(k,-1,n) % n
        if r and s:
            assert verify_digest(revision, digest, (r,s))
            return r, s

def verify_self(path):
    result = read(path, KEYS); info = result['header']; rev = info['key_revision']
    # The native packer emits only type-2 authenticated segments. Refuse any
    # other metadata profile here rather than silently omit its authentication.
    assert all(s[2]==2 and s[4]==2 for s in result['sections'])
    assert len(result['hmac_checks'])==len(result['sections'])
    mo = 32+info['metadata_offset']; hs = info['header_size']
    key, iv = app_key(KEYS, rev)
    mi = AES.new(key, AES.MODE_CBC, iv).decrypt(result['data'][mo:mo+64])
    header = bytearray(result['data'][:hs]); header[mo:mo+64] = mi; header[mo+64:] = result['meta_plain']
    offset = result['meta'][0]
    assert mo+64 <= offset and offset+42 <= hs
    signature = tuple(int.from_bytes(header[offset+i:offset+i+21], 'big') for i in (0,21))
    assert verify_digest(rev, hashlib.sha1(header[:offset]).digest(), signature), 'ECDSA public-key verification failed'
    assert result['hmac_checks'] and all(result['hmac_checks']), 'Section HMAC failed'
    return dict(ecdsa_public_key_verified=True, hmac_checks=result['hmac_checks'],
                revision=f'{rev:02X}', elf_sha256=hashlib.sha256(result['elf']).hexdigest())

if __name__ == '__main__':
    import sys
    print(json.dumps(prove(), indent=2))
    for path in sys.argv[1:]: print(json.dumps(dict(file=path, **verify_self(path)), indent=2))
