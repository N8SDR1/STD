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

## What keys

`trx:0,true,tci` is sent when the operator presses TX or Tune. `trx:0,false` follows when that transmission ends. No other button sends `trx`.

The text command START is never sent.

## What this program does not do

CW stays in Lyra. STD does not send CW.

MT63 and QPSK do not key. WEFAX-576 receives and does not key.

Audio inside STD is 48 kHz. The Windows build uses Qt 6.11.1 MSVC at `C:\Qt\6.11.1\msvc2022_64`.
