"""Derive a workspace-only RX/RW linker script from the installed SDK template."""
import os, pathlib
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=pathlib.Path(os.environ.get('PS3DK',r'E:\tools\ps3dk\ps3-sdk-v0.26.0-windows-x86_64'))
def prepare():
    text=(SDK/'ppu/lib/lv2-prx.ld').read_text()
    text=text[text.index('OUTPUT_FORMAT'):]
    old='hdr_load\tPT_LOAD FILEHDR PHDRS\tFLAGS(0x01 | 0x02 | 0x04);'
    assert text.count(old)==1
    text=text.replace(old,'hdr_load PT_LOAD FLAGS(5);\n\thdr_data PT_LOAD FLAGS(6);')
    text=text.replace('. = 0 + SIZEOF_HEADERS;','. = 0;')
    old='.rodata.sceResident : { KEEP (*(.rodata.sceResident)) }'
    assert text.count(old)==1
    text=text.replace(old,'. = ALIGN(16);\n\t'+old+' : hdr_data')
    # The following comments in the SDK describe its old FILEHDR layout.
    begin=text.index('\t/* Reserve the relocation')
    end=text.index('\thdr_rela',begin)
    text=text[:begin]+'\t/* Reserved PT_SCE_PPURELA header; filled by native_prx.py. */\n'+text[end:]
    text=text.replace('module-info block: first thing inside PHDR[0]','module-info block: first thing inside RX')
    prefix='/* Workspace derivative of PS3DK v0.26.0 lv2-prx.ld.\n * RX: module info, code, unwind tables. RW: ALL TOC-addressed data,\n * library metadata, compact OPDs, GOT, and BSS. No ELF headers mapped.\n * See research/load10/ps3dk-reloc.rs and build/LOAD_DIAGNOSIS.md.\n */\n'
    path=ROOT/'plugin/game/native-prx.ld'
    value=prefix+text
    if not path.exists() or path.read_text()!=value: path.write_text(value,encoding='utf-8')
    return path
if __name__=='__main__': print(prepare())
