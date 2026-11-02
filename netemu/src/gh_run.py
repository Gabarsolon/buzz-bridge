from pathlib import Path
import os,sys,hashlib,warnings
warnings.filterwarnings('ignore',category=DeprecationWarning)
H=Path(__file__).resolve().parent
for d in ['analysis','evidence','tmp','settings']:(H/d).mkdir(exist_ok=True)
for k in ['TEMP','TMP','TMPDIR','APPDATA','LOCALAPPDATA']:os.environ[k]=str(H/('tmp' if k in ['TEMP','TMP','TMPDIR'] else 'settings'))
os.environ['JAVA_HOME']=r'C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot';os.environ['GHIDRA_INSTALL_DIR']=r'C:\ghidra_12.1.3_PUBLIC'
import pyghidra
from pyghidra.launcher import HeadlessPyGhidraLauncher
L=HeadlessPyGhidraLauncher();L.java_home=os.environ['JAVA_HOME'];L.add_vmargs('-Xmx3G','-Duser.home='+str(H/'settings'),'-Dapplication.settingsdir='+str(H/'settings'),'-Djava.io.tmpdir='+str(H/'tmp'));L.start()
from ghidra.app.emulator import EmulatorHelper
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.util import PseudoDisassembler
from java.math import BigInteger
for variant in ([x for x in sys.argv[2:] if not x.startswith('--')] or ['S4A','S4']):
 with pyghidra.open_program(str(H/f'ps2_netemu_{variant}.elf'),project_location=str(H/'gh_native'),project_name=variant+hashlib.sha256((H/f'ps2_netemu_{variant}.elf').read_bytes()).hexdigest()[:12],analyze=False,language='PowerPC:BE:64:default') as flat:
  p=flat.getCurrentProgram();s=p.getAddressFactory().getDefaultAddressSpace();pd=PseudoDisassembler(p)
  exec((H/sys.argv[1]).read_text(encoding='utf-8-sig'))
