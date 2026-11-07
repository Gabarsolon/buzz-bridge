/* Journal banks 0x103fc000/3fd000, settings 3fe000, last sector for RP2350 ROM. */
ASSERT(__flash_binary_end <= 0x103fc000, "buzz_bridge overlaps reserved logs/settings")
