# Note for Cursor: STD landing page, needed stations with SDRLogger+

Written 2026-11-12 on branch wspr-hell-ft-multianswer. Read with
`.cursor/rules/std-invariants.mdc`; those rules still apply.

## What landed in STD

- **Mark Needed Stations** (Options → Decode List Options, off until
  ticked): a Need column in the FT8/FT4/FT2/JT65/Q65 list. NEW (DXCC entity
  never worked), BAND (entity not worked on the current band), GRID (grid
  never worked), GRID+BAND (grid not worked on this band), B4 (call already worked on this band, dim). Worked only, not
  confirmed. Entities from AD1C's cty.dat, bundled at `resources/cty.dat`;
  a newer copy in `%APPDATA%\N8SDR\STD\cty.dat` wins.
- **Worked list** (Options → Log Options → Log file and logger):
  1. STD's ADIF file only (default).
  2. STD's ADIF file + another ADIF file (read only: WSJT-X, Log4OM,
     DXKeeper, N1MM exports).
  3. STD's ADIF file + **SDRLogger+ (live)**: STD GETs
     `http://127.0.0.1:5050/api/adif/export` (port configurable) every five
     minutes and ten seconds after each logged QSO, caches it at
     `%APPDATA%\N8SDR\STD\log\sdrloggerplus.adi`, and keeps using the cache
     while SDRLogger+ is closed. Read only; nothing is sent to SDRLogger+.
  Code: `src/LoggerFeed.*`, `src/Needed.*`, `MainWindow::workedSources`.
- Also new on the branch: Clock check chip (FT decode DT median), cluster
  spots for digital DXpeditions and optionally hot list calls (Spothole).

## Landing page (`docs/index.html`)

The FT8/FT4/FT2 card already has one sentence on Mark Needed Stations and
the SDRLogger+ live worked list, and the Clock chip. When the page is next
reworked, give the SDRLogger+ pairing its own line or card, for example:

> **Better with SDRLogger+.** Point STD's worked list at SDRLogger+ and the
> NEW / BAND / GRID marks always match your whole SDRLogger+ log, no export
> needed. STD only reads it, on your own PC.

Keep the wording plain, no reference to other programs' internals, and do
not add a callsign or locator field anywhere.

## SDRLogger+ side (if editing that site/app)

SDRLogger+'s `/api/adif/export` is what STD uses; keep it reachable on
127.0.0.1 without login. If its port moves off 5050 (busy port), STD's
port box must be changed by hand; a stable port or a discoverable one
would help. The SDRLogger+ landing page (`SDRLoggerPlus-v2/website`) does
not mention STD yet; a line there pairing the two would match this one.
