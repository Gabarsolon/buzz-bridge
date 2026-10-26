"""Compare homebrew runtime code to the preserved pre-repair 0.8 PKG, in RAM.

No retail game data is read or saved. Optional historical evidence, not a build input.
"""
import hashlib,json,pathlib,re,struct
from Crypto.Cipher import AES
from native_prx import Elf
from self10 import decode
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'build'
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    pkg=(OUT/'BuzzPhones.pkg').read_bytes()
    off,size=struct.unpack_from('>QQ',pkg,32)
    source=(ROOT/'research/load10/psl1ght-oddkeys.h').read_text()
    body=re.search(r'retail_pkg_aes_key\[\]\s*=\s*\{([^}]+)',source)[1]
    key=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',body))
    plain=AES.new(key,AES.MODE_CTR,nonce=b'',initial_value=int.from_bytes(pkg[0x70:0x80],'big')).decrypt(pkg[off:off+size])
    members={}
    for i in range(struct.unpack_from('>I',pkg,20)[0]):
        no,nl,do,dl,_,_=struct.unpack_from('>IIQQII',plain,i*32)
        members[plain[no:no+nl].decode()]=plain[do:do+dl]
    before=decode(members['USRDIR/ps3buzz.sprx'],ROOT/'research/load10/key_vault.cpp')['elf']
    after=(OUT/'ps3buzz.prx').read_bytes()
    linked=Elf((OUT/'native/ps3buzz_game').read_bytes())
    code=linked.sections['.text'];address,size=code[3],code[5]
    def extract(b):
        phoff=struct.unpack_from('>Q',b,32)[0]
        for i in range(struct.unpack_from('>H',b,56)[0]):
            p=struct.unpack_from('>IIQQQQQQ',b,phoff+i*56)
            if p[0]==1 and p[3]<=address and address+size<=p[3]+p[5]:
                start=p[2]+address-p[3];return b[start:start+size]
        raise AssertionError('Code interval not mapped')
    old,new=extract(before),extract(after)
    assert old==new,'Runtime machine code changed beyond branding data'
    assert members['USRDIR/ps3buzz.cfg']==(OUT/'ps3buzz.cfg').read_bytes()
    report=dict(result='PASS',reference_pkg='BuzzPhones.pkg (pre-repair 0.8)',reference_sha256=sha(pkg),
                text_address=hex(address),text_bytes=size,text_sha256=sha(new),runtime_text_byte_exact=True,
                config_byte_exact=True,note='Only embedded HTML title/heading data changed in the runtime; SELF signing bytes differ.')
    (OUT/'bridge-runtime-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
