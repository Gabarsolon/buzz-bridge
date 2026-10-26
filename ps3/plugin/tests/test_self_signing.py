"""Offline revision-0A proof, payload invariance and tamper rejection tests."""
import hashlib
import json
import pathlib
import struct
import sys
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'plugin/tools'))
import self_ecdsa
from self10 import decode, read

def rejected(action):
    try:
        action()
    except (AssertionError, ValueError):
        return True
    raise AssertionError('Corruption was accepted')

def main():
    proof = self_ecdsa.prove(10)
    ct, pub, private = self_ecdsa.keypair(10)
    with patch.object(self_ecdsa, 'keypair', return_value=(ct, pub, private+1)):
        assert rejected(lambda: self_ecdsa.prove(10))
    c = self_ecdsa.curve(ct)
    assert self_ecdsa.multiply(c, 0, pub) is None
    assert self_ecdsa.multiply(c, 1, pub) == pub
    assert self_ecdsa.add(c, pub, (pub[0], (-pub[1]) % c['p'])) is None
    report = dict(result='PASS', build_note='signing rev 0A', proof=proof,
                  mismatched_private_key_rejected=True, files=[])
    for name, auth in [('ps3buzz','1010000001000003'), ('ps3buzz_vsh','1070000052000001')]:
        path = ROOT/'build'/(name+'.sprx')
        result = read(path, self_ecdsa.KEYS)
        old = read(ROOT/'build/signing-rev1'/(name+'.sprx'), self_ecdsa.KEYS)
        verified = self_ecdsa.verify_self(path)
        assert result['header']['key_revision'] == 10
        assert result['header']['auth'] == auth and result['header']['program_type'] == 4
        assert result['elf'] == old['elf'] == (ROOT/'build'/(name+'.prx')).read_bytes()
        assert result['ph'] == old['ph'] and result['segment_ext'] == old['segment_ext']
        assert result['control'] == old['control']
        assert result['encrypted_capabilities'] == old['encrypted_capabilities']
        assert struct.unpack_from('>I',result['elf'],48)[0] == 0x01000000
        assert [p[1] for p in result['ph'] if p[0] == 1] == [0x00400005,0x00600006]
        assert len(result['hmac_checks']) == len([s for s in result['sections'] if s[4] == 2])
        assert all(s[-1] == 2 for s in result['sections'] if s[1])
        # All plaintext fields except the revision are identical to the release.
        mo = 32+result['header']['metadata_offset']
        left=bytearray(result['data'][:mo]); right=bytearray(old['data'][:mo])
        left[8:10]=right[8:10]; assert left == right
        # Public signature rejects changes to its signed header and scalars.
        bad=bytearray(result['data']); bad[result['header']['extended_header'][1]] ^= 1
        with patch.object(self_ecdsa, 'read', return_value=decode(bytes(bad), self_ecdsa.KEYS)):
            assert rejected(lambda: self_ecdsa.verify_self(path))
        bad=bytearray(result['data']); bad[result['meta'][0]] ^= 1
        with patch.object(self_ecdsa, 'read', return_value=decode(bytes(bad), self_ecdsa.KEYS)):
            assert rejected(lambda: self_ecdsa.verify_self(path))
        # Flip the encrypted HMAC digest in metadata; section checking must fail.
        sha=result['sections'][0][5]
        bad=bytearray(result['data'])
        bad[mo+64+32+48*result['meta'][2]+16*sha] ^= 1
        assert not all(decode(bytes(bad), self_ecdsa.KEYS)['hmac_checks'])
        # Stored encrypted payload tampering must fail decryption/HMAC/inflation.
        bad=bytearray(result['data']); bad[result['sections'][0][0]] ^= 1
        try:
            changed=decode(bytes(bad), self_ecdsa.KEYS)
        except (ValueError, AssertionError):
            pass
        except Exception as exc:
            import zlib
            assert isinstance(exc,zlib.error)
        else:
            assert not all(changed['hmac_checks'])
        report['files'].append(dict(file=str(path.relative_to(ROOT)),bytes=path.stat().st_size,
            sha256=hashlib.sha256(path.read_bytes()).hexdigest(),**verified,auth=auth,
            prx_identical_to_rev1=True,layout_control_and_capabilities_identical=True,
            compressed=True,header_signature_hmac_and_payload_tampering_rejected=True))
    (ROOT/'build/hen-signing-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: revision 0A private/public proof; wrong-private rejection; both ECDSA signatures; every section HMAC; exact released payload/layout; header/signature/HMAC/payload tamper rejection.')

if __name__ == '__main__': main()
