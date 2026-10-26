"""Field-by-field offline control/old/new SELF audit. Never contacts a console."""
import collections, hashlib, json, pathlib, struct, subprocess
from self10 import read
from native_prx import Elf
from verify_native_prx import verify
from prx_compat import read_ph
ROOT=pathlib.Path(__file__).resolve().parents[2]
KEYS=ROOT/'research/load10/key_vault.cpp'
SDK=pathlib.Path(r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64')

def fields(data):
    eh=struct.unpack_from('>HHIQQQIHHHHHH',data,16)
    names=('type','machine','version','entry','phoff','shoff','flags','ehsize','phentsize','phnum','shentsize','shnum','shstrndx')
    header=dict(zip(names,eh));header.update(ident_hex=data[:16].hex(),elf_class=data[4],endian=data[5],osabi=data[7],abi_version=data[8])
    ph=[p for _,p in read_ph(data)];loads=[p for p in ph if p[0]==1]
    def file(address):
        found=[p[2]+address-p[3] for p in loads if p[3]<=address<p[3]+p[5]]
        assert len(found)==1,(hex(address),loads)
        return found[0]
    def cstr(address):
        off=file(address);return data[off:data.index(0,off)].decode()
    mi=loads[0][4];attr,major,minor,name=struct.unpack_from('>HBB28s',data,mi)
    toc,es,ee,ss,se=struct.unpack_from('>IIIII',data,mi+32)
    records=[]
    for start,end,kind in ((es,ee,'export'),(ss,se,'import')):
        address=start
        while address<end:
            off=file(address);size=data[off];assert size>=28 and address+size<=end
            version,attr,nfunc,nvar,ntls=struct.unpack_from('>HHHHH',data,off+2)
            np,nt,st=struct.unpack_from('>III',data,off+16)
            entries=[]
            for i in range(nfunc):
                nid=struct.unpack_from('>I',data,file(nt)+4*i)[0]
                entry=struct.unpack_from('>I',data,file(st)+4*i)[0]
                record=dict(nid=f'{nid:08x}',entry=entry)
                if kind=='export':record.update(zip(('code','toc'),struct.unpack_from('>II',data,file(entry))))
                entries.append(record)
            records.append(dict(kind=kind,address=address,bytes=size,version=version,attributes=attr,
                                functions=nfunc,variables=nvar,tls=ntls,name=cstr(np) if np else '',entries=entries,
                                raw_hex=data[off:off+size].hex()))
            address+=size
        assert address==end
    rel=[p for p in ph if p[0]==0x700000a4]
    rels=[struct.unpack_from('>QHBBIQ',data,at) for p in rel for at in range(p[2],p[2]+p[5],24)]
    return dict(header=header,program_headers=[dict(zip(('type','flags','offset','vaddr','paddr','filesz','memsz','align'),p)) for p in ph],
                module_info=dict(file_offset=mi,segment_offset=mi-loads[0][2],bytes=52,attributes=struct.unpack_from('>H',data,mi)[0],
                                 version=[major,minor],name=name.split(b'\0')[0].decode(),info_version=name[-1],toc=toc,
                                 exports=[es,ee],imports=[ss,se],raw_hex=data[mi:mi+52].hex()),
                libraries=records,relocation_count=len(rels),relocation_types=dict(collections.Counter(r[4] for r in rels)),
                relocation_unknown=dict(collections.Counter(r[1] for r in rels)),
                relocation_index_pairs={f'{k[0]}/{k[1]}':v for k,v in collections.Counter((r[2],r[3]) for r in rels).items()})

def container(path,expected=None):
    r=read(path,KEYS);data=r['data'];actual=r['elf']
    assert all(r['hmac_checks'])
    if expected:
        original=expected.read_bytes()
        assert actual[:64]==original[:64]
        for _,p in read_ph(original):assert actual[p[2]:p[2]+p[5]]==original[p[2]:p[2]+p[5]]
        if r['header']['key_revision']&0x8000:assert data[r['header']['header_size']:]==original
    target=path.with_suffix('.decrypted.elf');target.write_bytes(actual)
    f=fields(actual)
    return dict(file=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
                decrypted_file=target.relative_to(ROOT).as_posix(),decrypted_bytes=len(actual),decrypted_sha256=hashlib.sha256(actual).hexdigest(),
                self=r['header'],segment_ext=r['segment_ext'],control=r['control'],metadata=r['meta'],metadata_sections=r['sections'],
                encrypted_capabilities=r['encrypted_capabilities'],hmac_checks=r['hmac_checks'],
                expected_segment_bytes='PASS' if expected else 'control; no input PRX',elf=f)

def normal(data,letter):
    return data.replace(f'ps3buzz_diag_{letter}'.encode(),b'ps3buzz_diag_X').replace((letter+'\n\0').encode(),b'X\n\0')

def main():
    pairs=[('ctrl/ctrl_misc.sprx',None),('previous10/ps3buzz.sprx','previous10/ps3buzz.prx'),
           ('ps3buzz_v1.sprx','ps3buzz_v1.prx'),('ps3buzz_v1.fake.sprx','ps3buzz_v1.prx')]
    pairs +=[(f'diag/ps3buzz_diag_{v}.sprx',f'diag/ps3buzz_diag_{v}.prx') for v in 'ABCDEFGH']
    pairs += [('ps3buzz.sprx','ps3buzz.prx'),('ps3buzz.flags0.sprx','ps3buzz.flags0.prx'),
              ('ps3buzz.legacy-layout.sprx','ps3buzz.legacy-layout.prx'),('ps3buzz.fake.sprx','ps3buzz.prx'),('ps3buzz.system.sprx','ps3buzz.prx')]
    records=[container(ROOT/'build'/c,ROOT/'build'/p if p else None) for c,p in pairs]
    normalized={'build/ps3buzz.sprx','build/ps3buzz.flags0.sprx','build/ps3buzz.legacy-layout.sprx'}
    normalized.update(f'build/diag/ps3buzz_diag_{v}.sprx' for v in 'EFG')
    for record in records:
        if record['file'] not in normalized: continue
        path=ROOT/record['file'];source=path.with_suffix('.prx').read_bytes()
        control=record['control']
        assert control[0]['payload']==bytes(32).hex()
        assert bytes.fromhex(control[1]['payload'])[20:40]==hashlib.sha1(source).digest()
        assert record['encrypted_capabilities'].endswith('000000000000007b0000000100020000')
        assert record['self']['key_revision']==1
        if record['elf']['header']['shnum']==0:assert record['self']['extended_header'][4]==0
        record['normalized_APP_profile']='PASS'
    probes=[]
    for v in 'EFGH':
        path=ROOT/f'build/diag/ps3buzz_diag_{v}.prx';probes.append(verify(path,f'ps3buzz_diag_{v}',True))
        link=path.with_suffix('.elf');elf=Elf(link.read_bytes())
        assert not subprocess.check_output([str(SDK/'ppu/bin/ppu-nm.exe'),'-u',str(link)],text=True).strip()
        # Read compact OPD code words. Only file syscalls may occur inside
        # module_start, which must not make any function calls or spawn workers.
        code=struct.unpack_from('>I',elf.data,elf.file(elf.symbols['module_start']['value']))[0]
        next_code=struct.unpack_from('>I',elf.data,elf.file(elf.symbols['diag_cobra_start']['value']))[0]
        words=[struct.unpack_from('>I',elf.data,elf.file(at))[0] for at in range(code,next_code,4)]
        assert not any(w&0xfc000001==0x48000001 for w in words)
        assert 0x4e800421 not in words,'indirect function call in diagnostic module_start'
        syscall_ids=[w&65535 for w in words if w>>16==0x3960]
        assert set(syscall_ids)=={801,803,804,820} and words.count(0x44000002)==4
        probes[-1].update(imports=0,module_start_calls=0,module_start_syscalls=syscall_ids,module_start_result=1)
    main_audits=[verify(ROOT/'build'/file,'ps3buzz') for file in ('ps3buzz.prx','ps3buzz.flags0.prx','ps3buzz.legacy-layout.prx')]
    comparisons=[]
    for a,b,allowed in [('E','F',[48]),('E','H',[]),('C','G',[48,51])]:
        ad=normal((ROOT/f'build/diag/ps3buzz_diag_{a}.prx').read_bytes(),a)
        bd=normal((ROOT/f'build/diag/ps3buzz_diag_{b}.prx').read_bytes(),b)
        assert len(ad)==len(bd)
        diff=[i for i,(x,y) in enumerate(zip(ad,bd)) if x!=y];assert diff==allowed,(a,b,diff)
        comparisons.append(dict(pair=a+'/'+b,normalized_prx_changed_bytes=diff))
    main=next(r for r in records if r['file']=='build/ps3buzz.sprx')
    start=next(e for lib in main['elf']['libraries'] if lib['kind']=='export' for e in lib['entries'] if e['nid']=='bc9a0086')
    data=(ROOT/'build/ps3buzz.prx').read_bytes();rx=main['elf']['program_headers'][0]
    off=rx['offset']+start['code']-rx['vaddr'];ins=[];frames=[]
    for at in range(off,off+256,4):
        word=struct.unpack_from('>I',data,at)[0];ins.append(word)
        assert word&0xfc000003!=0x48000001,'call before first syscall'
        if word&0xffff0003==0xf8210001:frames.append(0x10000-(word&0xfffc))
        if word==0x44000002:break
    assert 0x39600321 in ins and frames and frames[0]<=256
    report=dict(result='PASS',console_tested=False,containers=records,probes=probes,main_audits=main_audits,
                diagnostic_comparisons=comparisons,main_entry_first_syscall=801,main_entry_frame=frames[0],
                native_ENOEXEC_predicate='not available in published Cobra wrapper',ECDSA_verified=False)
    (ROOT/'build/format10_audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (ROOT/'build/loader_audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Format audit PASS: {len(records)} containers, {sum(len(r["hmac_checks"]) for r in records)} segment HMACs; E-H import-free/FS-only; three main PRXs at independent bases; normalized probe comparisons; first main syscall=801.')
if __name__=='__main__': main()
