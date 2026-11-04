# STD

**Shift, Tone, Decode.** Software in testing. Version 0.1.5. Around the shack it also answers to **Sending Transmittal Disease**.

The real expansion is Shift, Tone, Decode. The other one is the joke. STD is a 64-bit Windows application, a Qt 6.11.1 MSVC digimode console for a TCI radio, currently in the contagious stage: it keys on the bench, it has not finished making the rounds, and the recommended treatment is another shot of bourbon while the modes get proved on the air. [Lyra](https://n8sdr1.github.io/Lyra-SDR-cpp/) is the transceiver. [SDRLogger+](https://n8sdr1.github.io/SDRLoggerPlus/) is the logbook. CW stays in Lyra.

**STD: Help keep the infections spreading and buy us a shot of bourbon.**

[☕ Donate via PayPal](https://www.paypal.com/donate/?business=NP2ZQS4LR454L&no_recurring=0&item_name=STD%3A+Help+keep+the+infections+spreading+and+buy+us+a+shot+of+bourbon.&currency_code=USD)

Copyright (C) 2026 Rick Langford (N8SDR).

## License

GNU General Public License, version 3. The license text is [`COPYING`](COPYING).

Original files in this repository are GPL-3.0-or-later (`SPDX-License-Identifier: GPL-3.0-or-later`). Modem math rewritten from Fldigi (Dave Freese W1HKJ and the Fldigi authors, GPL-3) and the FT8, FT4, and FT2 path from MSHV are GPL-3. Speech to text uses whisper.cpp (the ggml authors, MIT), built in; its notice is in `resources/licenses/whisper.cpp-LICENSE`. CW stays in Lyra.

## Screenshots

![STD on 20 m FT8](screenshots/landingpage/STD%20digital.png)

![STD transmitting on 20 m FT8](screenshots/landingpage/Screenshot%202026-10-29%20130502.png)

![Feld Hell test](screenshots/landingpage/Screenshot%202026-10-29%20125605.png)

## Modes that key

TX sends one of these when the operator presses TX or Tune. Opening the radio does not key. The text command START is never sent. On Hell, RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, and MT63, a finished pass takes the sent text off the front of the transmit box, as Fldigi does, so the next TX sends only new text. A stopped pass takes the characters that already went out. Text changed during the pass is left alone. JS8 keeps its own queue. RTTY, PSK, and QPSK send live, as Fldigi does: TX keys and sends what is in the box, text typed while on the air goes out as it is typed, and an empty box sends idle (LTRS in RTTY, phase reversals in PSK) without dropping TRX. Press TX once to end after the box is sent, and again to stop at once; characters not yet on the air go back into the box. A ▶ macro (one with `<TX>`) keys when Macros key is on and, with `<RX>`, returns to RX when its text is done. A macro pressed while on the air is added to the box. Back to RX after (on, 25 s by default; 5, 10, 15, or 25 s) ends a live transmission by itself when nothing new was typed for that long; turn it off to stay keyed until you press TX. Options → Speech… reads received text aloud with a Windows voice on a sound device you pick, not through the radio. It reads RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, and MT63 a line at a time, or a run of finished words on a long line, and waits quietly for new text. Callsigns and reports are spelled out, noise runs can be skipped, and it stops while you transmit. Hell is a picture and is not read; nor are JS8 or the FT8 family. With read-back on, a Speech chip beside CLR RX mutes and unmutes, and the number beside it is the volume. It is off until you turn it on. Talk does the reverse: it turns your voice into transmit text. In Options → Speech… → Transmit pick a microphone, set the gate, and download a Whisper model (75 to 490 MB, fetched only when you choose one, so STD itself stays small). It runs on the CPU. Hold Talk and speak. The words land at the end of the transmit box with callsigns and reports written the ham way (Kilo Eight Alpha Bravo becomes K8AB, five nine nine becomes 599), and nothing keys until you press TX. **My words** in the same tab lists what Talk should expect (club calls, names, towns, your rig and antenna); your name and QTH from Options → User and the station in the QSO bar are added by themselves. **Corrections** fixes words it keeps getting wrong, one per line as `heard = send`. To teach one on the spot, select the wrong text in the transmit box, right-click, and pick Talk: always send … as, or add it to My words. The waterfall picture updates once per turn, so a quiet band does not keep the CPU and the GPU busy.

| Mode | What this build sends |
| --- | --- |
| FT8 | 15 s. DIGU. MSHV generator. |
| FT4 | 7.5 s. DIGU. |
| FT2 | 3.75 s. DIGU. |
| JT65A, JT65B, JT65C | 60 s. DIGU. Tone 0 stays near 1270 Hz. |
| Q65A, Q65B, Q65C, Q65D | 30 s. DIGU. The tone follows the cursor. |
| Feld Hell | 17.5 columns/s, 14 pixels per column. A 2 ms raised-cosine edge, then the envelope is limited to the 245 Hz channel. The marker is centered on the carrier. Reverse video flips the tape. |
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
| JS8 | JS8Call-improved Normal. 15 s, 50 Hz. Band activity, conversation, and call activity, with HB, CQ, Reply, SNR?, INFO?, STATUS?, GRID?, Directed, and Deselect. TX waits for the next 15 s UTC period and sends one frame, starting half a second in. A longer message continues on the following periods. |
| RADE V1 | FreeDV RADE digital voice, about 1500 Hz wide, from the FreeDV rade_c library. DIGL on 160, 80, and 40 m, DIGU on 60 m and 30 m and up, the FreeDV convention. 60 m (the 5.3 chip) has the FreeDV Reporter dials 5.3685 and 5.4035, and FT8 has 5.357; other modes hide it. Speech from the Speech microphone. The end-of-over carries your callsign from Options → User the way the FreeDV app sends it, so FreeDV stations and FreeDV Reporter can show who you are, and **Heard** shows the call from the other station's end-of-over. The microphone is levelled before the encoder, as FreeDV does. TX waits about a quarter second to fill its buffer, so the modem frames go out without gaps, and holds 200 ms of silence after the end-of-over. Drive 100 is the FreeDV app's level. **RX** (green while receiving) and **TX** work like the radio's switch; click RX to end your over with your callsign. Halt is hidden in RADE. **Scan** steps the FreeDV Reporter calling dials (Dwell 5–30 s each) and listens for a RADE station; **Stay on sync** stops it on the first one heard, or it keeps looping. Right-click Scan to scan all bands or only the ones you check. **Mic NR** (RNNoise noise suppression) and **BW expand** (decoded speech widened to 48 kHz) are on by default, as in the FreeDV app. FreeDV Reporter is in Options → Network Configuration. |
| RADE V2 | Experimental, about 860 Hz, will not talk to V1. BPF (on by default) is the modem band-pass; it is greyed out on V1, which sends without one, as FreeDV does. |
| WSPR | WSPR-2 from WSJT-X and JTDX. Even UTC minutes. Tx power, Tx Pct, and band hopping. The locator is Options → User. Rows stay in the receive colour, under titles, with a rule between periods. Heard stations post to WSPRnet only when WSPRnet is On, and only while WSPR is the mode. |

## Receive only

**WEFAX-576** receives. IOC 576, 120 lines per minute. Stations are listed by ITU region, with NOAA hours on the frequencies that have them. A click sets the dial 1.9 kHz under the assigned frequency, USB, marker 1900 Hz. Start, a left-edge click, Auto save, Open folder, and Fit are on that screen. It does not key, and it does not send START.

## Still in progress

These are not on the Mode menu. They do not key.

- **8PSK.** A different constellation from QPSK. Not on the menu.
- **FLD-88** is queued. JS8 Fast, Turbo, Slow, and Ultra are not on the menu.
- MSHV modes still outside STD are listed in [`docs/mshv-modes.md`](docs/mshv-modes.md).
- Options → **Contest Mode (RTTY, PSK, Hell)** puts a contest row on the QSO bar: ARRL RTTY Roundup, CQ WW RTTY, NA Sprint, Field Day, a Feld Hell sprint, a serial-number contest, or Custom. F1 to F8 send the Run or S&P messages, Enter sends the next one, their exchange is read from their text, DUPE checks your log, and each QSO logs CONTEST_ID, STX/SRX, and the exchanges. Off until you turn it on. Details in [contest](docs/contest.md).
- In every menu, an option you can switch on or off has a small circle that fills green when on.
- Options → **TX Confirmation If 73 Or RR73** is on by default, as in MSHV: in MA Std and MA DX their RR73 is answered with 73 before the QSO ends and is logged. Off, FT8, FT4, FT2, and Q65 end the QSO on their RR73 with no 73. JT65 always sends the 73.

CW stays in Lyra. STD does not send CW.

## Macros and the log

STD reads Fldigi `.mdf` macro files as they are, and a macro's own tags decide what a click does: `<TX>` keys and sends (the button shows ▶), `<RX>` goes back to RX after the text, `<CLRTX>` empties the transmit box first, and a macro without `<TX>` adds its text to the end of the box, so macros can be chained. The **Macros key** chip beside the bank button is the lock: red ON lets ▶ macros key, OFF keys nothing. `<LOG>`, `<CLRQSO>`, `<CLRRX>`, `<INCR>`, and `<DECR>` run once a keyed macro's text has gone out, and `<TIMER:NN>` listens NN seconds and sends it again until you type, click, or hear your call.

`<MYCALL>`, `<MYLOC>`, `<MYNAME>`, `<MYQTH>`, `<ANTENNA>`, `<TX_PWR>` and the other station tags come from Options → User. On a keyboard mode the QSO bar fills `<CALL>`, `<NAME>`, `<QTH>`, `<ST>`, `<PR>`, `<LOC>`, `<RST>`, and `<QSOTIME>`. A ▶ macro that needs an empty QSO bar field does not key; `<NAME|OM>` sends OM instead. The QSO bar offers the name, QTH, report, and locator it reads in their text (a click or Tab takes one), and a call already in STD's log (or a second ADIF log, which STD keeps a copy of in its log folder) fills the empty fields from the last QSO; `<WELCOME>` and `<LASTQSO>` use that. Options → Lookup can also fill name, QTH, state, and locator from HamQTH or QRZ.com, only the name for a portable call, and anything you type wins. Hover a button to see what it will send. `<WX>` sends your local weather from Options → Weather: Open-Meteo from your locator (free, no account), an NWS station, or your Ambient Weather or Ecowitt station. Fldigi tags STD does not run, such as `<IDLE>` and `<MODEM:…>`, are left out and named, never sent as text. Details are in [macros](docs/macros.md).

Options → Log Options → Log file and logger → **Link to a logger over WSJT-X UDP** sends decodes and Status (dial, mode, and the call being worked) the way WSJT-X does, so SDRLogger+, GridTracker, JTAlert, and Log4OM see STD, and the call in STD fills the logger's log entry. With **Accept requests from the logger** on, a click on a decode in the logger works that station in STD just as a double-click in STD would, and the logger can halt TX or set the free text. For SDRLogger+: turn on its Decoder Link (UDP) on port 2237; in STD tick **Link to a logger over WSJT-X UDP**, **Accept requests from the logger**, and **WSJT-X UDP logged QSO** (leave logged ADIF off; SDRLogger+ logs only from logged QSO), port 2237; and tick **Log Automatically QSO** (or Prompt Me To Log QSO) under Options → Log Options. The link alone does not log. Step by step in [connect](docs/connect.md).

A separate path, not the macro tag, stores a QSO when the sent line ends in `73` or `RR73`, that line contains the other station's call, and a log option is on (automatic, prompt, WSJT-X UDP, or DXKeeper TCP). That path writes ADIF.

## Connect

Radio → Connect opens one WebSocket. The default port is 50001.

- Connect sends `AUDIO_START:0`.
- Disconnect sends `AUDIO_STOP:0`.
- The sideband command is `modulation:0,digu` or `modulation:0,digl`.
- `trx:0,true,tci` goes out only when the operator presses TX or Tune.

The text command START is never sent.

Options → PSK Reporter map opens the map for the callsign in Options → User. Options → Network Configuration uploads a spot after a decode only when Enable is checked. Antenna is the description sent with the spot. Also send WSPR spots to PSK Reporter stays off until checked. WSPRnet is Off or On in that same dialog, and Off posts nothing. That box is off until the operator turns it on. FreeDV Reporter is Off or On there too: On shows your call, locator, dial and TX on qso.freedv.org while a RADE mode is selected, and reports each RADE station you hear with its SNR. The box beside On is the Message column (name, QTH, rig), also on the RADE screen as Reporter message. The bottom status strip has a spot chip for the reporter the current mode feeds (PSK RPT in FT8/FT4/FT2/JT65/Q65/JS8, WSPRNET in WSPR, FREEDV RPT in RADE): click the name to open that service's live page in your browser, and On/Off to turn it on or off. View FreeDV Reporter opens the map without connecting. There is no second callsign field. File → Backup settings saves layouts and the callsign and locator from Options → User. It does not copy the TCI host or port. File → Import settings, Backup logbook, and Export ADIF do not key.

Help → Check for Updates uses the same notice as Lyra. A newer GitHub release, including a pre-release, opens a dialog the first time, then leaves an indicator on the brand bar.

## Build

STD is a 64-bit Windows application. The kit is Qt 6.11.1 MSVC at `C:\Qt\6.11.1\msvc2022_64`. CMake is `C:\Qt\Tools\CMake_64\bin\cmake.exe`. The compiler is Visual Studio 18 Community. It needs Qt 6 Widgets, Qt 6 GuiPrivate, and Qt 6 WebSockets. `ft8dec.dll` is built by `src/ft8/build_ft8dec.cmd` from the sibling MSHV tree and is loaded only to decode. The MinGW kit at `C:\Qt\6.11.1\mingw_64` is not the kit this build uses.

## Run

The built program is `build\std.exe`. The window title is STD. The icon is the purple capsule from [`resources/std-icon.png`](resources/std-icon.png). The Windows installer is `dist\STD-Setup-0.1.5.exe`, built with Inno Setup the same way as Lyra. Setup, the Start Menu, and the desktop shortcut use that icon.

A headless check constructs the window and exits. Set `STD_SELFTEST=1` and `QT_QPA_PLATFORM=offscreen`, then run `build\std.exe`. Exit status 0 prints `STD self-test ok`.

## More

- Web version of this page: <https://n8sdr1.github.io/STD/>
- Releases: <https://github.com/N8SDR1/STD/releases> (v0.1.5 pre-release, testing phase · highly transmissible)
- Wiki: <https://github.com/N8SDR1/STD/wiki>
- Longer notes: [modes](docs/modes.md), [Hell](docs/hell.md), [macros](docs/macros.md), [contest](docs/contest.md), [connect](docs/connect.md)
