---
title: Modes
---

Software in testing. STD is a 64-bit Windows application, Qt 6.11.1 MSVC. The rows below are what the Mode menu actually does in this tree. A mode is listed as keying only when TX builds its audio. On-air proof is still in progress.

Connect sends `AUDIO_START:0`. The text command START is never sent. CW stays in Lyra.

## Keying

| Menu | Rows | On the air path |
| --- | --- | --- |
| FT8, FT4, FT2 | one each | MSHV generator. 15 s, 7.5 s, 3.75 s. DIGU. The Tx line marker is a biohazard sign, glowing green on the line that goes out next. Multi works the queue and rebuilds the next pass after each one. DIST is miles or kilometers from Options → Decode List Options. |
| Hell | Feld Hell, Slow Hell, Feld Hell X5, Feld Hell X9, FSK Hell, FSK Hell-105, Hell 80 | Fldigi column rates. Feld Hell's raised-cosine edge is then limited to the 245 Hz channel. The marker is centered on the carrier. Reverse video flips the tape. [Hell notes](hell.html). |
| RTTY | one row, labeled RTTY | The transmit pane has Shift, Baud, Shape, Reverse, and ITA-2. Shape is on, Reverse is off, and ITA-2 is off. 45.45 baud and 170 Hz are selected. The lists are Fldigi's: baud 45, 45.45, 50, 56, 75, 100, 110, 150, 200, 300, and shift 23, 85, 160, 170, 182, 200, 240, 350, 425, 850. Fldigi lists 45 baud first; 45.45 is the usual amateur speed. Five bits, 1.5 stop, mark high unless Reverse is on. US TTY figures unless ITA-2 is on. USOS: a space sends LTRS, then the space. Squelch gates the print on the signal-to-noise meter. AFC, off until you turn it on, moves the waterfall cursor. |
| PSK | PSK31, PSK63, PSK125, PSK250, PSK500, PSK1000 | BPSK. Symbol rates 31.25, 62.5, 125, 250, 500, 1000 baud. A zero reverses the phase. The preamble is reversals and the tail is steady carrier, so the other station can open and close DCD. Latin-1 characters above ASCII 127 are sent. Squelch and AFC are the same switches as RTTY. |
| QPSK | QPSK31, QPSK63, QPSK125, QPSK250, QPSK500 | K=5 convolutional code. One dibit per varicode bit. The phase blend covers the whole symbol. 8PSK is not on the menu. |
| Olivia | 4/250, 8/250, 8/500, 16/500, 16/1000, 32/1000 | Gray-coded tones. Baud 62.5, 31.25, 62.5, 31.25, 62.5, 31.25. |
| Contestia | the same six | Same shape as Olivia, Contestia scramble. |
| MFSK | MFSK4, 8, 11, 16, 22, 31, 32, 64, 64L, 128, 128L | NASA K=7, varicode, diagonal interleaver. The L rows use the long interleaver. |
| DominoEX | Micro, 4, 5, 8, 11, 16, 22 | 18-tone incremental keying. FEC is off. Micro is 2 baud. |
| Thor | 4, 5, 8, 11, 16, 22, 25x4, 50x1, 50x2, 100 | Slow speeds are NASA K=7. 25x4, 50x1, 50x2, and 100 are IEEE K=15. 18-tone incremental keying. |
| MT63 | 500S, 500L, 1000S, 1000L, 2000S, 2000L | 64 carriers, Walsh code, short or long interleave. Centered on the cursor. |
| JT65 | JT65A, JT65B, JT65C | MSHV generator. 60 s. Tone 0 stays near 1270 Hz. DIGU. |
| Q65 | Q65A, Q65B, Q65C, Q65D | MSHV generator. 30 s. The tone follows the cursor. DIGU. |
| JS8 | one row, labeled JS8 | JS8Call-improved Normal. 15 s, 50 Hz, JS8 Costas. The screen is band activity, the conversation, and call activity, with HB, CQ, Reply, SNR?, INFO?, STATUS?, GRID?, Directed, and Deselect. Heartbeat, CQ, and directed frames go out at the next 15 s UTC period, one frame each, starting half a second in. A longer message continues on the following periods. Reply and Deselect do not key. Band buttons QSY to the suggested calling dials, 160 m through 6 m. 2 m is in that guide and is not on the row. Changing mode clears the receive and transmit panes. |
| WSPR | one row, labeled WSPR | WSPR-2 from WSJT-X and JTDX. Even UTC minutes. Band buttons QSY to the WSPR dials. Tx power, Tx Pct, Band Hopping. No macro bar. Locator is Options → User. Receive titles stay above the scrolling rows: Time, dB, DT, Frequency, Drift, Call, Locator, Report. A rule separates the periods. DT is the time offset. Report is the dBm in the message. Decodes stay in the receive colour after a red TX echo. Options → Network can post each heard station to WSPRnet. That post is off until WSPRnet is set to On, and those messages show only while WSPR is the mode. WSPRnet does not ask for a password. The reports page lists stations this callsign reported. |

Hell, RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, and MT63 show a QSO bar for the station being worked: Call, Name, QTH, St, Pr, Loc, RST in, Out, and Notes. Right-click a received word to fill a field. Double-click a callsign to fill Call. Auto RST fills Out from the TCI S-meter and stays off until you turn it on. Log writes ADIF. JS8, WSPR, WEFAX, and the FT8 family do not show the bar. The Feld Hell tape keeps full rows in a scrollable history, and Save tape writes a PNG under Documents\STD\hell.

TX or Tune is what sends `trx`. An empty line does not key.

## Receive only

**WEFAX-576.** IOC 576, 120 lines per minute. Switching to it hides the Hell box, the TX buttons, and the macros. Black is 400 Hz below the cursor and white is 400 Hz above it, about 1 kHz wide around the 1900 Hz marker. A 2.4 kHz USB filter passes it. Reception mixes to the cursor, filters, then reads the FM. The stations pane lists assigned frequencies by ITU region. Auto uses the locator in Options → User. NOAA hours are marked on the frequencies that have them. A click sets the dial 1.9 kHz under the assigned frequency, USB, marker 1900 Hz, and does not key. A 300 Hz start tone opens a picture and a 450 Hz stop tone saves it. Start opens a chart that is already running. A click on the picture sets the left edge, and a right-click puts it back. Slant, next to Save, is parts per million, 10 ppm a step. Zero is 120 lines per minute. Fit sizes the chart to the pane. Auto save, Open folder, and Save write PNGs. TX does not send this mode, and START is never sent.

## PSK Reporter

Options → Network Configuration. Enable PSK Reporter style spots is off until checked. Antenna is the description sent with each spot. Also send WSPR spots to PSK Reporter is a separate checkbox and stays off. The map uses the callsign already saved under Options → User. There is no second callsign or locator. It does not send START, and it does not key.

## WSPRnet

Options → Network Configuration. Off and On. On posts each WSPR station STD decodes. Off posts nothing. WSPRnet does not ask for a password. It does not post your own transmission. Options → WSPRnet reports opens the page of stations reported by the callsign in Options → User. There is no second callsign or locator. It does not send START, and it does not key.

## File

File → Backup settings saves layouts and the callsign and locator already stored under Options → User. It does not copy the TCI host or port. File → Import settings restores that file and leaves the TCI host and port as they are. File → Backup logbook copies the ADIF file and leaves the original in place. File → Export ADIF writes a copy another logger can import. None of this keys, and START is never sent.

## Halt

Halt ends the transmission immediately, even in the middle of a slot, turns TRX off, and stops the sequence. If nothing is transmitting, Halt does not key. Tx1 through Tx6 stay as they are.

## Decode

Options → Decode sets Fast, Normal, or Deep, and AP. Fast, Normal, Deep, and AP reach Q65. AP reaches JT65. Decode → JT65 and Q65 holds averaging, auto clear after a Q65 decode, Clear Averaging Now, Q65 single signal, EME delay, drift correction, JT65 VHF/UHF, deep search against the ADIF log, and the JT65 signal cap. Averages clear on a mode change or a dial move of more than 1 kHz. None of this keys.

## Waterfall

Floor is the palette bottom in dB. Range is how many dB the palette spans. A louder peak stretches that span, so the skirts of a strong trace stay colored. Auto sets the floor under the noise. Flatten levels the band slope. Hell, RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, MT63, WSPR, and WEFAX center the marker on the carrier. FT8, FT4, FT2, JT65, Q65, and JS8 start at the cursor. Speed, Start, and BW on the mode bar keep their last character. The Extra caption is the word Extra. View → Save layout stores Floor, Range, and Auto with that mode. None of this keys.

## Not on the menu

These do not key.

- **8PSK.** A different constellation from QPSK. Not on the menu.
- **Thor Micro.** Not in this build.
- MSHV modes still outside this program stay listed in [mshv-modes.md](mshv-modes.md): MSK144, MSKMS, JTMS, FSK441, FSK315, ISCAT-A, ISCAT-B, JT6M, and PI4. JT65 and Q65 are on the menu.

CW stays in Lyra.

## Later

FLD-88 is queued. It is not in this build. JS8 Fast, Turbo, Slow, and Ultra are a different symbol clock and are not on the menu.
