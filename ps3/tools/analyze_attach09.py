"""Recheck derived attach facts against read-only owner ELFs; save no game bytes."""
import json,pathlib
from game_elf import Elf
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE=pathlib.Path(__import__('os').environ.get('BUZZ_GAME_INPUTS','game-inputs'))
CAT=json.loads((ROOT/'plugin/game/profiles.json').read_text())
SPECS=[
 ('BCES00347','game',0xbd300,0xbd168,0xbca88,0xbd098,0xbd168,[0xbcb3c,0xbcbf4,0xbd21c],0xbb5f8),
 ('BCES00303','game_quiztv',0xbd120,0xbcf88,0xbc8a8,0xbceb8,0xbcf88,[0xbc95c,0xbca14,0xbd03c],0xbb418),
 ('BCES00644','game_quizworld',0x223500,0x2233e0,0x222e20,0x223320,0x2233e0,[0x222ec4,0x222f7c,0x22348c],0x4d8a18)]
results=[]
for title,folder,attach,manager,selector,net_begin,net_end,locks,device_attach in SPECS:
 e=Elf((BASE/folder/'EBOOT.elf').read_bytes())
 p=next(p for p in CAT['profiles'] if p['title']==title)
 assert e.sha256==p['sha256']
 r=p['resolved'];assert e.u32(r['opds'][1])==attach
 # First two selectors issue explicit li r4,0 / li r11,102 / sc.
 for at in locks[:2]:
  assert e.u32(at)==0x44000002 and e.u32(at-4)==0x39600066
  assert e.u32(at-8)==0x38800000
 assert e.u32(locks[2])==0x44000002
 assert any(e.u32(at)==0x39600066 for at in range(locks[2]-32,locks[2],4))
 im=e.imports()[2];net=[x for x in im if x['library']=='cellNetCtl' and x['fnid']==0x1e585b5d]
 assert len(net)==1
 calls=[a for a,t,link in e.branches() if link and t==net[0]['stub'] and net_begin<=a<net_end]
 assert len(calls)==3
 results.append(dict(title=title,sha256=e.sha256,attach=hex(attach),manager_attach=hex(manager),
  device_selector=hex(selector),manager_mutex_lock_syscalls=[hex(a) for a in locks],
  manager_mutex_address=hex(r['ready']+4),manager_mode_address=hex(r['ready']+8),
  count_address=hex(r['count']),presence_addresses=[hex(a) for a in r['presence']],
  netctl_function=hex(net_begin),netctl_info_calls=[hex(a) for a in calls],
  netctl_info_slot=hex(net[0]['slot']),netctl_info_stub=hex(net[0]['stub']),device_attach=hex(device_attach),
  netctl_codes=[1,4,12],mutex_timeout_usec=0))
report=dict(result='PASS',method='Selected Ghidra in-memory queries plus independent ELF/branch/OPD/syscall checks; never execute PPU code',
 titles=results,limits='Derived static facts. No sampled game PC, retail libusbd thread identity or kernel dump. Exact stall/freeze cause remains unknown.')
(ROOT/'build/attach_analysis09.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
