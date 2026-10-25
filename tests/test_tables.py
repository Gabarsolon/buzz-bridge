"""Checks the generated patch against the words verified on a real PS3. Run: python tests/test_tables.py"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import buzz_pad_bridge as b  # noqa: E402

# Patched words for Buzz! The Schools Quiz Original v2.00, as tested on a PS3 (ps2_netemu) and in PCSX2.
VERIFIED = [
    0xAC800030, 0x03E00008, 0x24020001,
    0xA0800024, 0x24020001, 0xAC800058, 0xAC820000, 0x3C08004D, 0x0000482D, 0x910C5F21, 0x11800002,
    0x950A5F22, 0x3949FFFF, 0x910C5FB5, 0x11800003, 0x950A5FB6, 0x394AFFFF, 0x012A4825, 0x000953C2,
    0x31430001, 0x00095342, 0x314A0002, 0x006A1825, 0x000952C2, 0x314A0004, 0x006A1825, 0x00095242,
    0x314A0008, 0x006A1825, 0x00095182, 0x314A0030, 0x006A1825, 0x00095082, 0x314A00C0, 0x006A1825,
    0x00095200, 0x314A0100, 0x006A1825, 0x00095180, 0x314A0200, 0x006A1825, 0x00095240, 0x314A0C00,
    0x006A1825, 0x3C0A0070, 0x006A1825, 0xAC830004, 0xAC830018, 0x03E00008, 0x24020001,
]


def main():
    for game in b.GAMES:
        table = b.patch_table(game)
        assert len(game['orig']) == len(table), f"{game['title']}: orig has {len(game['orig'])} words"
        addrs = [va for va, _, _ in table]
        assert len(set(addrs)) == len(addrs), 'duplicate addresses'
    sq = b.GAMES[0]
    assert [new for _, _, new in b.patch_table(sq)] == VERIFIED, 'generated Schools Quiz patch changed'
    try:
        b.bridge_words(0x4D5F20, 0x4E5F20)
    except ValueError:
        pass
    else:
        raise AssertionError('pads 64 KB apart must be rejected')
    print('ok')


if __name__ == '__main__':
    main()
