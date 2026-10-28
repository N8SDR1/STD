# STD

**Shift, Tone, Decode**

STD is a standalone digimode console. This slice is a Qt 6 dock shell plus one TCI radio socket. It does not decode and it does not transmit.

The title bar is **STD**. The subtitle under the name is Shift, Tone, Decode. That subtitle is not drawn on the icon.

Copyright (C) 2026 Rick Langford (N8SDR).

## License

GNU General Public License, version 3. The license text is [`COPYING`](COPYING).

Original files in this repository are GPL-3.0-or-later (`SPDX-License-Identifier: GPL-3.0-or-later`). A later build that links modem code granted under GPL-3 only, with no “or later” clause, is distributed as a whole under GPL-3.

## What this window contains

- **Waterfall** — the large dock. With no audio it paints a standby graticule, the dial, and “No signal”. Once type-1 RX frames arrive, time scrolls downward and frequency runs across the top. Each pixel of the visible span is its own FFT bin, so a signal is a thin vertical trace on a dark blue field. A green line marks the RX audio frequency and a red line marks TX. Click sets RX, Shift+click sets TX, Ctrl+click sets both. Lock keeps them together, and RX=TX copies RX onto TX. None of that keys the radio. The outer span is the receive band from `rx_filter_band` (receiver 0). For a 0–3400 Hz filter the ruler stops at 3400. The wheel, a drag, and Start/BW can narrow inside that span, for example 100–3000 or 200–2700, and cannot cross the reported edges. A double-click returns to the whole reported band. Until that report arrives, the limit is the audio that actually arrives. Contrast, Brightness, Lock, RX=TX, Ticks, and a Palette menu sit on the bar along the bottom of the waterfall. Ticks sets the ruler marks to 100, 250, or 500 Hz. Contrast and Brightness recolor the trace and leave the span alone. The menu lists Classic, Inferno, Viridis, Plasma, Rainbow, Ocean, Night, Grayscale, and Amber. Decode lines go to the Receive pane, not onto the trace. The widget can embed a `QWindow` later (a Vulkan surface included). This slice does not link Vulkan.
- **Receive** — empty, read-only text until a decode lands. Column headers sit above it: Time, dB, DT, Message, Freq. While the socket is down the pane says no radio is connected. With Auto Dec on (the default) and FT8 audio arriving, a line is written when the waveform is in, before the next 15-second period. Filter keeps lines that contain the text. CLR MSG clears the list. Freq is the audio frequency in Hz. Dist is not computed.
- **Transmit** — empty compose box. Text stays on this machine. A line under it says nothing is transmitted.
- **Macros** — eight empty slots, M1–M8, and “No macros loaded”.
- **Mode** — FT8 and FT4 show the MSHV bar under the waterfall: Speed, Start Hz, BW (3400 Hz), Auto Dec, and band buttons 1.8, 3.5, 7, 10, 14, 18, 21, 24, 28, and 50 MHz. The active band is highlighted. Choosing the mode, or a band button, tunes the open socket. The dial is `vfo:0,0,<Hz>` from MSHV’s column for that mode. The sideband is `modulation:0,digu` on every band. Neither command is `START`, and neither keys the radio. RTTY and PSK31 hide that bar. Their menus are not loaded yet.
- **Connect** — the gear on the brand bar, and Radio → Connect…. Host and port, default port 50001. One WebSocket. Connect sends `AUDIO_START:0` and nothing else: no `START`, no `TRX`, and no global audio-format setter. A band button is what writes the dial and the sideband, and only while the socket is already open. Disconnect sends `AUDIO_STOP:0` for this socket only. A server that allows only one client is reported on the status line, and STD closes so it does not keep that slot. The window never sends `TRX`.
- **Help → Guide** — short notes for connect, zoom, contrast, brightness, palettes, the mode profile, and FT8 decode.
- **Status strip** — Mode, AFC, Squelch, the dial, a PTT lamp that follows the radio, and “Connected …”, or “Radio not connected” with the socket error. The lamp does not key.

Panels can dock, float, and tab. View → Reset layout puts them back. The status strip stays pinned to the column.

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
