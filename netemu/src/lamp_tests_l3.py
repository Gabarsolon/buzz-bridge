"""S4L3: the lamp mask requested by the game rides in bytes 17..20 of the 21-byte poll
(real s2_pad on the final PPC code); the HID pump (real D15CC hook) converges on it
through the stock HID set-report chain. USB submit boundary (83060) modeled."""
exec((H/'gh_raw_tests.py').read_text().split('def check(name,condition):')[0])
_src=(H/'gh_lights_tests.py').read_text()
exec(_src[:_src.index('def check(name,value):')])
lchecks=[];lobs=[]
HOSTBUF=BUF+0x70000
def ck(name,ok):
 assert ok,name
 lchecks.append(dict(name=name,passed=True))

class L2Machine(LampMachine):
 pid=0x1000
 def __init__(self,pid=0x1000,**kw):
  super().__init__(**kw)
  self.pid=pid
  self.w(DESC+8,bytes.fromhex('4c05')+struct.pack('<H',pid))
  self.w(IFACE+0x16,struct.pack('>HH',0x054c,pid))
  self.w(RAW+40,bytes(12));self.submits=[];self.pending=[]
 def event(self,word=0,owner=None,vid=0x054c,pid=None,status=0):
  owner=IFACE if owner is None else owner
  pid=self.pid if pid is None else pid
  self.now+=79800
  report=bytes([0,0,word&255,(word>>8)&255,0xf0|((word>>16)&15)])
  self.w(owner+0x16,struct.pack('>HH',vid,pid));self.w(REPORT,report)
  self.w(self.transport+0x38,b'\0');self.w(self.transport+0x40,struct.pack('>Q',owner))
  self.w(self.request,struct.pack('>QQiIQQ',REPORT,self.transport,status,0,5,0))
  return self.call(0xd15cc,[self.transport,self.request])
 def poll(self,mask=None,valid=1,check=None,tail=None,gen=0,cursor=0,motor=0):
  """One guest 21-byte command-42 poll carrying a lamp level."""
  req=bytearray(21);req[0]=1;req[1]=0x42;req[5]=0x42;req[6]=0x5a;req[7]=0xe4;req[8]=1
  req[9:13]=struct.pack('<I',gen);req[13:17]=struct.pack('<I',cursor)
  if mask is not None and valid:
   req[17]=mask;req[18]=(0xa5^mask)&255 if check is None else check;req[19]=valid
  if tail is not None:req[20]=tail
  req[3]=motor
  self.w(FIFO,bytes(req));self.w(OUT,b'\xcc'*40);self.in_sio=True
  try:self.call(0x12825c,[0,0,0,21,21])
  finally:self.in_sio=False
  return self.rd(OUT,21)
 def settle(self,events=40,word=0,mask=None,**kw):
  """Repeat the poll (as the guest does) and feed HID events for about 40 ms."""
  for _ in range(events):
   if mask is not None or kw:self.poll(mask,**kw)
   self.event(word)
   while self.pending:
    ctx=self.pending.pop(0);self.call(0x5fb48,[0,7,ctx])
 def label(self):
  self.menu_page=3;self.call(syms['entry_menu'],[])
  return self.rd(self.get(4),32).split(b'\0')[0].decode()
 def payloads(self):return [x['payload'] for x in self.submits]
def pay(mask):return bytes([0]+[255 if mask>>i&1 else 0 for i in range(4)]+[0,0]).hex()


class L3Machine(L2Machine):
 def __init__(self,*a,**kw):
  super().__init__(*a,**kw)
  self.extra_ranges += [(0x247e000,0x247e010)]
  self.em.setBreakpoint(s.getAddress(0x4dc0))
 def host_tick(self,n=1,complete=True):
  """The 16 ms host-refresh call (original D15CC-independent path): BC240 -> D0BAC."""
  for _ in range(n):
   self.now+=1276800
   self.call(0xd0bac,[0x400100,2,HOSTBUF,0x80])
   while complete and self.pending:
    ctx=self.pending.pop(0);self.call(0x5fb48,[0,7,ctx])
 def extra(self,pc):
  if pc==0x4dc0:  # native_host trampoline: the original D0BAC is outside this test
   self.put(3,(1<<64)-1);self.pc(self.reg('lr'));return True
  return super().extra(pc)
 def set_busy(self,v):self.w(ARENA+268,struct.pack('>I',v))

m=L3Machine()
try:
 m.event(0);m.settle(5)
 m.poll(5);ck('desired word stored at the expected arena offset',int.from_bytes(m.rd(ARENA+264,4),'big')==21)
 m.submits=[]
 # no more HID events: only the host-refresh hook runs
 m.host_tick(40)
 ck('host hook alone applies the requested lamps (after the all-off reset)',m.payloads() in ([pay(5)],[pay(0),pay(5)]))
 # blink like the Buzz! main menu: all on / all off every ~280 ms
 m.submits=[]
 for k in range(10):
  m.poll(15 if k%2==0 else 0);m.host_tick(18)
 ck('every blink phase reaches the dongle without any input',m.payloads()==[pay(15 if k%2==0 else 0) for k in range(10)])
 ck('blink submissions are SET_REPORT only',all(x['wire_setup']=='2109000200000700' for x in m.submits))
 # busy guard: a pump already running makes the other caller skip
 m.poll(3);m.set_busy(1);before=len(m.submits);m.host_tick(40);m.settle(40,mask=3)
 ck('busy pump blocks the other caller',len(m.submits)==before)
 m.set_busy(0);m.host_tick(40)
 ck('after the busy flag clears the lamps converge',m.payloads()[-1]==pay(3))
 ck('busy flag is released after use',int.from_bytes(m.rd(ARENA+268,4),'big')==0)
 lobs.append(dict(case='host driven blink',payloads=m.payloads()[:12],label=m.label()))
finally:m.close()

# the host driver respects every fail-open rule of the HID driver
m=L3Machine()
try:
 m.event(0);m.settle(5);m.w(ILIST,struct.pack('>Q',IFACE+0x1000));m.poll(1);m.submits=[]
 m.host_tick(60)
 ck('host driver: unknown interface sends nothing',m.submits==[] and 'RNI' in m.label())
finally:m.close()
m=L3Machine()
try:
 m.event(0);m.settle(5);m.lock_error=-16;m.poll(1);m.submits=[]
 m.host_tick(60)
 ck('host driver: busy USB lock sends nothing',m.submits==[] and 'RLK' in m.label())
finally:m.close()
m=L3Machine()
try:
 m.poll(7);m.submits=[]
 m.host_tick(60)
 ck('host driver without an input owner sends nothing',m.submits==[])
finally:m.close()
m=L3Machine()
try:
 m.event(0);m.settle(5);m.submit_error=-5;m.poll(1);m.submits=[]
 m.host_tick(300)
 ck('host driver: failed submit is bounded',len(m.submits)<=6)
finally:m.close()

(H/'evidence/S4L3_lamp_tests.json').write_text(json.dumps(dict(passed=True,elf_sha256=hashlib.sha256(_elf).hexdigest(),checks=lchecks,observations=lobs,
 limits=['USB submit (83060) and OS locks are modeled; the original D0BAC returns early (no dispatcher), so the host hook is exercised up to the call into the original.',
 'Real thread interleaving of the HID worker and host-refresh thread is not modeled; the busy flag is the only serialization.']),indent=2))
print('S4L3',len(lchecks),'LAMPS PASS',flush=True)
