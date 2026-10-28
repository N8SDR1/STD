// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

// Buffers TCI receive audio and decodes one FT8, FT4, or FT2 period.
// FT8 can run MSHV's three passes. FT4 is one pass at 6.1 seconds.
// FT2 is one pass at 3.4 seconds. It never opens a socket and never transmits.
class Ft8Decode : public QObject {
  Q_OBJECT

public:
  enum class Period { Ft8, Ft4, Ft2 };

  explicit Ft8Decode(QObject* parent = nullptr);
  ~Ft8Decode() override;

  void pushAudio(const float* samples, int count, int sampleRateHz);
  void setArmed(bool armed, int lowHz, int highHz);
  void setSpot(int rxHz, int txHz);
  // depth is 1, 2, or 3. clockOffsetMs shifts the period clock.
  void setOptions(bool threeStage, int depth, bool ap, int clockOffsetMs);
  // FT4 is 7.5 s, one pass. FT2 is 3.75 s, one pass. FT8 keeps 15 s.
  void setMode(Period mode);
  // MSHV multi-answer a-priori state. This does not transmit.
  void setMultiAnswer(bool on);
  void clear();

signals:
  void decoded(const QString& time, int db, const QString& dt, const QString& message, int freqHz);

private:
  void poll();
  void ensureWorker();

  struct Private;
  Private* d_ = nullptr;
};
