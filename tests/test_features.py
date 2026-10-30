"""C/Python byte equality, recovery, malformed-input and hash vectors."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from build_pc import build, ROOT, OUT
sys.path.insert(0,str(ROOT/'ps2/universal/tests'))
from test_patcher import build_iso, fake_usbd, bb_patch

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

class Features(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        hashes={hashlib.sha1(fake_usbd(n,b'usbd'+f.encode())).hexdigest():'synthetic'
                for f in ('IOP','SYSTEM') for n in (8000,34985,34993,35009)}
        (ROOT/'tests/test_iso_hashes.h').write_text('/* Host test ONLY. Never compiled into PPU app. */\n'+''.join('"'+h+'",\n' for h in hashes))
        cls.known=hashes;cls.tool=build(True);cls.prod=build(False)
        cls.irx=ROOT/'ps2/universal/bbusbd.irx'
        cls.tmp=tempfile.TemporaryDirectory(prefix='features-',dir=OUT)
        cls.base=Path(cls.tmp.name)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def call(self,*args,ok=True,production=False):
        r=subprocess.run([str(self.prod if production else self.tool),*map(str,args)],capture_output=True,text=True,timeout=90)
        self.assertEqual(r.returncode,0 if ok else 1,r.stdout+r.stderr)
        return r.stdout
    def patch(self,path,layout=0,flags=0x11,counts=(0,0,0),check=0,ok=True):
        return self.call('patch',path,self.irx,layout,flags,*counts,100,100,check,ok=ok)
    def fixture(self,name,folder='IOP',n=34985,**kwargs):
        path=self.base/name;meta=build_iso(path,folder,n,**kwargs);return path,meta
    def oracle(self,src,target,layout,flags=0x11,counts=None):
        args=[str(src),'--out',str(target),'--irx',str(self.irx),'--poll',{0:'off',16:'auto',32:'always'}[flags&0x30]]
        if layout==13:
            args += ['--buzzer-pairs',str(counts[0]),'--whole-ds',str(counts[1]),'--shared-ds',str(counts[2])]
        else:args += ['--layout',str(layout)]
        if not flags&1:args+=['--quiet']
        if flags&2:args+=['--no-pad']
        if flags&4:args+=['--script-demo']
        if flags&8:args+=['--diag-tap']
        import contextlib,io
        with mock.patch.dict(bb_patch.KNOWN_USBD,self.known),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(bb_patch.main(args),0)
    def test_crypto_vectors(self):self.call('crypto')
    def test_every_layout_all_repo_fixture_families(self):
        count=0
        for folder in ('IOP','SYSTEM'):
            for length in (34985,34993,35009):
                src,_=self.fixture('src-%s-%d.iso'%(folder,length),folder,length)
                original=src.read_bytes()
                for layout in range(14):
                    name='%s-%d-%d'%(folder,length,layout)
                    c=self.base/(name+'-c.iso');p=self.base/(name+'-py.iso');c.write_bytes(original)
                    counts=(1,2,0) if layout==13 else None
                    self.patch(c,layout,counts=counts or (0,0,0));self.oracle(src,p,layout,counts=counts)
                    self.assertEqual(c.read_bytes(),p.read_bytes(),name)
                    self.call('restore',c,0);self.assertEqual(c.read_bytes(),original,name+' restore')
                    count+=1
        print('C/Python fixture/layout byte comparisons and exact restores:',count)
    def test_poll_and_diagnostic_flags(self):
        src,_=self.fixture('flags-source.iso');original=src.read_bytes();count=0
        for layout in range(13):
            for flags in (0,1,0x10,0x11,0x21,0x13,0x15):
                c=self.base/('flag%d-%d-c.iso'%(layout,flags));p=self.base/('flag%d-%d-p.iso'%(layout,flags));c.write_bytes(original)
                self.patch(c,layout,flags);self.oracle(src,p,layout,flags);self.assertEqual(c.read_bytes(),p.read_bytes());count+=1
        for layout in (5,6,7,8):
            c=self.base/('diag%d-c.iso'%layout);p=self.base/('diag%d-p.iso'%layout);c.write_bytes(original)
            self.patch(c,layout,0x19);self.oracle(src,p,layout,0x19);self.assertEqual(c.read_bytes(),p.read_bytes());count+=1
        print('C/Python flag/layout comparisons:',count)
    def test_custom_counts_and_wizard_presets(self):
        src,_=self.fixture('custom-source.iso');original=src.read_bytes();count=0
        # Every custom count combination the module supports (not just menu presets).
        for pairs in range(3):
            for whole in range(9):
                for shared in range(5):
                    if not 1<=2*pairs+whole+2*shared<=8:continue
                    counts=(pairs,whole,shared);name='custom-%d-%d-%d'%counts
                    c=self.base/(name+'-c.iso');p=self.base/(name+'-p.iso');c.write_bytes(original)
                    self.patch(c,13,counts=counts);self.oracle(src,p,13,counts=counts);self.assertEqual(c.read_bytes(),p.read_bytes());count+=1
        self.assertEqual(bb_patch.MENU[5][3],(1,2,0))
        print('C/Python custom-count comparisons:',count)
    def test_restore_interrupted_extent_and_refuse_unrelated(self):
        p,meta=self.fixture('interrupted.iso');orig=p.read_bytes();self.patch(p,2);patched=p.read_bytes();off=meta['usbd_lba']*2048
        mixed=bytearray(orig)
        for j in range(len(meta['usbd'])):
            if j%2:mixed[off+j]=patched[off+j]
        p.write_bytes(mixed);self.call('restore',p,1);self.assertEqual(p.read_bytes(),mixed)
        self.call('restore',p,0);self.assertEqual(p.read_bytes(),orig)
        bad=bytearray(patched);j=off+100;bad[j]=next(x for x in range(256) if x not in (orig[j],patched[j]));p.write_bytes(bad)
        self.call('restore',p,0,ok=False);self.assertEqual(p.read_bytes(),bad)
    def test_check_only_no_sidecars_and_production_refuses_synthetic(self):
        p,_=self.fixture('check.iso');before=p.read_bytes();self.patch(p,4,check=1)
        self.assertEqual(p.read_bytes(),before);self.assertFalse(Path(str(p)+'.usbd.orig').exists())
        self.call('patch',p,self.irx,4,17,0,0,0,100,100,0,ok=False,production=True)
        self.assertEqual(p.read_bytes(),before)
    def test_repatch_after_restore_preserves_original_backup(self):
        p,_=self.fixture('new-layout.iso');before=p.read_bytes();self.patch(p,0)
        backup=Path(str(p)+'.usbd.orig');h=sha(backup);self.call('restore',p,0);self.patch(p,4)
        self.assertEqual(sha(backup),h);self.call('restore',p,0);self.assertEqual(p.read_bytes(),before)
    def test_refusal_cases_never_change_iso(self):
        small,_=self.fixture('small.iso',n=8000);before=small.read_bytes();self.patch(small,ok=False);self.assertEqual(small.read_bytes(),before)
        unknown,_=self.fixture('unknown.iso',usbd_data=b'X'*34985);before=unknown.read_bytes();self.patch(unknown,ok=False);self.assertEqual(unknown.read_bytes(),before)
        for kind,at,value in (('endian',16*2048+156+6,99),('pvd',16*2048,2),('multi',21*2048+68+25,128),('interleave',21*2048+68+26,1)):
            p,_=self.fixture(kind+'.iso');b=bytearray(p.read_bytes());b[at]=value;p.write_bytes(b);self.patch(p,ok=False);self.assertEqual(p.read_bytes(),b)
        p,_=self.fixture('already.iso');self.patch(p);before=p.read_bytes();self.patch(p,ok=False);self.assertEqual(p.read_bytes(),before)
        p,_=self.fixture('existing-backup.iso');before=p.read_bytes();Path(str(p)+'.usbd.orig').write_bytes(b'do not overwrite');self.patch(p,ok=False);self.assertEqual(p.read_bytes(),before);self.assertEqual(Path(str(p)+'.usbd.orig').read_bytes(),b'do not overwrite')
    def test_restore_rejects_sidecar_tampering(self):
        for kind in ('original','patched','length','size','offset','path','trailing'):
            p,_=self.fixture('side-'+kind+'.iso');original=p.read_bytes();self.patch(p);before=p.read_bytes()
            side=Path(str(p)+'.bbps3.json');text=side.read_text()
            if kind in ('original','patched'):
                f=Path(str(p)+('.usbd.orig' if kind=='original' else '.usbd.buzz'));b=bytearray(f.read_bytes());b[200]^=1;f.write_bytes(b)
            elif kind=='trailing':side.write_text(text+'bad')
            else:
                k={'length':'length','size':'image_size','offset':'offset','path':'path_index'}[kind]
                v=json.loads(text)[k];side.write_text(text.replace('"%s":%d'%(k,v),'"%s":%d'%(k,v+1)))
            self.call('restore',p,0,ok=False);self.assertEqual(p.read_bytes(),before)
if __name__=='__main__':unittest.main(verbosity=2)
