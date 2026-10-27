#!/usr/bin/env python3
"""User entry point for Buzz! Bridge PS2 (stdlib only)."""
import sys

from universal.tools.bb_patch import main
from universal.tools.bb_console import Console


def run():
    # Double-click (no arguments) or drag/drop (one argument) on the exe: keep the
    # console visible for setup, progress and errors. Scripted invocations with
    # flags never pause.
    frozen = getattr(sys, 'frozen', False)
    double_click = frozen and len(sys.argv) == 1
    drag_drop = frozen and len(sys.argv) == 2 and not sys.argv[1].startswith('-')
    console = Console()
    try:
        if double_click:
            if console.enabled:
                console.banner()
            else:
                print('Buzz! Bridge PS2')
            console.line('\nUse an extracted .iso file (not a file inside a .zip).')
            try:
                iso = input('Drag your Buzz! game ISO into this window and press Enter: ').strip().strip('"')
            except (EOFError, KeyboardInterrupt):
                return 1
            if not iso:
                console.error('No ISO given.', 'Drag an extracted .iso into this window.')
                return 1
            return main([iso], console=console)
        return main(console=console)
    finally:
        if double_click or drag_drop:
            try:
                input('\nPress Enter to close.')
            except (EOFError, KeyboardInterrupt):
                pass


if __name__ == '__main__':
    sys.exit(run())
