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

## What is removed

These tags are deleted, both as `<TAG>` and as `<TAG:...>`:

`TX`, `RX`, `CLRTX`, `CLRRX`, `LOG`.

`<LOG>` and `<LOG:msg>` do not write the ADIF file. They disappear from the text.

## What stays literal

`<RST>` stays in the box as written. `<MYRST>` is filled from RST under Options → User. If the operator then presses TX, a leftover `<RST>` can be keyed as text.

`<CALL>` is also left as written. The RST/LOG button in a typical Fldigi file looks like:

```text
<CALL> de <MYCALL> <RST> TU <LOG> <CLRRX><CLRTX> <RX>
```

With call N8SDR that becomes:

```text
<CALL> de N8SDR <RST> TU
```

Nothing is stored.

## When a QSO is stored

This path is not the RST/LOG button, and it is not Fldigi `<LOG>`. Logging runs after a transmission, and only when all of these are true:

- The sent line's last word is `73` or `RR73`.
- The line contains the other station's call from the current QSO.
- One log option is on: automatic log, log prompt, WSJT-X UDP (`log/udpQso` or `log/udpAdif`), or DXKeeper TCP (`log/tcp`).

That path writes ADIF and, when those loggers are enabled, the UDP or TCP copy. A prompt asks before the write when automatic log is off and the prompt is on. The self-test does not write a log.

The text command START is never sent. CW stays in Lyra.
