"""Regenerate derived profiles from explicitly supplied, SHA256-checked own ELFs.

Never saves input bytes. The generated JSON/header are sufficient to rebuild.
"""
import argparse
import json
import pathlib
import zlib
import re
import inspect_game as inspect
from game_elf import Elf

ROOT=pathlib.Path(__file__).resolve().parents[1]
PINNED={
 'BCES00347':'6f4d1f05cde3d8a6e7836b65c7103cff0ccc6a248ca0b877a7da3e3453585a19',
 'BCES00303':'11848045c3884d68b250ab849dcd3d0e0e071e50327246a4f8936e972d70de6d',
 'BCES00644':'6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8'}

def recipe(world):
    if world:
        return dict(name='CellPort-QW',manager_delta=-0x600,manager_size=0x13c0,core_delta=-0x240,core_size=0xd28,
            getter_offset=0x968,registration_delta=-0x50,core_toc_delta=0xffe8,callback_size=0x70,
            bridge_size=0xa0,bridge_call_offset=0xdf8,bridge_delta=-0x60,ldd_offsets=[0xa08,0xe90,0x9d0])
    return dict(name='CellPort-QTV',manager_delta=-0x8c0,manager_size=0x1900,core_delta=-0x280,core_size=0xeb0,
        getter_offset=0xc80,registration_delta=-0x58,core_toc_delta=0,callback_size=0x100,
        bridge_size=0,bridge_call_offset=0,bridge_delta=0,ldd_offsets=[0xd40,0x1330,0xcf8])

def fingerprints(e,found,title):
    ranges=list(found['units'])+[(found['table'],120),(found['begin'],found['end']-found['begin'])]
    ranges += [(v,32) for v in found['stubs']]
    ranges += [(v,8) for v in found['opds']]
    ranges += [(v,8) for v,c in found['callbacks']]
    ranges += [(v,4) for v in found['callback_slots']+[found['root_slot'],found['table_slot']]]
    if title=='BCES00347':
        ranges += [(v,4) for v in (0x9f904,0xa292c,0xa2904,0x9f040)]
    # Import FNID tables (the resolved pointer slots are writable and excluded).
    for x in found['imports']:
        h=e.read(x['header'],44);count=int.from_bytes(h[6:8],'big');ids=int.from_bytes(h[20:24],'big')
        ranges.append((ids,count*4))
    result=[]
    for address,size in sorted(set(ranges)):
        for at in range(address,address+size,1024):
            n=min(1024,address+size-at)
            result.append(dict(address=at,size=n,crc=zlib.crc32(e.read(at,n))))
    return result

def write_header(catalog):
    out=['/* Generated derived metadata only; no game bytes. tools/make_profiles.py */',
         '#ifndef BUZZ_PROFILE_H','#define BUZZ_PROFILE_H','#include <stdint.h>',
         'typedef struct { uint32_t address,size,crc; } buzz_fingerprint;',
         'typedef struct { const char *title,*version,*sha256; uint32_t begin,end,slots[14],ldd,ready,count,presence[2],opds[3]; const buzz_fingerprint *checks; unsigned check_count; } buzz_exact_profile;']
    for i,p in enumerate(catalog['profiles']):
        out.append(f'static const buzz_fingerprint buzz_checks_{i}[]={{')
        out += [' {0x%08xu,%uu,0x%08xu},'%(x['address'],x['size'],x['crc']) for x in p['fingerprints']]
        out.append('};')
    out.append('static const buzz_exact_profile buzz_profiles[]={')
    def array(v):return '{'+','.join('0x%xu'%x for x in v)+'}'
    for i,p in enumerate(catalog['profiles']):
        r=p['resolved']
        out.append(' {"%s","%s","%s",0x%xu,0x%xu,%s,0x%xu,0x%xu,0x%xu,%s,%s,buzz_checks_%u,%uu},'%(
            p['title'],p['app_ver'],p['sha256'],r['begin'],r['end'],array(r['slots']),r['ldd'],r['ready'],r['count'],
            array(r['presence']),array(r['opds']),i,len(p['fingerprints'])))
    out += ['};','#define BUZZ_PROFILE_COUNT (sizeof(buzz_profiles)/sizeof(buzz_profiles[0]))','#endif','']
    (ROOT/'plugin/game/profile.h').write_text('\n'.join(out),encoding='utf-8')
    # Keep integer field order in sync with resolve_game.h; string/CRC/arrays explicit.
    fields=['manager_delta','manager_size','core_delta','core_size','getter_offset','registration_delta',
            'core_toc_delta','callback_size','bridge_size','bridge_call_offset','bridge_delta']
    out=['/* Generated structural CRCs and offsets, never game instructions. */','static const buzz_driver_family buzz_families[]={']
    for f in catalog['families']:
        out.append(' {"%s",%s,%s,%s},'%(f['name'],','.join(str(f[k]) for k in fields),array(f['ldd_offsets']),array(f['crcs'])))
    out+=['};','']
    (ROOT/'plugin/game/driver_shapes.h').write_text('\n'.join(out),encoding='utf-8')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input',action='append',default=[],metavar='TITLE=ELF')
    ap.add_argument('--add-exact',metavar='TITLE=ELF',help='append a hash-checked port that already passes generic inspection')
    ap.add_argument('--app-ver',default='01.00')
    args=ap.parse_args()
    if args.add_exact:
        if args.input:ap.error('choose --input or --add-exact')
        title,path=args.add_exact.split('=',1)
        if not re.fullmatch(r'[A-Z]{4}[0-9]{5}',title) or not re.fullmatch(r'[0-9]{2}\.[0-9]{2}',args.app_ver):
            ap.error('expected TITLE like BCES00347 and APP_VER like 01.00')
        catalog=json.loads((ROOT/'plugin/game/profiles.json').read_text())
        if any(p['title']==title and p['app_ver']==args.app_ver for p in catalog['profiles']):
            ap.error('profile already exists; review changes explicitly')
        e=Elf(pathlib.Path(path).read_bytes());r=inspect.resolve(e,catalog['families'])
        p=dict(title=title,app_ver=args.app_ver,sha256=e.sha256,fingerprints=fingerprints(e,r,title),resolved={k:v for k,v in r.items() if k!='imports'})
        catalog['profiles'].append(p)
        (ROOT/'plugin/game/profiles.json').write_text(json.dumps(catalog,indent=2)+'\n');write_header(catalog)
        print('Added exact profile',title,args.app_ver,e.sha256);return
    if not args.input:
        write_header(json.loads((ROOT/'plugin/game/profiles.json').read_text()))
        print('Headers regenerated from frozen derived profiles; no game input needed.')
        return
    families=[];profiles=[]
    for arg in args.input:
        title,path=arg.split('=',1);e=Elf(pathlib.Path(path).read_bytes())
        if title in PINNED and e.sha256!=PINNED[title]: raise ValueError('pinned input hash mismatch: '+title)
        f=recipe(title=='BCES00644');f['crcs']=[0]*(4 if f['bridge_size'] else 3)
        original=inspect.normalized_crc
        try:
            inspect.normalized_crc=lambda *a:f['crcs']
            r=inspect.resolve(e,[f])
        finally:inspect.normalized_crc=original
        f['crcs']=inspect.normalized_crc(e,r['units'],r['tocs'],r['imports'])
        r=inspect.resolve(e,[f])
        same=next((x for x in families if x==f),None)
        if same is None:
            if any(x['name']==f['name'] for x in families):f['name']+='-'+title
            families.append(f)
        r['family']=f['name']
        profiles.append(dict(title=title,app_ver='01.00',sha256=e.sha256,fingerprints=fingerprints(e,r,title),
            resolved={k:v for k,v in r.items() if k!='imports'}))
        print(title,e.sha256,'family',f['name'],'CRCs',list(map(hex,f['crcs'])))
    catalog=dict(families=families,profiles=profiles)
    (ROOT/'plugin/game/profiles.json').write_text(json.dumps(catalog,indent=2)+'\n')
    write_header(catalog)

if __name__=='__main__':main()
