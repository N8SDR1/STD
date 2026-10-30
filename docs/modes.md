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
| QPSK | QPSK31, QPSK63, QPSK125, QPSK250, QPSK500 | K=5 convolutional code. One dibit per varicode bit. 8PSK is not on the menu. |
| Olivia | 4/250, 8/250, 8/500, 16/500, 16/1000, 32/1000 | Gray-coded tones. Baud 62.5, 31.25, 62.5, 31.25, 62.5, 31.25. |
| Contestia | the same six | Same shape as Olivia, Contestia scramble. |
| MFSK | MFSK4, 8, 11, 16, 22, 31, 32, 64, 64L, 128, 128L | NASA K=7, varicode, diagonal interleaver. The L rows use the long interleaver. |
| DominoEX | Micro, 4, 5, 8, 11, 16, 22 | 18-tone incremental keying. FEC is off. Micro is 2 baud. |
| Thor | 4, 5, 8, 11, 16, 22, 25x4, 50x1, 50x2, 100 | Slow speeds are NASA K=7. 25x4, 50x1, 50x2, and 100 are IEEE K=15. 18-tone incremental keying. |
| MT63 | 500S, 500L, 1000S, 1000L, 2000S, 2000L | 64 carriers, Walsh code, short or long interleave. Centered on the cursor. |
| JS8 | one row, labeled JS8 | JS8Call-improved Normal. 15 s, 50 Hz, JS8 Costas. The transmit box is the payload. Band buttons QSY to the suggested calling dials, 160 m through 6 m. 2 m is in that guide and is not on the row. |
| WSPR | one row, labeled WSPR | WSPR-2 from WSJT-X and JTDX. Even UTC minutes. Band buttons QSY to the WSPR dials. Tx power, Tx Pct, Band Hopping. No macro bar. Locator is Options → User. Receive titles stay above the scrolling rows: Time, dB, DT, Frequency, Drift, Call, Locator, Report. DT is the time offset. Report is the dBm in the message. Options → Network can post each heard station to WSPRnet. That post is off until Enable WSPRnet is checked. WSPRnet does not ask for a password. The reports page lists stations this callsign reported. |

TX or Tune is what sends `trx`. An empty line does not key.

## Receive only

**WEFAX-576.** IOC 576, 120 lines per minute. Black is 400 Hz below the cursor and white is 400 Hz above it. Pictures can be saved. TX does not send this mode, and START is never sent.

## Not on the menu

These do not key.

- **8PSK.** A different constellation from QPSK. Not on the menu.
- **Thor Micro.** Not in this build.
- MSHV modes still outside this program (MSK144, Q65, JT65, and the rest) stay listed in [mshv-modes.md](mshv-modes.md) until they are implemented.

CW stays in Lyra.

## Later

FLD-88 is queued. It is not in this build. JS8 Fast, Turbo, Slow, and Ultra are a different symbol clock and are not on the menu.
