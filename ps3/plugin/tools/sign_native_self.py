"""Normalize installed make_self's APP profile and recompute its signature.

Uses make_self packing, then APP rev 0A root encryption and ECDSA signing.
The private/public curve proof is mandatory before invoking the SDK signer.
No keys are emitted in reports; the installed SDK remains read only.
"""
import argparse, hashlib, os, pathlib, re, struct, subprocess
from Crypto.Cipher import AES
from self10 import read,app_key,ctr
from self_ecdsa import prove, sign_digest, verify_self
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=pathlib.Path(os.environ.get('PS3DK',r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64'))
ENV=os.environ.copy()
ENV.update(TEMP=str(ROOT/'build/tmp'),TMP=str(ROOT/'build/tmp'))

def constants():
    text=(ROOT/'research/load10/psl1ght-keys.h').read_text()
    result=[]
    for name in ('appold_R','appold_n','appold_K','appold_Da'):
        body=re.search(r'\b'+name+r'\s*\[[^]]*\]\s*=\s*\{([^}]+)\}',text)[1]
        result.append(int.from_bytes(bytes(int(h,16) for h in re.findall(r'0x([a-fA-F0-9]{2})',body)),'big'))
    return result

def scalar(header,offset):
    r,n,k,d=constants()
    value=((int.from_bytes(hashlib.sha1(header[:offset]).digest(),'big')+r*d)*pow(k,-1,n))%n
    return r,value

def sign(source,output,fake=False,vsh=False):
    source=pathlib.Path(source);output=pathlib.Path(output)
    if not fake: prove(10)
    subprocess.run([str(SDK/'bin'/('fself.exe' if fake else 'make_self.exe')),str(source),str(output)],env=ENV,cwd=ROOT,check=True)
    keys=ROOT/'research/load10/key_vault.cpp';original=read(output,keys)
    data=bytearray(original['data']);info=original['header'];ext=info['extended_header']
    # No section table means BOTH ELF count/offset and SELF offset are zero.
    if struct.unpack_from('>H',source.read_bytes(),60)[0]==0: struct.pack_into('>Q',data,64,0)
    if fake:
        output.write_bytes(data)
        return
    assert info['key_revision']==1 and info['program_type']==4
    mo=32+info['metadata_offset'];hs=info['header_size']
    rootkey,rootiv=app_key(keys,1)
    mi=AES.new(rootkey,AES.MODE_CBC,rootiv).decrypt(data[mo:mo+64])
    header=bytearray(original['data'][:hs]);header[mo:mo+64]=mi;header[mo+64:]=original['meta_plain']
    offset=original['meta'][0]
    r,s=scalar(header,offset)
    assert int.from_bytes(header[offset:offset+21],'big')==r
    # geohot exports S without leading padding; accept its source-defined
    # serialization here, and always emit fixed-width 21-byte scalars below.
    stock_s=s.to_bytes(max(1,(s.bit_length()+7)//8),'big').ljust(20,b'\0')
    assert header[offset+22:offset+42]==stock_s,'installed signer/source scalar mismatch'
    if vsh:
        # Our own VSH helper, not a Sony system module. webMAN's APP authority.
        struct.pack_into('>Q',data,ext[1],0x1070000052000001)
    # scetool APP defaults: zero plaintext flags, a real ELF digest, and
    # encrypted capability flags 0x7b / unk6=1 / unk7=0x20000.
    _,_,_,_,_,_,_,co,cs,_=ext
    at=co
    while at<co+cs:
        kind,size,nxt=struct.unpack_from('>IIQ',data,at)
        if kind==1:
            data[at+16:at+48]=bytes.fromhex('4000000000000000000000000000000000000000000000000000000000000002') if vsh else bytes(32)
        if kind==2:data[at+36:at+56]=hashlib.sha1(source.read_bytes()).digest()
        at+=size
    header[:mo]=data[:mo]
    nsec,nkey,opt=original['meta'][2:5]
    optional=mo+64+32+48*nsec+16*nkey
    assert opt==48 and struct.unpack_from('>II',header,optional)==(1,48)
    header[optional+16:optional+48]=bytes(16)+struct.pack('>QII',0x7b,1,0x20000)
    struct.pack_into('>H',header,8,10)
    r,s=sign_digest(10,hashlib.sha1(header[:offset]).digest())
    header[offset:offset+21]=r.to_bytes(21,'big')
    header[offset+21:offset+42]=s.to_bytes(21,'big')
    data[:mo]=header[:mo]
    rootkey,rootiv=app_key(keys,10)
    data[mo:mo+64]=AES.new(rootkey,AES.MODE_CBC,rootiv).encrypt(mi)
    data[mo+64:hs]=ctr(mi[:16],mi[32:48],header[mo+64:hs])
    output.write_bytes(data)
    result=read(output,keys)
    assert all(result['hmac_checks']) and result['elf']==original['elf']
    assert result['header']['key_revision']==10
    verify_self(output)
    print('APP SELF signing rev 0A: private/public proof, all HMACs, ELF identity and public-key ECDSA PASS; '+('VSH authority/control flags' if vsh else 'game authority/zero control flags')+'.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');p.add_argument('--fake',action='store_true');p.add_argument('--vsh',action='store_true')
    a=p.parse_args();sign(a.source,a.output,a.fake,a.vsh)
