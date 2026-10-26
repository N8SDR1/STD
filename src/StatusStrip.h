// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

// Bottom strip: mode, AFC, squelch, an on-air PTT lamp, and the radio line.
// This slice has no radio, so the lamp stays off and the radio line says so.
class StatusStrip : public QWidget {
public:
  explicit StatusStrip(QWidget* parent = nullptr);
};
