// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <vector>

// One transmission at 48 kHz, float -1..1. FT8 and FT4 come from the
// MSHV generators. Hell is Feld Hell keyed at the tone. Empty when the
// mode has no generator or the message is blank.
bool buildTxWave(const QString& mode, const QString& message, int toneHz, std::vector<float>* out);
