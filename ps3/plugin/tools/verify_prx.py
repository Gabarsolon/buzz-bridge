"""Independent structural/relocation audit, not a PS3 loader or emulator."""
import collections,hashlib,json,pathlib,struct,subprocess,os
ROOT=pathlib.Path(__file__).resolve().parents[2]
p=ROOT/'build/ps3buzz.prx'; data=p.read_bytes()
assert data[:8]==b'\x7fELF\x02\x02\x01\x66'
assert struct.unpack_from('>HH',data,16)==(0xffa4,21)
phoff=struct.unpack_from('>Q',data,32)[0]
ph=[struct.unpack_from('>IIQQQQQQ',data,phoff+i*56) for i in range(struct.unpack_from('>H',data,56)[0])]
if len([p for p in ph if p[0]==1])==2:
    from verify_native_prx import main
    main()
    raise SystemExit(0)
load,= [p for p in ph if p[0]==1]
rel,= [p for p in ph if p[0]==0x700000a4]
assert load[2]==load[3]==0 and load[5]<=load[6] and load[1]==7
rels=[struct.unpack_from('>QHBBIQ',data,o) for o in range(rel[2],rel[2]+rel[5],24)]
counts=collections.Counter(r[4] for r in rels)
assert set(counts)<={1,4,5,6,38,57}
for base in (0x10000000,0x20000000):
    mem=bytearray(data[load[2]:load[2]+load[5]])+bytearray(load[6]-load[5])
    for off,unknown,iv,ia,kind,value in rels:
        assert unknown==0 and ia==0 and iv in (0,255) and off<len(mem)
        value+=base if iv==0 else 0
        if kind==1: struct.pack_into('>I',mem,off,value)
        elif kind==38: struct.pack_into('>Q',mem,off,value)
        elif kind==4: struct.pack_into('>H',mem,off,value&65535)
        elif kind==5: struct.pack_into('>H',mem,off,(value>>16)&65535)
        elif kind==6: struct.pack_into('>H',mem,off,((value+0x8000)>>16)&65535)
        elif kind==57: struct.pack_into('>H',mem,off,(struct.unpack_from('>H',mem,off)[0]&3)|(value&65532))
    info=load[4]
    assert bytes(mem[info+4:info+12])==b'ps3buzz\0'
    toc,es,ee,ss,se=struct.unpack_from('>IIIII',mem,info+32)
    assert base<=toc<base+len(mem)+0x8000
    assert all(base<=v<=base+len(mem) for v in (es,ee,ss,se))
    management={};libraries=[]
    for start,end,export in ((es,ee,True),(ss,se,False)):
        a=start-base
        while a<end-base:
            assert mem[a]==(28 if export else 44)
            attr,nfunc=struct.unpack_from('>HH',mem,a+4)
            name,nt,st=struct.unpack_from('>III',mem,a+16)
            if name: libraries.append(mem[name-base:mem.index(0,name-base)].decode())
            for j in range(nfunc):
                nid=struct.unpack_from('>I',mem,nt-base+4*j)[0]
                opd=struct.unpack_from('>I',mem,st-base+4*j)[0]
                assert base<=opd<base+len(mem)-8 and opd%4==0
                if export:
                    code,otoc=struct.unpack_from('>II',mem,opd-base)
                    assert code%4==0 and base<=code<base+load[5]
                    assert otoc==toc
                    management[nid]=opd-base
                else:
                    # PS3DK's attr=9 import table contains instruction stubs.
                    # The retail game's table uses resolved compact OPDs; the
                    # two representations must not be confused by this audit.
                    assert attr==9 and opd+52<=base+load[5]
                    assert struct.unpack_from('>I',mem,opd-base)[0]==0x7c0802a6
            a+=mem[a]
        assert a==end-base
    assert set(management)=={0xbc9a0086,0xab779874}
    assert set(libraries)=={'sys_io','sys_net','sysPrxForUser'}
sdk=pathlib.Path(os.environ.get('PS3DK',r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64'))
nm=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm.exe'),'-g',str(ROOT/'build/native/ps3buzz_game')],text=True)
assert not subprocess.check_output([str(sdk/'ppu/bin/ppu-nm.exe'),'-u',str(ROOT/'build/native/ps3buzz_game')],text=True).strip()
symbols={line.split()[2]:int(line.split()[0],16) for line in nm.splitlines() if len(line.split())==3 and line.split()[1] in ('T','D','B','R')}
assert not (set(symbols)&{'malloc','_malloc_r','__syscalls','__getreent','fopen','__librt_sbrk_r'})
assert symbols['module_start']==management[0xbc9a0086] and symbols['module_stop']==management[0xab779874]
report={'result':'PASS','elf_sha256':hashlib.sha256(data).hexdigest(),'module':'ps3buzz','version':'0.4',
        'mapped_bytes':load[6],'relocations':dict(counts),'simulated_bases':['0x10000000','0x20000000'],
        'imports':libraries,'compact_opd_bytes':8,'management_symbols':management,
        'export_record_bytes':28,'import_record_bytes':44,
        'unresolved_symbols':0,'process_crt_or_allocator':False,'console_tested':False}
(ROOT/'build/prx_audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('PRX audit PASS: relocation at two bases; compact management OPDs/import instruction stubs; 3 libraries; no unresolved symbols or process CRT/allocator.')
