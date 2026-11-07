# Experimental Mode C wire contract, version 1

This is a proposed bridge/IOP contract, not a Sony Buzz protocol. The DS3 outer
HID layout comes from GP2040-CE and Linux hid-sony. PS3 enumeration and stock
netemu transport are UNTESTED. `bbusbd.irx` has not been changed to implement it.

The saved diagnostic option `rumble probe on` bypasses the encoding below:
any nonzero motor value lights all four lamps, and both motors zero clears them.
The 1500 ms lease still applies. This allows a known PS2 rumble test to probe
the stock path without changing bbusbd. It defaults off; use `rumble probe off`
for the actual four-bit protocol. It is a diagnostic, not game lamp forwarding.

Clean C: VID 054C, PID 0268; EP0=64; one HID interface; interrupt OUT 02 and
IN 81, 64-byte maximum packet, 1 ms interval. Report descriptor is 148 bytes.
Input/output report ID is 01; each declares 48 payload bytes, 49 with the ID.
Input repeats every 10 ms while enabled, with changed input sent sooner when
the endpoint is available; before
enable only an activation PS report is emitted. Debug C adds CDC and
uses CAFE:B003; test clean C for actual DS3 recognition on the console.

## Input

Offsets below include the input report ID at byte 0. Byte 1 is reserved; digital
buttons occupy bytes 2 and 3 in standard PS2/DS3 order. Bits 16–19 of the Buzz
word use the four sticks: 80 means released, E0 means pressed. A guest threshold
of B0 distinguishes them. Byte 4 bit 0 is reserved for the PS activation gesture;
it carries no buzzer bit. Pressure bytes 14–25 mirror digital bits 4–15 as 00/FF.

| Buzz bit | Physical button (reference order) | DS3 / PS2 field |
|---|---|---|
| 0 | P1 red | Select |
| 1 | P1 yellow | L3 |
| 2 | P1 green | R3 |
| 3 | P1 orange | Start |
| 4 | P1 blue | Up |
| 5 | P2 red | Right |
| 6 | P2 yellow | Down |
| 7 | P2 green | Left |
| 8 | P2 orange | L2 |
| 9 | P2 blue | R2 |
| 10 | P3 red | L1 |
| 11 | P3 yellow | R1 |
| 12 | P3 green | Triangle |
| 13 | P3 orange | Circle |
| 14 | P3 blue | Cross |
| 15 | P4 red | Square |
| 16 | P4 yellow | LX, input byte 6 |
| 17 | P4 green | LY, input byte 7 |
| 18 | P4 orange | RX, input byte 8 |
| 19 | P4 blue | RY, input byte 9 |

Hold all four red buttons for two seconds to request PS. Before F4 this sends
one neutral PS report; while enabled it sends a 250 ms pulse alongside normal
input. Release/rehold to repeat. `ds3 auto on` also requests PS automatically.
This is a bridge activation gesture, not a buzzer bit. Its ability to
register/assign the pad on a PS3 is UNTESTED.

For an ordinary nine-byte PS2 analog-pad reply
`FF 73 5A buttons_lo buttons_hi RX RY LX LY` (or the first nine bytes of a
pressure-pad reply), the proposed guest decoder is:

```c
bits = ((~reply[3]) & 255) | (((~reply[4]) & 255) << 8);
bits |= (reply[7] >= 0xB0) << 16;  // LX
bits |= (reply[8] >= 0xB0) << 17;  // LY
bits |= (reply[5] >= 0xB0) << 18;  // RX
bits |= (reply[6] >= 0xB0) << 19;  // RY
bb_report_pack(bits, virtual_buzz_report);
```

Check source, length, pad ID, status and 5A before decoding. A five-byte digital
poll cannot carry the axes. This assumes stock netemu preserves the sixteen
digital bits, opposite directions, and stick values sufficiently; none of those
end-to-end assumptions is proven by native codec tests.

## Lamp output

DS3 output ID 01 has small-motor duration/value at wire bytes 2/3 and large-motor
duration/value at bytes 4/5. The TinyUSB callback normally receives the payload
without the ID: small value is payload[2], large value is payload[4]. The bridge
accepts 48- or 35-byte payloads, and the documented 34-byte Linux/TinyUSB quirk
shape. Interrupt OUT includes its ID and is normalized once.

Let `mask` be four lamp bits, P1 at bit 0. Send these standard pad motor states:

1. PREPARE: small=0, large=`17*mask`.
2. COMMIT: small=1, large=`17*mask`, within 500 ms of PREPARE.

The decoder requires matching levels and accepts amplitude error of at most
two around 0,17,...255. A matching COMMIT consumes PREPARE, writes the real Buzz
payload `00 L1 L2 L3 L4 00 00` (lamp bytes 00/FF), and starts a 1500 ms lease.
Malformed frames, unprepared commits, mismatched values and expired prepares
cannot change the lamp mask. Repeated commits alone are ignored. A committed
zero mask means all-off. Native USB unmount also sends all-off.

Use a 16 ms IOP worker cadence, PREPARE for two polls, COMMIT for two polls, then
idle. Repeat the pair at least every 500 ms while lamps should stay lit; 250 ms
refresh is recommended. Repeat even an unchanged mask. A new desired mask
restarts PREPARE for that mask. The strobe makes the motor states change even
when the requested mask repeats. The lease clears lamps after a lost channel.

The two-poll dwell, 16 levels, tolerance, strobe and lease are design choices.
They are not evidence that netemu preserves output timing/amplitude or avoids
coalescing. There is no return acknowledgement of physical lamp completion
over this protocol. It requires a dedicated Pico pad source: ordinary game
rumble could accidentally resemble a valid pair. Do not apply it to a real
DualShock controller. Game PADMAN traffic must not overwrite the reserved
Pico's motor bytes between the phases.

## Feature behavior implemented (task 3 / modec-v3)

The private HID control hook returns complete **wire bytes**, bounded by
`wLength` up to 64. It bypasses TinyUSB's generic report-ID insertion for Mode C
features: genuine DS3 headers are not uniformly the requested report ID.
This corrects the task-2 firmware's `01 01` and `EF EF` response prefixes.

| ID | Implemented behavior / provenance |
|---|---|
| 01 | 64 wire bytes beginning `00 01 04 00 0B`; reference capabilities |
| F2 | 64 bytes beginning `F2 FF FF 00`, stable locally administered device address at 4–9, firmware information at 10–17; Linux's 17-byte request truncates correctly |
| F5 | 64 bytes beginning `01 00`, paired host address at 2–7; SET `01 00 <six address bytes>` updates RAM, echoed by GET |
| EF | 64 bytes beginning `00 EF 04 00 0B 03 01 <page>`; SET payload offsets 4–6 select `03 01 <page>`; page data begins at 17 |
| F7 | 64 bytes beginning `01 00 08 02 DC 01 EE FF`, then zeros; static reference readings |
| F8 | 64 bytes echoing the selected EF page with first four bytes `00 01 00 00` |
| F4 SET | Exact PS3 payload `42 0C 00 00` enables streaming; `42 0B 00 00` disables it; a Windows API ID prefix/padding is also accepted |

The EF A0/B0 contents are coherent synthetic calibration, not a genuine IMU dump;
other pages contain zero data with the selected header. Unsupported GET_FEATURE
IDs stall. Unknown SETs are logged without effects after ACK. These addresses
are USB compatibility state, not Bluetooth pairing; Pico 2 has no radio.

Clean C retains the native **148-byte** descriptor, which declares Feature IDs
01/02/EE/EF, not F2/F4/F5/F7/F8. The omission exists in the genuine descriptor.
Debug C (CAFE:B003) adds five diagnostic declarations, making it **213 bytes**,
so Windows can issue the otherwise blocked feature requests. Use clean C for
the PS3 and debug C for PC feature tests.

Streaming waits for F4. A pre-enable PS pulse sends one neutral 49-byte input
with only PS set, then waits again; the host must acknowledge it before it is
consumed. `ds3 auto on/off` is saved (default on): request PS about two seconds
after configuration, or two seconds after an early F4 enable. It does not send
a second automatic pulse if the first already caused enable. While enabled,
manual/automatic PS stays on for 250 ms. Four reds held two seconds request a
pulse, and releasing/reholding permits another. A Linux-style one-byte USB OUT
poke also enables streaming. These convenience choices need console validation.

Hold **P1 red + blue for three seconds** in clean C to save the request log and
persistently switch to C/debug. `ds3log` prints previous flash banks plus the
current RAM log through CDC. See [REPORT_MODEC.md](REPORT_MODEC.md) for timing,
flash limits and the required log-save step before unplugging from the PS3.
