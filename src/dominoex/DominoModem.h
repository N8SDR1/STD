// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// DominoEX from fldigi (Dave Freese W1HKJ and Tomi Manninen OH2BNS, GPL-3).
// Eighteen tones, incremental frequency keying, and the nibble varicode.
// FEC is off. This is a rewrite. The fldigi sources are not built in.

struct DominoPlan {
  std::vector<float> samples;
  std::vector<int> echoAt;
};

// 0 when the name is not a DominoEX speed STD modulates.
int dominoBandwidthHz(const QString& mode);

DominoPlan planDomino(const QString& mode, const QString& text, int centerHz);
QString dominoDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                     int centerHz);

class DominoEngine : public QObject {
  Q_OBJECT

public:
  explicit DominoEngine(QObject* parent = nullptr);
  ~DominoEngine() override;

  void setArmed(bool armed);
  void setMode(const QString& mode);
  void setCenterHz(int hz);
  void setSquelch(bool on, int level);
  void setAfc(bool on);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  void textReady(const QString& text);

private:
  struct Private;
  Private* d_;
};
