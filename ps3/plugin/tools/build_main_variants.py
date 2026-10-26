"""Produce main-module counterparts for probes F/G/H with installed tools."""
import pathlib, struct
from build_diag import ROOT,SDK,run
from prx_compat import fix
from sign_native_self import sign

def build():
    out=ROOT/'build'
    flags0=bytearray((out/'ps3buzz.prx').read_bytes())
    struct.pack_into('>I',flags0,48,0)
    (out/'ps3buzz.flags0.prx').write_bytes(flags0)
    sign(out/'ps3buzz.flags0.prx',out/'ps3buzz.flags0.sprx')
    objects=sorted((out/'native/CMakeFiles/ps3buzz_game.dir').rglob('*.obj'))
    assert len(objects)==8,objects
    link=out/'native/ps3buzz_legacy_game';prx=out/'ps3buzz.legacy-layout.prx'
    run(SDK/'ppu/bin/ppu-gcc.exe','-mcpu=cell','-mhard-float','-ffunction-sections','-fdata-sections',
        '-nostartfiles',f'-specs={SDK.as_posix()}/ppu/lib/lv2-prx.specs',
        f'-Wl,-T,{SDK.as_posix()}/ppu/lib/lv2-prx.ld','-Wl,-q','-Wl,--gc-sections',
        '-Wl,--no-warn-rwx-segments',*objects,'-lrt','-lio','-lnet_stub',SDK/'ppu/lib/lv2-prx-crt.o','-o',link)
    run(SDK/'bin/prx-gen.exe','build',link,'-o',prx,'--name','ps3buzz','--version','0.4','--attributes','0')
    fix(prx)
    legacy=bytearray(prx.read_bytes());struct.pack_into('>I',legacy,48,0x01000000);prx.write_bytes(legacy)
    sign(prx,out/'ps3buzz.legacy-layout.sprx')
if __name__=='__main__': build()
