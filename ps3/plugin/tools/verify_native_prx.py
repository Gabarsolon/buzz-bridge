"""Independent PRX relocation/pointer audit at unrelated RX/RW load bases.

No PPU instruction execution and no network. Also used for file-only probes.
"""
import collections, hashlib, json, os, pathlib, struct, subprocess
from prx_compat import read_ph
ROOT=pathlib.Path(__file__).resolve().parents[2]

def verify(path,expected_name,diagnostic=False,helper=False):
    data=pathlib.Path(path).read_bytes()
    assert data[:8]==b'\x7fELF\x02\x02\x01\x66'
    assert struct.unpack_from('>HH',data,16)==(0xffa4,21)
    ph=[p for _,p in read_ph(data)];loads=[p for p in ph if p[0]==1]
    rel,=[p for p in ph if p[0]==0x700000a4]
    assert [p[0] for p in ph] in ([1,1,0x700000a4],[1,0x700000a4])
    assert [p[1] for p in loads] in ([0x00400005,0x00600006],[7])
    assert all(p[5]<=p[6] and p[7] and p[7]&(p[7]-1)==0 and p[2]%p[7]==p[3]%p[7] for p in loads)
    assert rel[5]%24==0 and rel[6]==0
    rels=[struct.unpack_from('>QHBBIQ',data,o) for o in range(rel[2],rel[2]+rel[5],24)]
    counts=collections.Counter(r[4] for r in rels)
    assert set(counts)<={1,4,5,6,38,57}
    runs=[]
    for bases in ((0x10000000,0x24000000),(0x31000000,0x15000000)):
        bases=bases[:len(loads)]
        memory=[bytearray(data[p[2]:p[2]+p[5]])+bytearray(p[6]-p[5]) for p in loads]
        def region(address,size=1):
            found=[(m,address-b) for b,m in zip(bases,memory) if b<=address and address+size<=b+len(m)]
            assert len(found)==1,(hex(address),size)
            return found[0]
        def get(fmt,address):
            m,off=region(address,struct.calcsize(fmt));return struct.unpack_from(fmt,m,off)
        def cstr(address):
            m,off=region(address);return m[off:m.index(0,off)].decode()
        for off,unknown,iv,ia,kind,value in rels:
            assert unknown==0 and ia<len(loads) and (iv<len(loads) or iv==255)
            value+=bases[iv] if iv!=255 else 0
            m=memory[ia];width=8 if kind==38 else 4 if kind==1 else 2
            assert 0<=off and off+width<=len(m)
            if kind==1: struct.pack_into('>I',m,off,value)
            elif kind==38: struct.pack_into('>Q',m,off,value)
            elif kind==4: struct.pack_into('>H',m,off,value&65535)
            elif kind==5: struct.pack_into('>H',m,off,(value>>16)&65535)
            elif kind==6: struct.pack_into('>H',m,off,((value+0x8000)>>16)&65535)
            elif kind==57: struct.pack_into('>H',m,off,(struct.unpack_from('>H',m,off)[0]&3)|(value&65532))
        info=bases[0]+loads[0][4]-loads[0][2]
        assert cstr(info+4)==expected_name
        minor=3 if helper else 9 if expected_name=='ps3buzz' else 4
        assert get('>H',info)[0]==0 and get('>BB',info+2)==(0,minor)
        toc,es,ee,ss,se=get('>IIIII',info+32)
        assert any(b<=toc<b+len(m)+0x8000 for b,m in zip(bases,memory))
        management={};libraries={}
        for start,end,export in ((es,ee,True),(ss,se,False)):
            address=start
            while address<end:
                size=get('>B',address)[0];assert size==(28 if export else 44)
                assert address+size<=end
                attr,nfunc=get('>HH',address+4)
                name,nt,st=get('>III',address+16)
                if name:libraries[cstr(name)]=nfunc
                for i in range(nfunc):
                    nid=get('>I',nt+4*i)[0];entry=get('>I',st+4*i)[0]
                    if export:
                        code,otoc=get('>II',entry)
                        assert code%4==0 and bases[0]<=code<bases[0]+loads[0][5] and otoc==toc
                        management[nid]=dict(opd=entry,code=code,toc=otoc)
                    else:
                        assert attr==9 and get('>I',entry)[0]==0x7c0802a6
                        assert bases[0]<=entry and entry+52<=bases[0]+loads[0][5]
                address+=size
            assert address==end
        assert set(management)=={0xbc9a0086,0xab779874}
        if diagnostic: assert not libraries and ss==se
        elif helper:
            assert set(libraries)<= {'sys_io','sysPrxForUser'},libraries
            assert 'sysPrxForUser' in libraries and 'cellNetCtl' not in libraries,libraries
        else: assert libraries=={'sys_io':47,'sys_net':55,'sysPrxForUser':118}
        runs.append(dict(bases=[hex(b) for b in bases],management=management,libraries=libraries))
    return dict(result='PASS',elf_sha256=hashlib.sha256(data).hexdigest(),module=expected_name,
                mapped_bytes=sum(p[6] for p in loads),relocations=dict(counts),runs=runs,
                compact_opd_bytes=8,export_record_bytes=28,import_record_bytes=44,
                console_tested=False)

def main():
    p=ROOT/'build/ps3buzz.prx';report=verify(p,'ps3buzz')
    data=p.read_bytes();loads=[p for _,p in read_ph(data) if p[0]==1]
    assert struct.unpack_from('>I',data,48)[0]==0x01000000
    assert struct.unpack_from('>Q',data,40)[0]==0 and struct.unpack_from('>HHH',data,58)==(0,0,0)
    assert len(loads)==2 and loads[0][2]==0xf0 and loads[0][3]==0
    sdk=pathlib.Path(os.environ.get('PS3DK',r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64'))
    link=ROOT/'build/native/ps3buzz_game'
    assert not subprocess.check_output([str(sdk/'ppu/bin/ppu-nm.exe'),'-u',str(link)],text=True).strip()
    nm=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm.exe'),'-g',str(link)],text=True)
    symbols={line.split()[2]:int(line.split()[0],16) for line in nm.splitlines() if len(line.split())==3 and line.split()[1] in ('T','D','B','R')}
    assert not(set(symbols)&{'malloc','_malloc_r','__syscalls','__getreent','fopen','__librt_sbrk_r'})
    for name,nid in [('module_start',0xbc9a0086),('module_stop',0xab779874)]:
        address=symbols[name];i=next(i for i,p in enumerate(loads) if p[3]<=address<p[3]+p[6])
        assert report['runs'][0]['management'][nid]['opd']==int(report['runs'][0]['bases'][i],16)+address-loads[i][3]
    report.update(unresolved_symbols=0,process_crt_or_allocator=False)
    # Host time mocks cannot test the PS3 branch. Check the PPU object too.
    disasm=subprocess.check_output([str(sdk/'ppu/bin/ppu-objdump.exe'),'-dr',
                                   str(ROOT/'build/native/CMakeFiles/ps3buzz_game.dir/native.c.obj')],text=True)
    import re
    clock_calls=len(re.findall(r'R_PPC64_REL24\s+sys_time_get_system_time\b',disasm))
    assert clock_calls>=3,'USB/HTTP/diag must call the correct microsecond clock import'
    assert not re.search(r'\bli\s+r11,145\b',disasm),'bad raw current-time syscall returned'
    report.update(native_clock_import_calls=clock_calls,raw_syscall145_in_native_object=False)
    (ROOT/'build/native-disasm12.txt').write_text(disasm)
    (ROOT/'build/prx_audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PRX audit PASS: independent RX/RW bases, management OPDs, 220 imports, bounds, no process CRT/allocator.')
if __name__=='__main__': main()
