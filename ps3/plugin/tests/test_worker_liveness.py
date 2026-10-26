"""Block each USB presentation call; exercise the actual independent loops."""
import json
from pathlib import Path
import socket
import subprocess
import time
from http.client import HTTPConnection

ROOT = Path(__file__).resolve().parents[2]
for step in ('lookup-call', 'probe-call', 'attach-call', 'attach-timeout', 'locks-held', 'safety-stop', 'safety-start', 'console-replay'):
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen([str(ROOT/'plugin/build/test_worker_liveness.exe'), str(port), step],
                               cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        lines = []
        while step not in ('locks-held', 'safety-stop', 'safety-start'):
            line = process.stdout.readline()
            assert line, process.stderr.read()
            lines.append(line)
            if step=='attach-timeout' and not line.startswith('PC attach-timeout quarantined'):
                continue
            if line.startswith(('PC worker deliberately blocked', 'PC console replay attached', 'PC attach-timeout quarantined')):
                break
        for path in (() if step in ('locks-held', 'safety-stop', 'safety-start') else ('/health.json', '/')):
            connection = HTTPConnection('127.0.0.1', port, timeout=1)
            connection.request('GET', path)
            response = connection.getresponse()
            body = response.read()
            assert response.status == 200
            if path == '/health.json':
                assert json.loads(body)['usb_backend_ready'] is (step == 'console-replay')
            connection.close()
        output, error = process.communicate(timeout=7)
        output = ''.join(lines)+output
        assert process.returncode == 0, (process.returncode, output, error)
        assert 'heartbeat t=2 worker loops=' in output, output
        if step == 'attach-timeout':
            assert output.count('attach-timeout device=425a0000')==1,output
            assert 'attach-call object=425a0001' not in output,output
            assert 'second dongle absent; HTTP continues' in output,output
            (ROOT/'build/attach-timeout09.log').write_text(output)
            print('PASS: first attach blocks >3s; once-only independent timeout; HTTP 200; late return quarantined; no second dongle')
        elif step == 'console-replay':
            for required in ('lookup-return object=425a0000 result=00000000',
                             'lookup-return object=425a0001 result=00000000',
                             'probe-return object=425a0000 result=00000000',
                             'attach-return object=425a0001 result=00000000',
                             'open-pipe device=', 'first-IN-submit', 'first-IN-complete'):
                assert required in output, (required, output)
            assert '801100ff' not in output
            print('PASS: console replay through actual native hooks + services: two attached, callback IN, HTTP usb_backend_ready=true')
            (ROOT/'build/console-replay07.log').write_text(output)
        elif step == 'locks-held':
            assert 'PC all shared plugin locks deliberately held' in output, output
            print('PASS: heartbeat while adapter mutex, trace lock and general-log writer lock ALL held')
        elif step == 'safety-start':
            assert 'stop=1' in output and 'PC safety start:' in output, output
            print('PASS: preexisting stop sentinel prevents all USB/HTTP work from first iteration')
        elif step == 'safety-stop':
            assert 'stop=1' in output and 'SAFETY STOP latched' in output and 'PC safety stop:' in output, output
            print('PASS: 1 Hz safety stop, frozen work counters, hook bypass and persistent latch')
        else:
            assert f'last={step}' in output, output
            assert 'http request /health.json' in output and 'http request / ' in output, output
            print(f'PASS: real native service loops: USB blocked at {step}; HTTP 200 + independent 2 s heartbeat')
    finally:
        if process.poll() is None:
            process.terminate()  # This test's own child only.
            process.wait(timeout=3)
