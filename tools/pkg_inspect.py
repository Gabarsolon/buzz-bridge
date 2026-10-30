"""Read-only finalized PS3 PKG acceptance, implemented from format fields.

References (format/algorithm facts, no upstream source copied):
https://github.com/ps3dev/PSL1GHT/blob/master/tools/geohot/package_finalize.c
https://github.com/ps3dev/ps3sdk/blob/master/tools/pkg/pkg.h
Host-only PyCryptodome AES; nothing here is linked into the PS3 app.
"""
import hashlib,struct
from pathlib import Path
from Crypto.Cipher import AES

def members(path):
    raw=Path(path).read_bytes()
    assert raw[:4]==b'\x7fPKG' and raw[4]==0x80
    count=struct.unpack_from('>I',raw,20)[0]
    length,offset,size=struct.unpack_from('>QQQ',raw,24)
    assert length==len(raw) and count*32<=size and offset+size<=len(raw)
    assert hashlib.sha1(raw[:-32]).digest()==raw[-32:-12]
    # Public retail PKG container key used by the SDK finalizer (not a SELF key).
    cipher=AES.new(bytes.fromhex('2e7b71d7c9c9a14ea3221f188828b8f8'),AES.MODE_CTR,
                   nonce=b'',initial_value=int.from_bytes(raw[112:128],'big'))
    data=cipher.decrypt(raw[offset:offset+size]);files={}
    for i in range(count):
        name_at,name_len,at,n,kind,_=struct.unpack_from('>IIQQII',data,32*i)
        assert name_at+name_len<=size and at+n<=size
        name=data[name_at:name_at+name_len].decode('utf-8')
        assert name not in files and not name.startswith('/') and '..' not in name.split('/')
        kind &= 0xfffffff
        assert kind in (1,3,4)
        if kind!=4:files[name]=data[at:at+n]
    return files
