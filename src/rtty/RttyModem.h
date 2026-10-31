// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// Fldigi RTTY on DIGU (rtty.cxx, Dave Freese W1HKJ, GPL-3).
// Mark is the higher tone unless Reverse is on. The usual amateur pair is
// 45.45 baud and 170 Hz, so each tone sits 85 Hz from the waterfall cursor.
// Fldigi lists 45 baud first; this program selects 45.45. Five bits, LSB
// first, one start bit (space), 1.5 stop bits (mark). US TTY figures unless
// ITA-2 is on. Shaped keying is on. A space sends LTRS and then the space code.

// Fldigi rtty::SHIFT[]. Custom shift is not on this row.
inline constexpr double kRttyShifts[] = {23, 85, 160, 170, 182, 200, 240, 350, 425, 850};
inline constexpr int kRttyShiftCount = int(sizeof(kRttyShifts) / sizeof(kRttyShifts[0]));

// Fldigi rtty::BAUD[]. Index 0 is 45. The selected amateur speed is 45.45.
inline constexpr double kRttyBauds[] = {45, 45.45, 50, 56, 75, 100, 110, 150, 200, 300};
inline constexpr int kRttyBaudCount = int(sizeof(kRttyBauds) / sizeof(kRttyBauds[0]));

struct RttyOptions {
  double baud = 45.45;
  double shiftHz = 170.0;
  bool reverse = false;
  bool ita2 = false;
  bool shaped = true;
};

struct RttyPlan {
  std::vector<float> samples;
  // 48 kHz sample where each source character starts. -1 if that character
  // was not sent.
  std::vector<int> echoAt;
};

RttyPlan planRtty(const QString& text, int centerHz, const RttyOptions& options = {});

// One-shot decode of a buffer. Used by the live engine and the self-test.
// shaped is a transmit option and does not change the receiver.
QString rttyDecode(const float* samples, int count, int sampleRateHz, int centerHz,
                   bool squelchOn = false, int squelchLevel = 10, bool afcOn = false,
                   const RttyOptions& options = {});

class RttyEngine : public QObject {
  Q_OBJECT

public:
  explicit RttyEngine(QObject* parent = nullptr);
  ~RttyEngine() override;

  void setArmed(bool armed);
  void setToneHz(int hz);
  void setOptions(const RttyOptions& options);
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
