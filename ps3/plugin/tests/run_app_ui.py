"""Build the real application UI with deterministic host firmware mocks."""
import json,os,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'build/app-tests';OUT.mkdir(exist_ok=True)
env=os.environ.copy();env.update(TEMP=str(ROOT/'build/tmp'),TMP=str(ROOT/'build/tmp'))
compiler='C:/Program Files/LLVM/bin/clang.exe'
commands=[[compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
 '-D_CRT_SECURE_NO_WARNINGS','-DBUZZ_APP_HOST_TEST=1','-Iplugin/tests',
 'plugin/autoload/app_ui.c','plugin/autoload/app_health.c','plugin/autoload/ui_layout.c','plugin/tests/test_app_ui.c','-o',str(OUT/'test_app_ui.exe')]]
scenarios=['work_stable','idle_stable','menu_select','menu_select_noinfo','menu_idle_exit','idle_exit','menu_cancel','menu_timeout','yes','no','cancel','quit','system_menu','timeout','status_timeout','late_answer','long_ps_menu','long_dialog','flip_timeout','late_label_timeout',
 'thread_fail','log_open_fail','log_sync_fail','host_alloc_fail','callback_register_fail','callback_fail',
 'gcm_fail','rsx_alloc_fail','configure_fail','video_state_timeout','display_buffer_fail','pad_fail',
 'flip_submit_fail','label_timeout','watchdog','total_watchdog',
 'query_fallback','bad_resolution','resume_change','refresh_query_fail']
scenarios += ['video_'+mode+'_'+aspect for mode in ('480i','480p','576i','576p','720p','1080i','1080p') for aspect in ('4_3','16_9')]
commands += [[str(OUT/'test_app_ui.exe'),s] for s in scenarios]
results=[]
for cmd in commands:
 p=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=30)
 results.append(dict(command=cmd,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
 print(p.stdout+p.stderr,end='',flush=True)
 if p.returncode:break
(ROOT/'build/app-pc-tests.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(0 if len(results)==len(commands) and all(r['returncode']==0 for r in results) else 1)
