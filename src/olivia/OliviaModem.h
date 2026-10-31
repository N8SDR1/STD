// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// Olivia and Contestia, rewritten from Pawel Jalocha's MFSK block code
// (fldigi pj_mfsk.h, GPL-3, maintained by Dave Freese W1HKJ).
// One character is an IFHT impulse. The block scrambles and rotates those
// bits across time, then each symbol is a Gray-coded tone.
// Contestia uses a 6-bit charset and a different scramble. It is not Olivia.

struct OliviaPlan {
  std::vector<float> samples;
  std::vector<int> echoAt;
};

// 0 when the name is not an Olivia or Contestia speed STD modulates.
int oliviaBandwidthHz(const QString& mode);

OliviaPlan planOlivia(const QString& mode, const QString& text, int centerHz);
QString oliviaDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                     int centerHz);

class OliviaEngine : public QObject {
  Q_OBJECT

public:
  explicit OliviaEngine(QObject* parent = nullptr);
  ~OliviaEngine() override;

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
