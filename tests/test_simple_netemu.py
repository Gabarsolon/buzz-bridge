"""Production C copy/rollback, bundled hashes and final package contents."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from netemu_assets import ASSETS,verify_assets
from pkg_inspect import members
out=ROOT/'build';(out/'pc').mkdir(exist_ok=True)
env=os.environ.copy();env.update(TEMP=str(out/'tmp'),TMP=str(out/'tmp'))
verify_assets(ROOT/'local_only/netemu_simple')
results=[]
def run(cmd,cwd=ROOT):
    p=subprocess.run(list(map(str,cmd)),cwd=cwd,env=env,capture_output=True,text=True)
    results.append(dict(command=list(map(str,cmd)),returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
    print(p.stdout+p.stderr,end='',flush=True)
    assert p.returncode==0,p.stderr
try:
    run(['C:/Program Files/LLVM/bin/clang.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-D_CRT_SECURE_NO_WARNINGS','-Ips3/plugin/autoload','ps3/plugin/autoload/bb.c','tests/test_installer.c','-o',out/'pc/installer_tests.exe'])
    for private in (True,False):
        exe=out/'pc'/('netemu-menu-full.exe' if private else 'netemu-menu-public.exe')
        cmd=['C:/Program Files/LLVM/bin/clang.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-D_CRT_SECURE_NO_WARNINGS','-DBUZZ_APP_HOST_TEST','-Ips3/plugin/tests','-Ips3/plugin/autoload']
        if private:cmd+=['-DBB_BUNDLED_NETEMU=1']
        run(cmd+['tests/test_netemu_menu.c','-o',exe]);run([exe])
    exe=out/'pc/app-flow.exe'
    run(['C:/Program Files/LLVM/bin/clang.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-D_CRT_SECURE_NO_WARNINGS','-DBUZZ_APP_HOST_TEST','-Itests','-Ips3/plugin/tests','-Ips3/plugin/autoload','tests/test_app_flow.c','-o',exe])
    run([exe])
    with tempfile.TemporaryDirectory(prefix='simple-netemu-',dir=out/'pc') as temp:
        run([out/'pc/installer_tests.exe',temp,ROOT/'local_only/netemu_simple/ps2_netemu_original.self',ROOT/'local_only/netemu_simple/ps2_netemu.self'])
    # Inspect actual finalized packages, not just staging manifests.
    packages=[]
    for private,pkg in ((True,'BuzzBridge.pkg'),(False,'BuzzBridge-public.pkg')):
        files=members(ROOT/'deliverables'/pkg)
        selfs={Path(n).name:b for n,b in files.items() if n.endswith('.self')}
        assert set(selfs)==(set(ASSETS) if private else set())
        for name,data in selfs.items():assert hashlib.sha256(data).hexdigest()==ASSETS[name]
        assert not any(n.endswith(('.bnp','.json','.own.bin')) for n in files)
        manifest=json.loads((ROOT/'deliverables'/('package_manifest.json' if private else 'package_manifest-public.json')).read_text())
        assert set(files)=={m['file'] for m in manifest['files']}
        for member in manifest['files']:
            assert hashlib.sha256(files[member['file']]).hexdigest()==member['sha256']
        assert hashlib.sha256((ROOT/'deliverables'/pkg).read_bytes()).hexdigest()==manifest['sha256']
        packages.append(dict(file=pkg,sha256=manifest['sha256'],bundled_hashes={n:hashlib.sha256(b).hexdigest() for n,b in selfs.items()},members_verified=len(files)))
    report=dict(passed=True,results=results,packages=packages,console_contact=False,real_flash_write=False,power_cut='partial target survives interruption; backup verified and restored on next run; live rollback cannot run while powered off')
except BaseException:
    (out/'simple-netemu-results.json').write_text(json.dumps(dict(passed=False,results=results),indent=2)+'\n')
    raise
(out/'simple-netemu-results.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS exact bundled hashes and finalized full/public package extraction')
