"""Small, checked PPC64 big-endian encoder. No external assembler required.

Only the instructions used by this prototype are supported. Branches are
PC-relative, alignment/range checked, and never silently truncated.
"""
import re
import struct

def reg(s):
    if isinstance(s,int): n=s
    else:
        assert re.fullmatch(r'r(?:[0-9]|[12][0-9]|3[01])',s),s
        n=int(s[1:])
    assert 0<=n<32
    return n

def imm(s): return int(s,0) if isinstance(s,str) else s
def signed(n,bits):
    n=imm(n)
    assert -(1<<(bits-1))<=n<(1<<(bits-1)),(n,bits)
    return n&((1<<bits)-1)
def unsigned(n,bits):
    n=imm(n)
    assert 0<=n<1<<bits,(n,bits)
    return n
def dform(op,rt,ra,d): return op<<26|reg(rt)<<21|reg(ra)<<16|signed(d,16)
def branch(pc,target,link=False):
    delta=target-pc
    assert delta%4==0 and -(1<<25)<=delta<(1<<25),(hex(pc),hex(target))
    return 18<<26|(delta&0x3fffffc)|int(link)

def encode(op,args,pc=0,labels=None):
    labels=labels or {}
    def target(s): return labels[s] if s in labels else imm(s)
    def mem(s):
        m=re.fullmatch(r'([^()]+)\((r\d+)\)',s)
        assert m,s
        return reg(m[2]),imm(m[1])
    if op in ('b','bl'): return branch(pc,target(args[0]),op=='bl')
    if op in ('beq','bne','blt','bge','bgt','ble'):
        assert len(args)==1
        bo,bi={'beq':(12,2),'bne':(4,2),'blt':(12,0),'bge':(4,0),'bgt':(12,1),'ble':(4,1)}[op]
        delta=target(args[0])-pc
        assert delta%4==0 and -(1<<15)<=delta<(1<<15),(pc,args)
        return 16<<26|bo<<21|bi<<16|(delta&0xfffc)
    if op=='mfcr':return 0x7c000026 | (reg(args[0])<<21)
    if op=='mtcr':return 0x7c0ff120 | (reg(args[0])<<21)
    if op=='mfctr':return 0x7c0902a6 | (reg(args[0])<<21)
    if op=='bctrl':return 0x4e800421
    if op=='blr': return 0x4e800020
    if op=='nop': return 0x60000000
    if op=='sync': return 0x7c0004ac
    if op in ('mflr','mtlr'):
        return (31<<26|reg(args[0])<<21|8<<16|(339 if op=='mflr' else 467)<<1)
    if op=='mr':
        rd,rs=map(reg,args)
        return 31<<26|rs<<21|rd<<16|rs<<11|444<<1
    if op in ('li','lis'): return dform(14 if op=='li' else 15,args[0],0,args[1])
    if op in ('addi','addis'): return dform(14 if op=='addi' else 15,*args)
    if op=='add':
        rt,ra,rb=map(reg,args)
        return 31<<26|rt<<21|ra<<16|rb<<11|266<<1
    if op in ('ori','andi.'):
        ra,rs=map(reg,args[:2])
        return (24 if op=='ori' else 28)<<26|rs<<21|ra<<16|unsigned(args[2],16)
    if op in ('lwz','lbz','lhz','stw','stb'):
        ra,ds=mem(args[1])
        return dform({'lwz':32,'lbz':34,'lhz':40,'stw':36,'stb':38}[op],args[0],ra,ds)
    if op in ('ld','std','stdu'):
        ra,ds=mem(args[1]); assert ds%4==0
        return dform(58 if op=='ld' else 62,args[0],ra,ds)|(1 if op=='stdu' else 0)
    if op in ('cmpwi','cmpdi','cmplwi'):
        ra=reg(args[0]); val=imm(args[1]); logical=op=='cmplwi'
        return (10 if logical else 11)<<26|(1 if op=='cmpdi' else 0)<<21|ra<<16|(unsigned(val,16) if logical else signed(val,16))
    if op=='cmpd': return 31<<26|1<<21|reg(args[0])<<16|reg(args[1])<<11
    if op in ('slwi','srwi'):
        ra,rs=map(reg,args[:2]); n=unsigned(args[2],5)
        sh,mb,me=(n,0,31-n) if op=='slwi' else ((32-n)%32,n,31)
        return 21<<26|rs<<21|ra<<16|sh<<11|mb<<6|me<<1
    raise ValueError((op,args))

class Assembler:
    def __init__(self,base):
        self.base=base; self.pc=base; self.labels={}; self.items=[]
    def label(self,name):
        assert name not in self.labels,name
        self.labels[name]=self.pc
    def emit(self,line):
        line=line.split('#',1)[0].strip()
        if not line:return
        if line.endswith(':'):self.label(line[:-1]);return
        op,*rest=line.split(None,1)
        args=[x.strip() for x in rest[0].split(',')] if rest else []
        self.items.append((self.pc,op,args));self.pc+=4
    def asm(self,src):
        for line in src.splitlines():self.emit(line)
    def finish(self):
        result=bytearray(); listing=[]
        for pc,op,args in self.items:
            w=encode(op,args,pc,self.labels);result+=struct.pack('>I',w)
            rendered=[hex(self.labels[x]) if x in self.labels else x for x in args]
            listing.append(dict(address=pc,word=f'{w:08x}',op=op,args=rendered))
        return bytes(result),listing
