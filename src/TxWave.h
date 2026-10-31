// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <vector>

// One transmission at 48 kHz, float -1..1. FT8, FT4, and FT2 come from the
// MSHV generators. Hell is Feld Hell keyed at the tone. RTTY is 45.45 baud
// mark/space. PSK31 and the other BPSK speeds are varicode. Empty when the
// mode has no generator or the message is blank.
// echoAt, when set, receives the 48 kHz sample where each source character
// starts. Hell fills it. The three leader dots and the tail are not entries.
// rtty, when set, is the RTTY row (shift, baud, Reverse, ITA-2, shaped
// keying). A null pointer uses the Fldigi defaults: 45.45 baud, 170 Hz,
// Reverse off, US figures, shaped keying on.
struct RttyOptions;
// startTones is the Olivia, Contestia, and MT63 tuning-tone preamble.
// Default on. Other modes ignore it.
bool buildTxWave(const QString& mode, const QString& message, int toneHz, std::vector<float>* out,
                 std::vector<int>* echoAt = nullptr, const RttyOptions* rtty = nullptr,
                 bool startTones = true);

// Decimate a 48 kHz JT65 or Q65 wave to 12 kHz and run the MSHV decoder.
// Empty when the mode is not one of those or nothing prints.
QString decodeSequencedMessage(const QString& mode, const std::vector<float>& wave48k);

// 48 kHz samples for one transmitted character. Hell, RTTY, and PSK stay
// 0 here and pace the echo from the echoAt list buildTxWave fills. A mode
// with no modulator stays 0 so the receive echo does not print text that
// never went out.
int textModeSamplesPerChar(const QString& mode);

// First ink row in a 14-pixel Hell column. 0 is the first sample in time,
// the bottom of the column, matching Fldigi. -1 when the column is blank.
int hellFirstInkRow(QChar ch, int column);

// True when a decode timestamp (hhmmss of the period start) falls in the
// even slot: TX FIRST. FT8 is 15 s, FT4 is 7.5 s, FT2 is 3.75 s.
bool txStampIsFirstHalf(const QString& mode, int minute, int second);

// True when the buffer is MSHV FT8: 8-FSK, 6.25 Hz spacing, Costas
// 3,1,4,0,6,5,2 at symbols 0, 36, and 72, each symbol 7680 samples
// (0.16 s at 48 kHz). toneHz is tone 0, the bottom of the 50 Hz channel.
bool ft8TxMatchesMshv(const std::vector<float>& wave, int toneHz);
