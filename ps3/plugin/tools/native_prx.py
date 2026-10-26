"""Pack a linked PPC64 ELF into a stripped RX/RW PS3 PRX.

Implements the published PS3 PRX record layout, not an emulator. Unlike the
SDK converter, TOC-relative relocations are checked against the TOC segment,
not the segment containing the instruction. Unsupported cases fail closed.
"""
import argparse, collections, json, pathlib, struct

class Elf:
    def __init__(self,data):
        self.data=data
        assert data[:7]==b'\x7fELF\x02\x02\x01'
        assert struct.unpack_from('>H',data,18)[0]==21
        po,so=struct.unpack_from('>QQ',data,32)
        pe,pn,se,sn,ss=struct.unpack_from('>HHHHH',data,54)
        assert pe==56 and se==64
        self.ph=[struct.unpack_from('>IIQQQQQQ',data,po+56*i) for i in range(pn)]
        self.loads=[p for p in self.ph if p[0]==1]
        self.sh=[struct.unpack_from('>IIQQQQIIQQ',data,so+64*i) for i in range(sn)]
        names=data[self.sh[ss][4]:self.sh[ss][4]+self.sh[ss][5]]
        self.sections={self.cstr(names,s[0]):s for s in self.sh}
        self.syms=[]
        for s in self.sh:
            if s[1]==2:
                strings=self.sh[s[6]]; strings=data[strings[4]:strings[4]+strings[5]]
                for at in range(s[4],s[4]+s[5],24):
                    n,info,other,idx,value,size=struct.unpack_from('>IBBHQQ',data,at)
                    self.syms.append(dict(name=self.cstr(strings,n),section=idx,value=value))
        self.symbols={s['name']:s for s in self.syms if s['name']}
    @staticmethod
    def cstr(data,offset): return data[offset:data.index(0,offset)].decode()
    def segment(self,address):
        found=[i for i,p in enumerate(self.loads) if p[3]<=address<p[3]+p[6]]
        assert len(found)==1,('address outside/overlapping LOADs',hex(address))
        return found[0]
    def file(self,address):
        p=self.loads[self.segment(address)]
        assert address-p[3]<p[5]
        return p[2]+address-p[3]
    def symbol_segment(self,symbol,value):
        idx=symbol['section']
        if 0<idx<len(self.sh) and self.sh[idx][2]&2:
            return self.segment(self.sh[idx][3])
        return self.segment(value)
    def relas(self):
        for s in self.sh:
            if s[1]!=4 or not self.sh[s[7]][2]&2: continue
            for at in range(s[4],s[4]+s[5],24):
                off,info,add=struct.unpack_from('>QQq',self.data,at)
                yield off,info&0xffffffff,self.syms[info>>32],add

def build(source,output,name,flags=0x01000000,start=None,stop=None):
    elf=Elf(pathlib.Path(source).read_bytes()); data=bytearray(elf.data)
    assert len(elf.loads)==2 and [p[1] for p in elf.loads]==[5,6]
    mi=elf.sections['.rodata.sceModuleInfo']; assert mi[5]==52 and elf.segment(mi[3])==0
    assert len(name.encode())<=27
    minor=9 if name=='ps3buzz' else 3 if name=='ps3buzz_vsh' else 4
    struct.pack_into('>HBB28s',data,mi[4],0,0,minor,name.encode())
    toc,es,ee,ss,se=struct.unpack_from('>IIIII',data,mi[4]+32)
    ef=elf.file(es); assert ee-es==44 and data[ef]==44
    assert struct.unpack_from('>HHH',data,ef+4)==(0x8000,2,0)
    data[ef]=28
    patches={mi[3]+40:es+28}
    nt,st=struct.unpack_from('>II',data,ef+20)
    for i in range(2):
        nid=struct.unpack_from('>I',data,elf.file(nt)+4*i)[0]
        sym={0xbc9a0086:start,0xab779874:stop}.get(nid)
        if sym: patches[st+4*i]=elf.symbols[sym]['value']
    for site,value in patches.items():struct.pack_into('>I',data,elf.file(site),value)
    # The nidgen archive leaves function counts for the packer to resolve.
    imports=[]
    for address in range(ss,se,44):
        off=elf.file(address); assert data[off]==44
        imports.append((off,struct.unpack_from('>I',data,off+20)[0]))
    if imports:
        fn=elf.sections['.rodata.sceFNID']; end=fn[3]+fn[5]
        for off,nids in imports:
            limit=min([end]+[n for o,n in imports if o!=off and n>=nids])
            assert limit>=nids and (limit-nids)%4==0
            struct.pack_into('>H',data,off+6,(limit-nids)//4)
    relocs=[]; drops=collections.Counter(); covered=set()
    tocseg=elf.symbol_segment(elf.symbols['.TOC.'],toc)
    for site,kind,sym,add in elf.relas():
        ia=elf.segment(site); value=sym['value']+add
        if site in patches: value=patches[site]; covered.add(site)
        iv=elf.symbol_segment(sym,value)
        if kind==0: continue
        if kind in (10,11,12,13,26,44,249,250,251,252):
            # ELFv1 branch relocations name the function's descriptor; ld
            # resolves its first compact word to the actual code entry.
            if kind in (10,11,12,13) and 0<sym['section']<len(elf.sh) and elf.sh[sym['section']]==elf.sections.get('.opd'):
                value=struct.unpack_from('>I',elf.data,elf.file(sym['value']))[0]+add
                iv=elf.segment(value)
            assert ia==iv,('cross-segment PC relocation',site,kind,sym)
            drops[str(kind)]+=1; continue
        if kind in (47,48,49,50,63,64):
            assert iv==tocseg,('cross-segment TOC relocation',site,kind,sym)
            drops[str(kind)]+=1; continue
        if kind in (14,15,16,17,58,59):
            assert elf.segment(elf.sections['.got'][3])==tocseg
            drops[str(kind)]+=1; continue
        native={1:1,4:4,5:5,6:6,38:38,43:38,51:38,57:57}.get(kind)
        assert native is not None,('unsupported allocated relocation',kind,site,sym)
        offset=site-elf.loads[ia][3]; ptr=value-elf.loads[iv][3]
        assert 0<=ptr<0x100000000
        width=8 if native==38 else 4 if native==1 else 2
        assert offset+width<=elf.loads[ia][6]
        relocs.append((offset,0,iv,ia,native,ptr))
    assert covered==set(patches)
    for site in range(mi[3]+32,mi[3]+52,4):
        assert any(r[0]+elf.loads[r[3]][3]==site for r in relocs)
    relocs.sort(key=lambda r:(r[3],r[0]))
    # Packed file: header + three PHDRs, 16-byte aligned RX, RW, PPURELA.
    result=bytearray(0xf0); result[:64]=data[:64]
    result[7]=0x66
    struct.pack_into('>HHIQQQIHHHHHH',result,16,0xffa4,21,1,0,64,0,flags,64,56,3,0,0,0)
    headers=[]
    for i,p in enumerate(elf.loads):
        result+=bytes((-len(result))%16); offset=len(result)
        result+=data[p[2]:p[2]+p[5]]
        paddr=offset+mi[3]-p[3] if i==0 else 0
        # The control also declares matching SPU read/write permission bits.
        # Preserve its complete p_flags pattern, not just the PPU low bits.
        flags_with_spu=p[1]|((p[1]&6)<<20)
        headers.append((1,flags_with_spu,offset,p[3],paddr,p[5],p[6],16))
    result+=bytes((-len(result))%16); offset=len(result)
    for r in relocs:result+=struct.pack('>QHBBIQ',*r)
    headers.append((0x700000a4,0,offset,0,0,len(relocs)*24,0,16))
    for i,p in enumerate(headers):struct.pack_into('>IIQQQQQQ',result,64+56*i,*p)
    pathlib.Path(output).write_bytes(result)
    report=dict(source=str(source),file=str(output),flags=hex(flags),program_headers=headers,
                relocation_types=dict(collections.Counter(r[4] for r in relocs)),
                invariant_relocations=dict(drops),imports=len(imports),section_count=0,
                module_info_file_offset=headers[0][4],toc_segment=tocseg)
    pathlib.Path(str(output)+'.layout.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Native PRX: {name}, RX/RW, {len(relocs)} relocations, no sections, flags={flags:#x}')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');p.add_argument('--name',required=True)
    p.add_argument('--flags',type=lambda n:int(n,0),default=0x1000000)
    p.add_argument('--start');p.add_argument('--stop');a=p.parse_args()
    build(a.source,a.output,a.name,a.flags,a.start,a.stop)
