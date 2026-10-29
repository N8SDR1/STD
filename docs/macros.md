---
title: Macros
---

Software in testing. The RST/LOG macro does not log. Fldigi's `<LOG>` tag is not implemented. A later change can make `<LOG>` store the QSO the way Fldigi does. Until then, pressing that button inserts text and nothing is written to the log.

## What is filled in

`<MYCALL>` becomes the call saved under Options → User. `<MYLOC>` becomes that locator.

## What is removed

These tags are deleted, both as `<TAG>` and as `<TAG:...>`:

`TX`, `RX`, `CLRTX`, `CLRRX`, `LOG`.

`<LOG>` and `<LOG:msg>` do not write the ADIF file. They disappear from the text.

## What stays literal

`<RST>` and `<MYRST>` are shown in the macro tag list and are not expanded. They remain in the box. If the operator then presses TX, those characters can be keyed as text.

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
