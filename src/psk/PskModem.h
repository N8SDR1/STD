// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// BPSK from fldigi's PSK31 varicode (Dave Freese W1HKJ, GPL-3, from gmfsk).
// A 0 reverses the carrier phase. A 1 does not. Two zeros separate characters.
// The baud is 31.25, 62.5, 125, 250, 500, or 1000 from the mode name.

struct PskPlan {
  std::vector<float> samples;
  std::vector<int> echoAt;
};

PskPlan planPsk(const QString& mode, const QString& text, int centerHz);
QString pskDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                  int centerHz);

class PskEngine : public QObject {
  Q_OBJECT

public:
  explicit PskEngine(QObject* parent = nullptr);
  ~PskEngine() override;

  void setArmed(bool armed);
  void setMode(const QString& mode);
  void setToneHz(int hz);
  void setSquelch(bool on, int level);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  void textReady(const QString& text);

private:
  struct Private;
  Private* d_;
};
