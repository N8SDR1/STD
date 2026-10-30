// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// Fldigi RTTY on DIGU with Reverse off (rtty.cxx, Dave Freese W1HKJ, GPL-3).
// Mark is the higher tone, space the lower. The shift is 170 Hz, so each
// tone sits 85 Hz from the waterfall cursor. 45.45 baud, 5 bits, LSB first,
// one start bit (space), 1.5 stop bits (mark). US TTY figures. A space
// sends LTRS and then the space code.

struct RttyPlan {
  std::vector<float> samples;
  // 48 kHz sample where each source character starts. -1 if that character
  // was not sent.
  std::vector<int> echoAt;
};

RttyPlan planRtty(const QString& text, int centerHz);

// One-shot decode of a buffer. Used by the live engine and the self-test.
QString rttyDecode(const float* samples, int count, int sampleRateHz, int centerHz,
                   bool squelchOn = false, int squelchLevel = 10, bool afcOn = false);

class RttyEngine : public QObject {
  Q_OBJECT

public:
  explicit RttyEngine(QObject* parent = nullptr);
  ~RttyEngine() override;

  void setArmed(bool armed);
  void setToneHz(int hz);
  void setSquelch(bool on, int level);
  void setAfc(bool on);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  void textReady(const QString& text);
  // Waterfall cursor, Hz. Emitted only while AFC is on and the tone moved.
  void toneTracked(int hz);

private:
  struct Private;
  Private* d_;
};
