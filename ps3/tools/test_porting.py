"""PC tests: synthetic discs, malformed inputs and optional own retail inputs.

All retail ELF/SELF/ISO bytes remain in RAM or their original read-only location.
Only derived facts and test results may be saved in the checkout.
"""
import ctypes as C
import io
import json
import os
import pathlib
import struct
import subprocess
import sys
import unittest
from extract_eboot import Iso, SECTOR, decrypt_eboot
from game_elf import Elf
from inspect_game import ROOT, inspect, resolve

DEFAULT=pathlib.Path(__import__('os').environ.get('BUZZ_GAME_INPUTS','game-inputs'))
OWN=[('BCES00347','game'),('BCES00303','game_quiztv'),('BCES00644','game_quizworld')]

def record(name,sector,length,directory=False):
    name=name.encode() if isinstance(name,str) else name
    n=33+len(name);n+=n%2
    b=bytearray(n);b[0]=n
    struct.pack_into('<I',b,2,sector);struct.pack_into('>I',b,6,sector)
    struct.pack_into('<I',b,10,length);struct.pack_into('>I',b,14,length)
    b[25]=2 if directory else 0;b[28]=b[31]=1;b[32]=len(name);b[33:33+len(name)]=name
    return b

def disc(eboot,sfo,encrypted=False):
    e_sector=40;s_sector=e_sector+(len(eboot)+SECTOR-1)//SECTOR
    total=s_sector+(len(sfo)+SECTOR-1)//SECTOR+2
    b=bytearray(total*SECTOR)
    struct.pack_into('>6I',b,0,2,0,0,39,total-1,total-1)
    p=16*SECTOR;b[p:p+7]=b'\1CD001\1'
    b[p+128:p+132]=b'\0\x08\x08\0';b[p+156:p+190]=record(b'\0',20,SECTOR,True)
    b[17*SECTOR:17*SECTOR+7]=b'\xffCD001\1'
    b[20*SECTOR:20*SECTOR+42]=record('PS3_GAME',21,SECTOR,True)
    game=record('USRDIR',22,SECTOR,True)+record('PARAM.SFO;1',s_sector,len(sfo))
    b[21*SECTOR:21*SECTOR+len(game)]=game
    r=record('EBOOT.BIN;1',e_sector,len(eboot));b[22*SECTOR:22*SECTOR+len(r)]=r
    b[e_sector*SECTOR:e_sector*SECTOR+len(eboot)]=eboot
    b[s_sector*SECTOR:s_sector*SECTOR+len(sfo)]=sfo
    key=bytes(range(16))
    if encrypted:
        from Crypto.Cipher import AES
        for sector in range(40,total-1):
            a=sector*SECTOR;b[a:a+SECTOR]=AES.new(key,AES.MODE_CBC,sector.to_bytes(16,'big')).encrypt(b[a:a+SECTOR])
    return bytes(b),key

class Layout(C.Structure):
    _fields_=[('begin',C.c_uint32),('end',C.c_uint32),('slots',C.c_uint32*14),('stubs',C.c_uint32*14),
              ('ldd',C.c_uint32),('ready',C.c_uint32),('count',C.c_uint32),('presence',C.c_uint32*2),
              ('opds',C.c_uint32*3),('profile',C.c_int),('family',C.c_uint)]
READ=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint32,C.c_void_p,C.c_size_t)
class Memory(C.Structure):
    _fields_=[('arg',C.c_void_p),('read',READ),('replace',C.c_void_p),('valid_opd',C.c_void_p)]

def runtime(elf):
    dll=C.CDLL(str(ROOT/'build/resolve_game.dll'))
    fn=dll.buzz_game_resolve;fn.argtypes=[C.POINTER(Memory),C.POINTER(Layout),C.POINTER(C.c_char_p)]
    @READ
    def read(arg,address,out,size):
        for p in elf.loads:
            if p[3]<=address and address+size<=p[3]+p[6]:
                offset=address-p[3];raw=elf.data[p[2]+offset:p[2]+min(offset+size,p[5])] if offset<p[5] else b''
                raw+=bytes(size-len(raw));C.memmove(out,raw,size);return 1
        return 0
    m=Memory(None,read,None,None);layout=Layout();reason=C.c_char_p()
    ok=fn(C.byref(m),C.byref(layout),C.byref(reason))
    return ok,layout,reason.value.decode()

def moved_data(e):
    """RAM-only relocation fixture: code stays put, data/TOC/slots move 64 KiB."""
    p=next(p for p in e.loads if p[1]&2)
    old,end=p[3],p[3]+p[6];delta=0x10000;b=bytearray(e.data)
    for h in e.loads:
        for off in range(0,h[5]-3,4):
            at=h[2]+off;v=struct.unpack_from('>I',b,at)[0]
            if old<=v<end:struct.pack_into('>I',b,at,v+delta)
    # ELF header and PHDR addresses are 64-bit, so the pass above updates low words too.
    for x in e.imports()[2]:
        at=next(h[2]+x['stub']-h[3] for h in e.loads if h[3]<=x['stub']<h[3]+h[5])
        slot=x['slot']+delta
        struct.pack_into('>I',b,at+4,0x658c0000|((slot+0x8000)>>16))
        struct.pack_into('>I',b,at+8,0x818c0000|(slot&65535))
    return Elf(bytes(b))

class ToolTests(unittest.TestCase):
    def test_iso_modes_and_bad_keys(self):
        for enc in (False,True):
            data,key=disc(b'SCE\0'+bytes(range(256))*17,b'\0PSFsynthetic',enc)
            iso=Iso(io.BytesIO(data),key if enc else None)
            self.assertEqual(iso.find('PS3_GAME/USRDIR/EBOOT.BIN'),b'SCE\0'+bytes(range(256))*17)
            self.assertEqual(iso.find('PS3_GAME/PARAM.SFO'),b'\0PSFsynthetic')
            with self.assertRaises(ValueError):iso.find('PS3_GAME/MISSING')
        data,key=disc(b'SCE\0payload',b'\0PSF',True)
        self.assertNotEqual(Iso(io.BytesIO(data),bytes(16)).find('PS3_GAME/USRDIR/EBOOT.BIN')[:4],b'SCE\0')
        with self.assertRaises(ValueError):decrypt_eboot(b'not a SELF')
        with self.assertRaises(ValueError):Iso(io.BytesIO(data[:-1]))
        b=bytearray(data);struct.pack_into('>I',b,0,33)
        with self.assertRaises(ValueError):Iso(io.BytesIO(b),key)
        b=bytearray(data);b[20*SECTOR]=1
        with self.assertRaises(ValueError):Iso(io.BytesIO(b)).find('PS3_GAME/USRDIR/EBOOT.BIN')

    def test_bad_elf(self):
        for b in (b'',b'SCE\0'+bytes(100),b'\x7fELF\2\2'+bytes(100)):
            with self.assertRaises(ValueError):Elf(b)

def own_test(title,folder):
    def test(self):
        root=pathlib.Path(os.environ.get('PS3BUZZ_INPUTS',str(DEFAULT)))/folder
        path=root/'EBOOT.elf'
        if not path.is_file():self.skipTest('own ELF absent: '+title)
        e=Elf(path.read_bytes());result=inspect(e.data)
        self.assertEqual(result['mode'],'exact profile',result.get('reason'))
        self.assertEqual(len(result['imports']),13)
        ok,layout,reason=runtime(e);self.assertTrue(ok,reason);self.assertGreaterEqual(layout.profile,0)
        self.assertEqual(list(layout.slots),result['resolved']['slots']);self.assertEqual(layout.ready,result['resolved']['ready'])
        moved=moved_data(e)
        self.assertEqual(inspect(moved.data)['mode'],'generic',inspect(moved.data).get('reason'))
        self.assertEqual(inspect(moved.data,False)['mode'],'refuse')
        ok,generic,reason=runtime(moved);self.assertTrue(ok,reason);self.assertEqual(generic.profile,-1)
        self.assertEqual(generic.ldd,layout.ldd+0x10000)
        # Mutations of required pieces must refuse both offline and runtime discovery.
        r=result['resolved'];bgn,end,imports=e.imports()
        bad=[]
        def changed(va,raw):
            b=bytearray(e.data)
            h=next(h for h in e.loads if h[3]<=va and va+len(raw)<=h[3]+h[5]);at=h[2]+va-h[3]
            b[at:at+len(raw)]=raw;return bytes(b)
        bad.append(changed(r['ldd']+18,b'\0\3'))
        bad.append(changed(r['ldd']+4,struct.pack('>I',r['opds'][1])))
        bad.append(changed(r['stubs'][0]+4,bytes(4)))
        bad.append(changed(r['units'][0][0]+8,bytes(4)))
        bad.append(changed(bgn,b'\x28'))
        usb=next(x for x in imports if x['library']=='cellUsbd');nt=e.u32(usb['header']+20)
        bad.append(changed(nt,struct.pack('>I',e.u32(nt+4))))
        bad.append(changed(usb['header']+8,b'\0\1'))
        bad.append(changed(usb['header']+20,struct.pack('>I',nt+1)))
        # Second well-formed table, elsewhere in writable file data: ambiguity.
        data_seg=next(h for h in e.loads if h[1]&2)
        bad.append(changed((data_seg[3]+data_seg[5]-128)&~3,e.read(r['table'],120)))
        if title=='BCES00347':bad.append(changed(0xa292c,b'\x38\xa0\0\1'))
        for b in bad:
            self.assertEqual(inspect(b)['mode'],'refuse')
            self.assertFalse(runtime(Elf(b))[0])
        # Exercise extraction/decryption on each actual own SELF with RAM-only ISO fixtures.
        self_path=root/'EBOOT.BIN';sfo_path=root/'PARAM.SFO'
        if not self_path.is_file() or not sfo_path.is_file():
            self.skipTest('ELF inspector passed; own SELF/SFO absent: '+title)
        raw=self_path.read_bytes();sfo=sfo_path.read_bytes()
        self.assertEqual(Elf(decrypt_eboot(raw)).sha256,e.sha256)
        for encrypted in (False,True):
            data,key=disc(raw,sfo,encrypted);iso=Iso(io.BytesIO(data),key if encrypted else None)
            self.assertEqual(iso.find('PS3_GAME/PARAM.SFO'),sfo)
            self.assertEqual(Elf(decrypt_eboot(iso.find('PS3_GAME/USRDIR/EBOOT.BIN'))).sha256,e.sha256)
        # Optional original ISO supplied by its owner, kept read-only.
        iso_path=os.environ.get('PS3BUZZ_ISO_'+title)
        if iso_path:
            from extract_eboot import extract_bytes,disc_key
            dkey=os.environ.get('PS3BUZZ_DKEY_'+title)
            raw,sfo=extract_bytes(iso_path,disc_key(dkey) if dkey else None)
            self.assertEqual(Elf(decrypt_eboot(raw)).sha256,e.sha256)
        print(title,'PASS: Python + actual C resolver; generic relocation; refusals; SELF + plain/encrypted RAM ISO',flush=True)
    return test

for title,folder in OWN:setattr(ToolTests,'test_own_'+title,own_test(title,folder))

if __name__=='__main__':
    compiler=os.environ.get('CC',r'C:\Program Files\LLVM\bin\clang.exe')
    task_tmp=ROOT/'build/tmp';task_tmp.mkdir(parents=True,exist_ok=True)
    compiler_env=dict(os.environ,TEMP=str(task_tmp),TMP=str(task_tmp),TMPDIR=str(task_tmp))
    subprocess.run([compiler,'-shared','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        str(ROOT/'plugin/game/resolve_game.c'),'-Wl,/export:buzz_game_resolve,/export:buzz_game_exact',
        '-o',str(ROOT/'build/resolve_game.dll')],check=True,cwd=ROOT,env=compiler_env)
    unittest.main(verbosity=2)
