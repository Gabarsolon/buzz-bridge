"""Offline final-package and helper relocation audit; never executes PPU code."""
import hashlib,json,pathlib,re,struct,subprocess,os,zlib
from Crypto.Cipher import AES
from self10 import read
from verify_native_prx import verify
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'build';KEYS=ROOT/'research/load10/key_vault.cpp'
def digest(b):return hashlib.sha256(b).hexdigest()

def icon(data):
    assert data==(ROOT/'assets/ICON0.PNG').read_bytes(), 'Must use the supplied icon byte-for-byte'
    assert data[:8]==b'\x89PNG\r\n\x1a\n'
    at=8; compressed=[];types=[]
    while at<len(data):
        n,kind=struct.unpack_from('>I4s',data,at);end=at+12+n
        assert end<=len(data)
        payload=data[at+8:at+8+n]
        assert zlib.crc32(kind+payload)==struct.unpack_from('>I',data,at+8+n)[0]
        types.append(kind)
        if kind==b'IHDR':assert struct.unpack('>IIBBBBB',payload)==(320,176,8,6,0,0,0)
        if kind==b'IDAT':compressed.append(payload)
        at=end
    assert types[0]==b'IHDR' and types[-1]==b'IEND' and types.count(b'IHDR')==1
    raw=zlib.decompress(b''.join(compressed));assert len(raw)==176*(1+320*4)
    assert all(raw[y*(1+320*4)]<=4 for y in range(176))
    return dict(bytes=len(data),sha256=digest(data),width=320,height=176,bit_depth=8,color_type='RGBA',interlaced=False,source_exact=True,crc_and_scanlines='PASS')
def sfo(data):
    assert data[:4]==b'\0PSF'
    _,keys,values,count=struct.unpack_from('<4I',data,4)
    result={}
    for i in range(count):
        ko,fmt,size,cap,off=struct.unpack_from('<HHIII',data,20+16*i)
        name=data[keys+ko:].split(b'\0',1)[0].decode()
        b=data[values+off:values+off+size]
        result[name]=struct.unpack('<I',b)[0] if fmt==0x404 else b.rstrip(b'\0').decode()
    return result
def package():
    b=(OUT/'BuzzBridge.pkg').read_bytes();magic,kind,info,unk,hs,count,total,off,size=struct.unpack_from('>6I3Q',b)
    assert magic==0x7f504b47 and kind==0x80000001 and total==len(b) and off+size<=len(b)
    assert b[0x30:0x60].split(b'\0',1)[0]==b'UP0001-BUZZB0001_00-BUZZBRIDGE000001'
    assert hashlib.sha1(b[:-32]).digest()==b[-32:-12], 'Finalized PKG footer digest mismatch'
    source=(ROOT/'research/load10/psl1ght-oddkeys.h').read_text()
    body=re.search(r'retail_pkg_aes_key\[\]\s*=\s*\{([^}]+)',source)[1]
    key=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',body))
    plain=AES.new(key,AES.MODE_CTR,nonce=b'',initial_value=int.from_bytes(b[0x70:0x80],'big')).decrypt(b[off:off+size])
    entries={};report=[]
    for i in range(count):
        no,nl,do,dl,flags,pad=struct.unpack_from('>IIQQII',plain,i*32)
        assert no+nl<=size and do+dl<=size
        name=plain[no:no+nl].decode();assert name not in entries and not name.startswith('/') and '..' not in name.split('/')
        data=plain[do:do+dl];entries[name]=data
        report.append(dict(path=name,bytes=dl,flags=hex(flags),sha256=digest(data)))
    expected={'ICON0.PNG','PARAM.SFO','USRDIR','USRDIR/EBOOT.BIN','USRDIR/ps3buzz.sprx','USRDIR/ps3buzz.cfg','USRDIR/ps3buzz_vsh.sprx'}
    assert set(entries)==expected,(set(entries),expected)
    for name in expected-{'USRDIR','USRDIR/EBOOT.BIN'}:
        assert entries[name]==(OUT/'pkg'/name).read_bytes(),name
    params=sfo(entries['PARAM.SFO']);assert params['CATEGORY']=='HG' and params['TITLE_ID']=='BUZZB0001' and params['TITLE']=='Buzz! Bridge' and params['APP_VER']=='01.00'
    icon_report=icon(entries['ICON0.PNG'])
    e=entries['USRDIR/EBOOT.BIN'];original=(OUT/'pkg/USRDIR/EBOOT.BIN').read_bytes()
    assert len(e)==((len(original)-0x30+63)&~63)+0x30 and e[:4]==b'SCE\0'
    assert set(e[len(original):]) <= {0}, 'Nonzero EBOOT padding'
    ext=struct.unpack_from('>10Q',e,32);auth,vendor,typ,version=struct.unpack_from('>QIIQ',e,ext[1]);assert typ==8
    # Installed pkg tool intentionally replaces the 128-byte NPD control payload.
    at=ext[7];end=at+ext[8];npd=None
    while at<end:
        kind,length,nxt=struct.unpack_from('>IIQ',e,at);assert length>=16 and at+length<=end
        if kind==3:npd=(at+16,at+16+128)
        at+=length
    assert npd and e[npd[0]:npd[0]+4]==b'NPD\0'
    assert e[:len(original)]==original, 'Unexpected packaged EBOOT mutation'
    assert b'UP0001-BUZZB0001_00-BUZZBRIDGE000001' in e[npd[0]:npd[1]]
    (OUT/'EBOOT.BIN').write_bytes(e)
    return dict(result='PASS',finalized=True,bytes=len(b),sha256=digest(b),members=report,sfo=params,icon=icon_report,eboot_program_type=typ,eboot_auth=f'{auth:016x}',eboot_pkg_mutation='Original SELF byte-exact, plus zero padding to SDK NPDRM reported file size')
def main():
    helper=verify(OUT/'ps3buzz_vsh.prx','ps3buzz_vsh',helper=True)
    signed=read(OUT/'ps3buzz_vsh.sprx',KEYS)
    assert signed['elf']==(OUT/'ps3buzz_vsh.prx').read_bytes() and all(signed['hmac_checks'])
    assert signed['header']['auth']=='1070000052000001' and signed['header']['program_type']==4
    assert signed['control'][0]['payload']=='4000000000000000000000000000000000000000000000000000000000000002'
    sdk=pathlib.Path(os.environ.get('PS3DK',r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64'))
    linked=OUT/'autoload-native/ps3buzz_vsh'
    assert not subprocess.check_output([str(sdk/'ppu/bin/ppu-nm.exe'),'-u',str(linked)],text=True).strip()
    symbols=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm.exe'),str(linked)],text=True)
    assert not re.search(r'\b(?:malloc|fopen|__getreent|_malloc_r|__librt_sbrk_r)$',symbols,re.M)
    src=(ROOT/'plugin/autoload/vsh.c').read_text()
    assert re.search(r'while\(atomic_load\(&running\)\)\s*\{\s*sys_timer_usleep\(1000000\)',src)
    assert 'worker,NULL,1100,65536,THREAD_JOINABLE' in src
    assert src.count('buzz_mapi(0x44,')==1 and src.index('buzz_mark_attempt(s);log_line("ATTEMPT')<src.index('buzz_mapi(0x44,')
    app=(ROOT/'plugin/autoload/app.c').read_text()
    assert app.index('if(buzz_is_hen())')<app.index('if(buzz_install(')
    assert app.index('app_ui_init()')<app.index('if(buzz_install(')
    elf=(OUT/'app/BuzzBridge.elf').read_bytes()
    assert b'/dev_hdd0/game/BUZZB0001/USRDIR' in elf and b'/dev_hdd0/game/BUZZP0001' not in elf
    assert b'/dev_hdd0/tmp/buzzbridge_app.log' in elf
    assert b'post-load passive entered' in signed['elf']
    assert b'ps3buzz_vsh 0.3 startup; runtime=0.9' in signed['elf']
    assert 'netCtlGetInfo' not in src and 'notify(' not in src
    passive=src[src.index('if(post_load_log_only)'):src.index('if(!stopped && exists(BUZZ_STOP))')]
    assert 'continue;' in passive and all(x not in passive for x in ('buzz_mapi(', 'title_now(', 'exists(', 'skip_pad('))
    assert src.index('if(post_load_log_only)')<src.index('int identified=')
    assert not subprocess.check_output([str(sdk/'ppu/bin/ppu-nm.exe'),'-u',str(OUT/'autoload-native/BuzzBridge')],text=True).strip()
    game=read(OUT/'ps3buzz.sprx',KEYS)
    assert game['elf']==(OUT/'ps3buzz.prx').read_bytes() and all(game['hmac_checks'])
    assert b'ps3buzz 0.9 ENTRY' in game['elf']
    report=dict(result='PASS',helper=helper,helper_self=signed['header'],helper_hmac_checks=signed['hmac_checks'],helper_unresolved=0,helper_process_allocator=False,package=package(),console_tested=False)
    (OUT/'autoload_audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Autoload audit PASS: relocated helper at independent bases; APP authority/HMACs; no CRT allocator; finalized PKG decrypted and every member checked; runtime 0.9 ELF/HMACs.')
if __name__=='__main__':main()
