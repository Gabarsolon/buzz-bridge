# Plan: 4 players, more games

## Why only 2 players today

`ps2_netemu` hands the Buzz! dongle to the game as **one controller in digital mode**. A digital controller reports 14 buttons the game can use, and `ps2_netemu`'s Buzz! layout fills 10 of them (handsets 1 and 2). Handset 3's red and yellow are mapped to L3/R3, which a digital-mode controller does not report. Handset 4 is not mapped at all.

Changing that layout would mean modifying `ps2_netemu.self`, which HEN users cannot do. So the plan stays a game patch only.

## Constraint: 2 controller ports

The game only opens PS2 controller ports 1 and 2 (no multitap), so it sees at most **2 devices**. 4 players therefore means **2 players per device**.

| Devices | Players |
|---|---|
| Buzz! set + 1 DualShock (2 people share it) | 4 |
| 2 DualShocks (each shared) | 4 |
| Buzz! set only | 2 |
| 1 DualShock (shared) | 2 |

A Buzz! set plus 2 DualShocks is 3 devices; the third would be ignored. 4 separate controllers would need multitap support the game does not have.

## Design: per-port layout

- Port 1 = players 1–2, port 2 = players 3–4 (today both ports feed players 1–2).
- Each port starts in the **Buzz! layout** (the one `ps2_netemu` produces from the dongle).
- The Buzz! dongle can never press the D-pad. So the first D-pad press on a port switches that port to the **DualShock halves layout** for the rest of the session:
  - Left person: red = L1, blue = ↑, orange = ←, green = →, yellow = ↓
  - Right person: red = R1, blue = △, orange = □, green = ○, yellow = ✕
- Handset-present flags follow the players that exist (bits 20–23).

## Work items

1. Find code space: the bridge already fills the original buzzer poll. Candidates are the now-unused USB/RPC routines (`0x275990`, `0x275bc0`, `0x275c00`, `0x275c90`, `0x275e30`); confirm they have no other callers.
2. Find a zero-initialised RAM spot for the two per-port layout flags.
3. Implement and test in PCSX2 (both scenarios), then on the PS3.

## More games

The fix lives in each game's own executable, at addresses that differ per game, region and edition. Today each version is one entry in `GAMES` (four addresses plus the original words), found by hand with [docs/PORTING.md](docs/PORTING.md) and `tools/new_game.py`. Later: a signature-based search that finds the same Relentless buzzer and TRC code automatically. Every supported version still has to be checked once against a real copy; unknown versions are refused.

Wanted for that: ISOs (your own dumps) of other PS2 Buzz! games, including the Schools Quiz Classroom Edition v1.00.

## Phones as buzzers

Same 2-port model: a board such as an ESP32-S3 (native USB) presents itself as 2 gamepads to the PS3 and serves a phone web page over Wi-Fi ([buzz-web-controller](https://github.com/Gabarsolon/buzz-web-controller)). With the per-port layout above, phones can be players 3–4 next to a real Buzz! set, or all 4 players.
