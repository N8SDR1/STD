# QRV

Software in testing. QRV is the live roster at [qrvsdr.cc](https://qrvsdr.cc). STD posts presence only: call, app, band, and mode. It does not post QSOs, ADIF, spots, or audio. Discord is updated by the server, not by STD.

## Turn it on

Options → Network Configuration → **QRV** is Off until On is selected.

On tells the board this station is QRV. It posts only while On is selected and the radio is connected, about every 25 seconds. Turning it off, disconnecting, or quitting sends QRT.

The callsign is the one in Options → User. There is no second callsign field, and no locator field on this dialog.

The mode is the Mode menu. FT8 stays FT8. It is not collapsed to DIGU. Q65A is posted as Q65. The Hell family is posted as HELL. Olivia, Contestia, MFSK, DominoEX, Thor, and MT63 are posted as the family name. RADE, WEFAX-576, SSTV, and Q65B are not on the board, so those modes post nothing.

**Share exact frequency** is off until ticked. Off, the board shows the band only. On also sends the dial in Hz. Discord never receives the frequency.

The optional note is 40 characters. The server drops links and mentions. Discord does not show the note. The note is not a callsign.

A band or mode change posts again after about 10 seconds. A refused post (the server says the fields are wrong) stops until you change something. A busy server or a dropped connection waits for the next heartbeat.

## Who is on

Options → **QRV board** lists the roster: call, app, band, mode, Hz when that station shared it, and the note. The list refreshes while the window is open, about every 25 seconds, and stops when you close it.

## What this does not do

QRV does not key. The text command START is never sent. CW stays in Lyra.
