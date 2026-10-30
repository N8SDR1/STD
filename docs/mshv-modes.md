# MSHV modes

Working list. STD does not vendor the MSHV tree. The names are `ModeStr` in
`MSHV/src/config_str_all.h` (`COUNT_MODE` is 19). Menu order is `pos_mods`
in that file. CW is not one of these modes. CW stays in Lyra.

Connect sends `AUDIO_START` for receiver 0. The text command START is never
sent.

## In STD now

| Mode | What works |
| --- | --- |
| FT8 | Decode and TX. 15 s. DIGU. MSHV generator and sequencer. |
| FT4 | Decode and TX. 7.5 s. DIGU. |
| FT2 | Decode and TX. 3.75 s. DIGU. |
| JT65A | Decode and TX. 60 s. Tone 0 stays near 1270 Hz. DIGU. |
| JT65B | Decode and TX. 60 s. Wider tone spacing than JT65A. |
| JT65C | Decode and TX. 60 s. Wider tone spacing than JT65B. |
| Q65A | Decode and TX. 30 s. The tone follows the cursor. DIGU. |
| Q65B | Decode and TX. 30 s. Wider tone spacing than Q65A. |
| Q65C | Decode and TX. 30 s. Wider tone spacing than Q65B. |
| Q65D | Decode and TX. 30 s. Wider tone spacing than Q65C. |

Contest exchanges on Tx1 through Tx6 are the MSHV activity list. They are
not extra modes.

## Not in the menu yet

These do not key. Do not drop them.

| Mode | MSHV note |
| --- | --- |
| MSK144 | Meteor scatter. |
| MSKMS | MSHV comments call this MSK144ms. |
| JTMS | |
| FSK441 | |
| FSK315 | |
| ISCAT-A | |
| ISCAT-B | |
| JT6M | |
| PI4 | |

## Not this list

Hell, RTTY, BPSK, Olivia, Contestia, MFSK, DominoEX, Thor, and WEFAX come from Fldigi,
not from `ModeStr`. Feld Hell is 17.5 columns/s, 14 pixels, 2 ms raised cosine,
ink on the carrier. Slow Hell is 2.1875 columns/s. X5 is 87.5 and X9 is 157.5,
hard keyed. FSK Hell, FSK Hell-105, and Hell 80 are two-tone, reverse off,
shifts 122.5, 55, and 300 Hz. The receive tape reads that shift. RTTY is 45.45 baud and 170 Hz selected, mark high on DIGU unless Reverse is on. The transmit pane also sets Fldigi's other baud rates and shifts, shaped keying, and ITA-2 figures. BPSK is
PSK31, PSK63, PSK125, PSK250, PSK500, and PSK1000. Olivia and Contestia are
4/250, 8/250, 8/500, 16/500, 16/1000, and 32/1000, Gray-coded tones from Pawel
Jalocha's block code. MFSK is MFSK4, MFSK8, MFSK11, MFSK16, MFSK22, MFSK31,
MFSK32, MFSK64, MFSK64L, MFSK128, and MFSK128L: Gray-coded tones, NASA K=7,
IZ8BLY varicode, and the fldigi diagonal interleaver. The L speeds use the
long interleaver.
DominoEX is Micro, 4, 5, 8, 11, 16, and 22: 18-tone incremental frequency
keying and the nibble varicode. FEC is off. Micro is 2 baud.
Thor is Thor 4, 5, 8, 11, 16, 22, 25x4, 50x1, 50x2, and 100. The slow speeds
are NASA K=7 with a 4 by 10 interleaver. 25x4, 50x1, 50x2, and 100 are IEEE
K=15. Thor Micro is not in this build.
WEFAX-576 receives only (IOC 576, 120 lines per minute) and saves PNGs to the
folder. They are still being proved on the air.

MT63-500S, MT63-500L, MT63-1000S, MT63-1000L, MT63-2000S, and MT63-2000L key.
QPSK31, QPSK63, QPSK125, QPSK250, and QPSK500 key. 8PSK is not in this build.
JS8 Normal keys. It is the JS8Call-improved 15 second frame. Fast, Turbo, Slow, and Ultra are not in this build.
WSPR-2 keys. It is the WSJT-X / JTDX-improved frame, not an MSHV generator. PSK Reporter upload is off until Options → Network Configuration enables it. The map uses the callsign already saved under Options → User. WSPRnet upload of stations STD decodes is also off until WSPRnet is set to On. WSPRnet does not ask for a password. Callsign and locator stay in Options → User.
CW stays in Lyra.
