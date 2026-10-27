"""Buzz! Pad Bridge - patch your own copy of a PS2 Buzz! game so it reads the
buzzers from the controller port.

The PS3's PS2 software emulator (ps2_netemu, used by every slim and super slim)
does not hand the Buzz! USB dongle to the game; it turns it into a DualShock.
This patch makes the game accept that controller as buzzers.

usage:
  buzz_pad_bridge.py <game.iso>             patch the ISO in place
  buzz_pad_bridge.py <game.iso> --check     only report whether it is patched
  buzz_pad_bridge.py <game.iso> --restore   undo the patch
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

VERSION = '0.2.0'
SECTOR = 2048

# Versions verified on hardware/PCSX2. Auto-detection never adds to this list.
#   trc   : the "are the buzzers connected" check behind the "buzzers not detected" screen
#   poll  : the buzzer poll that asks the IOP buzzer driver for reports; replaced by the bridge
#   pad1/2: scePadRead data of controller ports 1 and 2
#   orig  : original words at trc (3) and poll (len(bridge_words)), used to verify the version
GAMES = [
    dict(title='Buzz! The Schools Quiz (UK)', serial='SCES-54941', version='Original v2.00',
         redump_sha1='b3862f09cf0be9caa1ccf88dae5ab8de6691b31b', elf='SCES_549.41',
         trc=0x202A60, poll=0x275B00, pad1=0x4D5F20, pad2=0x4D5FB4,
         orig=[0x27BDFF70, 0x3C02004B, 0x7FB40040,
               0x27BDFFC0, 0x7FB00030, 0x0080802D, 0x7FB10020, 0x7FB20010, 0xFFBF0000, 0x0C09D664, 0x24110001,
               0x0000902D, 0x26030024, 0x24040001, 0x00000000, 0x80620000, 0x2484FFFF, 0x2463002C, 0x00021027,
               0x0481FFFB, 0x0002880B, 0x16200016, 0x0000102D, 0x26030020, 0x26040078, 0x8C620000, 0x00000000,
               0x50400004, 0x2463002C, 0xAC600000, 0x24120001, 0x2463002C, 0x0064102A, 0x5440FFF9, 0x8C620000,
               0x12400005, 0x00000000, 0x0C09D78C, 0x0200202D, 0x10000004, 0x24020001, 0x0C09D724, 0x0200202D,
               0x24020001, 0x7BB00030, 0x7BB10020, 0x7BB20010, 0xDFBF0000, 0x03E00008, 0x27BD0040]),
]


def trc_words(counter=0x30):
    """sw zero,0x30(a0) / jr ra / addiu v0,zero,1: report the buzzers as connected."""
    return [0xAC800000 | counter, 0x03E00008, 0x24020001]


def bridge_words(pad1, pad2, layout=None):
    """MIPS code replacing the buzzer poll (a0 = buzzer state).

    Marks device 0 connected, reads both controller ports, maps the buttons with
    ps2_netemu's Buzz! layout back to buzzer bits (handsets 1-2, handset 3 red/yellow),
    adds the handset-present flags (bits 20-22) and stores the report.
    """
    hi = (pad1 + 0x8000) >> 16

    def lo(addr):
        off = addr - (hi << 16)
        if not -0x8000 <= off < 0x8000:
            raise ValueError('pad1 and pad2 must lie within 32 KB of each other')
        return off & 0xFFFF

    fields = dict(status=0x24, index=0x58, count=0, slot=4, report=0x18, flag_shift=20)
    fields.update(layout or {})
    for key in ('status', 'index', 'count', 'slot', 'report'):
        if not 0 <= fields[key] < 0x8000:
            raise ValueError(f'{key} offset cannot be encoded in the bridge')
    flags = 7 << fields['flag_shift']
    if flags & 0xFFFF or flags > 0x7FFFFFFF:
        raise ValueError('handset flags cannot be encoded in the bridge')
    return [0xA0800000 | fields['status'], 0x24020001,
            0xAC800000 | fields['index'], 0xAC820000 | fields['count'],
            0x3C080000 | hi, 0x0000482D,                                # lui t0,hi ; t1 = 0
            0x910C0000 | lo(pad1 + 1), 0x11800002,                     # pad 1 present?
            0x950A0000 | lo(pad1 + 2), 0x3949FFFF,                     # t1 = ~buttons
            0x910C0000 | lo(pad2 + 1), 0x11800003,                     # pad 2 present?
            0x950A0000 | lo(pad2 + 2), 0x394AFFFF, 0x012A4825,         # t1 |= ~buttons
            0x000953C2, 0x31430001,                                     # SQUARE -> bit 0
            0x00095342, 0x314A0002, 0x006A1825,                         # CROSS -> 1
            0x000952C2, 0x314A0004, 0x006A1825,                         # CIRCLE -> 2
            0x00095242, 0x314A0008, 0x006A1825,                         # TRIANGLE -> 3
            0x00095182, 0x314A0030, 0x006A1825,                         # L1, R1 -> 4, 5
            0x00095082, 0x314A00C0, 0x006A1825,                         # L2, R2 -> 6, 7
            0x00095200, 0x314A0100, 0x006A1825,                         # SELECT -> 8
            0x00095180, 0x314A0200, 0x006A1825,                         # START -> 9
            0x00095240, 0x314A0C00, 0x006A1825,                         # L3, R3 -> 10, 11
            0x3C0A0000 | (flags >> 16), 0x006A1825,                     # handsets 1-3 present
            0xAC830000 | fields['slot'], 0xAC830000 | fields['report'],
            0x03E00008, 0x24020001]                                     # return 1


def patch_table(game):
    new = trc_words(game.get('counter', 0x30)) + bridge_words(game['pad1'], game['pad2'], game.get('layout'))
    addrs = [game['trc'] + 4 * i for i in range(3)] + [game['poll'] + 4 * i for i in range(len(new) - 3)]
    if len(game['orig']) != len(new):
        raise ValueError('Original-word count does not match the generated patch')
    if len(set(addrs)) != len(addrs):
        raise ValueError('Patch regions overlap')
    return list(zip(addrs, game['orig'], new))


class DetectionError(ValueError):
    """An unsupported, ambiguous or inconsistent executable; never safe to write."""


def signed16(word):
    return (word & 0x7FFF) - (word & 0x8000)


def pattern(spec):
    """Instruction masks: ? wildcards an immediate nibble; JAL masks only its target."""
    result = []
    for token in spec.split():
        if token == 'JAL':
            result.append((0x0C000000, 0xFC000000))
        else:
            result.append((int(token.replace('?', '0'), 16),
                           int(''.join('0' if c == '?' else 'F' for c in token), 16)))
    return result


class ElfImage:
    """Bounds-checked ELF32 LE MIPS mapping, including multiple PT_LOAD segments."""
    def __init__(self, data):
        self.data = data
        if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
            raise DetectionError('Expected an ELF32 little-endian executable')
        if struct.unpack_from('<HH', data, 16) != (2, 8):
            raise DetectionError('Expected an executable MIPS ELF')
        phoff = struct.unpack_from('<I', data, 28)[0]
        entsize, count = struct.unpack_from('<HH', data, 42)
        if entsize != 32 or not count or phoff + count * 32 > len(data):
            raise DetectionError('Invalid ELF program headers')
        self.segments = []
        for i in range(count):
            typ, off, va, _, size, memsize, flags, _ = struct.unpack_from('<8I', data, phoff + i * 32)
            if typ == 1:
                if size > memsize or off + size > len(data) or va + memsize > 0x2000000:
                    raise DetectionError('Invalid or out-of-EE-RAM load segment')
                if any(va < v + m and v < va + memsize for v, o, s, m, fl in self.segments):
                    raise DetectionError('Overlapping ELF load segments')
                self.segments.append((va, off, size, memsize, flags))
        if not self.segments:
            raise DetectionError('ELF has no loadable segments')

    def offset(self, va, size=4, executable=False):
        for v, o, s, m, flags in self.segments:
            if v <= va and va + size <= v + s and (not executable or flags & 1):
                return o + va - v
        raise DetectionError(f'Address {va:#x} (+{size:#x}) is outside file-backed ELF code/data')

    def memory(self, va, size=4):
        if not any(v <= va and va + size <= v + m and fl & 2 for v, o, s, m, fl in self.segments):
            raise DetectionError(f'Buffer {va:#x} (+{size:#x}) is outside writable ELF memory')
        return va

    def words(self, va, count):
        return list(struct.unpack_from(f'<{count}I', self.data, self.offset(va, count * 4, True)))

    def expect(self, va, pat, name):
        words = self.words(va, len(pat))
        for i, (w, (value, mask)) in enumerate(zip(words, pat)):
            if w & mask != value:
                raise DetectionError(f'{name}: unexpected instruction {w:08X} at {va + 4*i:#x}')
        return words

    def unique(self, pat, name):
        # Search using the longest exact run, then verify every masked instruction.
        runs, start = [], 0
        for i in range(len(pat) + 1):
            if i == len(pat) or pat[i][1] != 0xFFFFFFFF:
                if i > start:
                    runs.append((i - start, start))
                start = i + 1
        if not runs:
            raise ValueError('Signature needs an exact instruction anchor')
        length, start = max(runs)
        anchor = struct.pack(f'<{length}I', *(p[0] for p in pat[start:start+length]))
        hits = []
        for v, off, size, _, flags in self.segments:
            if not flags & 1:
                continue
            pos = off
            while True:
                pos = self.data.find(anchor, pos, off + size)
                if pos < 0:
                    break
                candidate = pos - start * 4
                if candidate >= off and candidate % 4 == 0 and candidate + len(pat) * 4 <= off + size:
                    words = struct.unpack_from(f'<{len(pat)}I', self.data, candidate)
                    if all(w & mask == val for w, (val, mask) in zip(words, pat)):
                        hits.append(v + candidate - off)
                pos += 1
        if len(hits) != 1:
            raise DetectionError(f'{name}: expected exactly one signature match, found {len(hits)}')
        return hits[0], self.words(hits[0], len(pat))


def call_target(pc, word):
    if word >> 26 != 3:
        raise DetectionError(f'Expected jal at {pc:#x}')
    return ((pc + 4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)


def require(condition, message):
    if not condition:
        raise DetectionError(message)


def detect_game(data, elf='unknown'):
    """Detect the observed Relentless code family, independent of serial/address.

    Unsupported compiler/code layouts deliberately fail closed. Every overwritten
    instruction is covered by a mask and all variable fields are cross-checked.
    This operates on ORIGINAL bytes; unknown patched builds need their sidecar.
    """
    image = ElfImage(data)
    bind, bw = image.unique(pattern('''
        3C055253 0040202D 34A53030 JAL 0000302D
        8E040060 8C820024 1040000B 00000000 24040040 JAL 24050800
    '''), 'buzzer RPC bind 0x52533030')

    # Full poll template, including its return delay slot. Only JAL targets and
    # the explicitly derived device offsets are variable, not arbitrary words.
    poll_pattern = [(w, 0xFFFFFFFF) for w in GAMES[0]['orig'][3:]]
    for i in (6, 34, 38):
        poll_pattern[i] = (0x0C000000, 0xFC000000)
    for i in (9, 14, 20, 21, 25, 28):
        poll_pattern[i] = (poll_pattern[i][0] & 0xFFFF0000, 0xFFFF0000)
    poll, _ = image.unique(poll_pattern[:9], 'buzzer poll prologue')
    for i in range(0x100):
        if image.words(poll + i * 4, 1)[0] == 0x03E00008:
            require(i + 2 >= 47, f'Bridge (47 words) does not fit before poll return at {poll + i*4:#x}')
            break
    else:
        raise DetectionError('Poll has no return within 0x400 bytes')
    full_poll, pw = image.unique(poll_pattern, 'complete buzzer poll (47 words)')
    require(full_poll == poll, 'Poll prologue and complete function disagree')
    status, stride, lights = (pw[i] & 0xFFFF for i in (9, 14, 20))
    require(0 < stride <= 0x100 and pw[25] & 0xFFFF == stride and pw[28] & 0xFFFF == stride,
            'Poll device strides disagree')
    require(pw[21] & 0xFFFF == lights + 2 * stride, 'Poll device-loop bound disagrees with stride')
    update = call_target(poll + 24, pw[6])
    fetch = call_target(poll + 152, pw[38])
    light_call = call_target(poll + 136, pw[34])

    # Follow the poll's actual calls, then insist each anchor is unique globally.
    status_at, sw = image.unique(pattern('''
        8202???? 14450019 00821021 00921021 90430000 5060001B 26520001
        A212???? 0220202D JAL 0240282D AE00????
    '''), 'buzzer status device loop')
    uw = image.expect(update, pattern('''
        27BDFF90 7FB10050 0080882D 7FB00060 7FB20040 7FB30030 0220802D
        7FB40020 0000902D 7FB50010 0000A02D FFBF0000 JAL 24150001
        2613001C 8E230064 2405FFFF 8C620004 24640002 AE220068
    '''), 'poll status-update target')
    require(status_at == update + len(uw) * 4 and sw[0] & 0xFFFF == status
            and sw[7] & 0xFFFF == status, 'Poll/status-update connection disagrees')
    rpc_status = call_target(update + 48, uw[12])
    rw = image.expect(rpc_status, pattern('''
        27BDFFE0 24050001 FFBF0010 0000302D AFA00000 0000382D 0000402D
        240A0008 8C890064 0000582D JAL 8C840060 DFBF0010 24020001 03E00008 27BD0020
    '''), 'RPC command 1')
    sif_call = call_target(rpc_status + 40, rw[10])
    fetch_at, fw = image.unique(pattern('''
        27BDFFC0 0000402D 7FB00030 0000582D 7FB10020 0080802D FFBF0010
        24050003 AFA00000 0000302D 0000382D 240A0030 8E040060 JAL 8E090064
        8E0B0064 24110018 2419???? 2418???? 260E???? 240F0018 260D???? 256C0004
    '''), 'RPC command 3/report fetch')
    require(fetch == fetch_at and call_target(fetch + 52, fw[13]) == sif_call,
            'Poll report-fetch/RPC target disagrees')
    require(fw[17] & 0xFFFF == stride and fw[18] & 0xFFFF == stride,
            'Report-fetch device stride disagrees')
    report, slot = fw[19] & 0xFFFF, fw[21] & 0xFFFF
    count_words = image.expect(fetch + 0x84, pattern('00502021 AC83???? 90A20001'), 'report count store')
    count = count_words[1] & 0xFFFF
    image.expect(fetch + 0xC0, pattern('AC820000 ACA20000'), 'slot/latest report stores')
    if image.words(light_call, 1)[0] == 0x27BDFFD0:
        lw = image.expect(light_call, pattern('''
            27BDFFD0 7FB10010 7FB00020 0080882D FFBF0000 JAL 0000802D
            0200282D JAL 0220202D 26100001 2A020002 1440FFFB 0200282D
            7BB00020 24020001 7BB10010 DFBF0000 03E00008 27BD0030
        '''), 'per-device lights wrapper')
        require(call_target(light_call + 20, lw[5]) == fetch, 'Lights wrapper report-fetch disagrees')
    else:
        image.expect(light_call, pattern('27BDFFC0 0000302D 7FB00030 240B????'), 'batch lights target')
        require(image.words(light_call + 12, 1)[0] & 0xFFFF == stride, 'Lights device stride disagrees')
        lc = image.expect(light_call + 0x78, pattern('''
            0000302D 8E040060 0000582D 24050005 AFA00000 2408000A 00E0482D JAL 240A0030
        '''), 'batch lights RPC command 5')
        require(call_target(light_call + 0x94, lc[7]) == sif_call, 'Lights RPC target disagrees')

    init, _ = image.unique(pattern('''
        0080382D 0000182D ACE0005C 24EB???? ACE00060 24EA???? ACE00064
        24E9001C 240F???? 240EFFFF 240D0001 240C????
    '''), 'buzzer singleton initializer')
    getter_pat = pattern('''
        27BDFFD0 7FB10010 3C11???? 7FB00020 8E22???? 3C10???? 14400008 FFBF0000
        JAL 2604???? 24020001 3C04???? AE22???? JAL 2484???? 2602????
        7BB10010 7BB00020 DFBF0000 03E00008 27BD0030
    ''')
    getter_pat[8] = (0x0C000000 | (init >> 2), 0xFFFFFFFF)
    getter, gw = image.unique(getter_pat, 'buzzer singleton getter')
    require(gw[9] & 0xFFFF == gw[15] & 0xFFFF and gw[4] & 0xFFFF == gw[12] & 0xFFFF,
            'Singleton getter addresses disagree')
    singleton = image.memory(((gw[5] & 0xFFFF) << 16) + signed16(gw[9]), 0x6C)
    init = call_target(getter + 32, gw[8])
    image.expect(init, pattern('0080382D 0000182D ACE0005C 24EB???? ACE00060 24EA????'), 'singleton initializer')
    require(image.words(init + 12, 1)[0] & 0xFFFF == report, 'Initializer report offset disagrees')
    require(image.words(init + 20, 1)[0] & 0xFFFF == lights, 'Initializer lights offset disagrees')
    require(init < bind < update, 'RPC binding is outside the matched buzzer implementation')
    # Bind function and getter are linked by the observed adjacent function boundary.
    image.expect(bind - 0x30, pattern('27BDFFE0 7FB00010 FFBF0000 0080802D'), 'RPC bind function')
    require(bind - 0x30 == getter + len(gw) * 4 + 4, 'Singleton/bind function boundary disagrees')
    require(update == bind - 0x30 + 0x98, 'RPC bind/status-update function boundary disagrees')

    connected, cw = image.unique(pattern('''
        27BDFFE0 7FB00010 FFBF0000 JAL 00A0802D 0040282D 2402FFFF 16020005
        2402???? 8CA2???? 2404???? 10000002 00441818 02021818 00651021
        8043???? 00031827 0003102B 7BB00010 DFBF0000 03E00008 27BD0020
    '''), 'buzzer connected query')
    index = cw[9] & 0xFFFF
    require(call_target(connected + 12, cw[3]) == getter and cw[8] & 0xFFFF == stride
            and cw[10] & 0xFFFF == stride and cw[15] & 0xFFFF == status,
            'Connected query singleton/layout disagrees')
    init_words = image.words(init, 12)
    require(init_words[8] & 0xFFFF == stride and init_words[11] & 0xFFFF == stride,
            'Singleton initializer device strides disagree')
    iw = image.expect(init + 0x50, pattern('A06E???? 01621021 AC8D0000 AC400000'), 'initial device status')
    require(iw[0] & 0xFFFF == status, 'Singleton initializer status offset disagrees')
    tail = image.expect(update + 0xAC, pattern('''
        AE32???? AC550000 1000000D 24020001 90430000 14600004 26520001 A205????
        10000007 24020001 2610???? 2694???? 2A420002 1440FFDB 2673????
    '''), 'status-update index and second-device stride')
    require(tail[0] & 0xFFFF == index and tail[7] & 0xFFFF == status
            and all(tail[i] & 0xFFFF == stride for i in (10, 11, 14)),
            'Status-update singleton index/status/stride disagrees')
    eligible, ew = image.unique(pattern('''
        27BDFFE0 AFA50000 37A60004 FFBF0010 JAL 03A0282D JAL 00000000
        0040302D 8FA40000 24020004 3C03???? 00441023 8FA70004 00431817
        2405FFFF 0003403C 0008403F 14E50005 2402???? 8CC2???? 2404????
        10000002 00441818 00E21818 00661021 8C43???? 00681824 0003102B
        DFBF0010 03E00008 27BD0020
    '''), 'handset-present query')
    require(call_target(eligible + 24, ew[6]) == getter and ew[19] & 0xFFFF == stride
            and ew[20] & 0xFFFF == index and ew[21] & 0xFFFF == stride
            and ew[26] & 0xFFFF == report, 'Handset query singleton/layout disagrees')
    flag_top = (ew[11] & 0xFFFF) << 16
    require(flag_top != 0 and flag_top & (flag_top - 1) == 0, 'Handset mask is not a single bit')
    flag_shift = flag_top.bit_length() - 4
    require(flag_shift == 20, 'Unsupported handset/button bit layout (expected 20 button bits)')
    require(count % 4 == slot % 4 == report % 4 == index % 4 == 0
            and count + 4 <= slot and slot + 20 <= report and report + 4 <= status < stride
            and index >= 2 * stride and index + 4 <= 0x5C,
            'Singleton fields overlap or exceed the matched RPC layout')

    core, tw = image.unique(pattern('''
        0000A82D 2604???? JAL 0000902D 0000882D JAL 0040B02D 0040202D
        JAL 0000282D JAL 0040B82D 0040202D JAL 24050001 0040982D
    '''), 'TRC connected-check body')
    require(call_target(core + 32, tw[8]) == connected and call_target(core + 52, tw[13]) == connected
            and tw[5] == tw[10], 'TRC body does not query both matched buzzer devices')
    prologues = [pattern('''
        27BDFF70 3C02???? 7FB40040 24030002 0080A02D 7FB00080 7FB10070
        7FB20060 7FB30050 7FB50030 7FB60020 7FB70010 FFBF0000 8C44????
        1083000C AE80???? 28820002 54400007 8E82002C 28820006 10400003
        28820004 10400005 3C10???? 8E82002C 10400042 00000000 3C10????
    '''), pattern('''
        27BDFF70 3C02???? 7FB40040 8C43???? 0080A02D 7FB00080 7FB10070
        28620005 7FB20060 7FB30050 7FB50030 7FB60020 7FB70010 FFBF0000
        10400004 AE80???? 28620002 10400004 3C10???? 8E82002C 10400041 3C10????
    ''')]
    candidates = []
    for pat in prologues:
        va = core - 4 * len(pat)
        try:
            words = image.expect(va, pat, 'TRC prologue')
        except DetectionError:
            continue
        candidates.append((va, words))
    require(len(candidates) == 1, 'TRC prologue: expected one supported shape before the connected-check body')
    trc, pro = candidates[0]
    counter = pro[15] & 0xFFFF
    require(counter % 4 == 0 and 0x30 <= counter < 0x8000, 'Invalid TRC counter offset')
    global_load = pro[13] if len(pro) == 28 else pro[3]
    image.memory(((pro[1] & 0xFFFF) << 16) + signed16(global_load))
    image.expect(core + 0xCC, pattern('24020001 10000011 AE82????'), 'TRC counter setter')
    require(image.words(core + 0xD4, 1)[0] & 0xFFFF == counter, 'TRC counter stores disagree')
    image.expect(core + 0x140, pattern('03E00008 27BD0090'), 'TRC return')

    pad_setup, pads = image.unique(pattern('''
        3C10???? 3C11???? 2610???? 2631???? 0200202D 0000282D 0000302D
        JAL 0220382D 2612???? 26270100 0240202D 24050001 JAL 0000302D
        0200202D 24050004 JAL 24060003 0240202D 24050004 JAL 24060003
    '''), 'two-port pad buffer setup')
    require(pads[7] == pads[13] and pads[17] == pads[21], 'Pad setup call targets disagree')
    pad1 = image.memory(((pads[0] & 0xFFFF) << 16) + signed16(pads[2]), 0x94)
    pad_stride = signed16(pads[9])
    require(pad1 % 4 == 0 and pad_stride >= 0x94 and pad_stride % 4 == 0, 'Invalid pad buffer base/stride')
    pad2 = image.memory(pad1 + pad_stride, 0x94)
    dma = image.memory(((pads[1] & 0xFFFF) << 16) + signed16(pads[3]), 0x200)
    require(dma % 64 == 0, 'Pad DMA buffers are not 64-byte aligned')
    open_pad = call_target(pad_setup + 28, pads[7])
    ow = image.expect(open_pad, pattern('''
        27BDFFC0 7FB00030 7FB10020 0080802D 7FB20010 00A0882D 00C0902D
        FFBF0000 00E0302D 0220202D JAL 0240282D
    '''), 'pad port-open wrapper')
    image.offset(call_target(open_pad + 40, ow[10]), executable=True)
    image.expect(open_pad + 0x84, pattern('A607008A A6000090'), 'pad structure buffer extent')
    # The per-frame updater uses the SAME base and stride and passes the buffer
    # itself as scePadRead's a2 destination (not the DMA buffer passed at open).
    pad_update, pu = image.unique(pattern('''
        3C02???? 0080902D 2450???? JAL 0200202D JAL 0200202D JAL 0200202D
        10400003 8E42???? 24420001 AE42???? 8E62???? 26310001 0222102A
        1440FFF2 2610????
    '''), 'per-frame pad read buffers')
    require(((pu[0] & 0xFFFF) << 16) + signed16(pu[2]) == pad1
            and signed16(pu[17]) == pad_stride, 'Pad setup/read buffers disagree')
    read_wrapper = call_target(pad_update + 12, pu[3])
    image.expect(read_wrapper, pattern('''
        27BDFFE0 7FB00010 FFBF0000 0080802D 8E040028 JAL 8E05002C
        0040182D 24020006 10620004 AE030030 24020002 54620016 8E020030
        8E040028 0200302D JAL 8E05002C
    '''), 'pad read destination wrapper')
    read_sdk = call_target(read_wrapper + 64, image.words(read_wrapper + 64, 1)[0])
    image.expect(read_sdk, pattern('''
        27BDFFB0 24030070 FFB10010 3C02???? FFB20020 0080882D 00A0902D
        2404001C 72231818 02442018 FFB30030 2442???? FFBF0040 FFB00000
        00832021 00441021 8C430010 14600003 00C0982D
    '''), 'scePadRead SDK destination')
    image.expect(read_sdk + 0x70, pattern('0260202D 8E260060 JAL 0220282D'), 'scePadRead copy to caller buffer')

    layout = dict(status=status, stride=stride, index=index, report=report,
                  count=count, slot=slot, flag_shift=flag_shift)
    game = dict(title=f'Buzz! auto-detected ({elf})', serial=elf, version='auto-detected, UNTESTED',
                elf=elf, trc=trc, poll=poll, pad1=pad1, pad2=pad2, counter=counter,
                layout=layout, singleton=singleton, orig=pro[:3] + pw,
                evidence=dict(bind=bind, update=update, fetch=fetch, getter=getter,
                              connected=connected, eligible=eligible, pad_setup=pad_setup,
                              pad_update=pad_update, read_sdk=read_sdk, poll_end=poll + 4 * len(pw)))
    table = patch_table(game)
    require(len(bridge_words(pad1, pad2, layout)) * 4 <= game['evidence']['poll_end'] - poll,
            'Bridge does not fit before the original poll return delay slot; no code cave is used')
    require(all(image.words(va, 1)[0] == old for va, old, new in table), 'Patch words do not match the validated code')
    return game


def read_dir(f, lba, size):
    f.seek(lba * SECTOR)
    data = f.read(size)
    i = 0
    while i < len(data):
        ln = data[i]
        if ln == 0:
            i = (i // SECTOR + 1) * SECTOR
            continue
        rec = data[i:i + ln]
        if ln < 34 or len(rec) != ln or rec[32] > ln - 33:
            raise DetectionError('Malformed or truncated ISO9660 directory')
        nl = rec[32]
        name = rec[33:33 + nl].split(b';')[0].decode('latin-1').upper()
        yield name, struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        i += ln


def find_file(f, name):
    """Return (byte offset, size) of a file in the ISO9660 root directory."""
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    if len(pvd) != SECTOR or pvd[:7] != b'\x01CD001\x01':
        raise SystemExit('Not an ISO9660 image (no CD001 volume descriptor).')
    root = pvd[156:156 + 34]
    matches = [(lba * SECTOR, size) for n, lba, size in
               read_dir(f, struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0])
               if n == name.upper()]
    if len(matches) > 1:
        raise DetectionError(f'Ambiguous ISO directory: multiple entries for {name}')
    if matches:
        return matches[0]
    raise SystemExit(f'{name} not found in the ISO.')


def boot_elf_name(f):
    off, size = find_file(f, 'SYSTEM.CNF')
    f.seek(off)
    m = re.search(rb'BOOT2\s*=\s*cdrom0:\\?([^;\r\n]+)', f.read(size))
    if not m:
        raise SystemExit('SYSTEM.CNF has no BOOT2 line; not a PS2 game ISO.')
    return m.group(1).decode('latin-1').strip().upper()


def elf_load_segment(f, elf_off):
    """Return (vaddr, file offset inside the ELF, file size) of the first PT_LOAD segment."""
    f.seek(elf_off)
    hdr = f.read(0x34)
    if hdr[:4] != b'\x7fELF':
        raise SystemExit('The boot file is not an ELF.')
    phoff, = struct.unpack_from('<I', hdr, 0x1C)
    phnum, = struct.unpack_from('<H', hdr, 0x2C)
    for i in range(phnum):
        f.seek(elf_off + phoff + i * 32)
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack('<IIIII', f.read(20))
        if p_type == 1:
            return p_vaddr, p_offset, p_filesz
    raise SystemExit('The boot ELF has no loadable segment.')


def open_game(f):
    """Locate the boot ELF; return (elf name, function mapping an EE address to an ISO offset)."""
    elf, elf_off, data = read_boot_elf(f)
    image = ElfImage(data)

    def iso_offset(va):
        return elf_off + image.offset(va)
    return elf, iso_offset


def read_boot_elf(f):
    elf = boot_elf_name(f)
    elf_off, size = find_file(f, elf)
    f.seek(elf_off)
    data = f.read(size)
    if len(data) != size:
        raise DetectionError(f'Truncated boot ELF {elf}: expected {size} bytes, read {len(data)}')
    return elf, elf_off, data


def identify_game(data, elf, recovery=None):
    """Exact verified table first; otherwise structurally detect original bytes.

    A recovery sidecar is only used after reconstructing and hashing the original
    ELF, re-running detection, and comparing its entire table with the sidecar.
    """
    image = ElfImage(data)
    for game in (g for g in GAMES if g['elf'] == elf):
        table = patch_table(game)
        try:
            if all(image.words(va, 1)[0] in (old, new) for va, old, new in table):
                return game, table, True
        except DetectionError:
            pass
    original = data
    if recovery is not None:
        try:
            require(recovery['format'] == 1 and recovery['elf'] == elf, 'Recovery sidecar belongs to a different ELF')
            records = recovery['table']
            require(isinstance(records, list) and len(records) == 50, 'Invalid recovery word count')
            rebuilt = bytearray(data)
            seen = set()
            for row in records:
                require(isinstance(row, list) and len(row) == 3 and
                        all(type(v) is int and 0 <= v <= 0xFFFFFFFF for v in row), 'Invalid recovery table row')
                va, old, new = row
                require(va % 4 == 0 and va not in seen, 'Duplicate or unaligned recovery address')
                seen.add(va)
                off = image.offset(va, executable=True)
                require(struct.unpack_from('<I', data, off)[0] in (old, new), f'Changed patch word at {va:#x}')
                struct.pack_into('<I', rebuilt, off, old)
            original = bytes(rebuilt)
            require(hashlib.sha256(original).hexdigest() == recovery['original_elf_sha256'],
                    'Recovery sidecar hash does not match this ELF (changed code or wrong sidecar)')
        except (KeyError, TypeError, IndexError) as e:
            raise DetectionError('Malformed recovery sidecar') from e
    game = detect_game(original, elf)
    table = patch_table(game)
    if recovery is not None:
        require([list(row) for row in table] == records, 'Recovery table disagrees with detected code')
    return game, table, False


def recovery_record(data, elf, table):
    return dict(format=1, elf=elf, original_elf_sha256=hashlib.sha256(data).hexdigest(),
                table=[list(row) for row in table])


def save_recovery(path, record):
    """Persist original words before the first ISO write; never replace a sidecar."""
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('x', encoding='utf-8') as f:
        json.dump(record, f, indent=2)
        f.write('\n')
        f.flush()
        os.fsync(f.fileno())
    temporary.rename(path)


def word_states(f, iso_offset, table):
    states = []
    for va, orig, new in table:
        f.seek(iso_offset(va))
        data = f.read(4)
        if len(data) != 4:
            raise DetectionError(f'Truncated patch word at {va:#x}')
        cur, = struct.unpack('<I', data)
        states.append('both' if orig == new == cur else 'orig' if cur == orig else 'new' if cur == new else 'other')
    return states


def main():
    ap = argparse.ArgumentParser(description=f'Buzz! Pad Bridge {VERSION}')
    ap.add_argument('iso', nargs='?', help='your Buzz! game ISO')
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='only report the patch state')
    mode.add_argument('--restore', action='store_true', help='undo the patch')
    args = ap.parse_args()
    iso = args.iso or input('Drag your Buzz! game ISO here and press Enter: ').strip().strip('"')

    try:
        f = open(iso, 'rb' if args.check else 'r+b')
    except PermissionError:
        action = 'reading' if args.check else 'writing'
        raise SystemExit(f'Cannot open the ISO for {action}. Check file permissions and any program using it.')
    with f:
        elf, elf_off, data = read_boot_elf(f)
        image = ElfImage(data)
        def iso_offset(va):
            return elf_off + image.offset(va, executable=True)
        sidecar = Path(str(iso) + '.buzz-pad-bridge.json')
        recovery = None
        if sidecar.exists():
            try:
                recovery = json.loads(sidecar.read_text(encoding='utf-8'))
            except (OSError, ValueError) as e:
                raise DetectionError(f'Cannot read recovery sidecar {sidecar}: {e}') from e
        game, table, verified = identify_game(data, elf, recovery)
        states = word_states(f, iso_offset, table)
        require('other' not in states, 'Game code changed while inspecting the ISO; refusing to overwrite it')
        print(f"{game['title']} ({game['serial']}, {game['version']})")
        if not verified:
            print('UNTESTED version: addresses and overwritten instructions were verified structurally; '
                  'hardware/PCSX2 testing is required.')
            print(f"Detected trc={game['trc']:#x}, poll={game['poll']:#x}, "
                  f"pad1={game['pad1']:#x}, pad2={game['pad2']:#x}.")
        patched = all(s in ('new', 'both') for s in states)
        clean = all(s in ('orig', 'both') for s in states)

        if args.check:
            print('Patched.' if patched else 'Not patched.' if clean else 'Partly patched; run again to finish.')
            return
        if args.restore:
            if clean:
                print('Already original. Nothing to do.')
                return
            for va, orig, _ in table:
                f.seek(iso_offset(va))
                f.write(struct.pack('<I', orig))
            f.flush()
            require(all(s in ('orig', 'both') for s in word_states(f, iso_offset, table)),
                    'Restore verification failed')
            print('Restored the original game code.')
            return
        if patched:
            print('Already patched. Nothing to do.')
            return
        if not verified and recovery is None:
            save_recovery(sidecar, recovery_record(data, elf, table))
            print(f'Recovery sidecar: {sidecar} (keep it with this ISO for --check/--restore).')
        for va, _, new in table:
            f.seek(iso_offset(va))
            f.write(struct.pack('<I', new))
        f.flush()
        if not all(s in ('new', 'both') for s in word_states(f, iso_offset, table)):
            raise SystemExit('Verification failed. Run with --restore and try again.')
        print(f'Patched {len(table)} words. Copy the ISO to your PS3 (/dev_hdd0/PS2ISO/) or load it in PCSX2.')


if __name__ == '__main__':
    try:
        try:
            main()
        except (DetectionError, OSError, struct.error) as e:
            raise SystemExit(f'Refused: {e}. No further writes were made. '
                             'For an auto-detected patched ISO, keep its .buzz-pad-bridge.json sidecar. '
                             'See docs/PORTING.md.') from e
    finally:
        if getattr(sys, 'frozen', False):
            input('\nPress Enter to close.')
