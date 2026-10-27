"""Pure-Python detector checks; optional local game inputs are never committed.

Set BUZZ_SCHOOLS_ELF / BUZZ_MEGA_ELF to original boot ELFs to run integration
and mutation checks. Without those variables the game-dependent tests skip.
Run: python -m unittest discover -s tests -v
"""
import copy
import os
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import buzz_pad_bridge as b
from test_tables import VERIFIED


def synthetic_elf():
    data = bytearray(0x300)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HH', data, 16, 2, 8)
    struct.pack_into('<I', data, 28, 52)
    struct.pack_into('<HH', data, 42, 32, 2)
    struct.pack_into('<8I', data, 52, 1, 0x100, 0x100000, 0, 0x100, 0x100, 5, 16)
    struct.pack_into('<8I', data, 84, 1, 0x200, 0x200000, 0, 0x100, 0x200, 6, 16)
    return data


class MappingTests(unittest.TestCase):
    def test_multiple_loads_and_bss(self):
        image = b.ElfImage(synthetic_elf())
        self.assertEqual(image.offset(0x100000, executable=True), 0x100)
        self.assertEqual(image.offset(0x200004), 0x204)
        self.assertEqual(image.memory(0x200180, 4), 0x200180)
        for address in (0x1000FF, 0x200100):
            with self.assertRaises(b.DetectionError):
                image.offset(address)
        with self.assertRaises(b.DetectionError):
            image.offset(0x200000, executable=True)

    def test_invalid_elf(self):
        for offset, value in ((4, 2), (5, 2), (18, 3), (42, 16)):
            data = synthetic_elf()
            data[offset] = value
            with self.assertRaises(b.DetectionError):
                b.ElfImage(data)
        with self.assertRaises(b.DetectionError):
            b.ElfImage(synthetic_elf()[:80])
        data = synthetic_elf()
        struct.pack_into('<I', data, 84 + 8, 0x100080)
        with self.assertRaisesRegex(b.DetectionError, 'Overlapping'):
            b.ElfImage(data)

    def test_signature_zero_one_many_aligned_executable(self):
        data = synthetic_elf()
        pat = b.pattern('24020001 3C04???? JAL 03E00008')
        code = struct.pack('<4I', 0x24020001, 0x3C041234, 0x0C040000, 0x03E00008)
        with self.assertRaisesRegex(b.DetectionError, 'found 0'):
            b.ElfImage(data).unique(pat, 'fixture')
        data[0x104:0x114] = code
        data[0x200:0x210] = code  # A non-executable match must be ignored.
        self.assertEqual(b.ElfImage(data).unique(pat, 'fixture')[0], 0x100004)
        data[0x144:0x154] = code
        with self.assertRaisesRegex(b.DetectionError, 'found 2'):
            b.ElfImage(data).unique(pat, 'fixture')

    def test_generated_layout_and_table_bounds(self):
        words = b.bridge_words(0x487070, 0x487104,
                               dict(status=0x28, index=0x60, count=4, slot=8, report=0x1C))
        self.assertEqual(words[:4], [0xA0800028, 0x24020001, 0xAC800060, 0xAC820004])
        self.assertEqual(words[-4:-2], [0xAC830008, 0xAC83001C])
        game = dict(b.GAMES[0], orig=[0] * 49)
        with self.assertRaises(ValueError):
            b.patch_table(game)
        game = dict(b.GAMES[0], poll=b.GAMES[0]['trc'])
        with self.assertRaisesRegex(ValueError, 'overlap'):
            b.patch_table(game)


class LocalGameTests(unittest.TestCase):
    def load_game(self, variable):
        value = os.environ.get(variable)
        if not value or not Path(value).is_file():
            self.skipTest(f'{variable} not supplied or file absent')
        return Path(value).read_bytes()

    def test_schools_exact_verified_50_words(self):
        data = self.load_game('BUZZ_SCHOOLS_ELF')
        game = b.detect_game(data, 'SCES_549.41')
        for key in ('trc', 'poll', 'pad1', 'pad2', 'orig'):
            self.assertEqual(game[key], b.GAMES[0][key], key)
        self.assertEqual([new for va, old, new in b.patch_table(game)], VERIFIED)

    def test_mega_hand_checked_50_words(self):
        data = self.load_game('BUZZ_MEGA_ELF')
        game = b.detect_game(data, 'SCES_545.05')
        self.assertEqual([game[k] for k in ('trc', 'poll', 'pad1', 'pad2', 'singleton')],
                         [0x1F4D18, 0x151AF8, 0x487070, 0x487104, 0x45FF70])
        self.assertEqual(game['layout'], dict(status=0x24, stride=0x2C, index=0x58,
                                             report=0x18, count=0, slot=4, flag_shift=20))
        expected = VERIFIED.copy()
        for i, word in {7: 0x3C080048, 9: 0x910C7071, 11: 0x950A7072,
                        13: 0x910C7105, 15: 0x950A7106}.items():
            expected[i] = word
        self.assertEqual([new for va, old, new in b.patch_table(game)], expected)
        self.assertEqual(game['evidence']['poll_end'], 0x151BB4)
        self.assertEqual(game['orig'][:3], [0x27BDFF70, 0x3C020047, 0x7FB40040])

    def test_every_overwritten_instruction_is_validated(self):
        for variable in ('BUZZ_SCHOOLS_ELF', 'BUZZ_MEGA_ELF'):
            data = self.load_game(variable)
            game = b.detect_game(data)
            image = b.ElfImage(data)
            for va, old, new in b.patch_table(game):
                with self.subTest(variable=variable, va=hex(va)):
                    mutated = bytearray(data)
                    struct.pack_into('<I', mutated, image.offset(va), old ^ (1 << 26))
                    with self.assertRaises(b.DetectionError):
                        b.detect_game(mutated)

    def test_ambiguity_short_poll_and_broken_links(self):
        data = self.load_game('BUZZ_MEGA_ELF')
        game = b.detect_game(data)
        image = b.ElfImage(data)
        duplicate = bytearray(data)
        bind = image.offset(game['evidence']['bind'])
        duplicate[0x1000:0x1000 + 48] = data[bind:bind + 48]
        with self.assertRaisesRegex(b.DetectionError, 'found 2'):
            b.detect_game(duplicate)
        shortened = bytearray(data)
        struct.pack_into('<I', shortened, image.offset(game['poll'] + 48), 0x03E00008)
        with self.assertRaisesRegex(b.DetectionError, 'does not fit'):
            b.detect_game(shortened)
        for va in (game['poll'] + 24, game['poll'] + 136, game['poll'] + 152,
                   game['evidence']['connected'] + 12, game['evidence']['pad_setup'] + 52):
            mutated = bytearray(data)
            off = image.offset(va)
            word = struct.unpack_from('<I', mutated, off)[0]
            struct.pack_into('<I', mutated, off, word + 1)
            with self.assertRaises(b.DetectionError):
                b.detect_game(mutated)
        for va in (game['poll'] + 36, game['poll'] + 56, game['poll'] + 80,
                   game['evidence']['fetch'] + 0x4C, game['evidence']['eligible'] + 80):
            mutated = bytearray(data)
            off = image.offset(va)
            mutated[off] ^= 4
            with self.assertRaises(b.DetectionError):
                b.detect_game(mutated)

    def test_unknown_recovery_and_partial_patch(self):
        data = self.load_game('BUZZ_MEGA_ELF')
        game, table, verified = b.identify_game(data, 'SCES_545.05')
        self.assertFalse(verified)
        record = b.recovery_record(data, 'SCES_545.05', table)
        image = b.ElfImage(data)
        for n in (1, 25, 50):
            patched = bytearray(data)
            for va, old, new in table[:n]:
                struct.pack_into('<I', patched, image.offset(va), new)
            actual, recovered, _ = b.identify_game(patched, 'SCES_545.05', record)
            self.assertEqual(recovered, table)
            with self.assertRaises(b.DetectionError):
                b.identify_game(patched, 'SCES_545.05')
        bad = copy.deepcopy(record)
        bad['table'][0][1] ^= 1
        with self.assertRaises(b.DetectionError):
            b.identify_game(patched, 'SCES_545.05', bad)
        patched[image.offset(game['evidence']['eligible'])] ^= 1
        with self.assertRaisesRegex(b.DetectionError, 'hash'):
            b.identify_game(patched, 'SCES_545.05', record)


if __name__ == '__main__':
    unittest.main()
