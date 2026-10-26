"""Build four tiny Cobra diagnostic modules using installed tools only."""
import hashlib,json,os,pathlib,struct,subprocess
from prx_compat import fix
from native_prx import build as native_build
from prepare_native_link import prepare
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=pathlib.Path(os.environ.get('PS3DK',r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64'))
OUT=ROOT/'build/diag';OUT.mkdir(parents=True,exist_ok=True)
ENV=os.environ.copy();ENV.update(PS3DK=str(SDK),PS3DEV=SDK.as_posix(),PSL1GHT=SDK.as_posix(),
 TEMP=str(ROOT/'build/tmp'),TMP=str(ROOT/'build/tmp'))
ENV['PATH']=str(SDK/'bin')+os.pathsep+str(SDK/'ppu/bin')+os.pathsep+ENV['PATH']
REASONS={
 'E':'Control-like RX/RW ELF, unmapped headers, align 16, no sections, flags 0x01000000; normalized encrypted game APP (primary).',
 'F':'E with e_flags=0, testing the alternative documented by the SDK binutils patch.',
 'G':'Old C ELF geometry with only ELF flags corrected to 0x01000000; normalized APP container shared with E.',
 'H':'E ELF in an unencrypted fake-signed game APP container, testing signing/control policy independently.',
}
def run(*args):
 subprocess.run([str(a) for a in args],cwd=ROOT,env=ENV,check=True)
def symbols(path):
 text=subprocess.check_output([str(SDK/'ppu/bin/ppu-nm.exe'),'-g',str(path)],env=ENV,text=True)
 return {x[2]:int(x[0],16) for line in text.splitlines() if len(x:=line.split())==3 and x[1]!='U'}
def build():
 from sign_native_self import sign
 prepare()
 records=[]
 for variant,reason in REASONS.items():
  stem=OUT/f'ps3buzz_diag_{variant}'; elf=stem.with_suffix('.elf');prx=stem.with_suffix('.prx');sprx=stem.with_suffix('.sprx')
  ld=SDK/'ppu/lib/lv2-prx.ld' if variant=='G' else ROOT/'plugin/game/native-prx.ld'
  extra=[] if variant=='G' else ['-Wl,-z,max-page-size=16']
  run(SDK/'ppu/bin/ppu-gcc.exe','-O2','-Wall','-Wextra','-Werror','-fno-asynchronous-unwind-tables','-fno-unwind-tables',
      '-ffunction-sections','-fdata-sections','-nostdlib','-nostartfiles',f'-specs={SDK.as_posix()}/ppu/lib/lv2-prx.specs',
      f'-Wl,-T,{ld.as_posix()}','-Wl,-q','-Wl,--no-warn-rwx-segments',*extra,
      f'-DDIAG_VARIANT="{variant}"',ROOT/'plugin/diag/diag.c',SDK/'ppu/lib/lv2-prx-crt.o','-o',elf)
  sym=symbols(elf)
  if variant=='G':
   run(SDK/'bin/prx-gen.exe','build',elf,'-o',prx,'--name',f'ps3buzz_diag_{variant}','--version','0.4','--attributes','0')
   patch=fix(prx,28,sym['diag_cobra_start'],sym['diag_cobra_stop'])
   probe=bytearray(prx.read_bytes());struct.pack_into('>I',probe,48,0x01000000);prx.write_bytes(probe)
  else:
   patch=native_build(elf,prx,f'ps3buzz_diag_{variant}',0 if variant=='F' else 0x01000000,
                      'diag_cobra_start','diag_cobra_stop')
  signer='fself.exe + normalization' if variant=='H' else 'make_self.exe + normalized APP profile'
  sign(prx,sprx,variant=='H')
  data=sprx.read_bytes();blob=prx.read_bytes()
  # No library imports or unresolved symbols: the probe uses raw FS syscalls.
  assert not subprocess.check_output([str(SDK/'ppu/bin/ppu-nm.exe'),'-u',str(elf)],env=ENV,text=True).strip()
  info=struct.unpack_from('>Q',blob,64+24)[0]
  assert struct.unpack_from('>II',blob,info+44)[0]==struct.unpack_from('>II',blob,info+44)[1]
  records.append({'variant':variant,'reason':reason,'file':sprx.relative_to(ROOT).as_posix(),
   'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'signer':signer,'patch':patch,
   'marker':f'/dev_hdd0/tmp/ps3buzz_diag_{variant}.txt','marker_content':variant+'\n',
   'imports':0,'module_start_result':1,'cobra_explicit_unload_required':True})
 (OUT/'manifest.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
 print(json.dumps([{k:r[k] for k in ('variant','reason','file','bytes','sha256')} for r in records],indent=2))
if __name__=='__main__':build()
