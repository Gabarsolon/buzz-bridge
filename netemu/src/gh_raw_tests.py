"""Actual final PPC code -> original SIO2 PIO pump -> wire fixtures for MIPS.
HID parser/requeue and host clock are explicit stubs; the displaced prologues,
original input-completion control flow and SIO2 dispatch execute unchanged.
"""
exec((H/'gh_harness.py').read_text())
import time
checks=[];wire=[];started=time.time()
O=BUF+0x10000;REPORT=BUF+0x20000;ROOT=BUF+0x30000;CACHE=BUF+0x40000
FIFO=0xb5b888;OUT=FIFO+0x400
class RawMachine(Machine):
 def __init__(self):
  super().__init__(lambda o,n:bytes(n),100000)
  self.now=1000000;self.decoder_calls=0;self.requeues=0
  self.extra_ranges += [(0xb5b860,0xb5c520),(0x2456608,0x2456640),(0x949fd4,0x949fd8)]
  for a in [0xb9cd0,0x10bf50,0xccedc,0xbc838,0x4e20]:self.em.setBreakpoint(s.getAddress(a))
  self.w(RAW,struct.pack('>I',1)+bytes(92));self.w(O,bytes(0x380));self.w(O+0x10,struct.pack('>I',1));self.w(O+0x2a,bytes.fromhex('054c1000'))
  self.w(O+0x16,bytes.fromhex('054c1000'))
  self.transport=BUF+0x60000;self.request=BUF+0x61000
  self.w(self.transport,bytes(0x240));self.w(self.request,bytes(0x40))
  self.w(0x2456608,struct.pack('>QQQ',ROOT,0,CACHE));self.w(ROOT,bytes(0x700));self.w(CACHE,bytes(7*0xf8));self.w(ROOT+0x6c9,b'\x7f')
  for i in (0,3):self.w(CACHE+i*0xf8+0xd4,bytes.fromhex('054c1000'))
  self.call(0x126110,[FIFO,OUT]);self.w(0xb5c1c8,struct.pack('>QQ',OUT,FIFO));self.w(0x949fd5,b'\3')
 def extra(self,pc):
  if pc not in [0xb9cd0,0x10bf50,0xccedc,0xbc838,0x4e20]:return False
  if pc==0x10bf50:self.decoder_calls+=1
  if pc==0xccedc:self.requeues+=1
  self.put(3,self.now if pc==0xb9cd0 else 0);self.pc(self.reg('lr'));return True
 def capture(self,word,**kw):
  self.now+=79800
  report=bytes([0x7f,0x7f,word&255,(word>>8)&255,0xf0|((word>>16)&15)])
  report=kw.get('report',report)
  obj=kw.get('object',O);status=kw.get('status',0)
  self.w(REPORT,report);self.w(self.transport+0x40,struct.pack('>Q',obj))
  self.w(self.request,struct.pack('>QQiIQQ',REPORT,self.transport,status,0,kw.get('n',5),0))
  ret=self.call(0xd15cc,[self.transport,self.request])
  self.w(REPORT+16,b'\0'+report)
  self.call(0x10d45c,[obj,REPORT+16,6,status])
  assert self.rd(REPORT,5)==report,'capture changed caller buffer'
  return ret
 def frame(self,gen=0,cursor=0,**kw):
  logical_req=bytearray([1,0x5b,ord('B'),ord('Z'),2,1,1,0])+bytearray(struct.pack('<II',gen,cursor))+bytearray(16)
  req=bytearray([1,0x42,0,0,0,66,90,0xe4,1])+bytearray(struct.pack('<II',gen,cursor))+bytearray(4)
  if 'edit' in kw:kw['edit'](req)
  self.w(FIFO,req);self.w(OUT,b'\xcc'*40)
  if kw.get('direct'):
   self.call(0x12825c,[kw.get('port',0),0,0,kw.get('ilen',21),kw.get('olen',21)])
  else:
   # Same PIO descriptor, timing and FIFO state the guest SIO2MAN submits.
   for a,v,n in [(FIFO+0x878,21,2),(FIFO+0x87a,0,2),(FIFO+0x800,(21<<18)|(21<<8)|0x40,4),(FIFO+0x804,0,4),(FIFO+0x884,0,4),(FIFO+0x840,0xffc00505,4),(FIFO+0x844,0x20014,4),(FIFO+0x868,0x10,4),(FIFO+0x86c,0,4),(FIFO+0x870,15,4),(FIFO+0x874,0,4),(FIFO+0x900,0,8),(FIFO+0x908,0,8),(FIFO+0x930,96000,8)]:self.w(a,v.to_bytes(n,'big'))
   self.call(0x126b18,[])
   assert not (int.from_bytes(self.rd(FIFO+0x86c,4),'big')&0xc000),'native pump transfer error'
  physical=self.rd(OUT,21)
  assert self.rd(OUT+21,19)==b'\xcc'*19,'channel overrun'
  b=bytearray(self.rd(OUT,32))
  if physical[:6]==bytes.fromhex('ff795a425ae4'):
   b=bytearray(32);b[:6]=bytes.fromhex('ffb25a425a02');b[6:19]=physical[6:19];b[19]=8;b[20]=physical[19];b[21]=physical[20]
  return dict(request=logical_req.hex(),physical_request=req.hex(),physical_response=physical.hex(),response=b.hex(),status=b[6],mode=b[7],gen=int.from_bytes(b[8:12],'little'),seq=int.from_bytes(b[12:16],'little'),word=int.from_bytes(b[16:19],'little'),exclude=b[20])
def check(name,condition):
 assert condition,name
 checks.append(dict(name=name,passed=True))
m=RawMachine()
try:
 check('no dongle',m.frame()['status']==0)
 gen=seq=0
 vectors=[0]+[x for i in range(20) for x in (1<<i,0)]+[0xfffff,0,0x55555,0xaaaaa,0x84210,0x84210,0]
 for i,w in enumerate(vectors):
  before=m.state()['produced'];m.capture(w);r=m.frame(gen,seq)
  check('word '+str(i),r['status']==1 and r['word']==w and r['seq']==before+1 and r['exclude']==33 and r['mode']==2)
  check('unacknowledged repeat '+str(i),m.frame(gen,seq)['response']==r['response'])
  gen,seq=r['gen'],r['seq'];wire.append(dict(name='vector_'+str(i),expected_word=w,**r))
  check('ack '+str(i),m.frame(gen,seq)['status']==2)
 for w in [1,0,0x80000,0,0xfffff,0,0x40000,0]:m.capture(w)
 for i,w in enumerate([1,0,0x80000,0,0xfffff,0,0x40000,0]):
  r=m.frame(gen,seq);check('ordered burst '+str(i),r['status']==1 and r['word']==w);gen,seq=r['gen'],r['seq'];wire.append(dict(name='burst_'+str(i),expected_word=w,**r))
 for i in range(100):m.capture(i)
 r=m.frame(gen,seq);check('unread overflow explicit',r['status']==3);gen,seq=r['gen'],r['seq']
 check('overflow resync bounded',m.frame(gen,seq)['status']==2)
 m.capture(0);r=m.frame(gen,seq);check('release after overflow',r['status']==1 and r['word']==0);gen,seq=r['gen'],r['seq']
 m.w(O+0x18,b'\0\3');before=m.state()['produced'];m.capture(123);check('wrong PID ignored',m.state()['produced']==before);m.w(O+0x18,b'\x10\0')
 m.capture(123,n=4);check('short ignored',m.state()['produced']==before)
 m.capture(123,status=-1);check('disconnect fail open',m.frame(gen,seq)['status']==0)
 m.capture(0x80000);r=m.frame(gen,seq);check('reconnect generation',r['gen']!=gen and r['status']==1 and r['word']==0x80000);gen,seq=r['gen'],r['seq']
 m.now+=79800000;check('stale fail open',m.frame(gen,seq)['status']==0)
 m.capture(0);m.w(RAW,bytes(4));check('unknown title disabled',m.frame(gen,seq)['status']==0);m.w(RAW,struct.pack('>I',1))
 e=int.from_bytes(m.rd(RAW+32,4),'big');m.w(RAW+32,struct.pack('>I',e|1));check('writer busy bounded',m.frame(gen,seq)['status']==4);m.w(RAW+32,struct.pack('>I',e))
 for kw in [dict(ilen=31),dict(olen=31),dict(port=1),dict(edit=lambda b:b.__setitem__(8,2)),dict(edit=lambda b:b.__setitem__(17,1))]:
  r=m.frame(direct=True,**kw);check('foreign command unchanged '+str(len(checks)),r['response']=='cc'*32)
 check('original callback continues',m.decoder_calls>150 and m.requeues>150)
finally:m.close()
result=dict(passed=True,variant=variant,elf_sha256=hashlib.sha256(_elf).hexdigest(),checks=checks,seconds=round(time.time()-started,2),wire=wire,limits=['Native HID parser/requeue, clock, host cached pad read stubbed; physical ownership and DMA excluded.','Eight native slots; overflow is explicit, cannot guarantee lossless input during an unbounded consumer stall.','sync executes as sequential no-op in Ghidra, real CPU ordering unproven.'])
(H/f'evidence/{variant}_raw_results.json').write_text(json.dumps(result,indent=2));print(variant,len(checks),'RAW PASS',flush=True)
