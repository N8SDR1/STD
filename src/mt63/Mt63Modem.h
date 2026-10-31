// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// MT63 from Pawel Jalocha, SP9VRC, as maintained in fldigi by Dave Freese, W1HKJ.
// Sixty-four carriers, a Walsh code, and the fldigi short or long interleaver.
// The audio inside the modem is 8 kHz. STD plays and records 48 kHz.

struct Mt63Plan {
  std::vector<float> samples;
  std::vector<int> echoAt;
};

// 0 when the name is not an MT63 speed STD modulates.
int mt63BandwidthHz(const QString& mode);
bool mt63LongInterleave(const QString& mode);

// startTones sends the lower and upper tuning tones for 4 seconds first.
// Default on, matching fldigi.
Mt63Plan planMt63(const QString& mode, const QString& text, int centerHz, bool startTones = true);
QString mt63Decode(const QString& mode, const float* samples, int count, int sampleRateHz,
                   int centerHz);

class Mt63Engine : public QObject {
  Q_OBJECT

public:
  explicit Mt63Engine(QObject* parent = nullptr);
  ~Mt63Engine() override;

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
