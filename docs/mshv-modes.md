# MSHV modes still out of STD

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
| Q65A | Submenu with Q65B, Q65C, and Q65D. |
| Q65B | |
| Q65C | |
| Q65D | |
| JT65A | Menu label adds HF/50 MHz. |
| JT65B | Menu label adds VHF/UHF. |
| JT65C | Menu label adds VHF/UHF. |
| PI4 | |

## Not this list

Hell, RTTY, BPSK, Olivia, Contestia, MFSK, DominoEX, and WEFAX come from Fldigi,
not from `ModeStr`. RTTY is 45.45 baud, 170 Hz, mark high on DIGU. BPSK is
PSK31, PSK63, PSK125, PSK250, PSK500, and PSK1000. Olivia and Contestia are
4/250, 8/250, 8/500, 16/500, 16/1000, and 32/1000, Gray-coded tones from Pawel
Jalocha's block code. MFSK is MFSK4, MFSK8, MFSK11, MFSK16, MFSK22, MFSK31,
MFSK32, MFSK64, and MFSK128: Gray-coded tones, NASA K=7, IZ8BLY varicode, and
the fldigi diagonal interleaver. MFSK64L and MFSK128L are not in this build.
DominoEX is 4, 5, 8, 11, 16, and 22: 18-tone incremental frequency keying and
the nibble varicode. FEC is off. DominoEX Micro is not in this build.
WEFAX-576 receives only (IOC 576, 120 lines per minute) and saves PNGs to the
folder. They are still being proved on the air.

MT63, Thor, and QPSK do not key yet.
CW stays in Lyra.
