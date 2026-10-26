"""Workspace-only PRX metadata fix. Does not move code, data or relocations."""
import argparse,json,pathlib,struct

def read_ph(data):
    off=struct.unpack_from('>Q',data,32)[0]
    return [(off+56*i,struct.unpack_from('>IIQQQQQQ',data,off+56*i))
            for i in range(struct.unpack_from('>H',data,56)[0])]

def fix(path, export_size=28, start=None, stop=None):
    path=pathlib.Path(path); original=path.read_bytes(); data=bytearray(original)
    assert data[:8]==b'\x7fELF\x02\x02\x01\x66'
    assert struct.unpack_from('>H',data,16)[0]==0xffa4
    ph=read_ph(data); load=next(p for _,p in ph if p[0]==1)
    assert load[2]==load[3]==0
    info=load[4]; es,ee=struct.unpack_from('>II',data,info+36)
    assert ee-es in (28,44) and data[es]==ee-es
    assert struct.unpack_from('>HHH',data,es+4)==(0x8000,2,0)
    assert data[es+28:ee]==bytes(ee-es-28)
    reloc=next(p for _,p in ph if p[0]==0x700000a4)
    patches={info+40:es+export_size}
    nt,st=struct.unpack_from('>II',data,es+20)
    for i in range(2):
        nid=struct.unpack_from('>I',data,nt+4*i)[0]
        new={0xbc9a0086:start,0xab779874:stop}.get(nid)
        if new is not None: patches[st+4*i]=new
    for site,value in patches.items():
        found=[]
        for at in range(reloc[2],reloc[2]+reloc[5],24):
            off,unk,iv,ia,kind,ptr=struct.unpack_from('>QHBBIQ',data,at)
            if off==site and ia==0:
                assert (unk,iv,kind)==(0,0,1); found.append(at)
        assert len(found)==1,(site,found)
        struct.pack_into('>I',data,site,value)
        struct.pack_into('>Q',data,found[0]+16,value)
    data[es]=export_size
    shoff=struct.unpack_from('>Q',data,40)[0]
    for i in range(struct.unpack_from('>H',data,60)[0]):
        at=shoff+64*i
        if struct.unpack_from('>QQ',data,at+16)==(es,es):
            assert struct.unpack_from('>Q',data,at+32)[0] in (28,44)
            struct.pack_into('>Q',data,at+32,export_size)
    changed=[i for i,(a,b) in enumerate(zip(original,data)) if a!=b]
    assert len(data)==len(original)
    path.write_bytes(data)
    return {'file':str(path),'export_size':export_size,'export_start':es,
            'export_end':es+export_size,'changed_byte_offsets':changed,
            'pointer_patches':patches}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('prx');p.add_argument('--export-size',type=int,choices=[28,44],default=28)
    args=p.parse_args();print(json.dumps(fix(args.prx,args.export_size),indent=2))
