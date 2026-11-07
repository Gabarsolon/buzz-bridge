"""Pico 2 PICOBOOT fallback for this PC's picotool-without-USB build.

Wire format/ACK sequence from raspberrypi/picotool picoboot_connection.c and
pico-sdk boot/picoboot.h. Uses installed PyUSB/libusb. Never writes a drive.
Only addresses the attached Pico 2 serial and the first MiB of its flash.
"""
import argparse
from pathlib import Path
import struct
import sys
import time
import json
import zlib
import usb.core
import usb.util
from usb_runtime import find

import os
# Optional: set BUZZ_PICO_SERIAL to pick one board when several are in BOOTSEL.
SERIAL = os.environ.get('BUZZ_PICO_SERIAL')
BASE = 0x10000000
ROOT = Path(__file__).resolve().parents[1]

class Boot:
    def __init__(self):
        devices = list(find(find_all=True, idVendor=0x2e8a, idProduct=0x000f))
        if SERIAL:
            devices = [d for d in devices if usb.util.get_string(d, d.iSerialNumber) == SERIAL]
        if len(devices) != 1:
            raise RuntimeError('Expected exactly the known Pico 2 in BOOTSEL')
        self.d = devices[0]
        cfg = self.d.get_active_configuration()
        itfs = [i for i in cfg if i.bInterfaceClass == 0xff and i.bInterfaceSubClass == 0]
        if len(itfs) != 1:
            raise RuntimeError('PICOBOOT interface not uniquely identified')
        self.itf = itfs[0].bInterfaceNumber
        usb.util.claim_interface(self.d, self.itf)
        self.out = next(e.bEndpointAddress for e in itfs[0] if e.bEndpointAddress & 0x80 == 0)
        self.epin = next(e.bEndpointAddress for e in itfs[0] if e.bEndpointAddress & 0x80)
        self.token = 0
        self.d.ctrl_transfer(0x41, 0x41, 0, self.itf, b'')

    def cmd(self, code, args=b'', payload=b'', count=0):
        self.token += 1
        count = count if code & 0x80 else len(payload)
        packet = struct.pack('<IIBBHI16s', 0x431fd10b, self.token, code,
                             len(args), 0, count, args.ljust(16, b'\0'))
        if self.d.write(self.out, packet, timeout=10000) != 32:
            raise RuntimeError('Short PICOBOOT command')
        result = b''
        if count:
            if code & 0x80:
                result = bytes(self.d.read(self.epin, count, timeout=10000))
                if len(result) != count:
                    raise RuntimeError('Short PICOBOOT read')
            elif self.d.write(self.out, payload, timeout=10000) != count:
                raise RuntimeError('Short PICOBOOT write')
        if code & 0x80:
            self.d.write(self.out, b'')
        else:
            self.d.read(self.epin, 1, timeout=10000)
        return result

    def read(self, address, size):
        return self.cmd(0x84, struct.pack('<II', address, size), count=size)

    def reboot(self):
        self.cmd(0xa, struct.pack('<IIII', 0, 500, 0, 0))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', nargs='?', type=Path)
    ap.add_argument('--backup', action='store_true')
    ap.add_argument('--reboot-only', action='store_true')
    ap.add_argument('--read-settings', action='store_true')
    args = ap.parse_args()
    data = None
    if args.binary:
        path = args.binary.resolve()
        if not path.is_relative_to(ROOT) or path.suffix != '.bin':
            raise ValueError('Binary must be a .bin inside the workspace')
        data = path.read_bytes()
        if not 256 <= len(data) < 1024*1024:
            raise ValueError('Unexpected image size')
        data += b'\xff' * (-len(data) % 256)
    b = Boot()
    if args.reboot_only:
        b.reboot(); print('PASS normal reboot'); return
    b.cmd(1, b'\x01')
    try:
        # Match picotool's exit_xip -> enter_cmd_xip setup. Entering command
        # XIP directly after BOOTSEL entry can ACK yet return all-zero reads.
        b.cmd(6)
        b.cmd(7)
        if args.read_settings:
            page = b.read(BASE+4*1024*1024-8192,256)
            magic,version,mode_debug,word3 = struct.unpack('<IIII',page[:16])
            if version==1:
                crc=word3;valid=mode_debug<=3 and zlib.crc32(page[:12])==crc
                mode_index=mode_debug&1;debug=bool(mode_debug&2);local_flags=0;hold_ms=2000
            elif version in (2,3):
                local_flags,hold_ms,crc=struct.unpack('<III',page[12:24])
                mode_index=mode_debug&0xff;debug=bool(mode_debug&0x100)
                valid=mode_index<=2 and not mode_debug&~0x103 and local_flags<=(15 if version==3 else 7) and 100<=hold_ms<=10000 and zlib.crc32(page[:20])==crc
            else:valid=False
            if magic != 0x42555a31 or not valid:raise RuntimeError('Invalid persisted settings or CRC')
            folder = Path(__file__).parent/'dumps'
            folder.mkdir(exist_ok=True)
            label=f'settings_{mode_debug}' if version==1 else f'settings_v{version}_{mode_debug}'
            (folder/f'{label}.bin').write_bytes(page)
            summary = {'version':version,'mode':'ABC'[mode_index], 'debug':debug,
                       'local_enabled':bool(local_flags&1),'local_first':bool(local_flags&2), 'hold_ms':hold_ms,
                       'rumble_probe':bool(local_flags&4),
                       'ds3_auto_ps':bool(local_flags&8) if version==3 else True,
                       'mode_debug':mode_debug, 'crc32':f'{crc:08x}', 'address':'0x103fe000'}
            (folder/f'{label}.json').write_text(json.dumps(summary,indent=2)+'\n')
            print(f'PASS persisted settings readback mode={summary["mode"]} debug={int(summary["debug"])} CRC32 valid')
        if args.backup:
            dest = Path(__file__).parent/'dumps/pre_flash_backup.bin'
            dest.parent.mkdir(exist_ok=True)
            if dest.exists():
                raise FileExistsError('Refusing to replace original flash backup')
            first_page = b.read(BASE,256)
            if first_page in (bytes(256),b'\xff'*256):
                raise RuntimeError('Backup read is blank; refusing to label it a valid backup')
            with dest.open('xb') as f:
                for offset in range(0, 4*1024*1024, 65536):
                    f.write(b.read(BASE+offset, 65536))
            print('PASS current 4 MiB pre-flash backup (not the original agy image)')
        if data is not None:
            b.cmd(6)
            b.cmd(3, struct.pack('<II', BASE, (len(data)+4095)//4096*4096))
            for offset in range(0, len(data), 4096):
                chunk = data[offset:offset+4096]
                b.cmd(5, struct.pack('<II', BASE+offset, len(chunk)), payload=chunk)
            b.cmd(7)
            actual = b''.join(b.read(BASE+off, min(4096, len(data)-off))
                              for off in range(0, len(data), 4096))
            if actual != data:
                raise RuntimeError('Flash readback mismatch; staying in BOOTSEL')
            print(f'PASS flash and byte-exact readback {len(data)} bytes')
    finally:
        b.cmd(1, b'\0')
    if data is not None:
        b.reboot(); print('PASS normal reboot requested')

if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print(f'FAIL {type(e).__name__}: {e}'); sys.exit(1)
