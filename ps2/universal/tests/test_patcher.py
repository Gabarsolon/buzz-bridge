"""Tests of tools/bb_patch.py on synthetic ISO9660 images (no game data).

  python -I tests/test_patcher.py            (from the work folder, after building bbusbd.irx)
"""
import hashlib
import contextlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from unittest import mock

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import bb_patch  # noqa: E402
import irxlib  # noqa: E402

IRX = os.path.join(ROOT, 'bbusbd.irx')
SECTOR = 2048


def both(v, size):
    return struct.pack('<I', v) + struct.pack('>I', v)


def dir_record(name, lba, size, is_dir=False):
    nm = name
    ln = 33 + len(nm)
    if ln % 2:
        ln += 1
    rec = bytearray(ln)
    rec[0] = ln
    rec[2:10] = both(lba, 4)
    rec[10:18] = both(size, 4)
    rec[18:25] = bytes([126, 1, 1, 0, 0, 0, 0])
    rec[25] = 2 if is_dir else 0
    rec[28:32] = struct.pack('<H', 1) + struct.pack('>H', 1)
    rec[32] = len(nm)
    rec[33:33 + len(nm)] = nm
    return bytes(rec)


def fake_usbd(size, seed):
    """A stand-in 'Sony' USBD: ELF header, the module name, deterministic filler. Not Sony data."""
    h = hashlib.sha256(seed).digest()
    body = bytearray(b'\x7fELF\x01\x01\x01\x00' + bytes(8) + b'\x80\xff\x08\x00' + bytes(0x400))
    body[0x100:0x10A] = b'USB_driver'
    while len(body) < size:
        h = hashlib.sha256(h).digest()
        body += h
    return bytes(body[:size])


def build_iso(path, folder, usbd_size, junk_neighbour=True, usbd_data=None):
    """root: SCES_000.00, <folder>/USBD.IRX (usbd_size), <folder>/BUZZER.IRX right after it."""
    usbd = usbd_data if usbd_data is not None else fake_usbd(usbd_size, b'usbd' + folder.encode())
    buzzer = fake_usbd(8636, b'buzzer')
    boot = fake_usbd(5000, b'boot')
    img = bytearray()
    img += bytes(16 * SECTOR)
    nsect = lambda n: (n + SECTOR - 1) // SECTOR  # noqa: E731
    root_lba, sub_lba, data_lba = 20, 21, 30
    boot_lba = data_lba
    usbd_lba = boot_lba + nsect(len(boot))
    buzzer_lba = usbd_lba + nsect(len(usbd))
    end_lba = buzzer_lba + nsect(len(buzzer))

    def dot(lba):
        return dir_record(b'\x00', lba, SECTOR, True) + dir_record(b'\x01', root_lba, SECTOR, True)

    root = dot(root_lba) + dir_record(folder.encode(), sub_lba, SECTOR, True) + dir_record(b'SCES_000.00;1', boot_lba, len(boot))
    sub = dot(sub_lba) + dir_record(b'USBD.IRX;1', usbd_lba, len(usbd)) + dir_record(b'BUZZER.IRX;1', buzzer_lba, len(buzzer))
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[6] = 1
    pvd[40:72] = b'BBTEST'.ljust(32)
    pvd[80:88] = both(end_lba, 4)
    pvd[156:156 + 34] = dir_record(b'\x00', root_lba, SECTOR, True)[:34]
    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b'CD001'
    term[6] = 1
    img += bytes(pvd) + bytes(term)
    img += bytes((root_lba - 18) * SECTOR)
    img += root.ljust(SECTOR, b'\0')
    img += sub.ljust(SECTOR, b'\0')
    img += bytes((data_lba - 22) * SECTOR)
    for d in (boot, usbd, buzzer):
        img += d.ljust(nsect(len(d)) * SECTOR, b'\0')
    with open(path, 'wb') as f:
        f.write(img)
    return dict(usbd=usbd, buzzer=buzzer, boot=boot, usbd_lba=usbd_lba, buzzer_lba=buzzer_lba, boot_lba=boot_lba)


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha1(f.read()).hexdigest()


def rd(path):
    with open(path, 'rb') as f:
        return f.read()


def run(*argv, input_text=None):
    # Synthetic bytes are allowlisted only inside this process. The shipped
    # CLI has no override and still refuses these exact fixtures (tested below).
    known = {bb_patch.sha1(fake_usbd(n, b'usbd' + folder.encode())): 'synthetic test fixture'
             for folder in ('IOP', 'SYSTEM') for n in (8000, 34985, 34993, 35009)}
    argv = list(argv)
    if input_text is None and not any(x in argv for x in ('--layout', '--info', '--restore', '--list-layouts', '--buzzer-pairs', '--whole-ds', '--shared-ds')):
        argv += ['--layout', 'buzzers']
    so, se = io.StringIO(), io.StringIO()
    input_mock = mock.patch('builtins.input', side_effect=input_text) if input_text is not None else contextlib.nullcontext()
    with mock.patch.dict(bb_patch.KNOWN_USBD, known), contextlib.redirect_stdout(so), contextlib.redirect_stderr(se), input_mock:
        try:
            rc = bb_patch.main(argv)
        except SystemExit as e:
            rc = e.code
    return rc, so.getvalue(), se.getvalue()


class PatcherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(IRX):
            raise unittest.SkipTest('bbusbd.irx not built')
        os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
        cls.tmp = tempfile.mkdtemp(prefix='bbpatch_', dir=os.path.join(ROOT, 'build'))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def iso(self, folder='IOP', size=34985, name='a.iso', **kw):
        p = os.path.join(self.tmp, name)
        meta = build_iso(p, folder, size, **kw)
        return p, meta

    def test_layout_list_matches_header(self):
        with open(os.path.join(ROOT, 'src', 'bb_layouts.h')) as f:
            hdr = f.read()
        parsed = [(int(a), b) for a, b, _ in re.findall(r'BB_LAYOUT\((\d+),\s*(\w+),\s*"([^"]*)"\)', hdr)]
        self.assertEqual(parsed, [(i, n) for i, n, _ in bb_patch.EMBEDDED_LAYOUTS])
        self.assertEqual([(i, n) for i, n, _ in bb_patch.load_layouts()], parsed)

    def test_any_slot_layouts(self):
        p, meta = self.iso(name='any.iso')
        for layout, name in ((9, 'any_buzzers_ds'), (10, 'any_ds4'), (11, 'any_2buzzers_ds'), (12, 'any_ds8')):
            out = os.path.join(self.tmp, 'any_%d.iso' % layout)
            rc, so, se = run(p, '--out', out, '--layout', name, '--irx', IRX, '--poll', 'off')
            self.assertEqual(rc, 0, so + se)
            cfg = bb_patch.read_config(rd(out)[meta['usbd_lba'] * SECTOR:])
            self.assertEqual((cfg['layout'], cfg['flags']), (layout, 0x11))
            refused = os.path.join(self.tmp, 'diag_any_%d.iso' % layout)
            rc, so, se = run(p, '--out', refused, '--layout', name, '--irx', IRX, '--diag-tap')
            self.assertEqual(rc, 1)
            self.assertIn('--diag-tap is not supported for any-slot', se)
            self.assertFalse(os.path.exists(refused))

    def test_info(self):
        p, meta = self.iso()
        rc, out, err = run('--info', p)
        self.assertEqual(rc, 0, err)
        self.assertIn('/IOP/USBD.IRX: LBA %d, 34985 bytes (18 sectors' % meta['usbd_lba'], out)

    def test_patch_quiz_layout_and_neighbours(self):
        p, meta = self.iso()
        before = sha(p)
        out = os.path.join(self.tmp, 'out.iso')
        rc, so, se = run(p, '--out', out, '--layout', 'buzzers_ds', '--irx', IRX)
        self.assertEqual(rc, 0, se + so)
        self.assertEqual(sha(p), before, 'input image was modified')
        a, b = rd(p), rd(out)
        self.assertEqual(len(a), len(b))
        lo = meta['usbd_lba'] * SECTOR
        hi = lo + len(meta['usbd'])
        self.assertEqual(a[:lo], b[:lo], 'bytes before the USBD extent changed')
        self.assertEqual(a[hi:], b[hi:], 'bytes after the USBD file changed (neighbour BUZZER.IRX or padding)')
        new = b[lo:hi]
        irx = rd(IRX)
        self.assertEqual(new[len(irx):], bytes(len(new) - len(irx)), 'tail is not zero padded')
        self.assertEqual(new[:irx.find(b'BBZCFG01')], irx[:irx.find(b'BBZCFG01')])
        cfg = bb_patch.read_config(new)
        self.assertEqual(cfg, dict(version=1, layout=1, flags=0x11, attach_delay_ms=100, heartbeat_ms=100))
        # everything except the 16 byte config block equals the build
        off = irx.find(b'BBZCFG01')
        self.assertEqual(new[:off] + new[off + 16:len(irx)], irx[:off] + irx[off + 16:])
        # the module still passes the structural checks, from the image
        exp, imp = irxlib.parse_tables(new[:len(irx)])
        self.assertEqual(len(exp['usbd'][1]), 19)
        self.assertTrue(os.path.exists(out + '.bbpatch.json'))

    def test_poll_flags(self):
        p, meta = self.iso(name='poll.iso')
        for mode, flag in (('off', 0x01), ('auto', 0x11), ('always', 0x21)):
            out = os.path.join(self.tmp, 'poll_%s.iso' % mode)
            rc, so, se = run(p, '--out', out, '--irx', IRX, '--poll', mode)
            self.assertEqual(rc, 0, se)
            lo = meta['usbd_lba'] * SECTOR
            self.assertEqual(bb_patch.read_config(rd(out)[lo:lo + len(meta['usbd'])])['flags'], flag)

    def test_diag_tap_flag(self):
        p, meta = self.iso(name='diag.iso')
        lo = meta['usbd_lba'] * SECTOR
        out = os.path.join(self.tmp, 'diag7.iso')
        rc, so, se = run(p, '--out', out, '--irx', IRX, '--layout', 'buzzers_ds2_tap2', '--diag-tap')
        self.assertEqual(rc, 0, se)
        self.assertEqual(bb_patch.read_config(rd(out)[lo:lo + len(meta['usbd'])])['flags'], 0x19)
        out = os.path.join(self.tmp, 'diag1.iso')
        rc, so, se = run(p, '--out', out, '--irx', IRX, '--layout', 'buzzers_ds', '--diag-tap')
        self.assertEqual(rc, 1)
        self.assertFalse(os.path.exists(out))

    def test_junior_path(self):
        p, meta = self.iso(folder='SYSTEM', size=35009, name='j.iso')
        out = os.path.join(self.tmp, 'jout.iso')
        rc, so, se = run(p, '--out', out, '--layout', 'two_ds_single', '--irx', IRX, '--quiet', '--no-pad')
        self.assertEqual(rc, 0, se + so)
        self.assertIn('/SYSTEM/USBD.IRX', so)
        lo = meta['usbd_lba'] * SECTOR
        cfg = bb_patch.read_config(rd(out)[lo:lo + len(meta['usbd'])])
        self.assertEqual((cfg['layout'], cfg['flags']), (3, 0x12))

    def test_all_layouts_and_flags(self):
        p, meta = self.iso(name='l.iso')
        for i, name, _ in bb_patch.load_layouts():
            if i == 13:
                continue  # custom requires counts; covered separately
            for sel in (name, str(i)):
                out = os.path.join(self.tmp, 'l_%s.iso' % sel)
                rc, so, se = run(p, '--out', out, '--layout', sel, '--irx', IRX, '--script-demo',
                                 '--attach-delay-ms', '250', '--heartbeat-ms', '40')
                self.assertEqual(rc, 0, se)
                lo = meta['usbd_lba'] * SECTOR
                cfg = bb_patch.read_config(rd(out)[lo:lo + len(meta['usbd'])])
                self.assertEqual(cfg, dict(version=1, layout=i, flags=0x15, attach_delay_ms=250, heartbeat_ms=40))
        rc, so, se = run(p, '--out', os.path.join(self.tmp, 'bad.iso'), '--layout', 'nope', '--irx', IRX)
        self.assertEqual(rc, 1)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, 'bad.iso')))

    def test_refuses_oversize_without_writing(self):
        p, meta = self.iso(size=8000, name='small.iso')  # module is 13 KB
        out = os.path.join(self.tmp, 'small_out.iso')
        rc, so, se = run(p, '--out', out, '--irx', IRX)
        self.assertEqual(rc, 1)
        self.assertIn('larger than the original', se)
        self.assertFalse(os.path.exists(out))
        self.assertFalse(os.path.exists(out + '.partial'))

    def test_refuses_input_as_output_and_existing_output(self):
        p, _ = self.iso(name='same.iso')
        before = sha(p)
        rc, so, se = run(p, '--out', p, '--irx', IRX)
        self.assertEqual(rc, 1)
        self.assertIn('must not be the input', se)
        self.assertEqual(sha(p), before)
        out = os.path.join(self.tmp, 'exists.iso')
        with open(out, 'wb') as f:
            f.write(b'x')
        rc, so, se = run(p, '--out', out, '--irx', IRX)
        self.assertEqual(rc, 1)
        self.assertEqual(rd(out), b'x')

    def test_wrong_file_is_refused(self):
        p, meta = self.iso(name='wrong.iso', usbd_data=b'NOTANELF' * 2000)
        out = os.path.join(self.tmp, 'wrong_out.iso')
        rc, so, se = run(p, '--out', out, '--irx', IRX)
        self.assertEqual(rc, 1)
        self.assertIn('unknown USBD.IRX version', se)
        self.assertFalse(os.path.exists(out))
        self.assertEqual(run(p, '--out', out, '--force')[0], 2)

    def test_restore_with_sidecar_and_from_iso(self):
        p, meta = self.iso(name='r.iso')
        out = os.path.join(self.tmp, 'r_out.iso')
        self.assertEqual(run(p, '--out', out, '--layout', 'one_ds_shared', '--irx', IRX)[0], 0)
        self.assertNotEqual(sha(p), sha(out))
        rc, so, se = run('--restore', out)
        self.assertEqual(rc, 0, se)
        self.assertEqual(sha(p), sha(out), 'restored image differs from the original')
        rc, so, se = run('--restore', out)
        self.assertEqual(rc, 0)
        self.assertIn('nothing to do', so)
        # again via --from-iso, without the sidecar
        out = os.path.join(self.tmp, 'r_from.iso')
        self.assertEqual(run(p, '--out', out, '--irx', IRX)[0], 0)
        os.remove(out + '.bbpatch.json')
        self.assertEqual(run('--restore', out)[0], 1)
        rc, so, se = run('--restore', out, '--from-iso', p)
        self.assertEqual(rc, 0, se)
        self.assertEqual(sha(p), sha(out))

    def test_restore_refuses_a_tampered_image(self):
        p, meta = self.iso(name='t.iso')
        out = os.path.join(self.tmp, 't_out.iso')
        self.assertEqual(run(p, '--out', out, '--irx', IRX)[0], 0)
        with open(out, 'r+b') as f:
            f.seek(meta['usbd_lba'] * SECTOR + 100)
            f.write(b'\xAA' * 8)
        rc, so, se = run('--restore', out)
        self.assertEqual(rc, 1)
        self.assertIn('neither', se)
        self.assertEqual(run('--restore', out, '--force')[0], 2)

    def test_not_an_iso(self):
        p = os.path.join(self.tmp, 'junk.iso')
        with open(p, 'wb') as f:
            f.write(os.urandom(100000))
        rc, so, se = run('--info', p)
        self.assertEqual(rc, 1)
        self.assertIn('ISO9660', se)


class SafetyTests(unittest.TestCase):
    def setUp(self):
        os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(prefix='safety_', dir=os.path.join(ROOT, 'build'))
        self.addCleanup(self.directory.cleanup)
        self.tmp = self.directory.name
        self.p = os.path.join(self.tmp, 'Game.iso')
        self.meta = build_iso(self.p, 'IOP', 34985)
        self.original = rd(self.p)
        self.out = os.path.join(self.tmp, 'Game (Buzz Bridge).iso')

    def test_default_copy_interactive_setup_and_poll(self):
        rc, so, se = run(self.p, input_text=['bad', '3'])
        self.assertEqual(rc, 0, se)
        self.assertIn('What do you have?', so)
        self.assertIn('Please enter', so)
        self.assertEqual(rd(self.p), self.original)
        cfg = bb_patch.read_config(rd(self.out)[self.meta['usbd_lba'] * SECTOR:])
        self.assertEqual((cfg['layout'], cfg['flags']), (2, 0x11))

    def test_menu_maps_choices_to_layouts_and_has_no_fixed_multitap_setups(self):
        rc, so, se = run(self.p, input_text=['6', '', '', ''])
        self.assertEqual(rc, 0, se)
        self.assertNotIn('Turn on the multitap check', so)
        self.assertNotIn('CONFIG', so)
        self.assertNotIn('port 1)', so)
        cfg = bb_patch.read_config(rd(self.out)[self.meta['usbd_lba'] * SECTOR:])
        self.assertEqual((cfg['layout'], cfg['flags']), (13, 0x11))

    def test_menu_player_column_is_aligned(self):
        rc, so, se = run(self.p, input_text=['1'])
        self.assertEqual(rc, 0, se)
        rows = [l for l in so.splitlines() if l[:3].strip().rstrip(')').isdigit() and ' players' in l]
        self.assertEqual(len(rows), 6)
        self.assertEqual(len(set(l.index(' players') for l in rows)), 1, rows)

    def test_custom_wizard_defaults_and_all_menu_choices(self):
        for choice, expected in enumerate((0,1,2,3,4,13),1):
            target = os.path.join(self.tmp,'menu_%d.iso' % choice)
            answers = [str(choice)] + (['','',''] if choice == 6 else [])
            rc,so,se = run(self.p,'--out',target,input_text=answers)
            self.assertEqual(rc,0,so+se)
            cfg = bb_patch.read_config(rd(target)[self.meta['usbd_lba']*SECTOR:])
            self.assertEqual(cfg['layout'],expected)
            self.assertEqual(cfg['flags'],0x11)
            self.assertNotIn('CONFIG',so)
            self.assertEqual(so.lower().count('multitap'),1)
            if choice == 6:
                self.assertEqual((cfg['buzzer_pairs'],cfg['whole_ds'],cfg['shared_ds']),(0,4,0))
                self.assertEqual((cfg['players'],cfg['dongles']),(4,1))
                self.assertIn('Total: 4 players',so)
                self.assertIn('Whole DualShocks',so)
                self.assertIn('Select, Start, L3 and R3 also buzz red.',so)

    def test_custom_wizard_validation_summary_and_encoding(self):
        rc,so,se = run(self.p,input_text=['6','bad','-1','3','1','9','2.5','3','0','9','2'])
        self.assertEqual(rc,0,so+se)
        self.assertEqual(so.count('Please enter a whole number'),7)
        self.assertIn('Total: 6 players',so)
        for fragment in ('Who uses what','Buzz! buzzer pairs','Whole DualShocks','Shared DualShocks',
                         'LEFT half = one player; RIGHT half = another.', 'L1 / L2', 'R1 / R2',
                         'Triangle','Square','Circle','Cross','Up','Left','Right','Down'):
            self.assertIn(fragment,so)
        cfg = bb_patch.read_config(rd(self.out)[self.meta['usbd_lba']*SECTOR:])
        self.assertEqual((cfg['layout'],cfg['flags'],cfg['buzzer_pairs'],cfg['whole_ds'],cfg['shared_ds'],
                          cfg['players'],cfg['dongles']),(13,0x11,1,2,1,6,2))
        blob = rd(self.out)[self.meta['usbd_lba']*SECTOR:]
        off = blob.index(bb_patch.CFG_MAGIC)
        self.assertEqual(blob[off:off+16],struct.pack('<8sBBBBHH',b'BBZCFG01',1,13,0x11,34,100,100))
        self.assertNotIn('CONFIG',so)
        self.assertEqual(so.lower().count('multitap'),1)
        rc,info,err = run(self.out,'--info')
        self.assertEqual(rc,0,err)
        for field in ("'buzzer_pairs': 1","'whole_ds': 2","'shared_ds': 1","'dongles': 2"):
            self.assertIn(field,info)
        self.assertEqual(run(self.out,'--restore','--from-iso',self.p)[0],0)
        self.assertEqual(rd(self.out),self.original)

    def test_custom_options_list_every_way_to_play(self):
        # (pairs, dualshocks) -> [(players, pairs, whole, shared), ...]
        self.assertEqual(bb_patch.custom_options(0, 3), [(3, 0, 3, 0), (4, 0, 2, 1), (5, 0, 1, 2), (6, 0, 0, 3)])
        self.assertEqual(bb_patch.custom_options(1, 3), [(5, 1, 3, 0), (6, 1, 2, 1), (7, 1, 1, 2), (8, 1, 0, 3)])
        self.assertEqual(bb_patch.custom_options(2, 0), [(4, 2, 0, 0)])
        self.assertEqual(bb_patch.custom_options(0, 4), [(4, 0, 4, 0), (5, 0, 3, 1), (6, 0, 2, 2), (7, 0, 1, 3), (8, 0, 0, 4)])
        self.assertEqual(bb_patch.custom_options(0, 8), [(8, 0, 8, 0)])
        self.assertEqual(bb_patch.custom_options(2, 8), [(8, 2, 4, 0)])
        for pairs in range(3):
            for ds in range(9):
                for players, p, w, s in bb_patch.custom_options(pairs, ds):
                    self.assertEqual(players, 2 * p + w + 2 * s)
                    self.assertTrue(1 <= players <= 8 and s <= 4 and w >= 0)

    def test_custom_wizard_shows_defaults_and_refuses_too_few_controllers_without_writes(self):
        prompts = []
        answers = iter(['0', '0'])

        def fake_input(prompt=''):
            prompts.append(prompt)
            return next(answers)
        with unittest.mock.patch('builtins.input', fake_input), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(bb_patch.PatchError) as raised:
                bb_patch.ask_custom()
        self.assertEqual([p.split('[')[-1] for p in prompts], ['default: 0]: ', 'default: 4]: '])
        self.assertIn('at least one pair of Buzz! buzzers or one DualShock', str(raised.exception))
        answers = iter(['1', '3', '2'])
        prompts.clear()
        with unittest.mock.patch('builtins.input', fake_input), contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(bb_patch.ask_custom(), (1, 2, 1))
        self.assertEqual([p.split('[')[-1] for p in prompts], ['default: 0]: ', 'default: 2]: ', 'default: 1]: '])
        self.assertIn('You can play with:', out.getvalue())
        self.assertIn('6 players: 1 pair of Buzz! buzzers + 2 DualShocks, one player each + 1 DualShock shared', out.getvalue())
        rc, so, se = run(self.p, input_text=['6', '0', '0'])
        self.assertEqual(rc, 1, so + se)
        self.assertIn('at least one pair of Buzz! buzzers or one DualShock', se)
        self.assertEqual(os.listdir(self.tmp), ['Game.iso'])
        self.assertEqual(rd(self.p), self.original)

    def test_custom_cli_all_examples_and_rejections(self):
        examples = ((0,4,0),(0,0,2),(0,2,1),(1,2,0),(1,0,1),(1,6,0),(1,4,1),(1,2,2),
                    (1,0,3),(2,0,0),(2,4,0),(2,0,2),(0,4,2),(0,0,4),(0,8,0),(2,2,1))
        for p,w,s in examples:
            target = os.path.join(self.tmp,'custom_%d_%d_%d.iso' % (p,w,s))
            flags = ('--buzzer-pairs',str(p),'--whole-ds',str(w),'--shared-ds',str(s))
            rc,so,se = run(self.p,'--out',target,*flags,'--poll','off','--attach-delay-ms','250','--heartbeat-ms','40')
            self.assertEqual(rc,0,so+se)
            blob = rd(target)[self.meta['usbd_lba']*SECTOR:]
            cfg = bb_patch.read_config(blob)
            self.assertEqual(cfg,dict(version=1,layout=13,flags=0x11,attach_delay_ms=250,heartbeat_ms=40,
                                      buzzer_pairs=p,whole_ds=w,shared_ds=s,players=2*p+w+2*s,
                                      dongles=1 if 2*p+w+2*s <= 4 else 2))
            self.assertEqual(blob[blob.index(bb_patch.CFG_MAGIC)+11],p+3*w+27*s)
            self.assertNotIn('What do you have?',so)
        rejected = os.path.join(self.tmp,'refused.iso')
        for flags,code,fragment in (
            (('--buzzer-pairs','1','--whole-ds','8'),1,'maximum 8'),
            (('--whole-ds','0'),1,'between 1 and 8'),
            (('--whole-ds','-1'),1,'0-8'),
            (('--buzzer-pairs','3'),1,'0-2'),
            (('--shared-ds','5'),1,'0-4'),
            (('--whole-ds','nine'),2,'invalid int value'),
            (('--whole-ds','4','--layout','0'),2,'cannot be combined with --layout'),
            (('--whole-ds','4','--layout','custom'),2,'cannot be combined with --layout'),
            (('--layout','custom'),1,'custom needs'),
            (('--whole-ds','4','--diag-tap'),1,'including custom')):
            rc,so,se = run(self.p,'--out',rejected,*flags)
            self.assertEqual(rc,code,so+se)
            self.assertIn(fragment,se)
            for suffix in ('','.bbpatch.json','.usbd.orig'):
                self.assertFalse(os.path.exists(rejected+suffix))
        self.assertEqual(rd(self.p),self.original)

    def test_info_writes_nothing_and_reports_patched_config(self):
        before = sorted(os.listdir(self.tmp))
        self.assertEqual(run('--info', self.p)[0], 0)
        self.assertEqual(sorted(os.listdir(self.tmp)), before)
        self.assertEqual(run(self.p, '--layout', '4')[0], 0)
        rc, so, se = run(self.out, '--info')
        self.assertEqual(rc, 0, se)
        self.assertIn("'layout': 4", so)

    def test_unknown_hash_real_cli_refuses_without_output(self):
        cli = os.path.join(ROOT, '..', 'buzz_bridge_ps2.py')
        proc = subprocess.run([sys.executable, cli, self.p, '--layout', '0'],
                              text=True, capture_output=True)
        self.assertEqual(proc.returncode, 1, proc.stderr)
        self.assertIn('unknown USBD.IRX version', proc.stderr)
        self.assertIn(bb_patch.sha1(self.meta['usbd']), proc.stderr)
        self.assertEqual(os.listdir(self.tmp), ['Game.iso'])
        self.assertEqual(rd(self.p), self.original)

    def test_disk_space_checked_before_copy_or_recovery_writes(self):
        with mock.patch.object(bb_patch.shutil, 'disk_usage', return_value=shutil._ntuple_diskusage(1, 1, 0)):
            rc, so, se = run(self.p)
        self.assertEqual(rc, 1)
        self.assertIn('free disk space', se)
        self.assertEqual(os.listdir(self.tmp), ['Game.iso'])

    def test_in_place_backup_exists_before_first_write_and_restores(self):
        original_write = bb_patch.write_extent
        def checked_write(target, rec, before, after):
            backup, side = bb_patch.side_paths(target)
            self.assertEqual(backup.read_bytes(), self.meta['usbd'])
            self.assertEqual(json.loads(side.read_text())['original_sha256'], bb_patch.sha256(before))
            return original_write(target, rec, before, after)
        with mock.patch.object(bb_patch, 'write_extent', side_effect=checked_write):
            rc, so, se = run(self.p, '--in-place', '--layout', '2')
        self.assertEqual(rc, 0, se)
        self.assertFalse(os.path.exists(self.out))
        self.assertNotEqual(rd(self.p), self.original)
        self.assertEqual(run(self.p, '--restore')[0], 0)
        self.assertEqual(rd(self.p), self.original)

    def test_interrupted_in_place_write_can_be_restored(self):
        def interrupted(target, rec, before, after):
            with open(target, 'r+b') as f:
                f.seek(rec['lba'] * SECTOR)
                f.write(after[:500])
            raise OSError('simulated disk failure')
        with mock.patch.object(bb_patch, 'write_extent', side_effect=interrupted):
            self.assertEqual(run(self.p, '--in-place')[0], 1)
        self.assertTrue(os.path.exists(self.p + '.bbpatch.json'))
        rc, so, se = run(self.p, '--restore')
        self.assertEqual(rc, 0, se)
        self.assertEqual(rd(self.p), self.original)

    def test_copy_failure_leaves_no_output_or_recovery(self):
        with mock.patch.object(bb_patch.shutil, 'copyfileobj', side_effect=OSError('simulated disk full')):
            self.assertEqual(run(self.p)[0], 1)
        self.assertEqual(os.listdir(self.tmp), ['Game.iso'])
        self.assertEqual(rd(self.p), self.original)

    def test_recovery_failure_leaves_input_untouched(self):
        with mock.patch.object(bb_patch, 'save_recovery', side_effect=OSError('simulated sidecar failure')):
            self.assertEqual(run(self.p, '--in-place')[0], 1)
        self.assertEqual(rd(self.p), self.original)

    def test_moved_iso_and_sidecars_restore_from_another_directory(self):
        self.assertEqual(run(self.p)[0], 0)
        moved = os.path.join(self.tmp, 'moved')
        os.mkdir(moved)
        target = os.path.join(moved, os.path.basename(self.out))
        for suffix in ('', '.usbd.orig', '.bbpatch.json'):
            shutil.move(self.out + suffix, target + suffix)
        rc, so, se = run('--restore', target)
        self.assertEqual(rc, 0, se)
        self.assertEqual(rd(target), self.original)

    def test_stale_recovery_and_backup_tampering_refused(self):
        self.assertEqual(run(self.p)[0], 0)
        side_path = self.out + '.bbpatch.json'
        side = json.loads(rd(side_path))
        side['lba'] += 1
        with open(side_path, 'w') as f:
            json.dump(side, f)
        before = rd(self.out)
        self.assertEqual(run('--restore', self.out)[0], 1)
        self.assertEqual(rd(self.out), before)
        with open(self.out + '.usbd.orig', 'r+b') as f:
            f.write(b'WRONG')
        self.assertIn('checksum', run('--restore', self.out)[2])

    def test_hard_link_input_as_output_refused(self):
        os.link(self.p, self.out)
        self.assertEqual(run(self.p, '--out', self.out)[0], 1)
        self.assertEqual(rd(self.p), self.original)

    def test_truncated_and_bad_endian_isos_refused(self):
        for data in (b'', bytes(200), self.original[:17 * SECTOR]):
            with open(self.p, 'wb') as f:
                f.write(data)
            self.assertEqual(run(self.p)[0], 1)
        bad = bytearray(self.original)
        bad[16 * SECTOR + 156 + 6] ^= 1
        with open(self.p, 'wb') as f:
            f.write(bad)
        self.assertEqual(run(self.p)[0], 1)
        self.assertFalse(os.path.exists(self.out))

    def test_restoring_from_original_refuses_tampered_module(self):
        self.assertEqual(run(self.p)[0], 0)
        with open(self.out, 'r+b') as f:
            f.seek(self.meta['usbd_lba'] * SECTOR + 100)
            f.write(b'BROKEN')
        before = rd(self.out)
        self.assertEqual(run(self.out, '--restore', '--from-iso', self.p)[0], 1)
        self.assertEqual(rd(self.out), before)

    def test_no_repatch_and_no_conflicting_flags(self):
        self.assertEqual(run(self.p)[0], 0)
        self.assertEqual(run(self.out)[0], 1)
        for flags in (('--out', self.out, '--in-place'), ('--restore', '--out', self.out),
                      ('--info', '--restore'), ('--from-iso', self.p)):
            self.assertEqual(run(self.p, *flags)[0], 2)


if __name__ == '__main__':
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    unittest.main(verbosity=2)
