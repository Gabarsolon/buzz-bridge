"""Self-contained build: installed PS3DK + frozen digest profile, no game ELF."""
import hashlib,json,os,pathlib,shutil,subprocess,sys
from self10 import read
from prepare_native_link import prepare
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=pathlib.Path(os.environ['PS3DK'])
OUT=ROOT/'build'
KEYS=ROOT/'research/load10/key_vault.cpp'
BASELINE='0702e1782f896f48c0972b05b182b761000527cfdc16b67c5d8526061c0663f4'
CONTENT='UP0001-BUZZB0001_00-BUZZBRIDGE000001'
def run(*args):subprocess.run([str(a) for a in args],cwd=ROOT,check=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def build():
    assert len(CONTENT)==36
    baseline=OUT/'confirmed07/ps3buzz.sprx';baseline.parent.mkdir(exist_ok=True)
    if not baseline.exists():
        assert sha(OUT/'ps3buzz.sprx')==BASELINE,'Unexpected 0.7 baseline'
        shutil.copyfile(OUT/'ps3buzz.sprx',baseline)
    assert sha(baseline)==BASELINE
    expected=json.loads((ROOT/'plugin/autoload/frozen_inputs.json').read_text())
    changed={'plugin/game/cellusbd_adapter.c','plugin/game/cellusbd_adapter.h','plugin/game/profile.h','plugin/game/native.c','plugin/game/import_hook.c','plugin/core/buzz.c','plugin/ps3buzz.cfg','plugin/web/index.html'}
    for name,digest in expected.items():
        if name not in changed:assert sha(ROOT/name)==digest,'Unexpected change to retained 0.7 input: '+name
    html=(ROOT/'plugin/web/index.html').read_bytes()
    original=html.replace(b'Buzz! Bridge</title>',b'Buzz Web Controller</title>').replace(b'<h1>Buzz! Bridge</h1>',b'<h1>Buzz!</h1>')
    assert hashlib.sha256(original).hexdigest()==expected['plugin/web/index.html'], 'Phone HTML changes must be branding only'
    run(sys.executable,'-B','tools/make_profiles.py')
    prepare()
    run(sys.executable,'-B','plugin/tools/audit_loops12.py')
    run(sys.executable,'-B','plugin/tools/embed_web.py')
    for source,folder in [('plugin/game','build/native'),('plugin/autoload','build/autoload-native')]:
        run('cmake','-S',source,'-B',folder,'-G','Ninja',f'-DCMAKE_TOOLCHAIN_FILE={SDK.as_posix()}/cmake/ps3-ppu-toolchain.cmake')
        run('cmake','--build',folder)
        if source=='plugin/game':
            rebuilt=read(OUT/'ps3buzz.sprx',KEYS)
            assert rebuilt['elf']==(OUT/'ps3buzz.prx').read_bytes() and all(rebuilt['hmac_checks'])
            assert b'ps3buzz 0.9 ENTRY' in rebuilt['elf']
            run(sys.executable,'-B','plugin/tools/verify_native_prx.py')
    shutil.copyfile(ROOT/'plugin/ps3buzz.cfg',OUT/'ps3buzz.cfg')
    stage=OUT/'pkg';usr=stage/'USRDIR';usr.mkdir(parents=True,exist_ok=True)
    allowed={'EBOOT.BIN','ps3buzz.sprx','ps3buzz.cfg','ps3buzz_vsh.sprx'}
    assert all(p.name in allowed and p.is_file() for p in usr.iterdir())
    for name in ('ps3buzz.sprx','ps3buzz.cfg','ps3buzz_vsh.sprx'):shutil.copyfile(OUT/name,usr/name)
    app_elf=OUT/'app/BuzzBridge.elf'
    assert app_elf.is_file(), 'Missing linked/stripped EBOOT input'
    run(SDK/'bin/make_self_npdrm.exe',app_elf,usr/'EBOOT.BIN',CONTENT)
    run(SDK/'bin/sfo.exe','--title','Buzz! Bridge','--appid','BUZZB0001','-f',SDK/'bin/sfo.xml',stage/'PARAM.SFO')
    shutil.copyfile(ROOT/'assets/ICON0.PNG',stage/'ICON0.PNG')
    assert {p.name for p in stage.iterdir()}=={'USRDIR','PARAM.SFO','ICON0.PNG'}
    run(SDK/'bin/sfo.exe','validate',stage/'PARAM.SFO')
    run(SDK/'bin/pkg.exe','--contentid',CONTENT,str(stage)+'/',OUT/'BuzzBridge.pkg')
    run(SDK/'bin/package_finalize.exe',OUT/'BuzzBridge.pkg')
    run(sys.executable,'-B','plugin/tools/audit_autoload.py')
    files=['build/BuzzBridge.pkg','build/ps3buzz.sprx','build/ps3buzz.cfg','build/ps3buzz_vsh.sprx','build/EBOOT.BIN','build/pkg/PARAM.SFO','build/pkg/ICON0.PNG']
    manifest=[dict(file=f,bytes=(ROOT/f).stat().st_size,sha256=sha(ROOT/f)) for f in files]
    (OUT/'autoload_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (OUT/'release09_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':build()
