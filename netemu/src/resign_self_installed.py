"""Repack template SELF for the CFW experiment; ECDSA is deliberately invalid.

Preserves key/IV material, auth/vendor/version, section encryption/compression
policies. Rebuilds section payloads, both hash tables, sizes and ELF copies.
No Sony private key is available. This is NOT cryptographic Sony signing.
"""
import hashlib
import hmac
import json
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import zlib
from Crypto.Cipher import AES

HERE=Path(__file__).resolve().parent;ROOT=HERE
dec=runpy.run_path(str(ROOT/'selfdec.py'))
TEMPLATE=ROOT/'original.self'

def main(elf_path,out_path):
    elf_path=Path(elf_path).resolve();out_path=Path(out_path).resolve()
    assert elf_path!=out_path and out_path.is_relative_to(ROOT.parent)
    e=elf_path.read_bytes();template=TEMPLATE.read_bytes()
    assert hashlib.sha256(template).hexdigest()=='bad3e45c9a81f25726e414b146ea5a112f28216ab4bdd2fd9f5c81b33d04c80a'
    d=bytearray(template)
    _,_,rev,htype,meta_off,hlen,_=struct.unpack_from('>4sIHHIQQ',d)
    _,app_off,elf_off,ph_off,sh_off,si_off,sv_off,ci_off,ci_size=struct.unpack_from('>9Q',d,0x20)
    auth,vendor,stype,version=struct.unpack_from('>QIIQ',d,app_off)
    key=next(k for k in dec['lv2_keys'](str(ROOT/'dl/key_vault.cpp')) if k[0]<=version<=k[1] and k[2]==rev)
    mi_off=0x20+meta_off;mh_off=mi_off+0x40
    mi=AES.new(key[3],AES.MODE_CBC,key[4]).decrypt(d[mi_off:mh_off])
    assert mi[16:32]==mi[48:64]==b'\0'*16
    mh=bytearray(dec['ctr'](mi[:16],mi[32:48],d[mh_off:hlen]))
    sig_off,_,nsec,nkeys,_,_,_=struct.unpack_from('>QIIIIII',mh)
    kbase=0x20+nsec*0x30
    original_key_table=bytes(mh[kbase:kbase+nkeys*16])
    keys=lambda i:bytes(mh[kbase+i*16:kbase+(i+1)*16])
    eph,esh=struct.unpack_from('>QQ',e,32)
    ephn=struct.unpack_from('>H',e,56)[0];eshn=struct.unpack_from('>H',e,60)[0]
    eshsz=struct.unpack_from('>H',e,58)[0]
    ph=[struct.unpack_from('>IIQQQQQQ',e,eph+i*56) for i in range(ephn)]
    sections=[list(struct.unpack_from('>QQIIIIIIII',mh,0x20+i*0x30)) for i in range(nsec)]
    payload=bytearray();reports=[]
    def align():
        payload.extend(b'\0'*((-len(payload))%16))
    for i,s in enumerate(sections):
        old_off,old_size,typ,pidx,hashed,sha,enc,ki,ivi,comp=s
        if typ==2:
            p=ph[pidx];raw=e[p[2]:p[2]+p[5]]
            assert len(raw)==p[5]
        elif typ==1:
            raw=e[esh:esh+eshn*eshsz]
        else:raise ValueError(('unknown metadata type',typ))
        plain=zlib.compress(raw,9) if comp==2 else raw
        assert comp in (1,2) and enc in (1,3) and hashed==2
        # Hash key starts two 16-byte slots after the SHA1 digest.
        hk=bytes(mh[kbase+(sha+2)*16:kbase+(sha+6)*16])
        digest=hmac.new(hk,plain,hashlib.sha1).digest()
        mh[kbase+sha*16:kbase+sha*16+20]=digest
        align();new_off=hlen+len(payload)
        encrypted=dec['ctr'](keys(ki),keys(ivi),plain) if enc==3 else plain
        payload.extend(encrypted)
        s[0]=new_off;s[1]=len(encrypted)
        struct.pack_into('>QQIIIIIIII',mh,0x20+i*0x30,*s)
        if typ==2:
            # section_info = offset,size,compression,unk0,unk1,encryption
            struct.pack_into('>QQ',d,si_off+pidx*0x20,new_off,len(encrypted))
        else:
            sh_off=new_off;struct.pack_into('>Q',d,0x40,new_off)
        # Independently decrypt/check each freshly encrypted payload.
        recovered=dec['ctr'](keys(ki),keys(ivi),encrypted) if enc==3 else encrypted
        actual=hmac.new(hk,recovered,hashlib.sha1).digest()
        assert actual==digest and recovered==plain
        assert (zlib.decompress(recovered) if comp==2 else recovered)==raw
        reports.append(dict(section=i,type=typ,index=pidx,offset=hex(new_off),compressed_size=len(encrypted),uncompressed_size=len(raw),compression=comp,encryption=enc,hmac_sha1_verified=True,payload_exact=True))
    align()
    d=d[:hlen]+payload
    struct.pack_into('>Q',d,24,len(payload))
    d[elf_off:elf_off+64]=e[:64]
    d[ph_off:ph_off+ephn*56]=e[eph:eph+ephn*56]
    assert d[sh_off:sh_off+eshn*eshsz]==e[esh:esh+eshn*eshsz]
    # Update the whole-ELF SHA1 (digest2) in type-2 control info.
    pos=ci_off;digest_updated=False
    while pos<ci_off+ci_size:
        typ,size,nxt=struct.unpack_from('>IIQ',d,pos)
        assert size>=16
        if typ==2 and size==0x40:
            d[pos+16+20:pos+16+40]=hashlib.sha1(e).digest();digest_updated=True
        pos+=size
    assert digest_updated and pos==ci_off+ci_size
    # Preserve all real encryption/HMAC keys and IVs; only digest slots change.
    allowed=set()
    for s in sections:allowed.update(range(s[5]*16,s[5]*16+20))
    for j,(before,after) in enumerate(zip(original_key_table,mh[kbase:kbase+nkeys*16])):
        assert before==after or j in allowed
    # Explicitly invalidate r and s; do not carry the template signature forward.
    mh[sig_off-mh_off:sig_off-mh_off+42]=b'\0'*42
    d[mh_off:hlen]=dec['ctr'](mi[:16],mi[32:48],mh)
    assert d[mi_off:mh_off]==template[mi_off:mh_off]
    out_path.write_bytes(d)
    roundtrip=out_path.with_suffix('.roundtrip.elf')
    run=subprocess.run([sys.executable,'-I',str(ROOT/'selfdec.py'),str(ROOT/'dl/key_vault.cpp'),str(out_path),str(roundtrip)],capture_output=True,text=True,check=True)
    assert roundtrip.read_bytes()==e,'SELF round trip does not equal patched ELF'
    # Parse output metadata again, rather than trusting build-time buffers.
    output=out_path.read_bytes()
    decrypted=dec['ctr'](mi[:16],mi[32:48],output[mh_off:hlen])
    for i in range(nsec):
        off,size,typ,pidx,hashed,sha,enc,ki,ivi,comp=struct.unpack_from('>QQIIIIIIII',decrypted,0x20+i*0x30)
        kk=decrypted[kbase:kbase+nkeys*16];plain=output[off:off+size]
        if enc==3:plain=dec['ctr'](kk[ki*16:(ki+1)*16],kk[ivi*16:(ivi+1)*16],plain)
        assert hmac.new(kk[(sha+2)*16:(sha+6)*16],plain,hashlib.sha1).digest()==kk[sha*16:sha*16+20]
        if typ==2:
            si=struct.unpack_from('>QQ',output,si_off+pidx*32);assert si==(off,size)
    report=dict(template=str(TEMPLATE),output=str(out_path),elf_sha256=hashlib.sha256(e).hexdigest(),self_sha256=hashlib.sha256(d).hexdigest(),size=len(d),header_size=hlen,data_size=len(payload),
        template_metadata_keys_ivs_preserved=True,elf_header_and_phdr_copies_updated=True,auxiliary_shdr_table_updated=True,whole_elf_control_sha1_updated=True,
        ecdsa_signature='INVALID: r=s=0, intentional CFW-only experiment; no Sony signing key',auth=hex(auth),vendor=hex(vendor),version=hex(version),sections=reports,selfdec_roundtrip_exact=True)
    out_path.with_suffix('.verification.json').write_text(json.dumps(report,indent=2)+'\n')
    out_path.with_suffix('.selfdec.txt').write_text(run.stdout)
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    args=sys.argv[1:]
    main(args[0] if args else HERE/'ps2_netemu_buzz.elf',args[1] if len(args)>1 else HERE/'ps2_netemu_buzz.self')
