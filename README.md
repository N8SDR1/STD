# STD

**Shift, Tone, Decode**

STD is a standalone digimode console. This slice is a Qt 6 dock shell plus one TCI radio socket. It does not decode and it does not transmit.

The title bar is **STD**. The subtitle under the name is Shift, Tone, Decode. That subtitle is not drawn on the icon.

Copyright (C) 2026 Rick Langford (N8SDR).

## License

GNU General Public License, version 3. The license text is [`COPYING`](COPYING).

Original files in this repository are GPL-3.0-or-later (`SPDX-License-Identifier: GPL-3.0-or-later`). A later build that links modem code granted under GPL-3 only, with no “or later” clause, is distributed as a whole under GPL-3.

## What this window contains

- **Waterfall** — the large dock. It paints a standby graticule, the dial, and “No signal”. The line under that says “Radio not connected” until a TCI socket is up, then “Connected”. The widget can embed a `QWindow` later (a Vulkan surface included). This slice does not link Vulkan.
- **Receive** — empty, read-only text. While the socket is down it says no radio is connected. While TCI is up it says audio is arriving and nothing is decoded yet.
- **Transmit** — empty compose box. Text stays on this machine. A line under it says nothing is transmitted.
- **Macros** — eight empty slots, M1–M8, and “No macros loaded”.
- **Connect** — the gear on the brand bar, and Radio → Connect…. Host and port, default port 50001. One WebSocket. Connect sends `START`, one real audio channel, float32, 12 kHz, `AUDIO_START`, `MODULATION` DIGU, a VFO read, and `TRX:0,false,tci`. That last command selects the TCI transmit stream and stays in receive. The window never sends `TRX:…,true`.
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

No modem code and no sound device. The dial stays blank until the radio sends `VFO`. AFC and squelch stay blank on purpose.
