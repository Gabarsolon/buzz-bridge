"""Reference build script (Windows, MSVC Build Tools for the host tools). Needs PICO_SDK_PATH and a Pico-PIO-USB checkout with this firmware at <Pico-PIO-USB>/buzz-pico-adapter/firmware_source. See pico/README.md."""
import os
from pathlib import Path
import subprocess
import sys
import re
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
def main():
    vcvars=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    p=subprocess.run(f'call "{vcvars}" >nul && set',shell=True,capture_output=True,text=True)
    if p.returncode:raise RuntimeError('vcvars64 failed')
    env={k.upper():v for k,v in (line.split('=',1) for line in p.stdout.splitlines() if '=' in line and not line.startswith('='))}
    # Put arm-none-eabi-gcc and ninja on PATH, or set ARM_GNU_BIN / NINJA_DIR.
    extra=[os.environ[k] for k in ('ARM_GNU_BIN','NINJA_DIR') if os.environ.get(k)]
    env['PATH']=os.pathsep.join(extra+[env.get('PATH','')])
    env['PICO_SDK_PATH']=os.environ['PICO_SDK_PATH']
    (HERE/'tmp').mkdir(exist_ok=True)
    for key in ('TEMP','TMP','TMPDIR'):env[key]=str(HERE/'tmp')
    cmds=[['cmake','-S',str(HERE/'firmware_source'),'-B',str(HERE/'build_modec'),'-G','Ninja',
           '-DPICO_PLATFORM=rp2350','-DPICO_BOARD=pico2',
           '-DPython3_EXECUTABLE='+sys.executable,
           '-Dpicotool_DIR='+str(ROOT/'examples/build_test/_deps/picotool'),
           '-Dpioasm_DIR='+str(ROOT/'examples/build_test/pioasm-install/pioasm')],
          ['cmake','--build',str(HERE/'build_modec'),'--target','buzz_bridge_modec','buzz_bridge_modec_recovery']]
    with (HERE/'build_modec.log').open('w') as log:
      for cmd in cmds:
        p=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        log.write(p.stdout);log.flush()
        if p.returncode:
          print('\n'.join(x for x in p.stdout.splitlines() if re.search(r'(error:|fatal error|^ninja:|^CMake Error)',x))[-5000:])
          raise RuntimeError('Build failed; see build_modec.log')
    print('PASS standard/recovery Mode C builds; all output inside buzz-pico-adapter')
if __name__=='__main__':
  try:main()
  except Exception as e:print('FAIL',e);sys.exit(1)
