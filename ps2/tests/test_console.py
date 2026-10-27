"""Console rendering and synthetic-only integration checks."""
import contextlib
import ctypes
import errno
import io
import os
import shutil
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'universal' / 'tests'))
from test_patcher import bb_patch, build_iso, run  # noqa: E402
import bb_console  # noqa: E402


class Terminal(io.StringIO):
    def isatty(self):
        return True


class ConsoleTests(unittest.TestCase):
    def setUp(self):
        base = Path(__file__).resolve().parents[1] / 'build' / 'console-tests'
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.iso = Path(self.temp.name) / 'Synthetic.iso'
        self.meta = build_iso(self.iso, 'IOP', 34985)
        self.original = self.iso.read_bytes()

    def terminal_run(self, *flags, answers=('2',), colour=False):
        out, err = Terminal(), Terminal()
        env = {} if colour else {'NO_COLOR': ''}
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), \
                mock.patch.dict(os.environ, env, clear=True), \
                mock.patch.object(bb_console, 'enable_windows_vt', return_value=True), \
                mock.patch.object(bb_console, 'set_window_title'), \
                mock.patch.dict(bb_patch.KNOWN_USBD, {bb_patch.sha1(self.meta['usbd']): 'Synthetic USBD 1.3.0'}), \
                mock.patch('builtins.input', side_effect=answers):
            rc = bb_patch.main([str(self.iso), *flags])
        return rc, out.getvalue(), err.getvalue()

    def test_plain_success_output_is_unchanged(self):
        rc, out, err = run(str(self.iso), '--layout', '2', '--quiet')
        target = self.iso.with_name('Synthetic (Buzz Bridge).iso')
        self.assertEqual(rc, 0, err)
        self.assertEqual(out, '%s: /IOP/USBD.IRX (synthetic test fixture)\n'
                         'Patched: %s\n'
                         'Layout 2 (two_ds_shared), poll auto. Keep both recovery files beside the ISO:\n'
                         '  %s.usbd.orig\n  %s.bbpatch.json\n' %
                         (self.iso, target, target, target))
        self.assertEqual(err, '')

    def test_full_terminal_flow_and_input_preserved(self):
        rc, out, err = self.terminal_run()
        self.assertEqual(rc, 0, err)
        steps = ('Buzz! Bridge PS2', '1 / Your game', 'Game / volume: BBTEST',
                 'Synthetic USBD 1.3.0', 'ISO size:', '2 / Your setup',
                 'Selected:', 'DualShock buttons (port 2, players 3-4):',
                 '3 / Patching', 'Patching the ISO now.', 'Copying the ISO',
                 'Copying ISO [', '100%', 'MB/s', 'ETA', 'Flushing ISO copy to disk ... done',
                 'Writing Buzz! Bridge module ... done', 'Saving recovery files ... done',
                 'Verifying ... done', 'Finishing patched ISO ... done', '4 / Done',
                 'Patched:', 'PCSX2:', 'PS3 CFW (untested):', 'PS3 HEN (untested):', 'Undo:')
        previous = -1
        for text in steps:
            position = out.index(text, previous + 1)
            self.assertGreater(position, previous, text)
            previous = position
        self.assertNotIn('\033', out)
        self.assertEqual(self.iso.read_bytes(), self.original)

    def test_every_layout_shows_its_own_buttons(self):
        for layout in range(5):
            target = Path(self.temp.name) / ('layout%d.iso' % layout)
            rc, out, err = self.terminal_run('--layout', str(layout), '--out', str(target))
            self.assertEqual(rc, 0, err)
            self.assertIn('Red (buzz)', out)
            self.assertEqual('PS3 wired buzzers (port 1):' in out, layout in (0, 1))
            self.assertEqual('DualShock buttons' in out, layout != 0)
            if layout == 3:
                self.assertIn('Either half works.', out)

    def test_colour_only_when_tty_and_vt_available(self):
        rc, out, err = self.terminal_run('--layout', '4', colour=True)
        self.assertEqual(rc, 0, err)
        for code in bb_console.COLOURS:
            self.assertIn('\033[%sm' % code, out)
        with contextlib.redirect_stdout(io.StringIO()), mock.patch.dict(os.environ, {}, clear=True):
            console = bb_console.Console()
            self.assertFalse(console.enabled)
            self.assertFalse(console.colour)
        with contextlib.redirect_stdout(Terminal()), \
                mock.patch.dict(os.environ, {}, clear=True), \
                mock.patch.object(bb_console, 'enable_windows_vt', return_value=False), \
                mock.patch.object(bb_console, 'set_window_title'):
            console = bb_console.Console()
            self.assertTrue(console.enabled)
            if os.name == 'nt':
                self.assertFalse(console.colour)

    def test_windows_vt_api_uses_existing_mode_and_falls_back(self):
        kernel = mock.Mock()
        kernel.GetStdHandle.return_value = 123

        def get_mode(handle, mode):
            mode._obj.value = 0x0001
            return True

        kernel.GetConsoleMode.side_effect = get_mode
        kernel.SetConsoleMode.return_value = True
        with mock.patch.object(ctypes, 'WinDLL', return_value=kernel, create=True):
            self.assertTrue(bb_console.enable_windows_vt())
            kernel.SetConsoleMode.assert_called_once_with(123, 0x0005)
            kernel.GetConsoleMode.side_effect = None
            kernel.GetConsoleMode.return_value = False
            self.assertFalse(bb_console.enable_windows_vt())
        with mock.patch.object(ctypes, 'WinDLL', side_effect=OSError('unavailable'), create=True):
            self.assertFalse(bb_console.enable_windows_vt())

    def test_zip_and_missing_paths_have_exact_extraction_message(self):
        zipped = Path(self.temp.name) / 'Buzz.ZIP'
        zipped.write_bytes(b'PK')
        for path in (zipped, str(zipped) + '\\Game.iso',
                     str(zipped) + '/Game.iso', Path(self.temp.name) / 'missing.iso'):
            rc, out, err = run(str(path))
            self.assertEqual(rc, 1)
            self.assertIn(bb_patch.ZIP_HINT, err)
            self.assertNotIn('Traceback', err)
            self.assertNotIn('\033', out + err)
        self.assertEqual(self.iso.read_bytes(), self.original)

    def test_error_hints_and_red_terminal_error(self):
        with mock.patch.object(bb_patch, 'require_space', side_effect=bb_patch.PatchError(
                'not enough free disk space: need 4000000000 bytes, have 10 bytes')):
            rc, out, err = self.terminal_run('--layout', '0', colour=True)
        self.assertEqual(rc, 1)
        self.assertIn('\033[31merror:', err)
        self.assertIn('need 4000000000 bytes, have 10 bytes', err)
        self.assertIn('--out', err)
        self.assertIn('another drive', err)
        self.assertIn('read-only', bb_patch.error_hint(PermissionError('denied')))
        self.assertIn('File in use', bb_patch.error_hint(OSError(errno.EBUSY, 'busy')))
        busy = OSError('busy')
        busy.winerror = 32
        self.assertIn('File in use', bb_patch.error_hint(busy))
        # run() allowlists the usual fixtures; use deliberately unknown bytes.
        unknown = Path(self.temp.name) / 'unknown.iso'
        build_iso(unknown, 'IOP', 34985, usbd_data=b'unknown' * 5000)
        self.assertIn('PORTING.md', run(str(unknown), '--layout', '0')[2])

    def test_sidecar_verification_failure_removes_copy_and_preserves_input(self):
        save = bb_patch.save_recovery

        def corrupted(*args):
            made = save(*args)
            made[0].write_bytes(b'corrupted')
            return made

        with mock.patch.object(bb_patch, 'save_recovery', side_effect=corrupted):
            rc, out, err = run(str(self.iso))
        self.assertEqual(rc, 1)
        self.assertIn('recovery verification failed', err)
        self.assertEqual(list(Path(self.temp.name).iterdir()), [self.iso])
        self.assertEqual(self.iso.read_bytes(), self.original)

    def test_terminal_copy_failure_cleans_partial(self):
        def fail(src, dst, size, console):
            dst.write(b'partial')
            raise OSError('synthetic copy failure')

        with mock.patch.object(bb_patch, 'copy_with_progress', side_effect=fail):
            rc, out, err = self.terminal_run()
        self.assertEqual(rc, 1)
        self.assertNotIn('4 / Done', out)
        self.assertIn('synthetic copy failure', err)
        self.assertEqual(list(Path(self.temp.name).iterdir()), [self.iso])

    def test_disk_fills_during_copy_reports_needed_free_and_cleans_up(self):
        usage = [shutil._ntuple_diskusage(10**12, 0, 10**12),
                 shutil._ntuple_diskusage(10**12, 10**12, 0)]
        with mock.patch.object(bb_patch.shutil, 'disk_usage', side_effect=usage), \
                mock.patch.object(bb_patch.shutil, 'copyfileobj', side_effect=OSError(errno.ENOSPC, 'disk full')):
            rc, out, err = run(str(self.iso))
        self.assertEqual(rc, 1)
        self.assertIn('need %d bytes' % (len(self.original) + 34985 * 3 + 65536), err)
        self.assertIn('have 0 bytes', err)
        self.assertIn('--out', err)
        self.assertEqual(list(Path(self.temp.name).iterdir()), [self.iso])

    def test_terminal_in_place_skips_copy_and_can_restore(self):
        rc, out, err = self.terminal_run('--in-place', '--layout', '3')
        self.assertEqual(rc, 0, err)
        self.assertNotIn('Copying ISO [', out)
        self.assertLess(out.index('Saving recovery files'), out.index('Writing Buzz! Bridge module'))
        self.assertIn('Verifying ... done', out)
        self.assertEqual(run(str(self.iso), '--restore')[0], 0)
        self.assertEqual(self.iso.read_bytes(), self.original)


class ProgressTests(unittest.TestCase):
    def test_fake_four_gb_copy_is_bounded_throttled_and_complete(self):
        size = 4_000_000_000
        block = bytes(8 * 1024 * 1024)

        class FakeSource:
            remaining = size

            def read(self, count):
                self.assert_bounded(count)
                amount = min(self.remaining, count)
                self.remaining -= amount
                return block[:amount]

            def assert_bounded(self, count):
                assert count <= len(block)

        class FakeDestination:
            copied = 0

            def write(self, data):
                self.copied += len(data)
                return len(data)

        out = Terminal()
        with contextlib.redirect_stdout(out), mock.patch.dict(os.environ, {'NO_COLOR': ''}), \
                mock.patch.object(bb_console, 'set_window_title'):
            console = bb_console.Console()
        ticks = iter(i * 0.05 for i in range(1000))
        dst = FakeDestination()
        bb_console.copy_with_progress(FakeSource(), dst, size, console, clock=lambda: next(ticks))
        self.assertEqual(dst.copied, size)
        output = out.getvalue()
        self.assertIn('  0%', output)
        self.assertIn('100%', output)
        self.assertIn('MB/s', output)
        self.assertIn('ETA 00:00', output)
        self.assertNotIn('\033', output)
        self.assertEqual(output.count('\n'), 1)
        frames = output.split('\r')[1:]
        self.assertGreater(len(frames), 20)
        self.assertLess(len(frames), 110)
        percentages = [int(frame.split('] ')[1].split('%')[0]) for frame in frames]
        self.assertEqual(percentages, sorted(percentages))

    def test_short_write_and_read_failure_end_progress_line(self):
        for src, dst in ((io.BytesIO(b'data'), mock.Mock(write=mock.Mock(return_value=2))),
                         (mock.Mock(read=mock.Mock(side_effect=OSError('read failure'))), io.BytesIO())):
            with self.subTest(src=type(src).__name__):
                out = Terminal()
                with contextlib.redirect_stdout(out), mock.patch.dict(os.environ, {'NO_COLOR': ''}), \
                        mock.patch.object(bb_console, 'set_window_title'):
                    console = bb_console.Console()
                with self.assertRaises(OSError):
                    bb_console.copy_with_progress(src, dst, 4, console)
                self.assertTrue(out.getvalue().endswith('\n'))
                self.assertNotIn('100%', out.getvalue())


if __name__ == '__main__':
    unittest.main()
