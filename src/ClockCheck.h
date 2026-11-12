// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Clock check for the FT modes. The DT of a decode is how late the signal
// started against this PC's clock. Every station is a little off, but the
// median over several periods is this PC's own error: positive means the PC
// clock is fast, negative slow. This only reads decodes; it does not set the
// clock and does not key.

#pragma once

#include <QString>
#include <QVector>

class ClockCheck {
public:
  enum class State { Unknown, Ok, Fast, Slow };

  // The offset a mode tolerates before STD calls the clock off: FT8 1.0 s,
  // FT4 0.5 s, FT2 0.3 s. Zero for modes this does not check.
  static double limitFor(const QString& mode);

  // One decode. period is the decode's UTC time (HHMMSS), nowMs the wall clock.
  void add(const QString& mode, const QString& period, double dt, qint64 nowMs);
  void reset();

  State state() const { return state_; }
  // The median DT in seconds, valid when state() is not Unknown.
  double offset() const { return offset_; }
  // Decodes and periods counted in the current median.
  int samples() const { return samples_.size(); }
  int periods() const;

private:
  struct Sample {
    QString period;
    double dt;
    qint64 ms;
  };
  void update(qint64 nowMs);

  QString mode_;
  QVector<Sample> samples_;
  State state_ = State::Unknown;
  double offset_ = 0.0;
};
