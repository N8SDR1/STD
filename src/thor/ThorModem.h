// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// Thor from fldigi (Dave Freese W1HKJ, GPL-3). NASA K=7, the IZ8BLY
// varicode, a 4 by 10 interleaver, and 18-tone incremental keying.
// The fast K=15 speeds are not in this build. This is a rewrite.

struct ThorPlan {
  std::vector<float> samples;
  std::vector<int> echoAt;
};

// 0 when the name is not a K=7 Thor speed STD modulates.
int thorBandwidthHz(const QString& mode);

ThorPlan planThor(const QString& mode, const QString& text, int centerHz);
QString thorDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                   int centerHz);

class ThorEngine : public QObject {
  Q_OBJECT

public:
  explicit ThorEngine(QObject* parent = nullptr);
  ~ThorEngine() override;

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
