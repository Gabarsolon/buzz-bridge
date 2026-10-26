"""Verify the accepted v4 container profile and derived game priority evidence.

Read-only game/SDK inputs; writes derived metadata under build/ only. No network.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
from audit_format10 import fields
from self10 import read

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'research'))
import game_elf as game


def artifact(name):
    data = (ROOT/'build'/name).read_bytes()
    return dict(file='build/'+name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    baseline = artifact('ps3buzz_v4.sprx')
    assert baseline['bytes'] == 44304
    assert baseline['sha256'] == '6eec959ee93a5144d70c47c6066517cbf54f2adc51bcbd96818d23cdc4e53925'
    keys = ROOT/'research/load10/key_vault.cpp'
    old = read(ROOT/'build/ps3buzz_v4.sprx', keys)
    new = read(ROOT/'build/ps3buzz.sprx', keys)
    old_fields, new_fields = fields(old['elf']), fields(new['elf'])
    assert all(old['hmac_checks']) and all(new['hmac_checks'])
    # Payload sizes/digests and random signing/encryption data necessarily differ.
    assert {k:v for k,v in old['header'].items() if k != 'elf_size'} == {
        k:v for k,v in new['header'].items() if k != 'elf_size'}
    assert old_fields['header'] == new_fields['header']
    assert [(p[0], p[1], p[7]) for p in old['ph']] == [(p[0], p[1], p[7]) for p in new['ph']]
    assert old['encrypted_capabilities'] == new['encrypted_capabilities']
    assert old['control'][0] == new['control'][0]
    for a,b in zip(old['control'], new['control']):
        assert (a['type'],a['size'],a['next']) == (b['type'],b['size'],b['next'])
    a,b = (bytes.fromhex(c['control'][1]['payload']) for c in (old,new))
    assert a[:20] == b[:20] and a[40:] == b[40:]
    assert b[20:40] == hashlib.sha1((ROOT/'build/ps3buzz.prx').read_bytes()).digest()
    assert [s[2:] for s in old['segment_ext']] == [s[2:] for s in new['segment_ext']]
    assert [s[2:] for s in old['sections']] == [s[2:] for s in new['sections']]
    libraries = lambda f: [(r['kind'],r['bytes'],r['version'],r['attributes'],r['functions'],r['name']) for r in f['libraries']]
    assert libraries(old_fields) == libraries(new_fields)
    assert old_fields['module_info']['version'] == new_fields['module_info']['version']
    assert hashlib.sha256(game.DATA).hexdigest() == '6f4d1f05cde3d8a6e7836b65c7103cff0ccc6a248ca0b877a7da3e3453585a19'
    trampoline = next(x['stub'] for x in game.imports() if int(x['fnid'],16)==game.nid('sys_ppu_thread_create'))
    known = [(0x192e8,0x192fc,600),(0xb56d0,0xb56e4,250),(0xf1f64,0xf1f7c,500),
             (0xf2d0c,0xf2d30,500),(0xf3148,0xf3160,500),(0x39fac8,0x39fae8,200)]
    priorities = []
    for li,call,priority in known:
        assert game.U32(game.off(li)) == 0x38c00000|priority  # li r6, priority
        assert game.U32(game.off(call)) == 0x48000001|((trampoline-call)&0x3fffffc)
        priorities.append(dict(priority=priority,argument_instruction=hex(li),create_call=hex(call)))
    report = dict(result='PASS',console_tested=False,
                  baseline_acceptance='User-reported syscall return 0 and v4 ENTRY/adoption log; not re-tested here',
                  artifacts=[artifact(n) for n in ('ps3buzz.sprx','ps3buzz_v4.sprx','ps3buzz.cfg')],
                  equal_profile=dict(self_header_except_payload_size=True,elf_header=True,segment_types_flags_alignment=True,
                                     capabilities=True,control_except_digest=True,compression_encryption_modes=True,
                                     library_shapes=True,module_version=True),
                  segment_hmac_checks=len(old['hmac_checks'])+len(new['hmac_checks']),
                  profile=new['header'],elf_header=new_fields['header'],
                  game_create_trampoline=hex(trampoline),game_fixed_priority_sites=priorities,
                  other_priority_sites='Arguments are loaded from runtime/configuration/stack values; current priorities unknown',
                  prior_plugin_priority=1500,new_priorities=dict(bootstrap=100,usb=100,http=150,diagnostics=160),
                  limitation='Static construction arguments do not prove which threads are currently runnable or the original blocked PC.')
    (ROOT/'build/runtime11_audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Runtime audit PASS: byte-identical accepted v4 backup; same SELF/ELF/library profile; six segment HMACs; six fixed game priority sites.')


if __name__ == '__main__':
    main()
