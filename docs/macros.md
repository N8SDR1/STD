---
title: Macros
---

STD reads Fldigi macro files (`.mdf`) as they are. File → Macros → Open picks the set in use; Save writes it back in Fldigi's format. There are 48 macros in 4 banks of 12. The bank button cycles them. Right-click it to go back a bank, or to give the bank a name such as CQ, QSO, or Sign-off.

## What a click does

A macro's own tags decide, as in Fldigi:

| In the macro | A click |
| --- | --- |
| `<TX>` | Presses TX and sends. The button shows ▶. |
| `<TX>` … `<RX>` | Sends, then goes back to RX. |
| no `<TX>` | Adds the text to the end of the transmit box. Nothing keys. |
| `<CLRTX>` | Empties the transmit box first. On its own it is a Clear button. |
| `<RX>` without `<TX>` | Pressed while on the air (Fldigi's KN and SK): the text joins the box and TX ends once it has gone out. |
| `<TX/RX>` | Keys, or on the air ends after the box. |

Macros add to the box instead of replacing it, so they can be chained: his call, then a reply, then your brag.

**Macros key** sits beside the bank button. It is the lock for all of this. Red, **▶ Macros key: ON**, lets ▶ macros key. **Macros key: OFF** keys nothing; every macro only puts its text in the transmit box and TX sends it. It stays as you left it.

Hover a button to see exactly what it will send, filled in, and what it does after. A label can hold tags too: `Reply <CALL>` shows as `Reply K8AB`.

## Filled in from Options → User

`<MYCALL>` is the callsign and `<MYLOC>` and `<MYGRID>` the locator.

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

## Filled in from the QSO bar

On Hell, RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, and MT63 the QSO bar holds the station you are working. Double-click a callsign in the receive text to fill Call. Right-click a word to put it in another field.

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
| `<WELCOME>` | nice to work you again, only when they are in your log |
| `<LASTQSO>` | The date of your last QSO with them |

**Their text, offered.** While you work a station STD reads their received text for the usual phrases: NAME IS BOB, OP BOB, QTH IS DAYTON OH, UR 579, LOC EM79, and DE K8AB while Call is empty. What it finds shows faintly in the empty field, such as *Bob?*, with a dashed outline. Click the field or press Tab to take it. Nothing is filled until you do, and a field you have filled is never changed.

**Your log, remembered.** When a call goes into Call, STD looks it up in its ADIF log. A station worked before fills the empty Name, QTH, St, and Loc from the last QSO, in blue italics until you change them, and the bar reads, for example, Worked 3x, last 2026-05-04 20m PSK31. A new call reads New station. Options → Lookup can add a second ADIF file, such as your logger's export, so stations you worked in other programs are remembered; a QSO in both counts once. STD keeps a copy of that file in its log folder (`log\imported`, under `%APPDATA%\N8SDR\STD`), refreshed when the original changes, and reads the copy if the original is moved or deleted. Options → Log Options → **Open log folder** opens that folder in Explorer; std.adi, STD's own log, is there too.

**The callbook.** Options → Lookup can also ask HamQTH (free) or QRZ.com when a call is entered. It fills only what your log left empty, in green: name, QTH, state, and locator. QRZ needs its XML Data subscription for more than the name. A portable call, one with a slash such as K8AB/P, W8XYZ/4, or VE3/K8AB, gets only the name, because the callbook QTH and grid are home and not where they are now; a checkbox turns that off. Type over any filled field: what is in the box when you log is what is logged.

Contest mode adds `<XOUT>` (or `<EXCH>`), your exchange with the 599 and serial, and `<XIN>`, theirs from the Exch box; see [contest](contest.md).

`<WELCOME>` sends "nice to work you again" when they are in your log and nothing when they are not; `<LASTQSO>` sends the date of the last QSO.

Auto RST, off until you turn it on, fills Out from the TCI S-meter. S9 is −73 dBm and each S-unit is 6 dB. It peak-holds a 1.25 second median that must stand 3 dB over a 30 second noise floor, or over Lyra's SNR when Lyra sends it. Typing in Out stops the fill until the next call. `<RST>` sends what Out shows when you click.

**An empty field.** A ▶ macro that needs an empty QSO bar field does not key. The field flashes red and the status line names it. Give a tag a fallback to send instead: `<NAME|OM>` sends OM when Name is empty. A macro that only adds to the box still adds, and the status line names what was empty.

## Other tags

| Tag | Sends |
| --- | --- |
| `<ZDT>`, `<ZT>`, `<ZD>` | UTC date and time, time, date. `<ZDT:%H%M>` takes a format. |
| `<LDT>`, `<LT>`, `<LD>` | The same, local time. |
| `<MODE>`, `<BAND>`, `<FREQ>` | The mode, the band (40m), the dial in kHz. |
| `<VER>` | STD and its version. |
| `<CNTR>` | The serial number, three digits: 007. `<LASTNO>` is the one before. |
| `<COMMENT:…>` | Nothing. A note for whoever reads the macro. |
| `<WX>` | Your local weather in one line: 54F, partly cloudy, wind SW 8 mph, humidity 72%. |
| `<WX_TEMP>`, `<WX_COND>`, `<WX_WIND>`, `<WX_HUM>` | One part each: 54F, partly cloudy, SW 8 mph, 72%. |

## Weather

Options → Weather picks where `<WX>` comes from:

| Source | Needs |
| --- | --- |
| Open-Meteo | Nothing but your locator in Options → User. Free, no account, anywhere. |
| NWS station | The nearest US observation station, such as KHAO. |
| Ambient Weather | Your station's API key and application key. |
| Ecowitt | Your station's application key, API key, and MAC. |

Units are °F and mph or °C and km/h. STD fetches every 10 minutes while a source is on and nothing while it is Off, which is the default. Save and fetch now shows what `<WX>` sends. Ambient and Ecowitt send no sky condition, so `<WX_COND>` stays empty with them. Until the first reading arrives a ▶ macro with `<WX>` does not key; `<WX|fine>` sends fine instead.

Lyra-style braces work as well: `{CALL}`, `{MYCALL}`, `{NAME|OM}`, and `{#}` for the serial.

## After it is sent

These run when a keyed macro's text has actually gone out. Halt or a stopped TX skips them. A macro without `<TX>` runs them when clicked.

| Tag | Does |
| --- | --- |
| `<LOG>`, `<LNW>` | Writes the QSO bar to the ADIF log, and to WSJT-X UDP when that logger is on. `<LOG:text>` adds text to Notes first. An empty Call logs nothing. |
| `<CLRQSO>`, `<CLRLOG>` | Clears the QSO bar. |
| `<CLRRX>` | Clears the receive text. |
| `<INCR>`, `<DECR>` | Adds 1 to or takes 1 off the serial. |
| `<TIMER:NN>` | Listens NN seconds, then sends the macro again. Typing, any macro, TX, Halt, picking a call, or your own call in the receive text stops it. |

A typical Fldigi RST/LOG button:

```text
<CALL> de <MYCALL> ur <RST> <RST> TU <LOG> <CLRRX><CLRTX> <RX>
```

With N8SDR in Options → User, W1AW in Call, and 599 in Out, it sends

```text
W1AW de N8SDR ur 599 599 TU
```

then logs W1AW and clears the receive text once that has gone out.

## Fldigi tags STD does not run

`<IDLE>`, `<ID>`, `<TXRSID>`, `<MODEM:…>`, `<QSYTO>`, `<INFO1>`, `<XOUT>` and the rest of Fldigi's rig and file tags are left out of the text, so they never go out as typing. The status line, the tooltip, and the editor name them. A macro made only of them does nothing.

## The editor

Right-click a macro, or Options → Macros. **A click** sets how it sends — add to the box, send and back to RX, or send and stay on — and **Replace the box** sets `<CLRTX>`. Both write the Fldigi tags into the text, so the file still works in Fldigi. **Sends now** under the text shows what it would send this moment, what is empty, and what STD leaves out. Apply stores it; Save writes the `.mdf`.

## The FT8 family

On FT8, FT4, FT2, JT65, Q65, and the other slot modes a macro puts its text in the selected message instead of a transmit box.

## When a QSO is stored

The QSO bar's Log button and `<LOG>` write the bar to the log. A second path, the FT8-family exchange, writes a QSO after a transmission when the sent line's last word is `73` or `RR73`, the line contains the other station's call, and a log option is on: automatic log, log prompt, WSJT-X UDP, or DXKeeper TCP.

The text command START is never sent. CW stays in Lyra.
