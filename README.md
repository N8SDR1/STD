# STD

**Shift, Tone, Decode**

STD is a standalone digimode console. This repository is the first slice: a Qt 6 dock shell. It does not decode, transmit, or open a radio connection.

The title bar is **STD**. The subtitle under the name is Shift, Tone, Decode. That subtitle is not drawn on the icon.

Copyright (C) 2026 Rick Langford (N8SDR).

## License

GNU General Public License, version 3. The license text is [`COPYING`](COPYING).

Original files in this repository are GPL-3.0-or-later (`SPDX-License-Identifier: GPL-3.0-or-later`). A later build that links modem code granted under GPL-3 only, with no “or later” clause, is distributed as a whole under GPL-3.

## What this window contains

- **Waterfall** — the large dock. It paints a standby graticule, an empty dial, and “No signal / Radio not connected”. The widget can embed a `QWindow` later (a Vulkan surface included). This slice does not link Vulkan.
- **Receive** — empty, read-only text.
- **Transmit** — empty compose box. Text stays on this machine. A line under it says nothing is transmitted.
- **Macros** — eight empty slots, M1–M8, and “No macros loaded”.
- **Status strip** — Mode —, AFC Off, Squelch —, a PTT lamp drawn as an on-air control and left off, and “Radio not connected”.

Panels can dock, float, and tab. View → Reset layout puts them back. The status strip stays pinned to the column.

## Build

Requirements: a C++17 compiler, CMake 3.21 or newer, and Qt 6 Widgets (6.2 or newer).

On Debian and Ubuntu the Qt package is **`qt6-base-dev`**:

```bash
sudo apt install build-essential cmake qt6-base-dev
```

Configure and build from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

If CMake cannot find Qt, the error names `qt6-base-dev` on Debian/Ubuntu (`qt6-qtbase-devel` on Fedora, `qt6-base` on Arch). A custom prefix can be passed as `-DQt6_DIR=/path/to/lib/cmake/Qt6`.

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

No modem code, no waterfall math, no sound device, and no TCI socket. Mode, squelch, and the dial stay blank on purpose.
