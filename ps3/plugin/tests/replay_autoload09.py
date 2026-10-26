"""Read supplied log copies only; replay derived counters through real C policy."""
import hashlib,json,pathlib,re,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
import os,sys
LOGS=pathlib.Path(os.environ.get('BUZZ_CONSOLE_LOGS','console-logs'))
if not LOGS.is_dir():
    print('SKIP: console log copies not present (set BUZZ_CONSOLE_LOGS)'); sys.exit(0)
files={n:(LOGS/n).read_bytes() for n in ('ps3buzz_vsh.log','ps3buzz.log','ps3buzz.heartbeat.log')}
helper,game,heart=(files[n].decode('utf-8-sig') for n in files)
assert 'ps3buzz_vsh 0.2 startup; runtime=0.8' in helper
assert 'settle=15s' in helper and helper.count('ATTEMPT pid=01050200')==1
assert 'LOAD result=00000000 pid=01050200' in helper
assert 'probe-return object=425a0000 result=00000000' in game
assert 'attach-call object=425a0000' in game and 'attach-return' not in game
assert 'open-pipe device=' not in game and 'lookup-call object=425a0001' not in game
rows=[tuple(map(int,m)) for m in re.findall(r'heartbeat t=(\d+) worker loops=(\d+) last=attach-call; http loops=(\d+)',heart)]
assert rows[0][0]==2 and rows[-1][0]==110 and {r[1] for r in rows}=={101}
assert all(rows[i][2]<rows[i+1][2] for i in range(len(rows)-1))
p=subprocess.run([str(ROOT/'plugin/build/test_worker_liveness.exe'),'0','historical-replay'],
 input=''.join('%u %u %u\n'%r for r in rows),text=True,capture_output=True,cwd=ROOT)
assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
report=dict(result='PASS',logs=[dict(name=n,bytes=len(b),sha256=hashlib.sha256(b).hexdigest()) for n,b in files.items()],
 observations=dict(pid='01050200',stalled_worker_loops=101,first_stall_observation_s=2,last_stall_observation_s=110,
                  heartbeat_rows=len(rows),http_advances=True,exact_blocking_subcall='unknown',freeze_cause='unknown',game_poll_frequency='not measured in 0.8 logs'),
 replay_stdout=p.stdout,limits='PC policy replay, not execution of retail attach or proof of console recovery')
(ROOT/'build/failure-replay09.json').write_text(json.dumps(report,indent=2)+'\n')
print(p.stdout,end='')
