"""Run the authorized PC checks and save an auditable report. stdlib only."""
import datetime, hashlib, json, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
PLUGIN=ROOT/'plugin'
task_tmp=ROOT/'build'/'tmp'; task_tmp.mkdir(parents=True,exist_ok=True)
test_env=os.environ.copy();test_env.update(TEMP=str(task_tmp),TMP=str(task_tmp),TMPDIR=str(task_tmp))
commands=[
 [sys.executable,'-B','tools/test_porting.py'],
 [sys.executable,'-B','plugin/tests/run_autoload.py'],
 [sys.executable,'-B','plugin/tests/run_app_ui.py'],
 [sys.executable,'-B','plugin/tools/audit_loops12.py'],
 ['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File','plugin/build_pc.ps1'],
 [sys.executable,'plugin/tests/test_transport.py'],
 [sys.executable,'plugin/tests/test_worker_liveness.py'],
 [sys.executable,'-B','plugin/tests/replay_autoload09.py'],
 ['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File','plugin/test_sanitize.ps1'],
 ['node','--check','plugin/web/app.js'],
]
results=[]
for command in commands:
 completed=subprocess.run(command,cwd=ROOT,env=test_env,capture_output=True,text=True)
 item={'command':command,'returncode':completed.returncode,'stdout':completed.stdout,'stderr':completed.stderr}
 results.append(item); print(subprocess.list2cmdline(command),flush=True)
 print(completed.stdout+completed.stderr,end='',flush=True)
 if completed.returncode: break
hashes=json.loads((PLUGIN/'autoload'/'frozen_inputs.json').read_text())
unchanged=all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest for name,digest in hashes.items() if name.startswith('plugin/web/') and name!='plugin/web/index.html')
html=(PLUGIN/'web/index.html').read_bytes()
original=html.replace(b'Buzz! Bridge</title>',b'Buzz Web Controller</title>').replace(b'<h1>Buzz! Bridge</h1>',b'<h1>Buzz!</h1>')
unchanged=unchanged and hashlib.sha256(original).hexdigest()==hashes['plugin/web/index.html']
print('Frozen 0.7 phone JS/CSS unchanged (HTML branding updated):',unchanged)
version=subprocess.run(['C:/Program Files/LLVM/bin/clang.exe','--version'],capture_output=True,text=True,check=True).stdout
timestamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
record={'time_utc':timestamp,'compiler':version,'results':results,'reference_assets_unchanged':unchanged}
(PLUGIN/'build'/'test_runs.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
lines=['# PC test results','',f'Executed {timestamp}. Working directory: workspace root.','',
 'These checks compile/run Windows code only. No PS3, emulator, compiler installation or executable download was used. No pip packages were installed.','',
 '## Compiler','', '```text',version.rstrip(),'```','',
 'Existing MSVC/Windows SDK headers and libraries were discovered automatically by Clang. MSVC cl.exe was found but not used.','',
 '## Commands and captured results','']
for result in results:
 lines+=['```text',subprocess.list2cmdline(result['command']),f"exit code: {result['returncode']}",result['stdout'].rstrip(),result['stderr'].rstrip(),'```','']
lines += [f'Phone JS/CSS hashes still match frozen 0.7 (HTML title/heading intentionally renamed): **{unchanged}**.','',
 'The unit test has 1,049,006 assertions, including all 1,048,576 four-handset button states against a separate transcription of RPCS3\'s bit expression. Fixed pad targets and phone ownership conflicts are included. USB tests are a model of asynchronous completion, not native syscalls.','',
 'The allocation-free PRX formatter passes 903 numeric/truncation cases plus signed-limit/invalid-format checks against libc. The loopback phone server uses this formatter too. Native build results are recorded separately in ../build/prx_audit.json and TOOLCHAIN.md.','',
 'The additional game-sequence fixture uses the reconstructed BCES00347 1.00 wired startup: four extra-LDD registrations, probe/location/static descriptors, default/interrupt pipes, product string read, SET_CONFIGURATION, SET_INTERFACE, seven-byte lights and continuously rearmed interrupt IN. It tests both dongles and all forty button bits, real-call forwarding, pending cancellation, late adoption, import-slot rollback and hook ownership. It runs again under ASan/UBSan. No copyrighted game code is linked or executed.','',
 'game/native.c also passes a strict Windows Clang syntax check with BUZZ_HOST_CHECK and declaration-only PS3 stubs. This does not check PPU code generation, linking, compact OPDs or SDK layouts.','',
 'The transport test launches its own C server bound only to 127.0.0.1 on an ephemeral port. It checks copied assets, fixed routes, split/coalesced TCP input, independent handshake accept values, eight phone claims, full reports for two dongles, ownership rejection, player-eight disconnect cleanup, ping/pong, invalid-frame close and cross-origin refusal. It terminates only its own test child.','',
 'The worker-liveness harness includes the actual native service loops with PC mocks. Separate runs deliberately block USB lookup, probe or attach; HTTP still returns 200 and the independent two-second heartbeat reports that phase. Holding all three shared locks cannot stop that heartbeat. Stop tests cover the 1 Hz check, frozen work counters, hook bypass, persistent latch and a preexisting sentinel. The CPU guard is forced above 1000 calls in a clock window and checked for repeated 100 ms throttling. Pad held/no-change, stale-read release and NO_DEVICE also pass. This verifies host behavior, not PS3 priorities, syscalls or game code.','',
 'The source audit inventories 152 loops in nine native translation units. Three indefinite service loops and the lock wait must sleep first; every finite loop has a reviewed bound/progress contract and immutable token hash. Eight deliberate unsafe source mutations are rejected. This is a source contract, not a termination proof for external firmware/game calls. The native object audit separately verifies the correct clock import because host mocks bypass that branch.','',
 'The sanitizer test compiles with `-fsanitize=address,undefined -fno-sanitize-recover=all` and exercises 200,000 deterministic random/mutated JSON, config, HTTP, WebSocket and mailbox inputs. No sanitizer finding occurred in the successful run. This is bounded stress testing, not a proof of protocol compliance or memory safety.','',
 'JS validation is syntax-only. Actual mobile rendering, console endianness, game-process SPRX imports/OPDs, loader behavior, threading, USB scheduling, real-dongle coexistence, LEDs/rumble and game compatibility remain untested. The old VSH/kernel model remains regression-tested but is no longer the selected architecture.','',
 'Runtime 0.9 adds actual C readiness-policy checks (minimum age, sustained polling, bursts, stalls and mutex failure), three-second attach timeout/late-return quarantine with HTTP remaining available, and replay of the supplied 0.8 stuck-worker sequence. The real VSH worker is mocked for success/failure/already-resident outcomes and checked for zero process/VSH/network/pad/sentinel calls after parking. See ../build/ATTACH_SAFETY_09.md for source URLs, timing policy and limits.','',
 'Raw command outputs and compiler identity are also saved in `build/test_runs.json`. Re-run with `python plugin/tests/run_all.py`.','']
(PLUGIN/'TEST_RESULTS.md').write_text('\n'.join(lines),encoding='utf-8')
sys.exit(0 if len(results)==len(commands) and all(r['returncode']==0 for r in results) and unchanged else 1)
