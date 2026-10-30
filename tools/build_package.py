"""Build only the new app; retain the owner's existing signed phone payloads.

Builds full and public packages with one compile-time switch.
The owner plans a release of the full package with the standalone modified file.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from pkg_inspect import members
ROOT=Path(__file__).resolve().parents[1]
CONTENT='UP0001-BUZZB0001_00-BUZZBRIDGE000001'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main(sdk):
    sdk=Path(sdk).resolve();build=ROOT/'build';build.mkdir(exist_ok=True)
    tmp=build/'tmp';tmp.mkdir(exist_ok=True)
    env=os.environ.copy();env.update(PS3DK=str(sdk),PS3DEV=sdk.as_posix(),PSL1GHT=sdk.as_posix(),TEMP=str(tmp),TMP=str(tmp),PYTHONDONTWRITEBYTECODE='1')
    env['PATH']=str(sdk/'bin')+os.pathsep+str(sdk/'ppu/bin')+os.pathsep+env['PATH']
    def run(*args):subprocess.run(list(map(str,args)),cwd=ROOT,env=env,check=True)
    stage_root=build/'staging';stage_root.mkdir(exist_ok=True)
    records=[]
    for bundled in (True,False):
        variant='full' if bundled else 'public'
        run('cmake','-S',ROOT/'ps3/plugin/autoload','-B',build/'ppu','-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(sdk/'cmake/ps3-ppu-toolchain.cmake'),'-DBB_BUNDLED_NETEMU='+('ON' if bundled else 'OFF'))
        run('cmake','--build',build/'ppu','--target','BuzzBridge')
        stage=Path(tempfile.mkdtemp(prefix=variant+'-',dir=stage_root));usr=stage/'USRDIR';usr.mkdir()
        retained=json.loads((ROOT/'ps3/payloads/SHA256.json').read_text())
        for name,expected in retained.items():
            src=ROOT/'ps3/payloads'/name
            assert sha(src)==expected,'retained phone payload changed: '+name
            shutil.copyfile(src,usr/name)
        module=ROOT/'ps2/universal/bbusbd.irx';shutil.copyfile(module,usr/module.name)
        from netemu_assets import ASSETS, verify_assets
        if bundled:
            netemu=usr/'netemu';netemu.mkdir()
            verify_assets(ROOT/'local_only/netemu_simple')
            for name in ASSETS:shutil.copyfile(ROOT/'local_only/netemu_simple'/name,netemu/name)
            verify_assets(netemu)
        else:
            assert not list(stage.rglob('*.self'))
        assert not list(stage.rglob('*.bnp')) and not list(stage.rglob('*.json'))
        run(sdk/'bin/make_self_npdrm.exe',ROOT/'ps3/build/app/BuzzBridge.elf',usr/'EBOOT.BIN',CONTENT)
        run(sdk/'bin/sfo.exe','--title','Buzz! Bridge','--appid','BUZZB0001','-f',sdk/'bin/sfo.xml',stage/'PARAM.SFO')
        run(sdk/'bin/sfo.exe','validate',stage/'PARAM.SFO')
        shutil.copyfile(ROOT/'ps3/assets/ICON0.PNG',stage/'ICON0.PNG')
        out=ROOT/'deliverables';out.mkdir(exist_ok=True)
        pkg=out/('BuzzBridge.pkg' if bundled else 'BuzzBridge-public.pkg')
        run(sdk/'bin/pkg.exe','--contentid',CONTENT,str(stage)+'/',pkg)
        run(sdk/'bin/package_finalize.exe',pkg)
        packaged=members(pkg)
        assert set(packaged)=={p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()}
        for name,data in packaged.items():
            # The SDK packager rewrites/pads NPDRM EBOOT; other files are exact.
            if name!='USRDIR/EBOOT.BIN':assert data==(stage/name).read_bytes(),name
        files=[dict(file=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()) for name,data in sorted(packaged.items())]
        record=dict(package=str(pkg.relative_to(ROOT)),sha256=sha(pkg),bytes=pkg.stat().st_size,files=files,console_tested=False,sony_data_in_package=bundled,private_use_only=False,compile_switch='BB_BUNDLED_NETEMU='+('ON' if bundled else 'OFF'))
        (out/('package_manifest.json' if bundled else 'package_manifest-public.json')).write_text(json.dumps(record,indent=2)+'\n')
        records.append(record)
        (out/('EBOOT.BIN' if bundled else 'EBOOT-public.BIN')).write_bytes(packaged['USRDIR/EBOOT.BIN'])
        shutil.copyfile(ROOT/'ps3/build/app/BuzzBridge.elf',out/('BuzzBridge.elf' if bundled else 'BuzzBridge-public.elf'))
    (build/'simple-build.json').write_text(json.dumps(records,indent=2)+'\n')
    print(json.dumps(records,indent=2))
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--sdk',default=r'E:/tools/ps3dk/ps3-sdk-v0.26.0-windows-x86_64');a=ap.parse_args();main(a.sdk)
