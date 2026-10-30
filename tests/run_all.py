"""One-command offline tests. Never connects to a console or stops a process.

The existing socket/liveness runners call terminate() on their test children,
so they are deliberately excluded under this task's no-process-stop rule.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build';OUT.mkdir(exist_ok=True)
for p in (ROOT/'ps3/build/tmp',ROOT/'ps3/plugin/build',ROOT/'ps2/universal/build',OUT/'tmp',ROOT/'local_only/test_runs'):p.mkdir(parents=True,exist_ok=True)
# Own reference payloads for the existing signing regression, including a clean extraction.
for name in ('ps3buzz','ps3buzz_vsh'):
    for src,dst in ((ROOT/'ps3/payloads'/(name+'.sprx'),ROOT/'ps3/build'/(name+'.sprx')),
                    (ROOT/'tests/reference/phone'/(name+'.prx'),ROOT/'ps3/build'/(name+'.prx')),
                    (ROOT/'tests/reference/phone'/('rev1-'+name+'.sprx'),ROOT/'ps3/build/signing-rev1'/(name+'.sprx'))):
        dst.parent.mkdir(parents=True,exist_ok=True)
        if not dst.exists():shutil.copyfile(src,dst)
env=os.environ.copy();env.update(PYTHONDONTWRITEBYTECODE='1',TEMP=str(OUT/'tmp'),TMP=str(OUT/'tmp'))
clang=r'C:/Program Files/LLVM/bin/clang.exe'
commands=[
 ([sys.executable,'-B','tests/test_browser.py'],ROOT),
 ([sys.executable,'-B','tests/test_ui_layout.py'],ROOT),
 ([sys.executable,'-B','tests/test_features.py'],ROOT),
 ([sys.executable,'-B','tests/test_simple_netemu.py'],ROOT),
]
commands += [
 ([sys.executable,'-B','tests/test_patcher.py'],ROOT/'ps2/universal'),
 ([sys.executable,'-B','-m','unittest','discover','-s','ps2/tests','-p','test_console.py','-v'],ROOT),
 ([sys.executable,'-B','plugin/tests/run_autoload.py'],ROOT/'ps3'),
 ([sys.executable,'-B','plugin/tests/run_app_ui.py'],ROOT/'ps3'),
 ([sys.executable,'-B','plugin/tests/test_self_signing.py'],ROOT/'ps3'),
 ([sys.executable,'-B','tools/test_porting.py'],ROOT/'ps3'),
 ([sys.executable,'-B','plugin/tools/audit_loops12.py'],ROOT/'ps3'),
 (['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File','plugin/build_pc.ps1'],ROOT/'ps3'),
 (['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File','plugin/test_sanitize.ps1'],ROOT/'ps3'),
 (['node','--check','plugin/web/app.js'],ROOT/'ps3'),
]
results=[]
for command,cwd in commands:
    print('RUN',subprocess.list2cmdline(command),flush=True)
    p=subprocess.run(command,cwd=cwd,env=env,capture_output=True,text=True)
    results.append(dict(command=command,cwd=str(cwd.relative_to(ROOT)),returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
    print(p.stdout+p.stderr,end='',flush=True)
    if p.returncode:break
report=dict(time_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=len(results)==len(commands) and all(x['returncode']==0 for x in results),results=results,
    excluded='Existing test_transport.py and test_worker_liveness.py stop their child process. Not executed under user rule.',console_contact=False)
(OUT/'test-results.json').write_text(json.dumps(report,indent=2)+'\n')
raise SystemExit(0 if report['passed'] else 1)
