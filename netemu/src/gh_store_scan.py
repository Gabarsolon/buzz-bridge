from elftools.elf.elffile import ELFFile
import json,hashlib
with open(H/f'build/{variant}_payload.elf','rb') as f:
 e=ELFFile(f);sec=e.get_section_by_name('.text');lo=sec['sh_addr'];hi=lo+sec['sh_size']
 functions={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols() if x['st_info']['type']=='STT_FUNC' and lo<=x['st_value']<hi}
blob=(H/f'build/{variant}.bin').read_bytes();assert bytes(flat.getBytes(s.getAddress(0x4800),len(blob)))==blob
ir=json.loads((H/f'evidence/{variant}_ir_store_audit.json').read_text());assert ir['passed']
syms=json.loads((H/f'{variant}_patch_table.json').read_text())['symbols']
roots={}
for row in ir['stores']:roots.setdefault(row['function'],set()).update(row['roots'])
for row in ir['calls']:
 if 'destination_roots' in row:roots.setdefault(row['function'],set()).update(row['destination_roots'])
intervals=[(0x4800+i*64,0x4800+i*64+64,'wrapper') for i in range(24)]+[(0x4e00,0x4e08,'trampoline'),(0x4e10,0x4e18,'trampoline'),(0x4e20,0x4e28,'trampoline'),(0x4e30,0x4e38,'trampoline'),(0x4e60,0x4e68,'trampoline'),(0x4e70,0x4e78,'trampoline'),(0x4e80,0x4f80,'wrapper'),(0x4f80,0x4f88,'trampoline'),(lo,hi,'C')]
rows=[];stores=[]
for begin,end,kind in intervals:
 for pc in range(begin,end,4):
  word=bytes(flat.getBytes(s.getAddress(pc),4))
  if word==bytes(4):continue
  ins=pd.disassemble(s.getAddress(pc));assert ins is not None,hex(pc)
  name=kind if kind!='C' else max(((k,v) for k,v in functions.items() if v<=pc),key=lambda x:x[1])[0]
  row=dict(pc=hex(pc),word=word.hex(),function=name,instruction=str(ins));rows.append(row)
  if any(x.getOpcode()==x.STORE for x in ins.getPcode()):
   assert str(ins.getMnemonicString()) in ('stb','stbu','stbx','stbux','sth','sthu','sthx','sthux','stw','stwu','stwx','stwux','std','stdu','stdx','stdux','stwcx.'),row
   if '(r1)' in str(ins):row['provenance']=['native stack/linkage']
   else:
    assert name in roots,row
    row['provenance']=sorted(roots[name])
   stores.append(row)
(H/f'analysis/{variant}_payload_listing.txt').write_text('\n'.join(f"{r['pc']} {r['word']} {r['function']} {r['instruction']}" for r in rows))
(H/f'evidence/{variant}_native_store_scan.json').write_text(json.dumps(dict(passed=True,variant=variant,elf_sha256=hashlib.sha256((H/f'ps2_netemu_{variant}.elf').read_bytes()).hexdigest(),instructions=len(rows),stores=stores,scope='Independent Ghidra opcode/store enumeration, joined by function to LLVM pointer provenance. It is not independent machine-code dataflow proof for every register. Dynamic native tests additionally bound all tracked RAM writes. Source guards bound the 8 queue slots and 32-byte SIO2 FIFO write.'),indent=2));print(variant,len(rows),'decoded instructions,',len(stores),'stores accounted',flush=True)
