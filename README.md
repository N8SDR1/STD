# STD

**Shift, Tone, Decode**

STD is a standalone digimode console. This slice is a Qt 6 dock shell plus one TCI radio socket. FT8, FT4, and Hell transmit only when TX is pressed.

The title bar is **STD**. The subtitle under the name is Shift, Tone, Decode. That subtitle is not drawn on the icon.

Copyright (C) 2026 Rick Langford (N8SDR).

## License

GNU General Public License, version 3. The license text is [`COPYING`](COPYING).

Original files in this repository are GPL-3.0-or-later (`SPDX-License-Identifier: GPL-3.0-or-later`). A later build that links modem code granted under GPL-3 only, with no “or later” clause, is distributed as a whole under GPL-3.

## What this window contains

- **Waterfall** — the large dock. With no audio it paints a standby graticule, the dial, and “No signal”. Once type-1 RX frames arrive, time scrolls downward and frequency runs across the top. Each pixel of the visible span is its own FFT bin, so a signal is a thin vertical trace on a dark blue field. A green line marks the RX audio frequency and a red line marks TX. Click sets RX, Shift+click sets TX, Ctrl+click sets both. Lock keeps them together, and RX=TX copies RX onto TX. None of that keys the radio. The outer span is the receive band from `rx_filter_band` (receiver 0). For a 0–3400 Hz filter the ruler stops at 3400. The wheel, a drag, and Start/BW can narrow inside that span, for example 100–3000 or 200–2700, and cannot cross the reported edges. A double-click returns to the whole reported band. Until that report arrives, the limit is the audio that actually arrives. Contrast, Brightness, Lock, RX=TX, Ticks, and a Palette menu sit on the bar along the bottom of the waterfall. Ticks sets the ruler marks to 100, 250, or 500 Hz. Contrast and Brightness recolor the trace and leave the span alone. The menu lists Classic, Inferno, Viridis, Plasma, Rainbow, Ocean, Night, Grayscale, and Amber. Decode lines go to the Receive pane, not onto the trace. The widget can embed a `QWindow` later (a Vulkan surface included). This slice does not link Vulkan.
- **Receive** — empty, read-only text until a decode lands. Column headers sit above it: Time, dB, DT, Dist, Message, Freq. Dist is miles from Options → your grid to the grid in the message, or kilometers. While the socket is down the pane says no radio is connected. With Auto Dec on (the default) and FT8 audio arriving, the first pass starts about 11.8 seconds into the period, before the tones end. FT4 is one pass at 6.1 seconds of its 7.5 second period. FT2 is one pass at 3.4 seconds of its 3.75 second period. Each line is painted as soon as the decoder finds it. Filter keeps lines that contain the text. CLR MSG clears the list. Freq is the audio frequency in Hz. Hell replaces that list with a Feld Hell tape from the Fldigi 4.2.06 column detector. The tape is the picture. It is not turned into text, and it does not transmit.
- **Mode** — one mode bar sits under the waterfall. Hell hides Start and BW and keeps Speed, Auto Dec, Region, and the band buttons. Hell dials are the Feld Hell Club signal minus 1500 Hz, so 20 m is 14.0615 MHz for the 14.063 MHz calling frequency. 30 m Region 1 uses 10.144 MHz. The tone starts at 1500 Hz. Click the waterfall to move it. DIGU. This does not key.
- **Transmit** — compose box plus a TX button. Press TX to send one FT8, FT4, or Hell transmission. FT8 and FT4 wait for the next period. Hell starts at once. TRX goes out only then. START is never sent. An empty box does not key. FT2, RTTY, and PSK31 do not key.
- **Macros** — eight slots, M1–M8. Options → Macros edits them. A filled button copies its text into the transmit box. Nothing is sent.
- **Options** — Decode (three-stage FT8, Fast/Normal/Deep, Enable AP, max decoded signals), Time Synchronization, Network Configuration for a PSK Reporter style address, decode-list columns, and log / ASeq / TX-confirm / Auto RESET QSO checkboxes. The reporter does not send. Log, ASeq, TX confirmation, and Auto RESET are saved and do not key, because transmit is off. MSK and Q65 decoders are not in this build.
- **FT8, FT4, and FT2** — the mode bar sits under the waterfall. Contrast, Brightness, Speed, Start, BW, Ticks, and Palette stay on the waterfall. Super Hound or Super Fox sets 40 m FT8 to 7.056 MHz DIGU and the RX marker to 750 Hz. That is still a 15 second FT8 period. The green and red bands are the protocol width: 50 Hz on FT8, 90 Hz on FT4, 125 Hz on FT2. The TX-gain notice stays off on receive. It can show only while STD is transmitting. The gain spin does not key. Super Hound widens the green band and Super Fox widens the red band to the fox waveform. TX FIRST and TX SECOND only remember a slot. FILTERS trims the receive list. Multi turns on MSHV multi-answer for more than one caller. None of these mode-bar controls key the radio. Lock and RX=TX sit with that waterfall group. The mode bar also has Auto Dec, Region, a narrow Extra field, and band buttons 1.8, 3.5, 7, 10, 14, 18, 21, 24, 28, and 50 MHz. Region Auto reads the callsign. A portable suffix such as N8SDR/VE uses that country, and the call is stored as typed. Region 1, 2, or 3 overrides it. Band buttons use the WSJT-X FrequencyList dials for FT8 and FT4. 40 m FT4 is 7.0475 MHz in every region. 80 m FT4 is 3.568 MHz in Region 3. FT2 uses the MSHV dials. Extra inserts more dials for the current mode and band. Clicking that mode again, or the same band button, cycles the WSJT-X default and those extras. Right-click Extra clears them. The sideband is `modulation:0,digu` on every band. Neither command is `START`, and neither keys the radio. RTTY and PSK31 hide that bar. Their menus are not loaded yet.
- **Connect** — Radio → Connect…. Host and port, default port 50001. One WebSocket. Connect sends `AUDIO_START:0` and nothing else: no `START`, no `TRX`, and no global audio-format setter. Auto-connect, on by default, opens that saved host and port when STD starts. Auto monitor, also on by default, stays on receive and decode and does not transmit. A band button is what writes the dial and the sideband, and only while the socket is already open. Disconnect sends `AUDIO_STOP:0` for this socket only. A server that allows only one client is reported on the status line, and STD closes so it does not keep that slot. `TRX` is sent only when the TX button is pressed.
- **Help → Guide** — short notes for connect, zoom, contrast, brightness, palettes, the mode profile, and FT8 decode.
- **Status strip** — Mode, the digital-mode chip (FT8, FT4, FT2, Hell, RTTY, PSK31), AFC, Squelch, the dial, an RX peak meter, RX and TX gain, a PTT lamp that follows the radio, and “Monitor …” while auto monitor is on, “Connected …” otherwise, or “Radio not connected” with the socket error. The meter is an average and is green from about -1 dB to 0 dB. RX gain is applied to the waterfall and the decoder only. TX gain stays local. Neither gain is sent to the radio. The lamp does not key, and the meter does not either.

Panels can dock, float, and tab. View → Save layout stores that arrangement for the current mode, and the next start restores it. View → Reset layout stacks the current mode again. The status strip stays pinned to the column. The RX meter is an average, not each peak.

## Build

Requirements: a C++17 compiler, CMake 3.21 or newer, Qt 6 Widgets, and Qt 6 WebSockets (6.2 or newer).

On Debian and Ubuntu:

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-websockets-dev
```

Configure and build from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

If CMake cannot find Qt, the error names the missing piece: Widgets (`qt6-base-dev`) or WebSockets (`qt6-websockets-dev`). A custom prefix can be passed as `-DQt6_DIR=/path/to/lib/cmake/Qt6`.

On Windows the same build compiles MSHV’s FT8 decoder from the sibling `MSHV` directory with MinGW and writes `ft8dec.dll` next to `std.exe`. Those algorithm files stay in that tree, copyright headers included. The DLL is loaded only to decode. It does not open the radio.

## Run

```bash
./build/std
```

The window title is STD. The icon is the rounded-square mark from [`resources/std-icon.jpg`](resources/std-icon.jpg). That file is a wide frame with dark side margins; [`resources/std-icon.png`](resources/std-icon.png) is the same mark cropped square and embedded in the binary.

A headless check constructs the window, checks the empty panes and the disconnected radio line, then exits:

```bash
STD_SELFTEST=1 QT_QPA_PLATFORM=offscreen ./build/std
```

Exit status 0 prints `STD self-test ok`.

## Not in this slice

No sound device. FT8 decode uses the MSHV GPL decoder on audio that TCI already delivered. The dial stays blank until the radio sends `VFO`. AFC and squelch stay blank on purpose.
