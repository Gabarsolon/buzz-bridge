# Mode C reference data

Consulted 2026-11-07. These hexadecimal files contain USB protocol facts;
they are independent comparison fixtures, not captures from the Pico or a
DS3 connected to this PC.

- `genuine_ds3_report.hex`: 148 native bytes transcribed from the first array in
  [Nefarius's native descriptor page](https://docs.nefarius.at/research/SIXAXIS-native-HID-Report-Descriptor/).
- `genuine_ds3_configuration.hex`: the 41-byte native DS3 configuration also
  exposed by [GP2040-CE PS3Descriptors.h](https://github.com/OpenStickCommunity/GP2040-CE/blob/main/headers/drivers/ps3/PS3Descriptors.h).
- [PS3_USB_STARTUP.md](https://github.com/nefarius/DsHidMini/blob/master/docs/PS3_USB_STARTUP.md):
  analysis of real PS3/DS3 captures; PS report precedes F4 enable, EP0 output
  omits its report ID, and steady-state OUT continues every frame.
- [MOTION.md](https://github.com/nefarius/DsHidMini/blob/master/docs/MOTION.md):
  genuine feature headers, EF page-select/read offsets, F2 firmware tail and
  model differences. Synthetic calibration in this bridge is labelled as such.
- [Linux hid-sony](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c):
  USB F2/F5 initialization, short reads, one-byte output poke; Bluetooth F4
  differs from the PS3's wired enable command.
- [GP2040-CE driver](https://github.com/OpenStickCommunity/GP2040-CE/blob/main/src/drivers/ps3/PS3Driver.cpp):
  existing TinyUSB implementation and MIT feature templates; not a complete
  substitute for genuine PS3-host USB captures.
- [GIMX EMUPS3 descriptors](https://github.com/matlo/GIMX-firmwares/blob/master/EMUPS3/Descriptors.c):
  independent established emulator reference. No GIMX GPL code was copied.

`task2_source_before.zip` preserves the source and instructions before task 3.
