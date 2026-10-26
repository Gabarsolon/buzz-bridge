"""Accepted-v5 backup/profile and revision-12 source/object evidence. Offline."""
import hashlib,json,pathlib
from self10 import read
from audit_format10 import fields
ROOT=pathlib.Path(__file__).resolve().parents[2]
def artifact(name):
    b=(ROOT/'build'/name).read_bytes()
    return dict(file='build/'+name,bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
    backup=artifact('ps3buzz_v5.sprx')
    assert backup['bytes']==47248 and backup['sha256']=='5bda6878e53d47e432ad5e13be6d73305ba5af5b72cefb6d19e04392e80ec4c4'
    keys=ROOT/'research/load10/key_vault.cpp'
    old,new=[read(ROOT/'build'/n,keys) for n in ('ps3buzz_v5.sprx','ps3buzz.sprx')]
    assert all(old['hmac_checks']) and all(new['hmac_checks'])
    assert {k:v for k,v in old['header'].items() if k!='elf_size'}=={k:v for k,v in new['header'].items() if k!='elf_size'}
    assert [(p[0],p[1],p[7]) for p in old['ph']]==[(p[0],p[1],p[7]) for p in new['ph']]
    assert old['encrypted_capabilities']==new['encrypted_capabilities']
    assert [(x['type'],x['size'],x['next']) for x in old['control']]==[(x['type'],x['size'],x['next']) for x in new['control']]
    assert old['control'][0]==new['control'][0]
    a,b=[bytes.fromhex(x['control'][1]['payload']) for x in (old,new)]
    assert a[:20]==b[:20] and a[40:]==b[40:]
    assert [s[2:] for s in old['sections']]==[s[2:] for s in new['sections']]
    assert [s[2:] for s in old['segment_ext']]==[s[2:] for s in new['segment_ext']]
    a,b=[fields(x['elf']) for x in (old,new)]
    assert a['header']==b['header'] and a['module_info']['version']==b['module_info']['version']
    libs=lambda x:[(l['kind'],l['bytes'],l['version'],l['attributes'],l['functions'],l['name']) for l in x['libraries']]
    assert libs(a)==libs(b)
    native=json.loads((ROOT/'build/prx_audit.json').read_text())
    loops=json.loads((ROOT/'build/loop_audit12.json').read_text())
    assert native['native_clock_import_calls']>=3 and not native['raw_syscall145_in_native_object']
    assert loops['result']=='PASS'
    report=dict(result='PASS',console_tested=False,accepted_v5_backup=backup,
                accepted_v5_profile_preserved=True,segment_hmac_checks=len(old['hmac_checks'])+len(new['hmac_checks']),
                native_clock_import_calls=native['native_clock_import_calls'],raw_syscall145_in_native_object=False,
                reviewed_loops=len(loops['loops']),negative_mutations_rejected=loops['negative_mutations_rejected'],
                artifacts=[artifact(n) for n in ('ps3buzz.sprx','ps3buzz_v5.sprx','ps3buzz.cfg')])
    (ROOT/'build/runtime12_audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Runtime12 audit PASS: exact v5 backup, same accepted APP/ELF/library profile, six HMACs, native clock import and source loop contracts.')
if __name__=='__main__': main()
