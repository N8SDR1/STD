// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClockCheck.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

// Decodes kept: the last three minutes, at most this many.
constexpr qint64 kKeepMs = 3 * 60 * 1000;
constexpr int kMaxSamples = 200;
// A median needs this many decodes from at least two periods.
constexpr int kMinSamples = 8;
constexpr int kMinPeriods = 2;
// Once off, the clock counts as good again only below this share of the limit.
constexpr double kClearShare = 0.8;
// A DT this large is a bad decode, not a clock.
constexpr double kWildDt = 5.0;

}  // namespace

double ClockCheck::limitFor(const QString& mode) {
  if (mode == QLatin1String("FT8"))
    return 1.0;
  if (mode == QLatin1String("FT4"))
    return 0.5;
  if (mode == QLatin1String("FT2"))
    return 0.3;
  return 0.0;
}

void ClockCheck::reset() {
  mode_.clear();
  samples_.clear();
  state_ = State::Unknown;
  offset_ = 0.0;
}

int ClockCheck::periods() const {
  QSet<QString> seen;
  for (const Sample& s : samples_)
    seen.insert(s.period);
  return seen.size();
}

void ClockCheck::add(const QString& mode, const QString& period, double dt, qint64 nowMs) {
  if (limitFor(mode) <= 0.0) {
    reset();
    return;
  }
  if (mode != mode_) {
    reset();
    mode_ = mode;
  }
  if (std::isfinite(dt) && std::fabs(dt) < kWildDt && !period.isEmpty())
    samples_.push_back(Sample{period, dt, nowMs});
  update(nowMs);
}

void ClockCheck::update(qint64 nowMs) {
  samples_.erase(std::remove_if(samples_.begin(), samples_.end(),
                                [nowMs](const Sample& s) { return nowMs - s.ms > kKeepMs; }),
                 samples_.end());
  if (samples_.size() > kMaxSamples)
    samples_.remove(0, samples_.size() - kMaxSamples);
  if (samples_.size() < kMinSamples || periods() < kMinPeriods) {
    state_ = State::Unknown;
    return;
  }
  QVector<double> dts;
  dts.reserve(samples_.size());
  for (const Sample& s : samples_)
    dts << s.dt;
  std::sort(dts.begin(), dts.end());
  const int n = dts.size();
  offset_ = n % 2 ? dts.at(n / 2) : (dts.at(n / 2 - 1) + dts.at(n / 2)) / 2.0;

  const double limit = limitFor(mode_);
  const bool wasOff = state_ == State::Fast || state_ == State::Slow;
  const double edge = wasOff ? limit * kClearShare : limit;
  if (offset_ >= edge)
    state_ = State::Fast;
  else if (offset_ <= -edge)
    state_ = State::Slow;
  else
    state_ = State::Ok;
}
