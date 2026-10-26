"""Compile real shared logic and filesystem editor; all fixtures inside build/."""
import pathlib,subprocess,os,datetime,json,shutil
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'build/autoload-tests';OUT.mkdir(exist_ok=True)
env=os.environ.copy();env.update(TEMP=str(ROOT/'build/tmp'),TMP=str(ROOT/'build/tmp'))
compiler=pathlib.Path('C:/Program Files/LLVM/bin/clang.exe')
commands=[[str(compiler),'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-D_CRT_SECURE_NO_WARNINGS',
 'plugin/autoload/logic.c','plugin/autoload/files.c','plugin/tests/test_autoload.c','-o',str(OUT/'test_autoload.exe')]]
# Per-run fresh fixture directory avoids deleting unrelated files.
fixture=OUT/datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f');fixture.mkdir()
commands.append([str(OUT/'test_autoload.exe'),str(fixture)])
commands.append([str(compiler),'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-D_CRT_SECURE_NO_WARNINGS',
 'plugin/tests/test_vsh_loop.c','plugin/autoload/logic.c','plugin/game/format.c','-o',str(OUT/'test_vsh_loop.exe')])
commands.append([str(OUT/'test_vsh_loop.exe')])
commands.append([str(OUT/'test_vsh_loop.exe'),'failure'])
commands.append([str(OUT/'test_vsh_loop.exe'),'resident'])
results=[]
for cmd in commands:
 r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True)
 results.append(dict(command=cmd,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
 print(r.stdout+r.stderr,end='')
 if r.returncode:break
(ROOT/'build/autoload-pc-tests.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(0 if len(results)==len(commands) and all(x['returncode']==0 for x in results) else 1)
