---
title: Macros
---

Supports importing Fldigi custom macros.

## What is filled in

`<MYCALL>` becomes the call saved under Options → User. `<MYLOC>` and `<MYGRID>` become that locator. Those two rows are the only ones for the callsign and the locator.

Name, QTH, Antenna, and Power sit on that same dialog at full height. QTH is the station city. The contest tags stay in the scroller under them. An empty box erases the tag.

| Tag | Options → User |
| --- | --- |
| `<MYNAME>` | Name |
| `<MYQTH>` | QTH, the station city |
| `<ANTENNA>` | Antenna |
| `<TX_PWR>` | Power |
| `<MYRST>` | RST |
| `<MYCLASS>` | FD class |
| `<MYSECTION>` | FD section |
| `<MYSTATE>` | State |
| `<MYST>` | ST |
| `<MYCOUNTY>` | County |
| `<MYCNTY>` | CNTY |

## The station you are working

On Hell, RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, and MT63 the QSO bar holds the other station. Right-click a received word to put it in a field. Double-click a callsign to fill Call. Those fields are not saved as settings. JS8, WSPR, WEFAX, and the FT8 family do not show the bar.

| Tag | QSO bar |
| --- | --- |
| `<CALL>` | Call |
| `<NAME>` | Name |
| `<QTH>` | Their town |
| `<ST>` | St |
| `<PR>` | Pr |
| `<LOC>` | Loc |
| `<RST>` | Out, the report you send |
| `<QSOTIME>` | Time on, UTC, HHMM |

Auto RST, off until you turn it on, fills Out from the TCI S-meter. S9 is −73 dBm and each S-unit is 6 dB. It peak-holds a 1.25 second median that must stand 3 dB over a 30 second noise floor, or over Lyra's SNR when Lyra sends it. Turning it on asks the radio for S-meter readings and never turns them off. Typing in Out stops the fill until the next call. It does not key.

## What is removed from the text

These tags are deleted from the transmitted text, both as `<TAG>` and as `<TAG:...>`:

`TX`, `RX`, `CLRTX`, `CLRRX`, `LOG`, `LNW`, `CLRQSO`, `CLRLOG`.

On a keyboard mode, `<LOG>` and `<LNW>` also write the QSO bar to the ADIF file (NAME, QTH, STATE, VE_PROV, and the name over WSJT-X UDP when that logger is on). `<CLRQSO>` and `<CLRLOG>` clear the bar. An empty Call logs nothing. The Log button does the same write. None of this keys.

## What stays literal

`<MYRST>` is filled from RST under Options → User. That is your RST, not the Out field. The RST/LOG button in a typical Fldigi file looks like:

```text
<CALL> de <MYCALL> <RST> TU <LOG> <CLRRX><CLRTX> <RX>
```

With your call N8SDR, Call set to W1AW, and Out set to 599, that becomes:

```text
W1AW de N8SDR 599 TU
```

On a keyboard mode that button also logs the QSO. An empty Call logs nothing.

## When a QSO is stored

The QSO bar Log button, and `<LOG>` or `<LNW>` on a keyboard mode, write the bar immediately. They do not wait for 73.

A second path, the FT8-family exchange, runs after a transmission, and only when all of these are true:

- The sent line's last word is `73` or `RR73`.
- The line contains the other station's call from the current QSO.
- One log option is on: automatic log, log prompt, WSJT-X UDP (`log/udpQso` or `log/udpAdif`), or DXKeeper TCP (`log/tcp`).

That path writes ADIF and, when those loggers are enabled, the UDP or TCP copy. A prompt asks before the write when automatic log is off and the prompt is on. The self-test does not write a log.

The text command START is never sent. CW stays in Lyra.
