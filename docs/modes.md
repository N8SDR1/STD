---
title: Modes
---

Software in testing. STD is a 64-bit Windows application, Qt 6.11.1 MSVC. The rows below are what the Mode menu actually does in this tree. A mode is listed as keying only when TX builds its audio. On-air proof is still in progress.

Connect sends `AUDIO_START:0`. The text command START is never sent. CW stays in Lyra.

## Keying

| Menu | Rows | On the air path |
| --- | --- | --- |
| FT8, FT4, FT2 | one each | MSHV generator. 15 s, 7.5 s, 3.75 s. DIGU. |
| Hell | Feld Hell, Slow Hell, Feld Hell X5, Feld Hell X9, FSK Hell, FSK Hell-105, Hell 80 | Fldigi column rates. [Hell notes](hell.html). |
| RTTY | one row, labeled RTTY | 45.45 baud, 170 Hz, 5 bits, 1.5 stop, mark high, reverse off. USOS: a space sends LTRS, then the space. There is no baud or shift submenu. |
| PSK | PSK31, PSK63, PSK125, PSK250, PSK500, PSK1000 | BPSK. Symbol rates 31.25, 62.5, 125, 250, 500, 1000 baud. A zero reverses the phase. |
| Olivia | 4/250, 8/250, 8/500, 16/500, 16/1000, 32/1000 | Gray-coded tones. Baud 62.5, 31.25, 62.5, 31.25, 62.5, 31.25. |
| Contestia | the same six | Same shape as Olivia, Contestia scramble. |
| MFSK | MFSK4, 8, 11, 16, 22, 31, 32, 64, 128 | NASA K=7, varicode, diagonal interleaver. |
| DominoEX | 4, 5, 8, 11, 16, 22 | 18-tone incremental keying. FEC is off. |
| Thor | 4, 5, 8, 11, 16, 22 | NASA K=7, varicode, 4 by 10 interleaver, 18-tone incremental keying. |

TX or Tune is what sends `trx`. An empty line does not key.

## Receive only

**WEFAX-576.** IOC 576, 120 lines per minute. Black is 400 Hz below the cursor and white is 400 Hz above it. Pictures can be saved. TX does not send this mode, and START is never sent.

## Not on the menu

These do not key.

- **MT63-500S, MT63-500L, MT63-1000S, MT63-1000L, MT63-2000S, MT63-2000L.** Walsh and the interleaver are checked in the bit domain. A transmitted character has not yet been read back from the audio, so the menu stays empty of MT63.
- **QPSK and 8PSK.** Not written. They will use Fldigi's convolutional code.
- **MFSK64L, MFSK128L, DominoEX Micro, Thor Micro.** Absent on purpose.
- **Thor 25x4, Thor 50x1, Thor 50x2, Thor 100.** Those speeds use a K=15 code and stay off until a character round-trips.
- MSHV modes still outside this program (MSK144, Q65, JT65, and the rest) stay listed in [mshv-modes.md](mshv-modes.md) until they are implemented.

CW stays in Lyra.

## Later

JS8Call is later, from [JS8Call-improved](https://js8call.com/JS8Call-improved/). FLD-88 is queued. Neither is in this build.
