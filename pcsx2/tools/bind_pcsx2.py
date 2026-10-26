#!/usr/bin/env python3
"""One-shot helper: detect which SDL joystick index each Buzz Web Controller
virtual pad landed on, and write the matching BuzzDevice_* bindings straight
into PCSX2.ini.

All four virtual pads look identical to SDL (same name/GUID -- they're all
"Xbox 360 Controller"), so which OS-level index a given player slot gets is
not something to hardcode: it's measured here by hitting the server's
loopback-only /admin/tap/<slot> endpoint (which presses that slot's pad
directly, bypassing the normal join/claim system a phone would use) and
watching pygame -- itself just another SDL client, the same as PCSX2 -- for
the resulting button event. Because it bypasses the claim system, this works
even while a phone already has that slot open; it does not disconnect or
affect that phone's session.

Requires: the server (server/app.py) already running, and PCSX2 closed (so it
can't overwrite this edit when it next saves its own settings).

Usage:
    python tools/bind_pcsx2.py [--base-url http://127.0.0.1:8420] [--players 4]
                                [--ini "C:/Users/you/Documents/PCSX2/inis/PCSX2.ini"]
"""
import argparse
import asyncio
import configparser
import datetime
import pathlib
import shutil
import subprocess
import sys

import aiohttp
import pygame

# Same face-button layout the Windows/Linux backends use -- see
# server/gamepad_windows.py and server/gamepad_linux.py.
BUTTON_ORDER = ['red', 'blue', 'orange', 'green', 'yellow']
SDL_NAME = {
    'red': 'RightShoulder',
    'blue': 'FaceNorth',
    'orange': 'FaceWest',
    'green': 'FaceSouth',
    'yellow': 'FaceEast',
}

DEFAULT_INI = pathlib.Path.home() / 'Documents' / 'PCSX2' / 'inis' / 'PCSX2.ini'


def pcsx2_is_running() -> bool:
    """PCSX2 only writes PCSX2.ini back out when it saves settings (e.g. on
    exit) -- if it's running while we edit the file directly, that save can
    silently discard this script's changes."""
    try:
        if sys.platform == 'win32':
            out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq pcsx2-qt.exe'],
                                 capture_output=True, text=True, timeout=5)
            return 'pcsx2-qt.exe' in out.stdout.lower()
        out = subprocess.run(['pgrep', '-f', 'pcsx2'],
                             capture_output=True, text=True, timeout=5)
        return bool(out.stdout.strip())
    except Exception:
        return False  # best-effort; don't block the script over a check failure


async def identify_slot(session: aiohttp.ClientSession, base_url: str, slot: int,
                        instance_to_index: dict, timeout: float = 2.0):
    """Tap `slot`'s pad via /admin/tap, return the 0-based SDL device index
    that reacted.

    The event only gives an SDL *instance id*, which is not guaranteed to
    equal the 0-based device index PCSX2's "SDL-N" bindings use, so it's
    translated through `instance_to_index` (built once from the live
    Joystick objects) rather than assumed equal.
    """
    pygame.event.pump()
    for ev in pygame.event.get():
        pass  # drain stale events before we start

    async with session.post(f'{base_url}/admin/tap/{slot}') as resp:
        if resp.status == 403:
            raise RuntimeError(
                'server rejected /admin/tap as non-loopback -- run this script '
                'on the same machine as the server.')
        if resp.status == 404:
            return None
        resp.raise_for_status()

    found = None
    deadline = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < deadline and found is None:
        pygame.event.pump()
        for ev in pygame.event.get():
            if ev.type == pygame.JOYBUTTONDOWN:
                instance = ev.instance_id if hasattr(ev, 'instance_id') else ev.joy
                if instance in instance_to_index:
                    found = instance_to_index[instance]
        await asyncio.sleep(0.01)
    return found


async def main_async(args):
    if pcsx2_is_running():
        sys.exit('PCSX2 appears to be running. Close it first -- otherwise it '
                 'may overwrite this edit next time it saves its own settings.')

    pygame.init()
    pygame.joystick.init()
    joys = [pygame.joystick.Joystick(i) for i in range(pygame.joystick.get_count())]
    for j in joys:
        j.init()
    instance_to_index = {j.get_instance_id(): i for i, j in enumerate(joys)}
    print(f'SDL sees {len(joys)} joystick(s) total.\n')

    mapping = {}  # slot -> sdl index
    async with aiohttp.ClientSession() as session:
        for slot in range(1, args.players + 1):
            idx = await identify_slot(session, args.base_url, slot, instance_to_index)
            if idx is None:
                sys.exit(f'slot {slot}: no button event detected within timeout -- '
                         f'is the server still running with --players >= {slot}?')
            mapping[slot] = idx
            print(f'  player {slot}  ->  SDL-{idx}')

    if len(set(mapping.values())) != len(mapping):
        sys.exit(f'\nERROR: two player slots resolved to the same SDL index '
                 f'({mapping}) -- refusing to write ambiguous bindings.')

    ini_path = pathlib.Path(args.ini)
    if not ini_path.exists():
        sys.exit(f'PCSX2.ini not found at {ini_path}')

    backup = ini_path.with_suffix(
        f'.ini.bak-{datetime.datetime.now():%Y%m%d-%H%M%S}')
    shutil.copy2(ini_path, backup)
    print(f'\nBacked up {ini_path.name} -> {backup.name}')

    cfg = configparser.ConfigParser(strict=False)
    cfg.optionxform = str  # preserve key case
    cfg.read(ini_path, encoding='utf-8')

    if not cfg.has_section('USB1'):
        cfg.add_section('USB1')
    cfg.set('USB1', 'Type', 'BuzzDevice')
    for slot, idx in mapping.items():
        for color, sdl_button in SDL_NAME.items():
            cfg.set('USB1', f'BuzzDevice_{color.capitalize()}{slot}',
                    f'SDL-{idx}/{sdl_button}')

    with open(ini_path, 'w', encoding='utf-8') as f:
        cfg.write(f, space_around_delimiters=True)

    print(f'Wrote {len(mapping)} player binding(s) to [USB1] in {ini_path}')
    print('Start PCSX2 -- the bindings are already in place.')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--base-url', default='http://127.0.0.1:8420')
    ap.add_argument('--players', type=int, default=4, choices=range(1, 5))
    ap.add_argument('--ini', default=str(DEFAULT_INI))
    args = ap.parse_args()
    asyncio.run(main_async(args))


if __name__ == '__main__':
    main()
