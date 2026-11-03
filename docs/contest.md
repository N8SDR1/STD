# Contest mode (RTTY, PSK, Hell, and the other keyboard modes)

Options → **Contest Mode (RTTY, PSK, Hell)…** turns STD into a contest station for the keyboard modes. It is off until you turn it on. FT8 and the other slot modes keep their own contest list under Options → Contest Tags.

## Setting it up

1. Options → Contest Mode. Tick **Contest mode on**.
2. Pick the contest:

   | Contest | CQ word | Your exchange | Logged as |
   |---|---|---|---|
   | ARRL RTTY Roundup | RU | 599 and your state or province (DX: # for the serial) | ARRL-RTTY |
   | CQ WW RTTY | TEST | 599, your CQ zone, and in the US and Canada your state | CQ-WW-RTTY |
   | NA RTTY Sprint | NA | serial, name, and state, no 599 | NA-SPRINT-RTTY |
   | ARRL Field Day | FD | class and section, no 599 | ARRL-FD |
   | Feld Hell sprint | FH | 599 and the sprint exchange | FH-SPRINT |
   | Serial number | TEST | 599 and the serial | (none) |
   | Custom | TEST | whatever the contest asks for | (none) |

3. **Your exchange** is what follows the 599, such as `OH`, `05 OH`, `2A OH`, or `# RICK OH`. A `#` is your serial, sent as three digits (007).
4. **Next serial** is the number you send next. It goes up by one each time a contest QSO is logged.
5. The table holds the eight **Run** and eight **S&P** messages for F1 to F8. Edit any of them; **Reset messages** puts back the contest's own. Edits are saved per contest.
6. OK. A session begins the first time you press OK with contest mode on. **New session** starts the contest over: dupes count only QSOs from then on and the serial goes back to 001. Your log is not changed.

## On the air

The QSO bar gets a contest row: the contest name, **Run** or **S&P** (click to switch), F1 to F8, **Exch** (their exchange), **Sent** (yours, with the serial), and **DUPE** or **New**.

| Key | Run | S&P |
|---|---|---|
| F1 | CQ RU N8SDR N8SDR CQ | the same CQ |
| F2 | his call, your exchange twice | his call, TU, your exchange twice, then log |
| F3 | TU N8SDR RU, then log and clear | TU |
| F4 | your call | your call twice |
| F5 | his call | his call |
| F6 | his call QSO B4 TU, then clear | his call and your call |
| F7 | AGN AGN | AGN AGN |
| F8 | your exchange again | your exchange again |

The keyboard F1 to F8 send them too, and Insert sends F2. A message is a macro: it keys only when **Macros key** is on, and `<TX>` … `<RX>` sends it and returns to receive. START is never sent.

**Enter sends the next message** in Call or Exch:

- Run: with Call empty, F1 (CQ). With a call and no exchange, F2 (his call and your exchange). Once their exchange is in, F3 (TU and log).
- S&P: with a call and no exchange, F4 (your call). Once their exchange is in, F2 (TU, your exchange, and log).

**Space in Call** jumps to Exch.

**Their exchange** is read from their text, the words after their 599 (or after their call where there is no 599), and offered in Exch with a dashed border. A click, Tab, or Enter takes it. You can also right-click a received word and pick Exch, or type it.

**Dupes.** When a call goes in Call, STD checks your log for the same call on the same band with this contest's CONTEST_ID since the session began. DUPE shows red; F6 sends QSO B4.

**Logging.** A contest QSO is logged with CONTEST_ID, STX_STRING (your exchange as sent), SRX_STRING (theirs), STX and SRX (the serial numbers), and 599 for RST when the contest uses it. It goes to the ADIF file and to SDRLogger+ or any other logger set under Options → Log Options, the same as any QSO.

## Tags for your own macros

| Tag | What it sends |
|---|---|
| `<XOUT>` or `<EXCH>` | your exchange with the 599 and the serial, such as 599 OH |
| `<XIN>` | their exchange from Exch; a ▶ macro will not key while it is empty |
| `<CNTR>` | the serial, three digits |
| `<LOG>` | logs once the text has gone out, and in contest mode moves the serial on |
| `<CLRQSO>` | clears the QSO bar for the next station |
