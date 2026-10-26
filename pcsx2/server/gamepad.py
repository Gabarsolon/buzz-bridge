"""Picks the right virtual-gamepad backend for the current OS.

Both backends expose the same tiny interface (press/release/release_all/close)
and the same five button names, so the rest of the server never needs to know
which platform it's on.
"""
import sys

BUTTONS = ('red', 'blue', 'orange', 'green', 'yellow')


def make_pad():
    if sys.platform == 'win32':
        from . import gamepad_windows as backend
    elif sys.platform.startswith('linux'):
        from . import gamepad_linux as backend
    else:
        raise RuntimeError(
            f"No virtual-gamepad backend for platform {sys.platform!r}. "
            "Supported: Windows (ViGEmBus) and Linux (uinput).")
    if not backend.available():
        raise RuntimeError(backend.__doc__.strip().splitlines()[0] +
                          " -- backend reported itself unavailable; see README.")
    return backend.VirtualPad()
