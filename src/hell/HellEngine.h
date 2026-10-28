// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>

// Receive-only Feld Hell. The column detector is fldigi 4.2.06
// feld::rx / feld::rx_process (tag v4.2.06). Transmit is not built.
class HellEngine : public QObject {
  Q_OBJECT

public:
  explicit HellEngine(QObject* parent = nullptr);
  ~HellEngine() override;

  void setArmed(bool armed);
  void setToneHz(int hz);
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  // One Feld Hell column, 40 bytes. 0 is ink, 255 is paper.
  void columnReady(QByteArray column);

private:
  struct Private;
  Private* d_;
};
