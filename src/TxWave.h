// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <vector>

// One transmission at 48 kHz, float -1..1. FT8 and FT4 come from the
// MSHV generators. Hell is Feld Hell keyed at the tone. Empty when the
// mode has no generator or the message is blank.
bool buildTxWave(const QString& mode, const QString& message, int toneHz, std::vector<float>* out);

// True when the buffer is MSHV FT8: 8-FSK, 6.25 Hz spacing, Costas
// 3,1,4,0,6,5,2 at symbols 0, 36, and 72, each symbol 7680 samples
// (0.16 s at 48 kHz). toneHz is tone 0, the bottom of the 50 Hz channel.
bool ft8TxMatchesMshv(const std::vector<float>& wave, int toneHz);
