"""Loaded inside gh_run.py. Real PPC instructions, original Cobra wrappers.
Only UFS open/stat/read are stubs; no overlay/parser instruction is replaced.
"""
import struct,json,hashlib,random,sys
from ghidra.util.task import TimeoutTaskMonitor
from java.util.concurrent import TimeUnit
MASK=(1<<64)-1;BUF=0x2200000;SP=0xaaa700bfe00;STOP=0x7ff0;V=0x3174478;RAW=0x930f20
syncpcs=[a for a in range(0x4800,0x7ff0,4) if bytes(p.getMemory().getBytes(s.getAddress(a),4))==bytes.fromhex('7c0004ac')] if False else []
syms=json.loads((H/f'{variant}_patch_table.json').read_text())['symbols']
import runpy
vaoff=runpy.run_path(str(H/'build.py'))['vaoff']
_elf=(H/f'ps2_netemu_{variant}.elf').read_bytes()
syncpcs=[a for a in range(0x4800,0x7ff0,4) if _elf[vaoff(_elf,a):vaoff(_elf,a)+4] in [bytes.fromhex(x) for x in ['7c0004ac','7c2004ac','4c00012c']]]
atomicpcs={}
for a in range(0x4800,0x7ff0,4):
 word=int.from_bytes(_elf[vaoff(_elf,a):vaoff(_elf,a)+4],'big')
 if word>>26==31 and (word>>1&1023) in [20,150]:atomicpcs[a]=word
class Machine:
 def __init__(self,source,size,cd=False,raw=False):
  self.ufs=[];self.allocations=[];self.source=source;self.size=size;self.cd=cd;self.raw=raw;self.fd=3;self.error=None;self.calls=0;self.short=None
  self.em=EmulatorHelper(p);self.monitor=ConsoleTaskMonitor();self.em.enableMemoryWriteTracking(True)
  self.reservation=None;self.atomic_events=[]
  for a in [0x1ed5cc,0x1ecc5c,0x1ec760,0xb9848,STOP,*syncpcs,*atomicpcs]:self.em.setBreakpoint(s.getAddress(a))
  self.extra_ranges=[(0x934958,0x934a18),(0x24565b8,0x24565c0),(0x2446588,0x2446590)]
  for j,n in enumerate([0x60000,0x70000,0x27000,0x200000]):self.w(0x934958+j*48+8,struct.pack('>Q',n));self.w(0x934958+j*48+40,struct.pack('>Q',0x80000000+j*0x800000))
  self.w(0x24565b8,struct.pack('>Q',0x80000000));self.w(0x2446588,struct.pack('>Q',0x80020000))
  self.w(V,struct.pack('>I',1)+b'/dev_hdd0/fake.iso\0');self.w(V+0x804,bytes([int(cd),int(raw)]));self.w(V+0x808,bytes(16));self.w(RAW,bytes(96))
 def w(self,a,b):self.em.writeMemory(s.getAddress(a),b)
 def rd(self,a,n):return bytes(self.em.readMemory(s.getAddress(a),n)) if n else b''
 def get(self,r):return int(str(self.em.readRegister('r'+str(r))))&MASK
 def put(self,r,v):self.em.writeRegister('r'+str(r),BigInteger(str(v&MASK)))
 def reg(self,r):return int(str(self.em.readRegister(r)))&MASK
 def pc(self,a):self.em.writeRegister(self.em.getPCRegister(),BigInteger(str(a)))
 def call(self,pc,args):
  self.monitor=TimeoutTaskMonitor.timeoutIn(30,TimeUnit.SECONDS)
  before=[0x1111111100000000+j for j in range(14,32)]
  if pc==syms.get('entry_menu') and variant in ['S4C6','S4L1','S4L3']:
   before[24-14]=BUF+0x90000
   self.w(BUF+0x9002c,struct.pack('>I',getattr(self,'menu_page',0)))
  for j,v in enumerate(before,14):self.put(j,v)
  self.put(1,SP);self.put(2,0x751280);[self.em.writeRegister('cr'+str(k),BigInteger(str(k))) for k in [2,3,4]]
  for j,v in enumerate(args,3):self.put(j,v)
  self.em.writeRegister('lr',BigInteger(str(STOP)));self.pc(pc)
  # run() does not break on its initial address; one step bypasses a UFS breakpoint.
  for _ in range(50000):
   if not self.em.run(self.monitor):raise RuntimeError((hex(self.em.getExecutionAddress().getOffset()),str(self.em.getLastError())))
   cur=self.em.getExecutionAddress().getOffset()
   if cur==STOP:break
   if cur in syncpcs:self.pc(cur+4);continue
   if cur in atomicpcs:
    # Ghidra's classic lwarx omits RESERVE; its conditional store delegates to
    # an unimplemented userop. Model only these two ISA operations. Comparisons,
    # weak-CAS failure branches and all pointer guards execute final PPC.
    word=atomicpcs[cur];rt=word>>21&31;ra=word>>16&31;rb=word>>11&31
    address=((self.get(ra) if ra else 0)+self.get(rb))&MASK
    if word>>1&1023==20:
     self.put(rt,int.from_bytes(self.rd(address,4),'big'));self.reservation=address
    else:
     success=self.reservation==address
     if success:self.w(address,(self.get(rt)&0xffffffff).to_bytes(4,'big'))
     self.em.writeRegister('cr0',BigInteger(str(2 if success else 0)));self.reservation=None
    self.atomic_events.append(dict(pc=hex(cur),address=hex(address)));self.pc(cur+4);continue
   if hasattr(self,"extra") and self.extra(cur):continue
   args=[self.get(j) for j in range(3,7)]
   if cur==0xb9848:
    assert args[:3]==([512,3,0] if variant=='OLD' else [1344,3,0]);self.allocations.append(args[:3])
    ret=getattr(self,'allocation',0x80030000)
   elif cur==0x1ed5cc:
    fd,off,buf,size=args
    if not hasattr(self,'host_read'):assert fd==self.fd,(fd,self.fd)
    self.calls+=1;self.ufs.append(dict(offset=off,buffer=hex(buf),bytes=size))
    if "--smoke" in sys.argv:print("UFS",hex(off),size,flush=True)
    if self.error and self.error(off,size):ret=-5
    else:
     b=self.host_read(fd,off,size) if hasattr(self,'host_read') else self.source(off,size)
     if self.short is not None:b=b[:self.short]
     assert len(b)<=size;self.w(buf,b);ret=len(b)
   elif cur==0x1ecc5c:ret=self.host_open(args[1]) if hasattr(self,'host_open') else self.fd
   elif cur==0x1ec760:self.w(args[1]+0x28,struct.pack('>Q',self.size));ret=0
   else:raise AssertionError(hex(cur))
   self.put(3,ret);self.pc(self.reg('lr'))
  else:raise RuntimeError('UFS budget')
  assert self.get(1)==SP and self.get(2)==0x751280,('stack/TOC',hex(self.get(1)),hex(self.get(2)))
  assert [self.get(j) for j in range(14,32)]==before,'nonvolatile GPR'
  assert [self.reg('cr'+str(k)) for k in [2,3,4]]==[2,3,4],'nonvolatile CR2-4'
  self.check_writes()
  return self.get(3)
 def check_writes(self):
  self.write_ranges=[]
  for r in self.em.getTrackedMemoryWriteSet().getAddressRanges():
   if r.getMinAddress().getAddressSpace()!=s:continue
   lo=r.getMinAddress().getOffset();hi=r.getMaxAddress().getOffset()+1
   assert (0x800000<=lo<hi<=0x3200000) or (0x400000000<=lo<hi<=0x402000000) or (SP-0x10000<=lo<hi<=SP+0x1000) or (0x80000000<=lo<hi<=0x82000000),('RO store',str(r),str(r.getMinAddress().getAddressSpace()),hex(lo),hex(hi))
   assert any(a<=lo and hi<=b for a,b in [(RAW,RAW+96),(0x80030000,0x80030540),(SP-0x10000,SP+0x1000),(BUF,BUF+0x200000),(0x2200000,0x2400000),(0x2458428,0x2458438),(0x400000000,0x402000000),(V,V+0x1000)]+getattr(self,"extra_ranges",[])),('unexpected write',hex(lo),hex(hi))
   self.write_ranges.append([hex(lo),hex(hi)])
 def start(self):
  assert self.call(syms['entry_open'],[0,V+4])==self.fd
  assert self.call(syms['entry_stat'],[self.fd,SP+0x100])==0
  return self.state()
 def state(self):
  b=self.rd(RAW,96);return dict(status=int.from_bytes(b[:4],'big')&1,diagnostic=int.from_bytes(b[:4],'big'),fd=int.from_bytes(b[4:8],'big'),start=int.from_bytes(b[8:16],'big'),iso_size=int.from_bytes(b[16:24],'big'),length=int.from_bytes(b[24:28],'big'),stride=int.from_bytes(b[28:30],'big'),skip=int.from_bytes(b[30:32],'big'),epoch=int.from_bytes(b[32:36],'big'),produced=int.from_bytes(b[36:40],'big'))
 def read(self,off,n,whole=True):
  self.w(BUF,b'\xcd'*(n+16));pc=0x1377c4 if whole else syms['entry_read']
  ret=self.call(pc,[self.fd,off+(24 if whole and self.cd and not self.raw else 0),BUF,n])
  assert self.rd(BUF+n,16)==b'\xcd'*16,'output overrun'
  return self.rd(BUF,n),ret
 def close(self):self.em.dispose()
if '--smoke' in sys.argv:
 path=Path(json.loads((H/'evidence/iso_inputs.json').read_text())[0]['path']);f=path.open('rb')
 def read(o,n):f.seek(o);return f.read(n)
 m=Machine(read,path.stat().st_size);print('init',m.start(),m.calls,m.rd(V+0x810,8).hex(),flush=True)
 print('PVDSTACK',m.rd(SP-2048,2304).find(bytes.fromhex('01434430303101')),'TOC',hex(syms['.TOC.']),m.rd(syms['.TOC.']-32768,32).hex(),flush=True)
 b,r=m.read(0,16384);print('limg',b[:16].hex(),r,flush=True)
 st=m.state();b,r=m.read(st['start'],st['length']);blob=(H/f'modules/{variant}.irx').read_bytes();assert b==(blob+bytes(st['length']-len(blob)) if variant in ['S4A','S4','S4D2','S4B1','S4C6','S4L1','S4L3'] else read(st['start'],st['length']));
 if variant in ['S4A','S4','S4D2','S4B1','S4C6','S4L1','S4L3']:(H/'served_bbusbd.irx').write_bytes(b[:len(blob)])
 print('replacement PASS',m.state(),m.write_ranges,flush=True);m.close();f.close()
