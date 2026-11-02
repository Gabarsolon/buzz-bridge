from pathlib import Path
import struct,json,hashlib,subprocess,sys,os
H=Path(__file__).resolve().parent;LLVM=Path(r'C:\Program Files\LLVM\bin')
VARIANT=sys.argv[sys.argv.index('--variant')+1] if '--variant' in sys.argv else 'S4C6'
assert VARIANT in ('S4C6','S4L1','S4L3')
BASE=H/'dependencies/stock.elf'
SHA='63f12b7e00959c758f8aedfd23d1b8bc78e8ae7d922048e4765aaaef1dbbc6d3'
def headers(e):
 po,so=struct.unpack_from('>QQ',e,32);pn=struct.unpack_from('>H',e,56)[0];sn=struct.unpack_from('>H',e,60)[0]
 return po,so,[list(struct.unpack_from('>IIQQQQQQ',e,po+56*i)) for i in range(pn)],[list(struct.unpack_from('>IIQQQQIIQQ',e,so+64*i)) for i in range(sn)]
def vaoff(e,a):
 for p in headers(e)[2]:
  if p[0]==1 and p[3]<=a<p[3]+p[5]:return p[2]+a-p[3]
 raise ValueError(hex(a))
def symtab(path):
 from elftools.elf.elffile import ELFFile
 with open(path,'rb') as f:
  e=ELFFile(f);return {s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
def branch(pc,to):return struct.pack('>I',0x48000001|((to-pc)&0x3fffffc))
def make_entries(syms):
 import runpy
 a=runpy.run_path(str(H/'dependencies/ppcasm.py'))
 code=bytearray(0xa00)
 entries=[('entry_read',0x4800,syms['s1_read'],True),('entry_open',0x4840,syms['s1_open'],True),('entry_stat',0x4880,syms['s1_stat'],True),('raw_read',0x48c0,0x1ed5cc,False),('cobra_open',0x4900,0x3960,False),('cobra_stat',0x4940,0x39b0,False),('entry_capture',0x4980,syms.get('s2_capture',0),True),('entry_pad',0x49c0,syms['s2_pad'],True),('native_capture',0x4a00,0x4e00,False),('native_pad',0x4a40,0x4e10,False),('native_clock',0x4a80,0xb9cd0,False),('native_alloc',0x4ac0,0xb9848,False),('native_lock',0x4b00,0xba24c,False),('native_unlock',0x4b40,0xb9074,False),('native_set_report',0x4b80,0x5f08c,False),('output_callback',0x4bc0,syms['s3_complete'],True)]
 entries.append(('entry_lights_init',0x4c00,syms['lights_init'],True))
 entries.append(('entry_menu',0x4c40,syms['s4_menu'],True))
 entries += [('entry_normalize',0x4c80,syms['s3_normalize'],True),('entry_mapper',0x4cc0,syms.get('s3_mapper',0),True),('native_normalize',0x4d00,0x4e20,False),('native_mapper',0x4d40,0x4e30,False),('entry_host',0x4d80,syms['s4_host'],True),('native_host',0x4dc0,0x4e60,False)]
 for name,pc,target,payload in entries:
  toc=syms['.TOC.'] if payload else 0x751280
  source=f'mflr r0\nstd r0,16(r1)\nstdu r1,-128(r1)\nstd r2,120(r1)\nlis r2,{(toc+0x8000)>>16}\naddi r2,r2,{((toc+0x8000)&0xffff)-0x8000}\nlis r12,{(target+0x8000)>>16}\naddi r12,r12,{((target+0x8000)&0xffff)-0x8000}\nbl {target}\nnop\nld r2,120(r1)\naddi r1,r1,128\nld r0,16(r1)\nmtlr r0\nblr'
  if name=='entry_menu':
   source=source.replace(f'bl {target}',f'lwz r3,44(r24)\nbl {target}').replace('nop\nld r2','mr r4,r3\nld r2')
  asm=a['Assembler'](pc);asm.asm(source);b,rows=asm.finish();assert len(b)<=64;code[pc-0x4800:pc-0x4800+len(b)]=b;syms[name]=pc
 # Relocated first instruction is stack update only; relative branch resumes original.
 for pc,target in [(0x4e00,0x10d45c),(0x4e10,0x12825c),(0x4e20,0xd15cc),(0x4e30,0x1092fc),(0x4e60,0xd0bac),(0x4e70,0xbc838),(0x4f80,0xbc168)]:
  original=BASE.read_bytes();word=original[vaoff(original,target):vaoff(original,target)+4]
  assert word.hex()=={0x10d45c:"f821fef1",0x12825c:"f821ff41",0xd15cc:"f821ff61",0x1092fc:"f821ff41",0xd0bac:"f821ff51",0xbc838:"f821ff61",0xbc168:"f821fec1"}[target]
  code[pc-0x4800:pc-0x4800+8]=word+struct.pack(">I",0x48000000|((target+4-(pc+4))&0x3fffffc))
 code[0x640:0x658]=struct.pack('>QQQ',0x4bc0,0x751280,0)
 return bytes(code)
def build():
 original=BASE.read_bytes();assert hashlib.sha256(original).hexdigest()==SHA
 assert not any(original[vaoff(original,0x930f20):vaoff(original,0x930f20)+96])
 po,so,ph,sh=headers(original);p=ph[1];assert p[3]+p[5]==0x74e930
 data=bytearray(16);symbols={};evidence=[]
 files=[('bridge',H/f'modules/{VARIANT}.irx',None),('sony0',H/'inputs/schools/IOP_USBD.IRX','cbae284db35baceeaaed642351df99817ec47586'),('sony1',H/'inputs/jungle_zip/SYSTEM_USBD.IRX','80ebc1a552dffaf5640714b69bac4cbc173e3d22'),('sony2',H/'inputs/monster/SYSTEM_USBD.IRX','4a9d0d25b523f72f1be111bf9e0a8d88b96db873'),('cnf0',H/'inputs/schools/SYSTEM.CNF',None),('cnf1',H/'inputs/jungle/SYSTEM.CNF',None),('cnf2',H/'inputs/monster/SYSTEM.CNF',None),('cnf3',H/'owned/ace_racers/SYSTEM.CNF',None),('cnf4',H/'owned/dino_den/SYSTEM.CNF',None),('cnf5',H/'owned/robojam/SYSTEM.CNF',None),('ioprp300',H/'owned/jungle_party/SYSTEM_IOPRP300.IMG','51fe51e904bb4ef87e996b8a5062aaeb46d53b31'),('ioprp310',H/'owned/monster_rumble/SYSTEM_IOPRP310.IMG','f1de5c46132f59076002139f84b72ac2a4b4ceeb')]
 for name,path,sha in files:
  b=path.read_bytes();digest=hashlib.sha1(b).hexdigest();assert not sha or sha==digest
  symbols[name]=0x74e930+len(data);data.extend(b);data.extend(bytes((-len(data))%16));evidence.append(dict(symbol=name,address=hex(symbols[name]),bytes=len(b),sha1=digest))
 symbols['diagnostic_labels']=0x74e930+len(data)
 labels=[]
 for code in range(32):
  for flags in range(16):
   if code==0:stage='0'
   elif code==3:stage='1'
   elif code==14:stage='2'
   elif code==22:stage='5' if flags&14==14 else '4' if flags&2 else '3'
   else:stage=f'X{code:02}'
   prefix='BUZZ '+VARIANT
   label=f'{prefix} {stage} D{flags&1}S{flags>>1&1}R{flags>>2&1}C{flags>>3&1}'
   assert len(label)<32
   labels.append(label);data.extend(label.encode().ljust(32,b'\0'))
 # Page 1: physical producer flags and exact accepted physical PID history.
 for bits in range(128):
  h,m,v,a,i=[bits>>k&1 for k in range(5)];pid=bits>>5
  label=f'BUZZ {VARIANT} H{h}M{m}V{v}A{a} I{i} P{pid}'
  assert len(label)<32
  labels.append(label);data.extend(label.encode().ljust(32,b'\0'))
 # Page 2: callback enter, six bytes, ID zero, complete Buzz signature,
 # accepted callback PID and selection of either exact Buzz mapping record.
 for bits in range(128):
  n,l,z,f,w=[bits>>k&1 for k in range(5)];pid=bits>>5
  label=f'BUZZ {VARIANT} N{n}L{l}Z{z}F{f} W{w} P{pid}'
  assert len(label)<32
  labels.append(label);data.extend(label.encode().ljust(32,b'\0'))
 # Page 3: lamp status. L pattern lit, S submitted ever, C completed, V last_valid, X poison;
 # R: OK, NI no interface, LK lock busy, TO timeout, ER other.
 rcs=['OK','NI','LK','TO','ER']
 for bits in range(64):
  for rc in rcs:
   label=f'BUZZ {VARIANT} L{bits&1}S{bits>>1&1}C{bits>>2&1}V{bits>>3&1}X{bits>>4&1}G{bits>>5&1} R{rc}'
   assert len(label)<32
   labels.append(label);data.extend(label.encode().ljust(32,b'\0'))
 (H/f'evidence/{VARIANT}_diagnostic_labels.json').write_text(json.dumps(labels,indent=2))
 end=0x74e930+len(data);assert end<0x821000
 linker='''OUTPUT_FORMAT(elf64-powerpc)\nENTRY(s1_read)\nSECTIONS {\n . = 0x5200;\n\n .text : { *(.text) *(.text.*) }\n .rodata : { *(.rodata*) }\n . = ALIGN(8);\n .toc : { *(.toc*) }\n .data : { *(.data*) }\n .bss : { *(.bss*) *(COMMON) }\n /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }\n}\n'''
 linker+='\n'.join(f'{k} = {v:#x};' for k,v in symbols.items())+'\nraw_read = 0x48c0; cobra_open = 0x4900; cobra_stat = 0x4940; native_capture = 0x4a00; native_pad = 0x4a40; native_clock = 0x4a80; native_alloc = 0x4ac0; native_lock = 0x4b00; native_unlock = 0x4b40; native_set_report = 0x4b80; output_callback_descriptor = 0x4e40; native_normalize = 0x4d00; native_mapper = 0x4d40; native_host = 0x4dc0; native_guest = 0x4ec0; native_refresh = 0x4f40;\n'
 (H/'build/link.ld').write_text(linker)
 flags=['--target=powerpc64-unknown-linux-gnu','-mcpu=970','-mabi=elfv2','-mno-altivec','-mno-vsx','-msoft-float','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-asynchronous-unwind-tables','-fno-unwind-tables','-Oz','-fno-vectorize','-fno-slp-vectorize','-Wall','-Wno-unused-function','-Wextra','-Werror','-S','-emit-llvm']
 def run(args):
  r=subprocess.run([str(x) for x in args],cwd=H,capture_output=True,text=True)
  with (H/'evidence/build.log').open('a') as f:f.write(' '.join(str(x) for x in args)+'\n'+r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 import llvmlite.binding as llvm
 llvm.initialize_all_targets();llvm.initialize_all_asmprinters()
 for variant,sub in [(VARIANT,1)]:
  run([LLVM/'clang.exe',*flags,f'-DSUBSTITUTE={sub}','-DRAW_MODE=2',f'-DDELIVERY={int(variant in ("S4L1","S4L3"))}',f'-DLIGHT_MODE={3 if variant=="S4L3" else 1 if variant=="S4L1" else 0}',f'-DBRIDGE_SIZE={(H/f"modules/{VARIANT}.irx").stat().st_size}','src/overlay.c','-o',f'build/{variant}.ll'])
  llpath=H/f'build/{variant}.ll';llpath.write_text(llpath.read_text().replace(', target_mem: none',''))
  module=llvm.parse_assembly(llpath.read_text());module.verify()
  tm=llvm.Target.from_triple(module.triple).create_target_machine(cpu='970',features='-altivec,-vsx',reloc='static',codemodel='small',abiname='elfv2')
  obj=bytearray(tm.emit_object(module));assert struct.unpack_from('>I',obj,48)[0]==2
  (H/f'build/{variant}.o').write_bytes(obj)
  (H/f'build/{variant}_compiler.s').write_text(tm.emit_assembly(module))
  run([LLVM/'ld.lld.exe','-m','elf64ppc','-T','build/link.ld','--no-undefined','--Map='+f'build/{variant}.map',f'build/{variant}.o','-o',f'build/{variant}_payload.elf'])
  run([LLVM/'llvm-objcopy.exe','-O','binary',f'build/{variant}_payload.elf',f'build/{variant}.bin'])
  syms=symtab(H/f'build/{variant}_payload.elf');
  from elftools.elf.elffile import ELFFile
  with open(H/f'build/{variant}_payload.elf','rb') as f:
   pe=ELFFile(f);opd=pe.get_section_by_name('.opd');ob=opd.data() if opd else b'';oa=opd['sh_addr'] if opd else 0
   for name,address in list(syms.items()):
    if oa<=address<oa+len(ob):syms[name+'_descriptor']=address;syms[name]=struct.unpack_from('>Q',ob,address-oa)[0]
  code=make_entries(syms)+(H/f'build/{variant}.bin').read_bytes();(H/f'build/{variant}.bin').write_bytes(code);assert 0x4800+len(code)<=0x7ff0
  insertion=ph[2][2];shift=((max(0,p[2]+end-p[3]-insertion)+65535)//65536)*65536
  e=bytearray(original[:insertion])+bytearray(shift)+bytearray(original[insertion:]);patches=[]
  newso=so+shift
  def patch(o,b,why,expected=None):
   old=bytes(e[o:o+len(b)]);assert expected is None or old==expected,(why,hashlib.sha256(old).hexdigest())
   e[o:o+len(b)]=b;patches.append(dict(file_offset=hex(o),length=len(b),original=old.hex(),new=b.hex(),purpose=why))
  patch(40,struct.pack('>Q',newso),'ELF section table relocation',struct.pack('>Q',so))
  patch(po+112+8,struct.pack('>Q',ph[2][2]+shift),'PHDR2 file offset only',struct.pack('>Q',ph[2][2]))
  for j,section in enumerate(sh):
   if section[4]>=insertion:patch(newso+j*64+24,struct.pack('>Q',section[4]+shift),'Section file offset relocation '+str(j),struct.pack('>Q',section[4]))
  patch(vaoff(e,0x4800),code,'PPC64 payload in unused Cobra code reservation',bytes(len(code)))
  for a,t,old in [(0x3cfc,'entry_read',bytes.fromhex('4e800421')),(0x135f70,'entry_open',branch(0x135f70,0x3960)),(0x136ff4,'entry_open',branch(0x136ff4,0x3960)),(0x135fac,'entry_stat',branch(0x135fac,0x39b0))]:patch(vaoff(e,a),branch(a,syms[t]),'Hook '+t,old)
  for target,name,word in ([(0x10d45c,'entry_capture','f821fef1')][:0 if variant=='S4L3' else 1]+([(0x12825c,'entry_pad','f821ff41')] if variant in ('S4L1','S4L3') else [])):
   patch(vaoff(e,target),struct.pack('>I',int.from_bytes(branch(target,syms[name]),'big')&~1),'Hook '+name,bytes.fromhex(word))
  for target,name,word in [(0xd15cc,'entry_normalize','f821ff61'),(0x1092fc,'entry_mapper','f821ff41'),(0xd0bac,'entry_host','f821ff51')][:1]+[(0xd0bac,'entry_host','f821ff51')] if variant=='S4L3' else [(0xd15cc,'entry_normalize','f821ff61'),(0x1092fc,'entry_mapper','f821ff41'),(0xd0bac,'entry_host','f821ff51')]:
   patch(vaoff(e,target),struct.pack('>I',int.from_bytes(branch(target,syms[name]),'big')&~1),'Hook '+name,bytes.fromhex(word))
  patch(vaoff(e,0x304ec0),('BUZZ '+variant).encode().ljust(18,b'\0'),'Static menu marker',b'Debug Information\0')
  patch(vaoff(e,0x270ffc),branch(0x270ffc,syms['entry_menu']),'Runtime menu selector',bytes.fromhex('e882cea8'))
  # Reuse the tested B1/B3 method: extend only the existing RO load and its last section.
  newsize=end-p[3];patch(po+56+32,struct.pack('>QQ',newsize,newsize),'PHDR1 RO filesz/memsz',struct.pack('>QQ',p[5],p[6]))
  assert sh[33][3]+sh[33][5]==0x74e930
  patch(newso+33*64+32,struct.pack('>Q',end-sh[33][3]),'Last RO section size',struct.pack('>Q',sh[33][5]))
  dataoff=p[2]+p[5];patch(dataoff,bytes(data),'Immutable bridge and original authentication templates',bytes(len(data)))
  assert len(e)==len(original)+shift
  assert e[insertion+shift:insertion+shift+ph[2][5]]==original[insertion:insertion+ph[2][5]]
  mapped=bytearray(original[:insertion])+bytearray(shift)+bytearray(original[insertion:])
  # Exhaustive byte diff independently checks each changed byte lies inside the declared patches.
  covered=bytearray(len(e))
  for t in patches:
   o=int(t['file_offset'],16);covered[o:o+t['length']]=b'\1'*t['length']
  changed=sum(a!=b for a,b in zip(mapped,e));assert all(a==b or covered[j] for j,(a,b) in enumerate(zip(mapped,e)))
  assert headers(e)[2][0]==ph[0] and headers(e)[2][2][3:]==ph[2][3:]
  out=H/f'ps2_netemu_{variant}.elf';out.write_bytes(e)
  table=dict(variant=variant,baseline=str(BASE),baseline_sha256=SHA,elf_sha256=hashlib.sha256(e).hexdigest(),elf_size=len(e),file_insertion=dict(offset=hex(insertion),size=shift,reason='Move later file payload preserving its VA and all bytes'),code_start='0x4800',code_end=hex(0x4800+len(code)),data_start='0x74e930',data_end=hex(end),changed_bytes=changed,symbols=syms,immutable_blobs=evidence,patches=patches)
  (H/f'{variant}_patch_table.json').write_text(json.dumps(table,indent=2))
  print(variant,'code',len(code),'RO',len(data),'end',hex(end),'changed',changed,flush=True)
  runpy_audit=__import__('runpy').run_path(str(H/'audit_ir.py'));runpy_audit['audit'](variant)
  # The existing packager is imported read-only; its outputs are explicitly supplied here.
  import runpy
  pack=runpy.run_path(str(H/'dependencies/resign_self_installed.py'))
  pack['main'](out,H/f'ps2_netemu_{variant}.self')
if __name__=='__main__':build()
