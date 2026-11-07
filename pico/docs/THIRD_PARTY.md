# Third-party code in the Pico firmware

Task 3 also uses private MIT TinyUSB `hid_device.c` in addition to `usbd.c`
and `usbd_control.c`; the installed SDK remains unchanged. The generator
records normalized source and patched hashes in `tinyusb_runtime/provenance.json`.
New trace, journal, activation and request-adapter code is original bridge code.
Published genuine protocol facts are referenced in
[REFERENCES.md](REFERENCES.md).

The DS3 report descriptor and feature-response wire values in `src/ds3.c`
are based on GP2040-CE's PS3Descriptors.h and PS3Driver.cpp:
https://github.com/OpenStickCommunity/GP2040-CE/tree/main/headers/drivers/ps3
https://github.com/OpenStickCommunity/GP2040-CE/tree/main/src/drivers/ps3

Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
Copyright (c) 2021 Jason Skuby (mytechtoybox.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Private copies of TinyUSB's `usbd.c` and `usbd_control.c` retain their original
MIT notices. Only runtime EP0 packet-size expressions differ; provenance hashes
are recorded beside those copies. No upstream SDK files are modified.

Local lamp policy, Buzz-to-DS3 encoding, and the proposed rumble lamp protocol
are new bridge code. These are not official Sony protocols for controlling
Buzz lamps; only the outer DS3 HID transport follows the reference projects.
