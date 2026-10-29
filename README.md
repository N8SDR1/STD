# STD

**Shift, Tone, Decode.** Software in testing. Version 0.1.0. Around the shack it also answers to **Sending Transmittal Disease**.

The real expansion is Shift, Tone, Decode. The other one is the joke. STD is a 64-bit Windows application, a Qt 6.11.1 MSVC digimode console for a TCI radio, currently in the contagious stage: it keys on the bench, it has not finished making the rounds, and the recommended treatment is another shot of bourbon while the modes get proved on the air. [Lyra](https://n8sdr1.github.io/Lyra-SDR-cpp/) is the transceiver. [SDRLogger+](https://n8sdr1.github.io/SDRLoggerPlus/) is the logbook. CW stays in Lyra.

**STD: Help keep the infections spreading and buy us a shot of bourbon.**

[☕ Donate via PayPal](https://www.paypal.com/donate/?business=NP2ZQS4LR454L&no_recurring=0&item_name=STD%3A+Help+keep+the+infections+spreading+and+buy+us+a+shot+of+bourbon.&currency_code=USD)

Copyright (C) 2026 Rick Langford (N8SDR).

## License

GNU General Public License, version 3. The license text is [`COPYING`](COPYING).

Original files in this repository are GPL-3.0-or-later (`SPDX-License-Identifier: GPL-3.0-or-later`). Modem math rewritten from Fldigi (Dave Freese W1HKJ and the Fldigi authors, GPL-3) and the FT8, FT4, and FT2 path from MSHV are GPL-3. CW stays in Lyra.

## Screenshots

![STD on 20 m FT8](screenshots/landingpage/STD%20digital.png)

![STD transmitting on 20 m FT8](screenshots/landingpage/Screenshot%202026-10-29%20130502.png)

![Feld Hell test](screenshots/landingpage/Screenshot%202026-10-29%20125605.png)

## Modes that key

TX sends one of these when the operator presses TX or Tune. Opening the radio does not key. The text command START is never sent.

| Mode | What this build sends |
| --- | --- |
| FT8 | 15 s. DIGU. MSHV generator. |
| FT4 | 7.5 s. DIGU. |
| FT2 | 3.75 s. DIGU. |
| Feld Hell | 17.5 columns/s, 14 pixels per column, 2 ms raised cosine. Ink is the carrier. |
| Slow Hell | 2.1875 columns/s, same cosine. |
| Feld Hell X5 | 87.5 columns/s, hard keyed. |
| Feld Hell X9 | 157.5 columns/s, hard keyed. |
| FSK Hell | 17.5 columns/s, 122.5 Hz shift, reverse off. A tone stays up. |
| FSK Hell-105 | 17.5 columns/s, 55 Hz shift, reverse off. |
| Hell 80 | 35 columns/s, 300 Hz shift, reverse off. |
| RTTY | One menu row. 45.45 baud, 170 Hz, 5 bits, 1.5 stop, mark high, reverse off. A space sends LTRS, then the space. |
| PSK31 through PSK1000 | BPSK at 31.25, 62.5, 125, 250, 500, and 1000 baud. A zero reverses the phase. |
| Olivia | 4/250, 8/250, 8/500, 16/500, 16/1000, 32/1000. |
| Contestia | The same six rows. |
| QPSK31 through QPSK500 | K=5 convolutional code at 31.25, 62.5, 125, 250, and 500 baud. |
| MFSK | MFSK4, 8, 11, 16, 22, 31, 32, 64, 64L, 128, 128L. NASA K=7. The L rows use the long interleaver. |
| DominoEX | Micro, 4, 5, 8, 11, 16, 22. 18-tone incremental keying. FEC is off. Micro is 2 baud. |
| Thor | 4, 5, 8, 11, 16, 22 are NASA K=7. 25x4, 50x1, 50x2, and 100 are IEEE K=15. Thor Micro stays off the menu. |
| MT63 | 500S, 500L, 1000S, 1000L, 2000S, 2000L. 64 carriers, Walsh code, short or long interleave. |
| JS8 | JS8Call-improved Normal. 15 s, 50 Hz. The transmit box is the payload. |
| WSPR | WSPR-2 from WSJT-X and JTDX. Tx power, Tx Pct, and band hopping. The locator is Options → User. |

## Receive only

**WEFAX-576** receives. IOC 576, 120 lines per minute. Black is 400 Hz below the cursor and white is 400 Hz above it. It does not key, and it does not send START.

## Still in progress

These are not on the Mode menu. They do not key.

- **8PSK.** A different constellation from QPSK. Not on the menu.
- **FLD-88** is queued. JS8 Fast, Turbo, Slow, and Ultra are not on the menu.
- MSHV modes still outside STD are listed in [`docs/mshv-modes.md`](docs/mshv-modes.md).

CW stays in Lyra. STD does not send CW.

## Macros and the log

The RST/LOG macro does not log. Fldigi's `<LOG>` tag is not implemented. Pressing that button inserts text, and nothing is stored. Adding Fldigi-style `<LOG>` is a later change.

`<MYCALL>` and `<MYLOC>` are filled in. `<LOG>` is deleted from the text. `<RST>` and `<MYRST>` stay in the box as written, so a later TX can send the characters `<RST>`. `<CALL>` is left as written too.

A separate path, not the macro tag, stores a QSO when the sent line ends in `73` or `RR73`, that line contains the other station's call, and a log option is on (automatic, prompt, WSJT-X UDP, or DXKeeper TCP). That path writes ADIF.

## Connect

Radio → Connect opens one WebSocket. The default port is 50001.

- Connect sends `AUDIO_START:0`.
- Disconnect sends `AUDIO_STOP:0`.
- The sideband command is `modulation:0,digu` or `modulation:0,digl`.
- `trx:0,true,tci` goes out only when the operator presses TX or Tune.

The text command START is never sent.

Options → PSK Reporter map opens the map for the callsign in Options → User. Options → Network Configuration uploads a spot after a decode only when Enable is checked. That box is off until the operator turns it on. There is no second callsign field.

Help → Check for Updates uses the same notice as Lyra. A newer GitHub release, including a pre-release, opens a dialog the first time, then leaves an indicator on the brand bar.

## Build

STD is a 64-bit Windows application. The kit is Qt 6.11.1 MSVC at `C:\Qt\6.11.1\msvc2022_64`. CMake is `C:\Qt\Tools\CMake_64\bin\cmake.exe`. The compiler is Visual Studio 18 Community. It needs Qt 6 Widgets, Qt 6 GuiPrivate, and Qt 6 WebSockets. `ft8dec.dll` is built by `src/ft8/build_ft8dec.cmd` from the sibling MSHV tree and is loaded only to decode. The MinGW kit at `C:\Qt\6.11.1\mingw_64` is not the kit this build uses.

## Run

The built program is `build\std.exe`. The window title is STD. The icon is the purple capsule from [`resources/std-icon.png`](resources/std-icon.png). The Windows installer is `dist\STD-Setup-0.1.0.exe`, built with Inno Setup the same way as Lyra. Setup, the Start Menu, and the desktop shortcut use that icon.

A headless check constructs the window and exits. Set `STD_SELFTEST=1` and `QT_QPA_PLATFORM=offscreen`, then run `build\std.exe`. Exit status 0 prints `STD self-test ok`.

## More

- Web version of this page: <https://n8sdr1.github.io/STD/>
- Releases: <https://github.com/N8SDR1/STD/releases> (v0.1.0 pre-release, testing phase · highly transmissible)
- Wiki: <https://github.com/N8SDR1/STD/wiki>
- Longer notes: [modes](docs/modes.md), [Hell](docs/hell.md), [macros](docs/macros.md), [connect](docs/connect.md)
