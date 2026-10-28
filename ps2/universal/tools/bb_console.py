"""Small terminal UI for the patcher. No escapes in redirected output."""
import os
import shutil
import sys
import time


COLOURS = ('31', '94', '33', '32', '93')
BUTTONS = ('Red (buzz)', 'Blue', 'Orange', 'Green', 'Yellow')
SHARED = (('L1 / L2', 'R1 / R2'), ('Up', 'Triangle'), ('Left', 'Square'),
          ('Right', 'Circle'), ('Down', 'Cross'))
BUZZERS = (('Square', 'R1'), ('L1', 'Start'), ('Triangle', 'Select'),
           ('Circle', 'R2'), ('Cross', 'L2'))


def enable_windows_vt():
    """Use pointer-sized HANDLEs, including in a 64-bit frozen executable."""
    try:
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetStdHandle.argtypes = [wintypes.DWORD]
        kernel.GetStdHandle.restype = wintypes.HANDLE
        kernel.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.GetConsoleMode.restype = wintypes.BOOL
        kernel.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.SetConsoleMode.restype = wintypes.BOOL
        handle = kernel.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
        mode = wintypes.DWORD()
        return bool(kernel.GetConsoleMode(handle, ctypes.byref(mode)) and
                    kernel.SetConsoleMode(handle, mode.value | 0x0004))
    except (AttributeError, OSError):
        return False


def set_window_title():
    if os.name == 'nt':
        try:
            import ctypes
            ctypes.windll.kernel32.SetConsoleTitleW('Buzz! Bridge PS2')
        except (AttributeError, OSError):
            pass


def human_size(size):
    return '%.2f GB' % (size / 1_000_000_000) if size >= 1_000_000_000 else '%.1f MB' % (size / 1_000_000)


class Console:
    def __init__(self):
        self.stream = sys.stdout
        self.enabled = self.stream.isatty()
        self.colour = (self.enabled and 'NO_COLOR' not in os.environ and
                       (os.name != 'nt' or enable_windows_vt()))
        self.banner_shown = False
        self.pending = False
        if self.enabled:
            set_window_title()

    def paint(self, text, code):
        return '\033[%sm%s\033[0m' % (code, text) if self.colour else text

    def line(self, text=''):
        if self.enabled:
            print(text, file=self.stream, flush=True)

    def banner(self):
        if self.banner_shown:
            return
        self.banner_shown = True
        self.line(self.paint('  Buzz! Bridge PS2', '1'))
        self.line('  ' + '  '.join(self.paint('*', c) for c in COLOURS))
        self.line('  Your Buzz! game. Your controllers.')

    def section(self, number, title):
        self.line('\n' + self.paint('%d / %s' % (number, title), '1;96'))
        self.line('-' * 60)

    def start(self, text):
        if self.enabled:
            print(text + ' ... ', end='', file=self.stream, flush=True)
            self.pending = True

    def done(self):
        self.line(self.paint('done', '32'))
        self.pending = False

    def end_step(self):
        if self.pending:
            self.line()
            self.pending = False

    def setup(self, layout):
        descriptions = (
            'Port 1: wired buzzer handsets 1-2 (PS3 mapping).',
            'Port 1: buzzer handsets 1-2. Port 2: players 3-4 split.',
            'Port 1: players 1-2 split. Port 2: players 3-4 split.',
            'Port 1: player 1. Port 2: player 2. Either half works.',
            'Port 1: players 1-2 split. Port 2: unused.',
            'Multitap on port 1: buzzers in slot 1-A (handsets 1-2), DualShocks in 1-B and 1-C (players 3-4).',
            'Multitap on port 1: DualShocks in slots 1-A to 1-D (players 1-4).',
            'Port 1: buzzers (handsets 1-2). Multitap on port 2: DualShocks in 2-A and 2-B (players 3-4).',
            'Multitaps on ports 1 and 2: DualShocks in 1-A to 1-D (players 1-4) and 2-A to 2-D (players 5-8).',
        )
        self.line('\nSelected: ' + descriptions[layout])
        if layout in (0, 1, 5, 7):
            self.line('PS3 wired buzzers (port 1):')
            self.table('Handset 1', 'Handset 2', BUZZERS)
        if layout >= 5:
            self.line('DualShock buttons (one player per controller, either half works):')
            self.table('Left half', 'Right half', SHARED)
            self.line('Multitap beta: tested in PCSX2 (Settings > Controllers > Global Settings > Multitap).')
            self.line('On a PS3 only slot A works so far.')
        elif layout != 0:
            self.line('DualShock buttons' + (' (port 2, players 3-4):' if layout == 1 else ':'))
            self.table('Left half', 'Right half', SHARED)

    def table(self, left, right, rows):
        self.line('  %-12s %-18s %s' % ('Buzz! button', left, right))
        for button, colour, (a, b) in zip(BUTTONS, COLOURS, rows):
            self.line('  %s %-18s %s' % (self.paint('%-12s' % button, colour), a, b))

    def error(self, error, hint):
        self.end_step()
        # stderr can be redirected independently of stdout.
        message = 'error: %s' % error
        if self.colour and sys.stderr.isatty():
            message = self.paint(message, '31')
        print(message, file=sys.stderr, flush=True)
        print('Hint: ' + hint, file=sys.stderr, flush=True)


def copy_with_progress(src, dst, size, console, clock=None):
    """Bounded-memory copy; rate-limit terminal refreshes to four per second."""
    clock = clock or time.monotonic
    start = last = clock()
    copied = 0
    previous_length = 0
    width = min(24, max(8, shutil.get_terminal_size().columns - 52))

    def render(now, finished=False):
        nonlocal previous_length
        fraction = min(copied / size, 1) if size else 1
        rate = copied / max(now - start, 0.001)
        remaining = max(size - copied, 0) / rate if rate else None
        eta = '--:--' if remaining is None else '%02d:%02d' % divmod(int(remaining), 60)
        filled = int(width * fraction)
        bar = console.paint('#' * filled, '32') + '-' * (width - filled)
        text = 'Copying ISO [%s] %3d%%  %6.1f MB/s  ETA %s' % (bar, fraction * 100, rate / 1_000_000, eta)
        padding = ' ' * max(0, previous_length - len(text))
        previous_length = len(text)
        print('\r' + text + padding,
              end='\n' if finished else '', file=console.stream, flush=True)

    render(start)
    try:
        while True:
            data = src.read(8 * 1024 * 1024)
            if not data:
                break
            if dst.write(data) != len(data):
                raise OSError('short write while copying ISO')
            copied += len(data)
            now = clock()
            if now - last >= 0.25 and copied < size:
                render(now)
                last = now
    except BaseException:
        print(file=console.stream, flush=True)
        raise
    render(clock(), finished=True)
