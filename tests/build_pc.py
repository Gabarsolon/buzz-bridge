"""Build host tools with Clang; ISO and hash code are shared unchanged with PPU."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'ps3/plugin/autoload'
OUT=ROOT/'build/pc'
OUT.mkdir(parents=True,exist_ok=True)
def build(fixtures=False):
    exe=OUT/('bbtest.exe' if fixtures else 'bbtool.exe')
    sources=[APP/f for f in ('bb.c','bb_io.c','iso_patch.c')]+[ROOT/'tests/pc_cli.c']
    cmd=[r'C:/Program Files/LLVM/bin/clang.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-D_CRT_SECURE_NO_WARNINGS','-I'+str(APP),'-I'+str(ROOT/'tests')]
    if fixtures:cmd+=['-DBB_TEST_FIXTURES']
    subprocess.run(cmd+list(map(str,sources))+['-o',str(exe)],cwd=ROOT,check=True)
    return exe
if __name__=='__main__':print(build())
