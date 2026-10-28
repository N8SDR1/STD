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
  void setDigitalMode(const QString& mode);
  void setDial(const QString& text);
  void setPtt(bool onAir, bool connected);
  void setRadio(bool connected, const QString& line);
  // Shows "TX gain is too hot" beside the TX gain. Does not change it and does not key.
  void setTxHot(bool hot);
  // samples are float -1..1. A null pointer clears the meter.
  void setRxLevel(const float* samples, int count);

private:
  void refreshRxLevel();

  double rxMeanSq_ = 0.0;
  qint64 rxHeardMs_ = 0;
};
