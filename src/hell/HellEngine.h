// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>

// Hell receive. The column detector is fldigi 4.2.06 feld::rx,
// feld::FSKH_rx, and feld::rx_process (TCIdigi carries the same file).
// Transmit is buildHell() in TxWave.cpp.
class HellEngine : public QObject {
  Q_OBJECT

public:
  explicit HellEngine(QObject* parent = nullptr);
  ~HellEngine() override;

  void setArmed(bool armed);
  void setToneHz(int hz);
  // fldigi feldcolumnrate. Feld Hell and both FSK Hell speeds are 17.5.
  // Slow Hell is 2.1875, X5 is 87.5, X9 is 157.5, Hell 80 is 35.
  void setColumnRate(double columnsPerSecond);
  // 0 keeps the amplitude detector. 122.5, 55, and 300 select fldigi FSKH_rx.
  // Reverse is off: the lower tone is ink.
  void setFskShiftHz(double shiftHz);
  // Fldigi sqlonoff and sldrSquelchValue. Off prints every column.
  // On drops a column whose metric is at or below level (0..100).
  void setSquelch(bool on, int level);
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  // One Feld Hell column, 40 bytes. 0 is ink, 255 is paper.
  void columnReady(QByteArray column);

private:
  struct Private;
  Private* d_;
};
