"""Offline SELF/PRX comparison. Optional existing RPCS3 key source, no downloads.

Uses already-installed Crypto solely to decrypt our own modules. No keys are
written to reports. Never runs PS3 code or makes a network request.
"""
import argparse,contextlib,hashlib,io,json,pathlib,runpy,struct,subprocess,sys
from prx_compat import read_ph
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=pathlib.Path(r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64')

def sha(data):return hashlib.sha256(data).hexdigest()
def elf_summary(data):
    ph=read_ph(data);load=next(p for _,p in ph if p[0]==1);info=load[4]
    es,ee,ss,se=struct.unpack_from('>IIII',data,info+36)
    nt,st=struct.unpack_from('>II',data,es+20)
    entries={}
    for i in range(struct.unpack_from('>H',data,es+6)[0]):
        nid=struct.unpack_from('>I',data,nt+4*i)[0];opd=struct.unpack_from('>I',data,st+4*i)[0]
        code,toc=struct.unpack_from('>II',data,opd)
        assert 0xb0<=code<load[5] and code%4==0
        # stdu r1,-N(r1), or stwu for a different user ABI.
        frames=[]
        for at in range(code,min(code+80,load[5]),4):
            ins=struct.unpack_from('>I',data,at)[0]
            if ins&0xffff0003==0xf8210001:
                imm=ins&0xfffc;frames.append(0x10000-imm if imm&0x8000 else -imm)
        entries[f'{nid:08x}']={'opd':opd,'code':code,'toc':toc,'entry_frame_bytes':frames[0] if frames else 0}
    return {'elf_type':hex(struct.unpack_from('>H',data,16)[0]),'osabi':data[7],
      'program_headers':[{'type':hex(p[0]),'flags':p[1],'offset':p[2],'vaddr':p[3],
                         'paddr':p[4],'filesz':p[5],'memsz':p[6],'align':p[7]} for _,p in ph],
      'module_info_offset':info,'module_info_hex':data[info:info+52].hex(),
      'export_range':[es,ee],'export_hex':data[es:ee].hex(),
      'import_range':[ss,se],'management':entries}

def container(path,prx,decrypt):
    data=path.read_bytes();expected=prx.read_bytes()
    rev=struct.unpack_from('>H',data,8)[0]
    hsize=struct.unpack_from('>Q',data,16)[0]
    ai,eo,po,so,pi,ve,co,cs=struct.unpack_from('>8Q',data,40)
    auth,vendor,typ,version=struct.unpack_from('>QIIQ',data,ai)
    blocks=[];at=co
    while at<co+cs:
        kind,size,nxt=struct.unpack_from('>IIQ',data,at);assert size>=16
        blocks.append({'type':kind,'payload_hex':data[at+16:at+size].hex()});at+=size
    assert at==co+cs
    if rev==0x8000:
        actual=data[hsize:hsize+len(expected)];assert actual==expected
    else:
        dest=ROOT/'build/tmp'/(path.name+'.unwrapped.elf')
        with contextlib.redirect_stdout(io.StringIO()):decrypt(str(path),str(dest))
        actual=dest.read_bytes()
    # make_sprx intentionally omits the section table. Compare every runtime
    # segment byte; compare the entire unsigned PRX in fself above.
    for _,p in read_ph(expected):assert actual[p[2]:p[2]+p[5]]==expected[p[2]:p[2]+p[5]]
    return {'file':path.relative_to(ROOT).as_posix(),'bytes':len(data),'sha256':sha(data),
      'key_revision':hex(rev),'self_type':typ,'auth_id':f'{auth:016x}','vendor_id':f'{vendor:08x}',
      'sce_version':f'{version:016x}','self_elf_header_offset':eo,'self_section_header_offset':so,
      'elf_section_count':struct.unpack_from('>H',data,eo+60)[0],'control_blocks':blocks,
      'segment_round_trip':'PASS','prx':elf_summary(expected)}

def normalize_variant(data,variant):
    return data.replace(('ps3buzz_diag_'+variant).encode(),b'ps3buzz_diag_X').replace((variant+'\n\0').encode(),b'X\n\0')

def main():
    if sum(p[0]==1 for _,p in read_ph((ROOT/'build/ps3buzz.prx').read_bytes()))==2:
        from audit_format10 import main as audit_native
        return audit_native()
    p=argparse.ArgumentParser();p.add_argument('--keys-cpp',type=pathlib.Path,required=True)
    p.add_argument('--decrypt-helper',type=pathlib.Path,required=True);a=p.parse_args()
    helper=runpy.run_path(str(a.decrypt_helper))['main']
    def decrypt(src,dest):helper(str(a.keys_cpp),src,dest)
    pairs=[('ps3buzz_v1.sprx','ps3buzz_v1.prx'),('ps3buzz_v1.fake.sprx','ps3buzz_v1.prx'),
           ('ps3buzz.sprx','ps3buzz.prx'),('ps3buzz.system.sprx','ps3buzz.prx'),('ps3buzz.fake.sprx','ps3buzz.prx')]
    pairs +=[(f'diag/ps3buzz_diag_{v}.sprx',f'diag/ps3buzz_diag_{v}.prx') for v in 'ABCD']
    records=[container(ROOT/'build'/c,ROOT/'build'/prx,decrypt) for c,prx in pairs]
    comparisons=[]
    for a,b in [('A','B'),('B','C'),('C','D')]:
        ad=normalize_variant((ROOT/f'build/diag/ps3buzz_diag_{a}.prx').read_bytes(),a)
        bd=normalize_variant((ROOT/f'build/diag/ps3buzz_diag_{b}.prx').read_bytes(),b)
        assert len(ad)==len(bd)
        changed=[i for i,(x,y) in enumerate(zip(ad,bd)) if x!=y]
        assert len(changed)==(4 if a=='A' else 0),changed
        comparisons.append({'pair':a+'/'+b,'identity_normalized_prx_diff_offsets':changed})
    main_prx=records[2]['prx'];entry=main_prx['management']['bc9a0086'];assert entry['entry_frame_bytes']<=256
    code=entry['code'];data=(ROOT/'build/ps3buzz.prx').read_bytes();ins=[]
    for at in range(code,code+256,4):
        word=struct.unpack_from('>I',data,at)[0];ins.append(word)
        assert word&0xfc000003!=0x48000001,'A function call precedes the first syscall'
        if word==0x44000002:break
    assert 0x39600321 in ins,'First syscall must be FS open (801)'
    # Validate diagnostics at two load bases independently of the SDK checker.
    for v in 'ABCD':
        data=(ROOT/f'build/diag/ps3buzz_diag_{v}.prx').read_bytes()
        ph=read_ph(data);load=next(p for _,p in ph if p[0]==1);rel=next(p for _,p in ph if p[0]==0x700000a4)
        for base in (0x10000000,0x20000000):
            mem=bytearray(data[:load[5]])+bytearray(load[6]-load[5])
            for at in range(rel[2],rel[2]+rel[5],24):
                off,unk,iv,ia,kind,ptr=struct.unpack_from('>QHBBIQ',data,at)
                assert (unk,iv,ia,kind)==(0,0,0,1)
                struct.pack_into('>I',mem,off,base+ptr)
            info=load[4];toc,es,ee,ss,se=struct.unpack_from('>IIIII',mem,info+32)
            assert ss==se and ee-es==(44 if v=='A' else 28)
            nt,st=struct.unpack_from('>II',mem,es-base+20)
            for i in range(2):
                opd=struct.unpack_from('>I',mem,st-base+4*i)[0];code,otoc=struct.unpack_from('>II',mem,opd-base)
                assert otoc==toc and base+0xb0<=code<base+load[5]
    report={'result':'PASS','console_tested':False,'containers':records,'diagnostic_comparisons':comparisons,
      'new_entry_first_syscall':801,'new_entry_frame_bytes':entry['entry_frame_bytes'],
      'diag_two_base_relocations':'PASS','cfw_loader_acceptance':'UNVERIFIED'}
    (ROOT/'build/loader_audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Loader audit PASS: {len(records)} container segment round trips; diag A/B differs at four format bytes; B/C and C/D have identical normalized PRX payloads; new entry {entry["entry_frame_bytes"]} bytes, first syscall=801.')
if __name__=='__main__':main()
