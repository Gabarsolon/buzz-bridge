"""Compare runtime 0.7 with the exact preserved 0.6.1 APP SELF. Offline."""
import hashlib
import json
from pathlib import Path
from self10 import read
from audit_format10 import fields

ROOT = Path(__file__).resolve().parents[2]


def artifact(name):
    data = (ROOT/'build'/name).read_bytes()
    return dict(file='build/'+name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    backup = artifact('ps3buzz_v61.sprx')
    assert backup['bytes'] == 48352
    assert backup['sha256'] == 'd009e173ecc004a450068c516e3975b5071a75e3b66d20631c1388388a45eaed'
    old, new = [read(ROOT/'build'/n, ROOT/'research/load10/key_vault.cpp')
                for n in ('ps3buzz_v61.sprx', 'ps3buzz.sprx')]
    assert all(old['hmac_checks']) and all(new['hmac_checks'])
    assert {k:v for k,v in old['header'].items() if k!='elf_size'} == {
        k:v for k,v in new['header'].items() if k!='elf_size'}
    assert [(p[0],p[1],p[7]) for p in old['ph']] == [(p[0],p[1],p[7]) for p in new['ph']]
    assert old['encrypted_capabilities'] == new['encrypted_capabilities']
    assert old['control'][0] == new['control'][0]
    assert [(x['type'],x['size'],x['next']) for x in old['control']] == [
        (x['type'],x['size'],x['next']) for x in new['control']]
    a,b = [bytes.fromhex(x['control'][1]['payload']) for x in (old,new)]
    assert a[:20] == b[:20] and a[40:] == b[40:]
    assert b[20:40] == hashlib.sha1((ROOT/'build/ps3buzz.prx').read_bytes()).digest()
    for key in ('sections','segment_ext'):
        assert [s[2:] for s in old[key]] == [s[2:] for s in new[key]]
    a,b = [fields(x['elf']) for x in (old,new)]
    assert a['header'] == b['header'] and a['module_info']['version'] == b['module_info']['version']
    libs = lambda x: [(l['kind'],l['bytes'],l['version'],l['attributes'],l['functions'],l['name']) for l in x['libraries']]
    assert libs(a) == libs(b)
    assert b'ps3buzz 0.7 ENTRY' in new['elf'] and b'ps3buzz 0.7 bootstrap' in new['elf']
    native = (ROOT/'plugin/game/native.c').read_text()
    for text in ('WORKER_PRIORITY=1000','NETWORK_PRIORITY=1010','DIAGNOSTIC_PRIORITY=990',
                 'bootstrap,NULL,1000,65536', 'ready=buzz_cell_ready(&adapter)'):
        assert text in native
    assert (ROOT/'plugin/net/server.c').read_bytes() == (ROOT/'build/v61_source/server.c').read_bytes()
    adapter = (ROOT/'plugin/game/cellusbd_adapter.c').read_text()
    poll = adapter.split('void buzz_cell_poll(',1)[1].split('int buzz_cell_can_unload(',1)[0]
    assert 'c->real.' not in poll and 'result=buzz_cell_location(' in poll
    loops = json.loads((ROOT/'build/loop_audit12.json').read_text())
    assert loops['result'] == 'PASS'
    report = dict(result='PASS',console_tested=False, accepted_061_backup=backup,
                  same_accepted_container_profile=True,
                  segment_hmac_checks=len(old['hmac_checks'])+len(new['hmac_checks']),
                  priorities=dict(usb=1000,http=1010,diag=990,bootstrap=1000),
                  server_061_byte_identical=True,virtual_presentation_native_exports=0,
                  reviewed_loops=len(loops['loops']),
                  artifacts=[artifact(n) for n in ('ps3buzz.sprx','ps3buzz_v61.sprx','ps3buzz.cfg')])
    (ROOT/'build/runtime07_audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Runtime07 audit PASS: exact 0.6.1 backup; accepted APP/ELF/library profile; six HMACs; priorities/socket fix preserved; no native presentation export.')


if __name__ == '__main__':
    main()
