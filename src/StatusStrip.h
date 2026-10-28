// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

// Bottom strip: mode, dial, an on-air PTT lamp, and the radio line.
// The lamp follows the radio. It does not key.
class StatusStrip : public QWidget {
public:
  explicit StatusStrip(QWidget* parent = nullptr);

  void setMode(const QString& text);
  void setDial(const QString& text);
  void setPtt(bool onAir, bool connected);
  void setRadio(bool connected, const QString& line);
  // samples are float -1..1. A null pointer clears the meter.
  void setRxLevel(const float* samples, int count);

private:
  void refreshRxLevel();

  float rxHeld_ = 0.0f;
  qint64 rxHeardMs_ = 0;
};
