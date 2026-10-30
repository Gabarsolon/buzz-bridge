"""Compile actual browser and LV2 enumeration against synthetic metadata."""
import json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/pc';out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.update(TEMP=str(ROOT/'build/tmp'),TMP=str(ROOT/'build/tmp'))
commands=[['C:/Program Files/LLVM/bin/clang.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-D_CRT_SECURE_NO_WARNINGS','-DBUZZ_APP_HOST_TEST','-DBB_DIR_HOST_TEST','-Itests','-Ips3/plugin/tests','-Ips3/plugin/autoload','tests/test_browser.c','ps3/plugin/autoload/bb_dir.c','-o',str(out/'browser_tests.exe')],[str(out/'browser_tests.exe')]]
results=[]
for cmd in commands:
    r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True)
    results.append(dict(command=cmd,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
    print(r.stdout+r.stderr,end='',flush=True)
    if r.returncode:break
passed=len(results)==len(commands) and all(r['returncode']==0 for r in results)
(ROOT/'build/browser-results.json').write_text(json.dumps(dict(passed=passed,results=results,console_tested=False),indent=2)+'\n')
raise SystemExit(0 if passed else 1)
