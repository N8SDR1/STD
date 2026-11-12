---
title: Connect
---

Software in testing. STD speaks TCI over one WebSocket. Radio → Connect uses the saved host and port. The default port is 50001.

## What Connect sends

Connect sends one command:

```text
AUDIO_START:0
```

Disconnect sends:

```text
AUDIO_STOP:0
```

That stop is for this socket. A band change, while the socket is already open, may send the dial and the sideband:

```text
modulation:0,digu
```

or `modulation:0,digl`. That sideband command is not START, and it does not key.

## Link to a logger

Options → Log Options → Log file and logger → **Link to a logger over WSJT-X UDP** makes STD talk to a logger the way WSJT-X, JTDX, and MSHV do, on the UDP server and port set there (127.0.0.1 port 2237 to start). SDRLogger+ (Settings → Decoder Link), GridTracker, JTAlert, and Log4OM all listen for it.

- **Decodes** go to the logger as they are heard, so its decode list and alerts show STD's stations.
- **Status** goes once a second when something changes: the dial, the mode, the TX and RX offsets, whether STD is transmitting, and the call being worked. On RTTY, PSK, and the other keyboard modes that is the QSO bar's Call; on FT8 and the other slot modes it is the station in the selected message. The logger fills its log entry and looks the call up.
- **Accept requests from the logger**, on until you turn it off, lets the logger work stations in STD. A click on a decode there is a double-click on that decode in STD: the reply is set up and TX is armed, as it would be by hand. On a keyboard mode it puts the call in the QSO bar instead. The logger can also halt TX and set the free text. Requests addressed to another program on the same port are ignored.

### Setting up SDRLogger+

1. **In SDRLogger+:** Settings → Decoder Link (UDP) → turn **Source 1** on, UDP port **2237**, multicast blank. Save.
2. **In STD:** Options → Log Options → **Log file and logger…**
   - Tick **Link to a logger over WSJT-X UDP**.
   - Leave **Accept requests from the logger** ticked, so a click in SDRLogger+ works the station in STD.
   - Tick **WSJT-X UDP logged QSO**, so each finished QSO is logged in SDRLogger+ without a click there. Leave **WSJT-X UDP logged ADIF** off: SDRLogger+ logs only from the logged QSO datagram and ignores logged ADIF, and with both on another logger can get the same QSO twice.
   - UDP server **127.0.0.1**, UDP port **2237**, the same port as step 1. OK.
3. **In STD:** Options → Log Options → tick **Log Automatically QSO**, or **Prompt Me To Log QSO** to be asked each time. With neither ticked, nothing is logged, here or in SDRLogger+.

Within about 15 seconds SDRLogger+'s Decoder Link shows STD under Clients. STD's decodes then appear in its Digital Decodes panel, the call you are working fills its log entry, and a click on a CQ there works that station in STD. A QSO is logged when it ends: after a 73 or RR73 goes out, or in MA Std when their RR73 arrives and Options → TX Confirmation If 73 Or RR73 is off, as MSHV does. With it on (the default) STD answers their RR73 with 73 and logs then. STD keeps its own ADIF too.

4. **Optional, for Mark Needed Stations:** in the same Log file and logger dialog set **Worked list** to **STD's ADIF file + SDRLogger+ (live)**. STD then asks SDRLogger+ on this PC (127.0.0.1, port **5050**) for its whole log every five minutes and ten seconds after each QSO, so NEW, BAND, GRID, and B4 follow everything in SDRLogger+, not just what STD logged. The last copy is kept, so the marks still work while SDRLogger+ is closed. If SDRLogger+ started on another port because 5050 was busy, set that port. STD only reads; it sends nothing to SDRLogger+ this way.

**The link alone does not log.** It carries decodes, Status, and the logger's clicks. Sending a QSO to the logger is the logged QSO box (logged ADIF for loggers that want it), and deciding to log at all is Log Automatically QSO or Prompt Me To Log QSO.

The link sends a heartbeat every 15 seconds and logs nothing by itself; the WSJT-X UDP logged QSO and logged ADIF options still decide whether a finished QSO is sent. TCI to the radio is separate and unchanged. START is never sent.

## What keys

`trx:0,true,tci` is sent when the operator presses TX or Tune. `trx:0,false` follows when that transmission ends. No other button sends `trx`.

The text command START is never sent.

## What this program does not do

CW stays in Lyra. STD does not send CW.

QPSK31 through QPSK500 key. 8PSK does not. WEFAX-576 receives and does not key.

Audio inside STD is 48 kHz. STD is a 64-bit Windows application, built with Qt 6.11.1 MSVC at `C:\Qt\6.11.1\msvc2022_64`.
