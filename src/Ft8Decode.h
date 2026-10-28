// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

// Buffers TCI receive audio and decodes one FT8 or FT4 period at a time.
// FT8 can run MSHV's three passes. FT4 is one pass at 6.1 seconds.
// It never opens a socket and never transmits.
class Ft8Decode : public QObject {
  Q_OBJECT

public:
  explicit Ft8Decode(QObject* parent = nullptr);
  ~Ft8Decode() override;

  void pushAudio(const float* samples, int count, int sampleRateHz);
  void setArmed(bool armed, int lowHz, int highHz);
  void setSpot(int rxHz, int txHz);
  // depth is 1, 2, or 3. clockOffsetMs shifts the period clock.
  void setOptions(bool threeStage, int depth, bool ap, int clockOffsetMs);
  // FT4 uses a 7.5 s period and one pass. FT8 keeps the 15 s schedule.
  void setMode(bool ft4);
  void clear();

signals:
  void decoded(const QString& time, int db, const QString& dt, const QString& message, int freqHz);

private:
  void poll();
  void ensureWorker();

  struct Private;
  Private* d_ = nullptr;
};
