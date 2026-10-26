"""Exercise the real C HTTP/WebSocket server on 127.0.0.1 only. stdlib only."""
import base64, hashlib, json, os, pathlib, socket, struct, subprocess, time
from http.client import HTTPConnection
ROOT=pathlib.Path(__file__).resolve().parents[1]
def http(port,path='/'):
 c=HTTPConnection('127.0.0.1',port,timeout=2)
 c.request('GET',path); r=c.getresponse(); body=r.read(); code=r.status; c.close(); return code,body
def masked(data,op=1):
 data=data.encode() if isinstance(data,str) else data; key=os.urandom(4)
 header=bytes([0x80|op,0x80|len(data)]) if len(data)<126 else bytes([0x80|op,0xfe])+struct.pack('!H',len(data))
 return header+key+bytes(b^key[i%4] for i,b in enumerate(data))
class Peer:
 def __init__(self,port,slot=None):
  self.s=socket.create_connection(('127.0.0.1',port),timeout=2); self.s.settimeout(2); self.buf=b''
  key=base64.b64encode(os.urandom(16)).decode()
  req=(f'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
       f'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\nOrigin: http://127.0.0.1:{port}\r\n'
       'Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n\r\n').encode()
  # Header is split across TCP writes; first WebSocket frame shares the final write.
  self.s.sendall(req[:23]); self.s.sendall(req[23:]+(masked(json.dumps({'type':'join','slot':slot})) if slot is not None else b''))
  while b'\r\n\r\n' not in self.buf: self.buf+=self.s.recv(4096)
  header,self.buf=self.buf.split(b'\r\n\r\n',1)
  assert b'101 Switching Protocols' in header
  expected=base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
  assert expected in header and b'permessage-deflate' not in header
  self.until('status')
 def take(self,n):
  while len(self.buf)<n:
   chunk=self.s.recv(8192)
   if not chunk: raise EOFError('server closed')
   self.buf+=chunk
  out,self.buf=self.buf[:n],self.buf[n:]; return out
 def receive(self):
  a,b=self.take(2); assert a&128 and not b&128
  n=b&127
  if n==126: n=struct.unpack('!H',self.take(2))[0]
  data=self.take(n)
  if a&15==9: self.s.sendall(masked(data,10)); return self.receive()
  if a&15==10: return {'type':'pong','payload':data}
  if a&15==8: return {'type':'close','payload':data}
  return json.loads(data)
 def until(self,kind):
  for _ in range(200):
   msg=self.receive()
   if msg['type']==kind: return msg
  raise AssertionError('missing message '+kind)
 def send(self,kind,**kw): self.s.sendall(masked(json.dumps({'type':kind,**kw})))
 def close(self): self.s.close()
def main():
 probe=socket.socket(); probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]; probe.close()
 process=subprocess.Popen([str(ROOT/'build'/'pc_server.exe'),str(port)],cwd=ROOT,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                          creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
 peers=[]
 try:
  deadline=time.monotonic()+4
  while True:
   try: code,page=http(port); break
   except OSError:
    if time.monotonic()>deadline: raise
    time.sleep(.02)
  assert code==200 and b'BUZZ' in page
  assert http(port,'/static/app.js')[0]==200
  assert http(port,'/static/style.css')[0]==200
  assert http(port,'/../../ps3buzz.cfg')[0]==404
  assert http(port,'/admin/tap/1')[0]==404
  # Idle and partial requests must not block the next client.
  idle=socket.create_connection(('127.0.0.1',port),timeout=2)
  partial=socket.create_connection(('127.0.0.1',port),timeout=2)
  try:
   partial.sendall(b'GET /health.json HTTP/1.1\r\nHost: local')
   assert http(port,'/health.json')[0]==200
   partial.sendall(b'\r\n\r\n'); assert b'200 OK' in partial.recv(4096)
   assert http(port,'/')[0]==200
  finally:
   idle.close(); partial.close()
  for slot in range(1,9):
   p=Peer(port,slot); peers.append(p); assert p.until('joined')['slot']==slot
  extra=Peer(port,1); peers.append(extra); assert extra.until('join_rejected')['slot']==1
  peers[0].send('join',slot=2); assert peers[0].until('join_rejected')['slot']==2
  for p in peers[:8]:
   # Coalesced messages and arbitrary network boundaries.
   data=b''.join(masked(json.dumps({'type':'down','button':b})) for b in ['red','yellow','green','orange','blue'])
   p.s.sendall(data[:7]); p.s.sendall(data[7:])
  deadline=time.monotonic()+2
  while True:
   health=json.loads(http(port,'/health.json')[1])
   if health['reports']==[[127,127,255,255,255]]*2: break
   if time.monotonic()>deadline: raise AssertionError(health)
   time.sleep(.01)
  assert health['usb_backend_ready'] is False
  peers[7].close(); peers[7]=None
  deadline=time.monotonic()+2
  while True:
   reports=json.loads(http(port,'/health.json')[1])['reports']
   if reports[1]==[127,127,255,127,240]: break
   if time.monotonic()>deadline: raise AssertionError(reports)
   time.sleep(.01)
  extra.send('join',slot=8); assert extra.until('joined')['slot']==8
  extra.s.sendall(masked(b'hello',9)); assert extra.until('pong')['payload']==b'hello'
  # Unsupported fragmented frames close with protocol error; no stuck claim remains.
  bad=bytearray(masked(b'{}')); bad[0]=1; extra.s.sendall(bad)
  assert extra.until('close')['payload']==b'\x03\xea'
  rejected=socket.create_connection(('127.0.0.1',port),timeout=2)
  rejected.sendall((f'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                    'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://evil.invalid\r\n\r\n').encode())
  assert b'400' in rejected.recv(4096); rejected.close()
  print('PASS: C HTTP/WS loopback integration: idle/partial clients, lock/observer contract, copied assets, split/coalesced input, 8 phones, full reports, ownership, disconnect, origin, close handling')
 finally:
  for p in peers:
   if p is not None: p.close()
  process.terminate() # Only our own test child; never touches other processes.
  process.wait(timeout=3)
if __name__=='__main__': main()
