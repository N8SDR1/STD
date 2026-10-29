// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// MFSK from fldigi (Dave Freese W1HKJ, GPL-3, from gmfsk / IZ8BLY).
// Varicode, NASA K=7 convolutional code, and the diagonal interleaver.
// This is a rewrite. The fldigi sources are not built in.

struct MfskPlan {
  std::vector<float> samples;
  std::vector<int> echoAt;
};

// 0 when the name is not an MFSK speed STD modulates.
int mfskBandwidthHz(const QString& mode);

// IZ8BLY varicode bit string. Out of range returns the NUL code.
const char* mfskVaricode(int ch);
// The character 0..255 whose code equals symbol, or -1.
int mfskVaridec(unsigned symbol);

MfskPlan planMfsk(const QString& mode, const QString& text, int centerHz);
QString mfskDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                   int centerHz);

class MfskEngine : public QObject {
  Q_OBJECT

public:
  explicit MfskEngine(QObject* parent = nullptr);
  ~MfskEngine() override;

  void setArmed(bool armed);
  void setMode(const QString& mode);
  void setCenterHz(int hz);
  void setSquelch(bool on, int level);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  void textReady(const QString& text);

private:
  struct Private;
  Private* d_;
};
