# Buzz Web Controller

Use phones as Buzz! buzzers for PCSX2, over LAN or over the internet, no
physical hardware and no PCSX2 patching required.

```
phone(s) ──WebSocket──▶ this server ──virtual gamepad──▶ PCSX2 (BuzzDevice)
```

Each connected phone claims a player slot and gets a big red BUZZ button plus
four colored buttons, matching a real Buzz! buzzer. The server turns presses
into a virtual gamepad (ViGEmBus on Windows, `uinput` on Linux). PCSX2 already
supports binding its Buzz-controller emulation (`BuzzDevice`) to *any*
SDL-visible controller -- that's how a real USB buzzer works today -- so a
virtual one works identically. Nothing about PCSX2 itself is modified.

## Why not a PCSX2 plugin?

There isn't one to write. PCSX2 dropped its old plugin-DLL architecture
(separate GS/PAD/SPU2 `.dll`s) in v2.0 -- everything, including `BuzzDevice`,
is compiled into the single binary now. The only supported way to feed it
custom input is exactly what this project does: present a normal
SDL/controller device and use PCSX2's own controller-binding UI.

## Setup

### 1. Install

```
pip install -r requirements.txt
```

- **Windows**: needs [ViGEmBus](https://github.com/nefarius/ViGEmBus/releases)
  installed once (a small, well-known driver; many controller-remap tools
  already ship it -- check `services.msc` for "ViGEmBus" before installing
  again).
- **Linux**: needs `/dev/uinput` access without root. One-time setup:
  ```
  sudo tee /etc/udev/rules.d/99-uinput.rules <<'EOF'
  KERNEL=="uinput", SUBSYSTEM=="misc", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput"
  EOF
  sudo usermod -aG input "$USER"
  sudo modprobe uinput
  sudo udevadm control --reload && sudo udevadm trigger --name-match=uinput
  ```
  Log out/in once for the group change to apply.

### 2. Run the server

```
python -m server.app
```

This prints a LAN URL, e.g. `http://192.168.0.241:8420`. Everyone on the same
Wi-Fi opens that in their phone's browser.

For players **not** on your network, add `--public`:

```
python -m server.app --public
```

This opens a [Cloudflare quick tunnel](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/do-more-with-tunnels/trycloudflare/)
(via `pycloudflared`, which fetches the `cloudflared` binary automatically --
no separate install, no account, no signup) and prints an
`https://...trycloudflare.com` URL alongside the LAN one. It was chosen over
ngrok specifically because it needs no account at all and WebSocket support
is confirmed working end-to-end through it. Internet players will have extra
latency (typically tens to a couple hundred ms) versus LAN, which matters for
a reflex-buzzer game -- worth trying before an actual game night. Quick
tunnels are meant for exactly this: temporary use, not something to leave
running unattended for weeks (each run gets a fresh random URL; there's no
SLA on uptime).

Use `--players N` (1-4) to limit how many slots exist, and `--port` to change
the port.

### 3. Bind PCSX2 to the virtual pads (one-time per machine)

The virtual pads exist as soon as the server starts, even before any phone
joins, so you can bind them immediately:

1. Start the server.
2. In PCSX2: **Settings → Controllers → USB**.
3. Set the port(s) you want to `BuzzDevice`.
4. Click each button field (Red 1, Blue 1, Orange 1, Green 1, Yellow 1, ...)
   and press the corresponding button on a connected phone -- or trigger it
   manually once by having any phone press it. PCSX2 detects the source
   automatically, the same way it would a real controller.
5. Repeat for player 2/3/4 slots if you're playing multiplayer.

Your existing `BuzzDevice_Red1 = SDL-0/RightShoulder` style config still
applies -- this project uses the exact same face-button layout
(RightShoulder/Y/X/A/B) so if you already had player 1 bound to a physical
pad, rebinding to the virtual one is a drop-in swap.

## Notes

- Closing a phone's tab or losing connection immediately releases its slot
  and un-presses all its buttons, so a dropped phone can't leave a button
  stuck down.
- The five buttons are the only input surface, matching real Buzz! hardware
  -- Buzz games navigate menus with the colored buttons themselves.
- `--players` creates that many virtual pads up front; PCSX2 sees them as
  ordinary controllers the moment the server starts, whether or not a phone
  has joined yet.

## License

MIT
