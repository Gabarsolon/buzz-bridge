S4E carries input in a standard-length pressure pad poll: 21 TX / 21 RX bytes,
command `42`, port 0, PIO, existing SIO2MAN begin/transfer/end session.
SEND3 is `00541540`, CTRL1 `FFC00505`, CTRL2 `00020014`.
This exact private payload is CPU-tested, not hardware-proven.

| Request bytes | Meaning |
|---|---|
| 0..4 | `01 42 00 00 00`; ordinary pad address/read command, zero motor bytes |
| 5..8 | `42 5A E4 01`; exact channel identity/version |
| 9..12 | Last observed device generation, little endian u32 |
| 13..16 | Last report successfully queued to virtual USB, little endian u32 |
| 17..20 | Zero |

| Reply bytes | Meaning |
|---|---|
| 0..5 | `FF 79 5A 42 5A E4`; standard pressure reply prefix plus channel identity |
| 6 | 0 unavailable, 1 next report, 2 no new report, 3 overflow/resync, 4 busy |
| 7 | 2: all four raw handsets |
| 8..11 | Device generation, full u32 |
| 12..15 | Report sequence, full u32 |
| 16..18 | Active-high twenty button bits; high nibble zero |
| 19 | Excluded cached Buzz pad sources for optional additional pads |
| 20 | Remaining queued reports, maximum eight |

Both lengths must be 21, offsets <=235 and FIFO pointers must be exactly the
original input/output bases. Every other request continues through the original
pad handler with all five arguments unchanged. Recognition requires the complete
signature, zero motors and reserved bytes. A recognized inactive/stale request
gets an unavailable response and the guest falls back to ordinary pad input.
No WRITE, STATUS, output capability, physical output call, or second output
transaction is supported. A lighting protocol is deferred.

The native queue, one producer, bounded epoch snapshot, overflow status,
freshness policy, full-width generation/sequence acknowledgement and USB
completion-before-acknowledgement rule are retained. The guest adapter expands
the 21 physical bytes into its existing 32-byte local RAM representation; that
representation is never submitted to SIO2 by this build. No sequence truncation,
new shared cursor, additional allocation, new lock or new retry loop is added.
The fixed eight-entry capacity is implicit in this protocol version.

Default: layout 14, W=0, one virtual dongle, four real handsets. Legacy fallback
is P=1/W=2/S=0; absence of raw input does not reconstruct the missing raw bits.
The optional W>0 path remains a developer test and is not a separate package.

Normal legacy polling and logger L0/v2/RAM retain five-byte reads plus six-byte
detection and seven-byte selection/restoration. Old L1 and experimental L2x
remain excluded from the proposed console test sequence.
