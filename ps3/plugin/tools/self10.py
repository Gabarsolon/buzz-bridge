"""Offline APP SELF reader using the already-present RPCS3 key source.

No network, execution, key output, or key copies. Crypto is already installed.
"""
import hashlib, hmac, pathlib, re, struct, zlib
from Crypto.Cipher import AES

def ctr(key, iv, data):
    return AES.new(key, AES.MODE_CTR, nonce=b'', initial_value=int.from_bytes(iv,'big')).decrypt(data)

def app_key(key_file, revision):
    source = pathlib.Path(key_file).read_text(encoding='utf-8')
    body = source.split('void KeyVault::LoadSelfAPPKeys()',1)[1].split('\n}',1)[0]
    pattern = r'emplace_back\((0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*KEY_APP,\s*"([\dA-F]+)",\s*"([\dA-F]+)"'
    keys = [(int(m[3],16), bytes.fromhex(m[4]), bytes.fromhex(m[5])) for m in re.finditer(pattern,body)]
    return next((key,iv) for rev,key,iv in keys if rev==revision)

def read(path, key_file):
    data = pathlib.Path(path).read_bytes()
    return decode(data, key_file)

def decode(data, key_file):
    """Decode an APP SELF in memory; callers choose whether to save the ELF."""
    magic,version,rev,typ,mo,hs,es = struct.unpack_from('>4sIHHIQQ',data)
    assert magic==b'SCE\0' and typ==1 and hs<=len(data)
    ext = struct.unpack_from('>10Q',data,32)
    _,ai,eo,po,so,si,sv,co,cs,_ = ext
    auth,vendor,program,sce = struct.unpack_from('>QIIQ',data,ai)
    assert program==4
    elf_header = data[eo:eo+64]
    phnum = struct.unpack_from('>H',elf_header,56)[0]
    ph = [struct.unpack_from('>IIQQQQQQ',data,po+56*i) for i in range(phnum)]
    se = [struct.unpack_from('>QQIIQ',data,si+32*i) for i in range(phnum)]
    control=[]; at=co
    while at<co+cs:
        kind,size,nxt=struct.unpack_from('>IIQ',data,at)
        assert size>=16 and at+size<=co+cs
        control.append(dict(type=kind,size=size,next=nxt,payload=data[at+16:at+size].hex()))
        at+=size
    assert at==co+cs
    meta=None; sections=[]; checks=[]; mh=b''; caps=''
    if rev & 0x8000:
        sections=[(s[0],s[1],2,i,0,0,1,0,0,s[2]) for i,s in enumerate(se)]
        keys=[]
    else:
        key,iv=app_key(key_file,rev)
        mi=AES.new(key,AES.MODE_CBC,iv).decrypt(data[32+mo:32+mo+64])
        assert mi[16:32]==mi[48:64]==bytes(16),'bad metadata padding'
        mh=ctr(mi[:16],mi[32:48],data[32+mo+64:hs])
        meta=struct.unpack_from('>QIIIIII',mh)
        siglen,unknown,nsec,nkey,opt,_,_=meta
        sections=[struct.unpack_from('>QQIIIIIIII',mh,32+48*i) for i in range(nsec)]
        kb=32+48*nsec
        keys=[mh[kb+16*i:kb+16*(i+1)] for i in range(nkey)]
        caps=mh[kb+16*nkey:kb+16*nkey+opt].hex()
    shoff=struct.unpack_from('>Q',elf_header,40)[0]
    shent,shnum=struct.unpack_from('>HH',elf_header,58)
    length=max([64+56*phnum,shoff+shent*shnum if so else 0]+[p[2]+p[5] for p in ph])
    elf=bytearray(length); elf[:64]=elf_header
    phoff=struct.unpack_from('>Q',elf_header,32)[0]
    elf[phoff:phoff+56*phnum]=data[po:po+56*phnum]
    if so and shnum: elf[shoff:shoff+shnum*shent]=data[so:so+shnum*shent]
    for off,size,stype,index,hashed,sha,enc,ki,ii,comp in sections:
        # Retail metadata can also describe section headers (type 1).
        # Their table was copied from the SELF header above; only type 2 maps PT_LOADs.
        if stype!=2: continue
        assert index<len(ph) and off+size<=len(data)
        blob=data[off:off+size]
        if enc==3:
            assert ki<len(keys) and ii<len(keys)
            blob=ctr(keys[ki],keys[ii],blob)
        if hashed==2:
            expected=b''.join(keys[sha:sha+2])[:20]
            hkey=b''.join(keys[sha+2:sha+6])
            checks.append(hmac.compare_digest(hmac.new(hkey,blob,hashlib.sha1).digest(),expected))
        if comp==2: blob=zlib.decompress(blob)
        assert len(blob)==ph[index][5],(index,len(blob),ph[index][5])
        elf[ph[index][2]:ph[index][2]+len(blob)]=blob
    return dict(data=data,elf=bytes(elf),ph=ph,meta=meta,meta_plain=mh,sections=sections,
                segment_ext=se,control=control,encrypted_capabilities=caps,
                hmac_checks=checks,header=dict(magic=magic.hex(),version=version,key_revision=rev,
                  header_type=typ,metadata_offset=mo,header_size=hs,elf_size=es,
                  extended_header=ext,auth=f'{auth:016x}',vendor=f'{vendor:08x}',
                  program_type=program,sce_version=f'{sce:016x}'))

if __name__=='__main__':
    import json,sys
    for path in sys.argv[1:]:
        r=read(path,pathlib.Path(__file__).resolve().parents[2]/'research/load10/key_vault.cpp')
        print(path,json.dumps({k:r[k] for k in ('header','segment_ext','control','meta','sections','encrypted_capabilities','hmac_checks')},indent=2))
