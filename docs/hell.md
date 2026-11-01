---
title: Hell
---

Software in testing. Hell keys. The rates below were checked against Fldigi 4.2.06 `feld.cxx` (Dave Freese W1HKJ, GPL-3) and rewritten into STD. They have not all been proved on a live QSO.

The text command START is never sent. Connect sends `AUDIO_START:0` only. CW stays in Lyra.

## Column rate

Each column is 14 pixels. Pixels per second are 14 times the column rate.

| Mode | Columns/s | Pixels/s | Keying |
| --- | --- | --- | --- |
| Feld Hell | 17.5 | 245 | Carrier on for ink, silence for paper. 2 ms raised-cosine edge, then the envelope is limited to the 245 Hz channel. A CQ measures about 258 Hz at −26 dB. Peak stays 0.6. The marker is that 245 Hz channel, centered on the carrier. |
| Slow Hell | 2.1875 | 30.625 | Same raised-cosine edge. The envelope limit is Feld Hell only. |
| Feld Hell X5 | 87.5 | 1225 | Hard keyed. |
| Feld Hell X9 | 157.5 | 2205 | Hard keyed. |
| FSK Hell | 17.5 | 245 | Two tones, 122.5 Hz shift. A tone stays up. |
| FSK Hell-105 | 17.5 | 245 | Two tones, 55 Hz shift. A tone stays up. |
| Hell 80 | 35 | 490 | Two tones, 300 Hz shift. A tone stays up. |

Reverse is off. On the FSK rows, ink is the lower tone and white is the higher tone.

The font is Fldigi's `feldhell_12` (12-pixel variable width, sent as 14-row columns). Case is kept. A space is a blank column run. Three leader dots go out before the operator's text, and three dots plus a space follow it.

![Feld Hell test](screenshots/std-hell.png)

## Receive tape

The amplitude-Hell tape uses that mode's column rate, with 20-pixel receive columns. FSK Hell, FSK Hell-105, and Hell 80 read the shift, so a steady tone still draws. Reverse video is on the Hell row. Off prints the tape black on white. On prints white on black. The carrier does not move, and the switch does not key.

The log under the tape keeps received text in the receive colour and prints sent text in red. A new transmission appends, and a finished one ends on its own line. The list follows the bottom only while you are already there. Switching from one Hell variant to another keeps that log. Leaving Hell clears it.

A full tape width moves up into a scrollable history, up to 500 rows. The wheel or the scroll bar reads a long transmission back. Save tape writes the history and the live strip as a PNG under Documents\STD\hell. Open opens that folder. Neither one keys.

## What TX does

Pressing TX builds the Hell audio and sends `trx` for that transmission. CLR TX does not key. The red marker for every Hell variant is centered on the carrier.
