"""Send one line to the bridge's optional CDC interface (auto-detect COM port)."""
import argparse
from pathlib import Path
import time
from datetime import datetime
import serial
import serial.tools.list_ports

HERE = Path(__file__).resolve().parent
# Optional: set BUZZ_PICO_SERIAL to pick one board when several are attached.
import os
SERIAL = os.environ.get('BUZZ_PICO_SERIAL')
LAST_DS3LOG_PATH=None

def port_for_bridge(timeout=8):
    deadline = time.monotonic()+timeout
    while time.monotonic()<deadline:
        ports = [p for p in serial.tools.list_ports.comports()
                 if p.vid == 0xcafe and p.pid in (0xb001, 0xb002, 0xb003)
                 and (not SERIAL or p.serial_number == SERIAL)]
        if len(ports) == 1:
            return ports[0].device
        time.sleep(.15)
    raise RuntimeError('Bridge CDC missing; clean mode has no COM port. See FLASH_AND_TEST.md recovery.')

def send(command, wait=.6, port=None):
    global LAST_DS3LOG_PATH
    if command=='ds3log':wait=max(wait,4)
    with serial.Serial(port or port_for_bridge(), 115200, timeout=.1) as s:
        s.dtr = True
        s.write((command+'\n').encode('ascii')); s.flush()
        data = bytearray()
        deadline = time.monotonic()+wait
        while time.monotonic()<deadline:
            try:
                data.extend(s.read(16384))
                if command=='ds3log' and b'DS3LOG_END' in data:break
            except serial.SerialException:
                break  # configuration/reboot commands deliberately disconnect
    text = data.decode('ascii', errors='replace')
    (HERE/'dumps').mkdir(exist_ok=True)
    with (HERE/'dumps/bridge_cdc.log').open('a', encoding='utf-8') as f:
        f.write(f'\nPC_COMMAND {command}\n{text}')
    if command=='ds3log':
        target=HERE/'dumps'/('modec_ds3log_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.log')
        target.write_text(text,encoding='utf-8')
        LAST_DS3LOG_PATH=target
    return text

def bootsel():
    from usb_runtime import find
    port = port_for_bridge()
    try:
        with serial.Serial(port, 115200, timeout=.2) as s:
            s.baudrate = 1200
            time.sleep(.05)
    except serial.SerialException:
        pass  # older capture firmware disconnected before status ACK
    deadline = time.monotonic()+5
    while time.monotonic()<deadline:
        d = find(idVendor=0x2e8a, idProduct=0x000f)
        if d is not None:
            print('PASS 1200-baud reset; BOOTSEL 2E8A:000F present')
            return
        time.sleep(.1)
    raise RuntimeError('1200-baud reset did not produce BOOTSEL')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('command', nargs='+')
    args = ap.parse_args()
    command = ' '.join(args.command)
    if command == '1200':
        bootsel()
    else:
        text = send(command)
        for line in text.splitlines():
            if line.startswith(('STATUS', 'RAW', 'LOCAL', 'DS3_STATUS','DS3LOG_BANK','DS3LOG_SUMMARY','FAIL', 'CONFIG_', 'COMMANDS', 'TRIAL', 'BOOTSEL')):
                print(line)
        if command=='ds3log':
            if 'DS3LOG_END' not in text:raise RuntimeError('Incomplete ds3log response; saved partial dump, check firmware and debug CDC')
            print('SUMMARY complete DS3 request log saved:',LAST_DS3LOG_PATH)
        else:print('SUMMARY command sent; CDC log saved in dumps/bridge_cdc.log')

if __name__ == '__main__':
    try:main()
    except Exception as e:
        print(f'FAIL {type(e).__name__}: {e}')
        raise SystemExit(1)
