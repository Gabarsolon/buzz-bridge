#!/usr/bin/env python3
"""Buzz Web Controller server.

Serves a phone-friendly buzzer page and turns WebSocket button presses into
virtual-gamepad input, which PCSX2's BuzzDevice binds to like any other
controller. See README.md for the PCSX2-side setup (one-time button binding).

Usage:
    python -m server.app                  # LAN only, port 8420
    python -m server.app --port 9000
    python -m server.app --public         # also open an ngrok tunnel (WAN)
    python -m server.app --players 2      # fewer than 4 virtual pads
"""
import argparse
import asyncio
import json
import logging
import pathlib
import socket
import sys

from aiohttp import web, WSMsgType

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from server import gamepad

log = logging.getLogger('buzz-web-controller')
STATIC_DIR = pathlib.Path(__file__).resolve().parent / 'static'


class Hub:
    """Owns the virtual pads and which WebSocket currently holds each slot."""

    def __init__(self, num_players: int):
        self.pads = {slot: gamepad.make_pad() for slot in range(1, num_players + 1)}
        self.holders: dict[int, web.WebSocketResponse] = {}

    def status(self):
        return {str(slot): (slot in self.holders) for slot in self.pads}

    async def broadcast_status(self):
        msg = json.dumps({'type': 'status', 'slots': self.status()})
        dead = []
        for ws in list(self.holders.values()):
            if ws.closed:
                continue
            try:
                await ws.send_str(msg)
            except ConnectionResetError:
                dead.append(ws)

    def claim(self, slot: int, ws: web.WebSocketResponse) -> bool:
        if slot not in self.pads or slot in self.holders:
            return False
        self.holders[slot] = ws
        return True

    def release(self, ws: web.WebSocketResponse):
        for slot, holder in list(self.holders.items()):
            if holder is ws:
                self.pads[slot].release_all()
                del self.holders[slot]


routes = web.RouteTableDef()


@routes.post('/admin/tap/{slot}')
async def admin_tap(request):
    """Press-and-release red on a pad directly, bypassing the slot-claim
    system entirely. For tools/bind_pcsx2.py: identifying which SDL index a
    slot's virtual pad landed on must work even while a phone already holds
    that slot, and this doesn't touch hub.holders or affect that phone's
    session. Loopback-only -- this must never be reachable from the LAN/WAN,
    since it lets anyone who can reach it press any player's button."""
    if request.remote not in ('127.0.0.1', '::1'):
        return web.Response(status=403, text='admin endpoint is loopback-only')
    hub: Hub = request.app['hub']
    try:
        slot = int(request.match_info['slot'])
    except ValueError:
        return web.Response(status=400)
    pad = hub.pads.get(slot)
    if pad is None:
        return web.Response(status=404, text=f'no such slot: {slot}')
    pad.press('red')
    await asyncio.sleep(0.15)
    pad.release('red')
    return web.Response(status=204)


@routes.get('/')
async def index(request):
    return web.FileResponse(STATIC_DIR / 'index.html')


@routes.get('/ws')
async def ws_handler(request):
    hub: Hub = request.app['hub']
    ws = web.WebSocketResponse(heartbeat=20)
    await ws.prepare(request)
    my_slot = None

    async def send(obj):
        if not ws.closed:
            await ws.send_str(json.dumps(obj))

    await send({'type': 'status', 'slots': hub.status(),
               'players': len(hub.pads)})

    async for msg in ws:
        if msg.type != WSMsgType.TEXT:
            continue
        try:
            data = json.loads(msg.data)
        except ValueError:
            continue

        kind = data.get('type')
        if kind == 'join':
            slot = int(data.get('slot', 0))
            if hub.claim(slot, ws):
                my_slot = slot
                await send({'type': 'joined', 'slot': slot})
                await hub.broadcast_status()
            else:
                await send({'type': 'join_rejected', 'slot': slot})
        elif kind in ('down', 'up') and my_slot is not None:
            button = data.get('button')
            if button in gamepad.BUTTONS:
                pad = hub.pads[my_slot]
                (pad.press if kind == 'down' else pad.release)(button)

    hub.release(ws)
    await hub.broadcast_status()
    return ws


def lan_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        return s.getsockname()[0]
    except OSError:
        return '127.0.0.1'
    finally:
        s.close()


def build_app(num_players: int) -> web.Application:
    app = web.Application()
    app['hub'] = Hub(num_players)
    app.add_routes(routes)
    app.router.add_static('/static/', STATIC_DIR, name='static')
    return app


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=8420)
    ap.add_argument('--players', type=int, default=4, choices=range(1, 5))
    ap.add_argument('--public', action='store_true',
                    help='Also open a Cloudflare quick tunnel so players can join over the internet.')
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO, format='%(message)s')
    app = build_app(args.players)

    public_url = None
    tunnel_process = None
    if args.public:
        from pycloudflared import try_cloudflare
        urls = try_cloudflare(port=args.port, verbose=False)
        public_url = urls.tunnel
        tunnel_process = urls.process

    ip = lan_ip()
    log.info('Buzz Web Controller -- %d player slot(s)', args.players)
    log.info('  LAN:  http://%s:%d', ip, args.port)
    if public_url:
        log.info('  WAN:  %s  (Cloudflare quick tunnel; latency will be higher than LAN)', public_url)
    else:
        log.info('  (run with --public to also allow players over the internet)')
    log.info('Open PCSX2 > Settings > Controllers > USB and bind BuzzDevice to these pads once players join.')

    try:
        web.run_app(app, host='0.0.0.0', port=args.port, print=None)
    finally:
        if tunnel_process is not None:
            tunnel_process.terminate()


if __name__ == '__main__':
    main()
