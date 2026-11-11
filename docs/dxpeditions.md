---
title: DXpeditions and the hot list
---

View → DXpeditions lists the announced DX operations, filtered to the ones that run digital modes. The hot list keeps the calls you are watching for, shows them on the top bar, and marks them when STD decodes them. Both are modelled on SDRLogger+.

Nothing on this page keys the radio. The text command START is never sent. A double-click moves only the dial and the mode, over TCI.

## Quick start

1. View → **DXpeditions**. The panel opens floating the first time and fetches the list from NG3K.
2. Leave the filter on **Digital modes**, or pick FT8 / FT4 / FT2, RTTY, or PSK. Tick **On the air now** to see only the operations running today.
3. **Double-click** a row to move STD to its band and digital mode.
4. **Tick** a call to put it on the hot list. It appears on the top bar, right of the dial.
5. Click the alerts chip on the top bar (it reads OFF) to choose **Visual** or **Visual + Voice**.

## The DXpeditions panel

The list is NG3K's Announced DX Operations feed (<https://www.ng3k.com/Misc/adxo.html>). The columns are Status, Call, Entity, Start, End, Bands, Modes, QSL, and Info (the announcement's notes). Bands and modes are read from those notes, so an announcement that names no mode shows a dash in Modes. Hover a row for the whole announcement and its source.

- **Status** reads NOW with the days left for operations whose dates include today (UTC), and "in N d" for later ones. Rows on the air now are tinted green.
- **Filter.** Digital modes, the default, keeps FT8, FT4, FT2, RTTY, PSK, JT65, Q65, MSK144, JS8, Olivia, MFSK, Hell, and the announcements that say "+ digital" or "all modes". FT8 / FT4 / FT2, RTTY, and PSK narrow it further. All modes shows everything, including the CW and SSB operations and the ones that name no mode.
- **On the air now** keeps only today's operations. **Find** matches the call, entity, QSL, or notes.
- **Sort** by clicking a column title. Status puts the operations on the air first.
- **Prefix calls.** When an announcement gives only a prefix, such as 3B9, and its link names the full call, the full call is used (3B9KW) for tuning, the hot list, and the QSO bar.
- **Right-click** a row to tune, put the call on or off the hot list, put it in the QSO bar (copied on the FT8 family, where that bar is hidden), copy it, or open its info page. **NG3K** opens the whole list in the browser. **Hot list…** opens the hot list.

### Double-click: band and mode

A double-click moves STD to that DXpedition's band and digital mode, and the status line says what was done.

- **Mode.** DXpeditions run the FT modes, RTTY, and PSK, so STD takes the first one listed of **FT8, FT4, RTTY, PSK**, then FT2, JT65, Q65, and MSK144. An announcement that only says "+ digital" or "all modes" means FT8.
- **Fox/Hound.** FT8 turns **Super Hound** on when the listing says Fox/Hound (f/h, SuperFox, hound), and off when it does not.
- **PSK** is PSK31 unless the listing names another rate, such as PSK63. The Modes column shows the rate it read.
- **Band.** The band you are on, when it is listed and the mode has a dial there. Otherwise the nearest listed band that has one. With no band named, the band stays.
- **RTTY and PSK** have no band dial in STD, so only the mode changes and the status line says to tune by hand.
- An announcement that lists only CW or SSB changes nothing.
- With no radio connected, only STD's band chip moves.

### Fetching and offline use

Nothing is fetched until the panel is shown. Then the list is fetched when it is more than an hour old, and once an hour while the panel stays open. **Refresh** fetches now. Each list is saved as `dxpeditions.xml` in the STD AppData folder, so the panel opens with the last list when there is no network; the status line under the table says when that list was fetched.

## The hot list

The calls you are watching for, as SDRLogger+ keeps them. Tick a call in View → DXpeditions, or type calls (spaces or commas between them) in View → **Hot list…**. That window also removes calls, clears the list, and holds the alerts, Top bar shows, and On/Off settings.

### On the top bar

The hot list sits on the top bar, right of the dial: HOT LIST, the count, the alerts chip, and up to 12 calls (+N opens the rest).

- **On and off.** View → **Hot list on top bar**, the checkbox in View → Hot list…, or right-click → Hide from the top bar. In JS8 the JS8 clock row takes that space, so the hot list is not shown there.
- **Top bar shows** All calls, Ticked calls (the calls ticked in View → Hot list…), or Heard calls. Heard calls come first. Choose it in View → Hot list… or right-click → Show.
- **Click** a call to put it in the QSO bar (copied on the FT8 family).
- **Double-click** a call to tune: to the band, mode, and dial where STD last heard it; if it has not been heard since STD started, to its DXpedition's band and mode, the same as a double-click in the DXpeditions panel; with neither, the status line says STD does not know and nothing moves.

### Alerts

Alerts are **Off** until chosen. Click the chip on the top bar to cycle Off, Visual, and Visual + Voice.

- **Visual.** When STD decodes a watched call it turns red on the top bar for 15 minutes, then amber, and the status line names the band, mode, and report. Calls not heard are dim.
- **Visual + Voice** also says "Hot list" and the call, letter by letter, once per call every 15 minutes. It uses the voice, rate, and volume from Options → Speech and plays on the **Speech output device**, the same device RADE's Speaker box uses (the Windows default only when that is the device chosen). The hot list has its own voice, so it speaks in every mode, FT8 and WSPR included, with "Read received text aloud" off or the Speech chip muted, while transmitting, and in RADE.
- STD looks in FT8, FT4, FT2, JT65, Q65, MSK144, JS8, and WSPR decodes and in received keyboard text. A call matches as a whole word, inside FT8 brackets, or on either side of a slash, so a hot FO matches FO/WE9G.
- Heard marks last until STD closes. Right-click → Clear heard marks clears them.

### Highlighting

- **Top bar:** red when heard in the last 15 minutes, amber when heard earlier, dim when not heard.
- **DXpeditions panel:** a row whose call is on the hot list is tinted orange, amber once heard, and red when heard in the last 15 minutes, the same colours as the top bar. Its tooltip says when and where it was heard.
