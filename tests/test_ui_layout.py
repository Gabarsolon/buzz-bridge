"""Run the production C layout/rasterizer for every output and UI text corpus.

No GPU, console, desktop capture, user configuration, or new runtime dependency.
PNG encoding uses Python's standard library (zlib); font and drawing are C.
"""
import ast
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'ps3/plugin/autoload'
OUT=ROOT/'tests/ui_preview'
BUILD=ROOT/'build/pc'
BUILD.mkdir(parents=True,exist_ok=True)
STRING=r'"(?:\\.|[^"\\])*"'
def literals(s):return [ast.literal_eval(m[0]) for m in re.finditer(STRING,s)]
def joined(s):return ''.join(literals(s))
class Layout(C.Structure):
    _fields_=[(x,C.c_int) for x in 'width height wide interlaced left top right bottom scale title_scale advance line gap body_top body_bottom columns lines'.split()]
class View(C.Structure):
    _fields_=[('title',C.c_char_p),('text',C.c_char_p),('items',C.POINTER(C.c_char_p))]+[(x,C.c_int) for x in 'count selected page question yes'.split()]
OBS=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_int,C.c_int,C.c_char_p)
class Canvas(C.Structure):
    _fields_=[('pixels',C.POINTER(C.c_uint32)),('stride',C.c_int),('observe',OBS),('user',C.c_void_p)]
class Result(C.Structure):
    _fields_=[(x,C.c_int) for x in 'pages page first last selection_visible text_lines body_top body_lines'.split()]
def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
def png(path,pixels,w,h):
    # Numeric ARGB is identical on host little endian and PPU big endian.
    packed=bytes(pixels);rgb=bytearray(w*h*3)
    for channel,offset in enumerate((2,1,0) if sys.byteorder=='little' else (1,2,3)):
        rgb[channel::3]=packed[offset::4]
    raw=b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b''))
def fixture_format(s):
    """Instantiate production diagnostic formats with representative long values."""
    counter=[0]
    def replace(m):
        spec=m[0][-1]
        if spec=='%':return '%'
        if spec=='s':
            counter[0]+=1
            if 'SHA' in s or 'hash' in s.lower():return 'bad3e45c9a81f25726e414b146ea5a112f28216ab4bdd2fd9f5c81b33d04c80a'
            if 'ISO' in s or 'path' in s.lower() or 'Dry run' in s:return '/dev_hdd0/PS2ISO/Buzz! The Very Long Game Name - Special Edition (Europe) [En,Fr,De,Es,It] [SCES-12345].iso'
            return 'Buzz! Bridge diagnostic result with a long description'
        return '80010006' if spec in 'xX' else '13'
    return re.sub(r'%(?:[-+ #0]*\d*(?:\.\d+)?(?:ll|l|z|h)?[sduixXcf]|%)',replace,s)
def corpus():
    menu=(APP/'menu.c').read_text()
    iso=(APP/'iso_patch.c').read_text()
    cases=[]
    for m in re.finditer(r'const char \*const (?:actions|items)\[\]\s*=\s*\{([^}]+)\}',menu):
        items=literals(m[1]);name='root' if items[0].startswith('1.') else 'iso_actions' if items[0].startswith('Check only') else 'netemu'
        title={'root':'Choose how to play','iso_actions':'PS2 ISO actions','netemu':'Real Buzz! handsets in PS2 games - no ISO patching'}[name]
        cases.append(dict(name=name,title=title,items=items))
    setups=re.search(r'bb_setup_names\[7\]=\{([^}]+)\}',iso)
    cases.append(dict(name='wizard_seven_choices',title='Your setup: What do you have?',items=literals(setups[1])))
    cases += [dict(name='wizard_pairs',title='How many pairs of Buzz! buzzers do you have? (1 pair = 2 players)',items=['0 (default)','1','2']),
              dict(name='wizard_dualshocks',title='How many DualShocks do you have?',items=[str(n) for n in range(9)]),
              dict(name='wizard_sharing',title='You can play with: Which one?',items=[f'{4+n} players: 0 pairs + {4-n} whole DualShocks + {n} shared DualShocks' for n in range(5)]),
              dict(name='iso_locations',title='Choose ISO location',items=['/dev_hdd0/PS2ISO','/dev_hdd0']+[f'/dev_usb{n:03}' for n in range(8)]),
              dict(name='iso_browser',title='Page 1 (more entries on next page): /dev_hdd0/PS2ISO/Very long folder name / another folder / games',items=[f'[4.7 GB] {n:03} - Buzz! The Very Long Game Name - Special Edition (Europe) [En,Fr,De,Es,It] - more filename characters to test truncation and the selected row.iso' for n in range(254)]+['Next page (more than 254 entries)']),
              dict(name='iso_browser_unbroken',title='/dev_usb000',items=['[Folder] '+('LongFolder'*30), 'A'*250+'.iso','Buzz! Caf\u00e9 - \u65e5\u672c\u8a9e.iso']),
              dict(name='maximum_menu',title='Maximum menu capacity / scroll boundary',items=[f'{n:03}: '+'W'*150 for n in range(256)])]
    seen=set()
    # Include every user-visible literal and formatted diagnostic from all UI
    # producers. This is a deliberate superset (some log-only strings included).
    for filename in ('menu.c','app.c','netemu_installer.c','iso_patch.c','netemu_platform.c','app_ui.c'):
        source=(APP/filename).read_text()
        source=re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|\'(?:\\.|[^\'\\])*\'|'+STRING,
                      lambda m:m[0] if m[0].startswith('"') else ' ',source)
        # Concatenate adjacent C literals before extracting, preserving paragraphs.
        groups=re.findall(r'(?:'+STRING+r'\s*)+',source)
        for group in groups:
            text=joined(group)
            if len(text)<24 or not re.search('[A-Za-z] [A-Za-z]',text) or text in seen:continue
            if '%s' not in text and text.startswith('#'):continue
            seen.add(text)
            if filename=='netemu_installer.c' and text.startswith('%s installed. SHA256 read-back verified.'):
                for name,done in (('modified ps2_netemu (this build)','Done. You can start your PS2 game now.'),
                                  ('Original Evilnat 4.93','Done. The original is back.')):
                    cases.append(dict(name=f'netemu_installer_{len(cases):03}',title='Status / result',
                                      text=text%(name,'/dev_hdd0/buzzbridge/netemu_backup/ps2_netemu_bad3e45c_20261030_123456_0000.self',done),question=0))
                continue
            text=fixture_format(text)
            question=('Yes:' in text or 'No / Circle' in text or text.endswith('?'))
            cases.append(dict(name=f'{Path(filename).stem}_{len(cases):03}',title='Please confirm' if question else 'Status / result',text=text,question=int(question)))
    cases += [dict(name='progress_wait',title='Working',text='Checking ISO folders for existing Buzz! Bridge patches',progress=True),
              dict(name='status_two_hashes',title='STATUS: installed build and stock backup',text='Installed: modified ps2_netemu (this build)\nSHA256: '+('0123456789abcdef'*4)+'\nStock backup: verified\nSHA256: '+('fedcba9876543210'*4)),
              dict(name='long_confirmation',title='Please confirm',question=1,text=('Restore original bytes only after all recovery hashes have been verified.\n\n'*20)+'Yes: restore. No / Circle: cancel.')]
    return cases

def main():
    env=os.environ.copy();env.update(TEMP=str(ROOT/'build/tmp'),TMP=str(ROOT/'build/tmp'))
    dll=BUILD/'ui_layout.dll'
    subprocess.run(['C:/Program Files/LLVM/bin/clang.exe','-shared','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(APP/'ui_layout.c'),'-o',str(dll),'-Wl,/EXPORT:ui_layout_init','-Wl,/EXPORT:ui_render','-Wl,/EXPORT:ui_filter_interlaced'],check=True,env=env)
    lib=C.CDLL(str(dll));lib.ui_layout_init.argtypes=[C.POINTER(Layout),C.c_int,C.c_int,C.c_int,C.c_int]
    lib.ui_render.argtypes=[C.POINTER(Layout),C.POINTER(View),C.POINTER(Canvas),C.POINTER(Result)];lib.ui_render.restype=C.c_int
    lib.ui_filter_interlaced.argtypes=[C.POINTER(Canvas),C.c_int,C.c_int]
    # Independent per-channel kernel oracle, including clamped edges and padding.
    def filtered(a,b,c):
        return 0xff000000 | sum(((((a>>k)&255)+2*((b>>k)&255)+((c>>k)&255))//4)<<k for k in (0,8,16))
    source=[0xff102030,0xffabcdef,0xff7f8103,0xdeadbeef,
            0xfff4f6f8,0xff142334,0xff102037,0xdeadbeef,
            0xff010305,0xff807f00,0xfffafbfd,0xdeadbeef]
    impulse=(C.c_uint32*len(source))(*source);test_canvas=Canvas(impulse,4,OBS(),None)
    lib.ui_filter_interlaced(C.byref(test_canvas),3,3)
    for y in range(3):
        for x in range(3):assert impulse[y*4+x]==filtered(source[max(0,y-1)*4+x],source[y*4+x],source[min(2,y+1)*4+x])
        assert impulse[y*4+3]==0xdeadbeef
    cases=corpus();OUT.mkdir(exist_ok=True)
    assert OUT.resolve().is_relative_to(ROOT.resolve())
    for old in OUT.rglob('*.png'):old.unlink()  # Only this runner's generated PNGs.
    (OUT/'corpus.json').write_text(json.dumps(cases,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    images=[];checks=0;summary=[]
    for mode,w,h,interlaced in [('480i',720,480,1),('480p',720,480,0),('576i',720,576,1),('576p',720,576,0),('720p',1280,720,0),('1080i',1920,1080,1),('1080p',1920,1080,0)]:
        for wide in (0,1):
            name=mode+('_16x9' if wide else '_4x3');folder=OUT/name;folder.mkdir(exist_ok=True)
            l=Layout();lib.ui_layout_init(C.byref(l),w,h,wide,interlaced)
            assert l.scale>=2 and l.title_scale>=2 # all glyph strokes / selection marker
            assert l.scale*7>=math.ceil(h*.03)
            assert l.left>=w*(.10 if h<=576 else .05) and l.top>=h*(.10 if h<=576 else .05)
            # An integer-rounded pixel edge differs by at most half a pixel.
            expected=6*l.scale*w/(h*(16/9 if wide else 4/3))
            assert abs(l.advance-expected)<=.500001
            pixels=(C.c_uint32*(w*h))()
            geometry=[]
            @OBS
            def observe(user,x,y,bw,bh,item,text):geometry.append((x,y,bw,bh,item,text.decode('ascii')))
            canvas=Canvas(pixels,w,observe,None)
            def render(view,draw=False):
                nonlocal checks
                geometry.clear();canvas.pixels=pixels if draw else C.POINTER(C.c_uint32)()
                result=Result();assert lib.ui_render(C.byref(l),C.byref(view),C.byref(canvas),C.byref(result))==0
                for x,y,bw,bh,item,text in geometry:
                    assert l.left<=x and x+bw<=l.right,(name,text,x,bw,l.right)
                    assert l.top<=y and y+bh<=l.bottom,(name,text,y,bh,l.bottom)
                    assert bh>=math.ceil(h*.03),(name,text,bh)
                nonempty=[r for r in geometry if r[2]>0]
                for a,b in zip(nonempty,nonempty[1:]):assert a[1]+a[3]<=b[1],(name,a,b)
                if view.count:assert result.selection_visible and result.first<=view.selected<=result.last
                checks+=1;return result
            # Verify the actual preview path applies the kernel and softened white
            # for every interlaced mode/aspect. Progressive raster is unchanged.
            probe=View(title=b'Progress: g p 9 P',text=b'gp glyph descenders and bright text.\n_ - = >',page=0)
            render(probe,True);actual=list(pixels)
            file=folder/'font_filter_probe.png';png(file,pixels,w,h);images.append(str(file.relative_to(ROOT)))
            if interlaced:
                raw_layout=Layout.from_buffer_copy(l);raw_layout.interlaced=0
                raw=(C.c_uint32*(w*h))();raw_canvas=Canvas(raw,w,OBS(),None);res=Result()
                assert lib.ui_render(C.byref(raw_layout),C.byref(probe),C.byref(raw_canvas),C.byref(res))==0
                # Every scanline, including all stroke edges in the glyph areas.
                def soften(v):return 0xffdce0e4 if v==0xfff4f6f8 else v
                for y in range(h):
                    for x in range(0,w,7):
                        expected=filtered(soften(raw[max(0,y-1)*w+x]),soften(raw[y*w+x]),soften(raw[min(h-1,y+1)*w+x]))
                        assert actual[y*w+x]==expected,(name,x,y,'interlace kernel missing')
                assert set(actual)-set(raw) # blended scanlines present
            # Descenders extend one scaled row below the normal 7-row baseline.
            assert any(bh==8*l.scale and 'gp' in text for x,y,bw,bh,it,text in geometry)
            for case in cases:
                title=case['title'].encode();v=View();v.title=title;v.page=-1 if case.get('progress') else 0
                v.question=case.get('question',0)
                if 'items' in case:
                    encoded=[s.encode('utf-8') for s in case['items']];items=(C.c_char_p*len(encoded))(*encoded);v.items=items;v.count=len(encoded)
                    for selected in range(v.count):
                        v.selected=selected;save=v.count<=10 or selected in (0,v.count//2,v.count-1)
                        result=render(v,save)
                        if save:
                            file=folder/f'{case["name"]}_selected_{selected:03}.png';png(file,pixels,w,h);images.append(str(file.relative_to(ROOT)))
                else:
                    v.text=case['text'].encode('utf-8');result=render(v)
                    # Hash and warning pages must preserve all characters, including
                    # the last columns; pagination must not silently replace them.
                    collected=[]
                    for page in range(result.pages):
                        v.page=-1 if case.get('progress') else page
                        rendered=render(v,True)
                        collected.extend(t for x,y,bw,bh,it,t in geometry if rendered.body_top<=y<l.body_bottom)
                        file=folder/f'{case["name"]}_page_{page+1:02}.png';png(file,pixels,w,h);images.append(str(file.relative_to(ROOT)))
                    if case['name']=='status_two_hashes':
                        text=''.join(collected).replace(' ','');assert '0123456789abcdef'*4 in text and 'fedcba9876543210'*4 in text
                    if not case.get('progress'):
                        expected=re.sub(r'[^\x00-\x7f]','?',case['text'])
                        assert re.sub(r'\s','',expected)==re.sub(r'\s','',''.join(collected)),(name,case['name'],'dialog text lost across pages')
                    if v.question:
                        v.yes=1;render(v,True);file=folder/f'{case["name"]}_yes.png';png(file,pixels,w,h);images.append(str(file.relative_to(ROOT)))
            summary.append(dict(mode=name,minimum_stroke_px=l.scale,interlace_filter=bool(interlaced),white='dce0e4' if interlaced else 'f4f6f8',glyph_height=l.scale*7,title_height=l.title_scale*7,columns=l.columns,body_lines=l.lines,safe=[l.left,l.top,l.right,l.bottom]))
            print('PASS',name,'glyph',l.scale*7,'columns',l.columns,flush=True)
    record=dict(passed=True,kernel_oracle=True,descenders_checked=True,geometry_checks=checks,png_count=len(images),corpus_screens=len(cases),modes=summary,images=images,source_sha256={n:hashlib.sha256((APP/n).read_bytes()).hexdigest() for n in ('ui_layout.c','ui_layout.h')},console_tested=False)
    (ROOT/'build/ui-layout-results.json').write_text(json.dumps(record,indent=2)+'\n')
    print(f'PASS {checks} layouts; {len(images)} PNGs; {len(cases)} screen fixtures; same C renderer on PC/PPU',flush=True)
if __name__=='__main__':main()
