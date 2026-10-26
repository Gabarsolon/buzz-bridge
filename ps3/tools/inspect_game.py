"""Inspect a decrypted PS3 EBOOT without executing it or saving game data.

The strict generic contract recognizes two reviewed Buzz driver compiler shapes.
It is deliberately narrower than 'any executable importing cellUsbd'.
"""
import argparse
import json
import pathlib
import struct
import sys
import zlib
import hashlib
from game_elf import Elf

ROOT=pathlib.Path(__file__).resolve().parents[1]
NIDS=[0xd0e766fe,0x35f22ac3,0x7fe92c54,0xbd554bcb,0x90460081,
      0x2fb08e1e,0x254289ac,0x9763e962,0x97cf128e,0x0f411262,
      0x63bfdb97,0x5de3af36,0xdb819e03,0x8b72cda1]
NAMES=['Init','End','RegisterExtraLdd','RegisterExtraLdd2','UnregisterExtraLdd',
       'ScanStaticDescriptor','OpenPipe','ClosePipe','ControlTransfer','InterruptTransfer',
       'SetPrivateData','GetPrivateData','GetDeviceLocation','cellPadGetData']
NAMES={n:('cellUsbd'+s if i<13 else s) for i,(n,s) in enumerate(zip(NIDS,NAMES))}
NAMES.update({int(k,16):v for k,v in json.loads((ROOT/'tools/cellusbd_names.json').read_text()).items()})
def fnid(name):
    return int.from_bytes(hashlib.sha1(name.encode()+bytes.fromhex('6759659904250490566427499489741a')).digest()[:4],'little')
assert fnid('cellUsbdInit')==NIDS[0]
for _name in ('cellUsbdRegisterLdd','cellUsbdUnregisterLdd'):
    NAMES[fnid(_name)]=_name


def branch(e, address):
    w=e.u32(address)
    if w>>26!=18 or w&2:
        raise ValueError('expected relative branch')
    disp=w&0x3fffffc
    if disp&0x2000000: disp-=0x4000000
    return address+disp


def ldd_candidates(e):
    result=[]
    for p in e.loads:
        if p[1]&1: continue
        for off in range(0,p[5]-23,4):
            a=p[3]+off
            vid,lo,hi=struct.unpack_from('>HHH',e.data,p[2]+off+16)
            if not (vid==0x54c and (lo<=2<=hi or lo<=0x1000<=hi) or vid==0x46d and lo<=0xc220<=hi): continue
            try:
                name,probe,attach,detach=struct.unpack('>4I',e.read(a,16))
                text=e.string(name,64)
                callbacks=[e.u32(v) for v in (probe,attach,detach)]
                if not text or any(not any(h[1]&1 and h[3]<=v<h[3]+h[5] for h in e.loads) for v in callbacks): continue
                result.append(dict(address=a,name=text,vid=vid,pid_min=lo,pid_max=hi,
                                   opds=[probe,attach,detach],callbacks=callbacks))
            except ValueError: pass
    return result


def normalized_crc(e,units,tocs,imports):
    """Retain literals/local flow; canonicalize only inter-unit addresses/TOC loads.

    Cross-unit branches carry region+offset; known import calls carry their FNID.
    Other external calls carry a zero target. These are compatibility shapes,
    not cryptographic authentication of an untrusted executable.
    """
    stub_nids={x['stub']:x['fnid'] for x in imports}
    def coordinate(address):
        for i,(a,n) in enumerate(units):
            if a<=address<a+n: return ((i+1)<<24)|(address-a)
        return 0
    crcs=[]
    for unit,(start,size) in enumerate(units):
        words=[]
        for at in range(start,start+size,4):
            w=e.u32(at);extra=0
            if w>>26==18 and not w&2:
                dest=branch(e,at)
                if not start<=dest<start+size:
                    w &= 0xfc000003
                    extra=stub_nids.get(dest,coordinate(dest))
            elif w>>26==32 and (w>>16)&31==2: # lwz reg,offset(r2)
                off=struct.unpack('>h',struct.pack('>H',w&65535))[0]
                value=e.u32(tocs[unit]+off)
                extra=coordinate(value)
                if not extra:
                    try:
                        code,toc=struct.unpack('>II',e.read(value,8))
                    except ValueError: code=toc=0
                    extra=coordinate(code)
                    if extra and toc!=tocs[unit]: raise ValueError('callback TOC mismatch')
                w &= 0xffff0000
            words.extend((w,extra))
        crcs.append(zlib.crc32(struct.pack('>'+str(len(words))+'I',*words)))
    return crcs


def resolve(e, families):
    begin,end,im=e.imports()
    selected=[]
    for nid in NIDS:
        lib='sys_io' if nid==NIDS[-1] else 'cellUsbd'
        found=[x for x in im if x['library']==lib and x['fnid']==nid]
        if len(found)!=1: raise ValueError(f'{NAMES[nid]} found {len(found)} times')
        selected.append(found[0])
    if len({x['slot'] for x in selected})!=14:
        raise ValueError('aliased import slots')
    # All thirteen imports are handled. Unknown USB imports could escape local IDs.
    usb=[x for x in im if x['library']=='cellUsbd']
    if len(usb)!=13: raise ValueError('unsupported extra cellUsbd imports')
    # Standard compact-OPD import thunk, including reconstructed slot address.
    for x in selected:
        words=struct.unpack('>8I',e.read(x['stub'],32))
        if words[0]!=0x39800000 or words[1]&0xffff0000!=0x658c0000 or words[2]&0xffff0000!=0x818c0000:
            raise ValueError('unsupported import trampoline shape')
        slot=((words[1]&65535)<<16)+struct.unpack('>h',struct.pack('>H',words[2]&65535))[0]
        if slot!=x['slot'] or words[3:]!=(0xf8410028,0x800c0000,0x804c0004,0x7c0903a6,0x4e800420):
            raise ValueError('import trampoline/slot mismatch')
    calls={nid:[] for nid in NIDS}
    targets={x['stub']:x['fnid'] for x in selected}
    for at,dest,link in e.branches():
        if link and dest in targets: calls[targets[dest]].append(at)
    if len(calls[NIDS[3]])!=1 or len(calls[NIDS[0]])!=1 or len(calls[NIDS[9]])!=2:
        raise ValueError('registration/init/interrupt callsite is missing or ambiguous')
    candidates=ldd_candidates(e)
    wired=[c for c in candidates if c['pid_min']==2 and c['pid_max']==0x42 and c['name']=='buzzer2']
    if len(wired)!=1: raise ValueError(f'wired buzzer2 LDD found {len(wired)} times')
    ldd=wired[0];table=ldd['address']-48
    expected=[('buzzer0',0x54c,0x1000,0x1040),('buzzer1',0x54c,1,0x41),('buzzer2',0x54c,2,0x42),('buzzlog',0x46d,0xc220,0xc220)]
    # Names 0/3 vary in spelling; the full names are shape-independent diagnostic data.
    for i,(_,vid,lo,hi) in enumerate(expected):
        rec=e.read(table+24*i,24)
        if rec[4:16]!=e.read(ldd['address']+4,12) or struct.unpack_from('>HHH',rec,16)!=(vid,lo,hi):
            raise ValueError('four-record LDD/filter shape mismatch')
    if e.read(table+96+16,2)!=b'\0\0': raise ValueError('missing LDD sentinel')
    matched=[]
    for family in families:
        try:
            reg=calls[NIDS[3]][0];intr=min(calls[NIDS[9]])
            manager=reg+family['manager_delta'];core=intr+family['core_delta']
            getter=manager+family['getter_offset']
            w=e.u32(getter)
            if w&0xffff0000!=0x81220000: continue
            root_slot=e.toc+struct.unpack('>h',struct.pack('>H',w&65535))[0]
            root=e.u32(root_slot)
            core_toc=e.toc+family['core_toc_delta']
            callbacks=[];callback_slots=[]
            for at in range(core,core+family['core_size'],4):
                w=e.u32(at)
                if w&0xffff0000==0x80c20000:
                    slot=core_toc+struct.unpack('>h',struct.pack('>H',w&65535))[0]
                    opd=e.u32(slot);code=e.u32(opd)
                    if e.u32(opd+4)!=core_toc: raise ValueError('USB callback TOC mismatch')
                    callbacks.append((opd,code));callback_slots.append(slot)
            if len(set(callbacks))!=6: continue
            cb=min(c for o,c in callbacks)
            units=[(manager,family['manager_size']),(core,family['core_size']),(cb,family['callback_size'])]
            if family['bridge_size']:
                bridge=branch(e,manager+family['bridge_call_offset'])+family['bridge_delta']
                units.append((bridge,family['bridge_size']))
            tocs=[e.toc,core_toc,core_toc]+([e.toc] if len(units)==4 else [])
            # Registration must load this exact table; every LDD callback belongs to this manager.
            regfun=reg+family['registration_delta']
            rw=e.u32(regfun)
            if rw&0xffff0000!=0x81220000: continue
            table_slot=e.toc+struct.unpack('>h',struct.pack('>H',rw&65535))[0]
            if e.u32(table_slot)!=table: continue
            if ldd['callbacks']!=[manager+v for v in family['ldd_offsets']]: continue
            if normalized_crc(e,units,tocs,selected)!=family['crcs']: continue
            matched.append(dict(family=family['name'],begin=begin,end=end,slots=[x['slot'] for x in selected],
                stubs=[x['stub'] for x in selected],ldd=ldd['address'],table=table,root_slot=root_slot,
                manager=root+8,ready=root+8+0x94,count=root+8+0xa8,presence=[root+8+0x69,root+8+0x8d],
                opds=ldd['opds'],units=units,tocs=tocs,callback_slots=callback_slots,callbacks=callbacks,
                table_slot=table_slot,imports=selected))
        except (ValueError,struct.error): continue
    if len(matched)!=1: raise ValueError(f'reviewed driver shape found {len(matched)} times')
    return matched[0]


def _inspect(data, generic=True):
    e=Elf(data)
    catalog=json.loads((ROOT/'plugin/game/profiles.json').read_text())
    begin,end,imports=e.imports()
    result=dict(sha256=e.sha256,toc=e.toc,import_begin=begin,import_end=end,
                imports=[dict(x,name=NAMES.get(x['fnid'],'UNKNOWN')) for x in imports if x['library']=='cellUsbd'],
                ldd_candidates=ldd_candidates(e))
    registrations=[x for x in imports if x['library']=='cellUsbd' and x['fnid'] in (NIDS[2],NIDS[3],fnid('cellUsbdRegisterLdd'))]
    edges=list(e.branches())
    result['registrations']=[dict(name=NAMES[x['fnid']],fnid=x['fnid'],slot=x['slot'],calls=[a for a,t,link in edges if link and t==x['stub']]) for x in registrations]
    try:
        found=resolve(e,catalog['families'])
        for p in catalog['profiles']:
            r=p['resolved']
            if r['slots']==found['slots'] and r['ldd']==found['ldd']:
                if any(zlib.crc32(e.read(f['address'],f['size']))!=f['crc'] for f in p['fingerprints']):
                    raise ValueError('known layout fingerprint mismatch; no generic fallback')
        exact=[p for p in catalog['profiles'] if p['sha256']==e.sha256]
        mode='exact profile' if len(exact)==1 else 'generic' if generic else 'refuse'
        result.update(mode=mode,reason='hash and structural checks pass' if exact else 'UNVERIFIED; reviewed driver shape' if generic else 'generic=0',resolved=found)
        from make_profiles import fingerprints
        title=exact[0]['title'] if exact else 'YOURTITLE'
        result['profile_entry']=dict(title=title,app_ver='01.00',sha256=e.sha256,
            fingerprints=fingerprints(e,found,title),resolved={k:v for k,v in found.items() if k!='imports'})
    except ValueError as exc: result.update(mode='refuse',reason=str(exc))
    return result


def inspect(data, generic=True):
    try:
        return _inspect(data,generic)
    except (ValueError,struct.error) as exc:
        import hashlib
        return dict(sha256=hashlib.sha256(data).hexdigest(),imports=[],ldd_candidates=[],mode='refuse',reason=str(exc))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('elf',type=pathlib.Path)
    ap.add_argument('--json',action='store_true')
    ap.add_argument('--no-generic',action='store_true')
    args=ap.parse_args()
    try: result=inspect(args.elf.read_bytes(),not args.no_generic)
    except (ValueError,OSError,struct.error) as exc: ap.exit(1,f'REFUSE: {exc}\n')
    if args.json: print(json.dumps(result,indent=2))
    else:
        print('SHA256',result['sha256'])
        for x in result['imports']:print(f"{x['fnid']:08x} -> {x['name']} -> slot {x['slot']:08x}")
        for c in result['ldd_candidates']:print(f"LDD {c['address']:08x} {c['name']}: {c['vid']:04x}:{c['pid_min']:04x}-{c['pid_max']:04x}")
        for c in result.get('registrations',[]):print(c['name']+' registration sites: '+', '.join(f'{a:08x}' for a in c['calls']))
        print(result['mode'].upper()+': '+result['reason'])
        if 'profile_entry' in result:print('Ready-to-paste profile metadata:\n'+json.dumps(result['profile_entry'],indent=2))
    return 0 if result['mode']!='refuse' else 2


if __name__=='__main__': sys.exit(main())
