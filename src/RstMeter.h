// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <deque>
#include <optional>
#include <utility>

// Turns the TCI S-meter (about 4 readings a second from Lyra) into a
// level that Auto RST can peak-hold without a single spike pegging it.
//
// - The level is the median of the last 1.25 s, never one reading.
// - The noise floor is the 20th percentile of the last 30 s.
// - A level counts only when it is at least 3 dB over that floor. If the
//   radio sends its own SNR (Lyra's lyra_snr), that is used instead.
// A level that passes is returned by level(); Auto RST peak-holds it.
class RstMeter {
public:
  static constexpr qint64 kAverageMs = 1250;
  static constexpr qint64 kFloorMs = 30000;
  static constexpr double kGateDb = 3.0;

  void add(qint64 ms, double dbm);
  void addSnr(qint64 ms, double snrDb);
  void reset();

  // The averaged level when it is a real signal, else nothing.
  std::optional<double> level(qint64 ms) const;
  // The 1.25 s median, and the floor, if known.
  std::optional<double> average(qint64 ms) const;
  std::optional<double> floor() const;

private:
  std::deque<std::pair<qint64, double>> readings_;
  qint64 snrMs_ = 0;
  double snrDb_ = 0.0;
};
