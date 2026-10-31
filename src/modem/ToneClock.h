// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared live-receive pieces for the multi-tone text modes.
// The 8 kHz decimator matches the RTTY windowed-sinc (16 cycles,
// Blackman). Symbol timing and AFC follow fldigi's per-symbol
// nudge: timing on a tone change, frequency only while one tone
// holds. This is a rewrite. The fldigi sources are not built in.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <QChar>
#include <QString>

// Print Latin-1. Drop NUL and CR. Keep LF. Drop other controls.
inline QString latin1Rx(int ch) {
  if (ch < 0)
    return {};
  const unsigned u = unsigned(ch) & 0xFFu;
  if (u == 0 || u == '\r')
    return {};
  if (u != '\n' && u < 32u)
    return {};
  return QString(QChar(char16_t(u)));
}

inline double toneQuality(double peak, double sum, int tones) {
  if (sum < 1e-12 || tones < 2)
    return 0.0;
  const double ratio = peak * double(tones) / sum;
  const double span = std::max(4.0, double(tones) * 0.5);
  return std::clamp((ratio - 2.0) * (100.0 / span), 0.0, 100.0);
}

struct BinPick {
  int index = 0;
  double peak = 0.0;
  double sum = 0.0;
};

inline BinPick pickBin(const double* mag, int count) {
  BinPick bin;
  if (!mag || count < 1)
    return bin;
  for (int i = 0; i < count; ++i) {
    bin.sum += mag[i];
    if (mag[i] > bin.peak) {
      bin.peak = mag[i];
      bin.index = i;
    }
  }
  return bin;
}

inline bool binStrong(const BinPick& bin, int tones) {
  return tones > 1 && bin.sum > 1e-8 && bin.peak * double(tones) > bin.sum * 2.5;
}

// Symbol phase. A steady tone is sharp at every offset, so a later offset
// replaces phase 0 only when it is clearly better across several symbols.
struct SymbolLock {
  int at = 0;
  double rank = -1.0;
};

template <typename ScoreFn>
SymbolLock lockSymbolPhase(int separ, int sampleCount, ScoreFn scoreAt) {
  SymbolLock pick;
  if (separ < 8 || sampleCount < separ * 4)
    return pick;
  const int step = std::max(1, separ / 8);
  const int symbols = std::min(sampleCount / separ, 24);
  for (int off = 0; off < separ; off += step) {
    double worst = 1.0;
    double acc = 0.0;
    int n = 0;
    for (int s = 0; s < symbols; ++s) {
      const int pos = off + s * separ;
      if (pos + separ > sampleCount)
        break;
      const double score = scoreAt(pos);
      if (score < 0.0)
        continue;
      worst = std::min(worst, score);
      acc += score;
      ++n;
    }
    if (n < 4)
      continue;
    const double rank = 0.5 * worst + 0.5 * (acc / double(n));
    if (rank > pick.rank + 0.04) {
      pick.rank = rank;
      pick.at = off;
    }
  }
  return pick;
}

// One-bin complex correlator. Phase is 0 at the first sample.
inline void toneIq(const float* samples, int count, int begin, int length, double freqHz, int rate,
                   double* si, double* sq) {
  *si = 0.0;
  *sq = 0.0;
  if (!samples || length < 4 || begin < 0 || begin + length > count || rate < 1000)
    return;
  const double step = 2.0 * 3.14159265358979323846 * freqHz / double(rate);
  double phase = 0.0;
  double iacc = 0.0;
  double qacc = 0.0;
  for (int n = 0; n < length; ++n) {
    const double sample = samples[begin + n];
    iacc += sample * std::sin(phase);
    qacc += sample * std::cos(phase);
    phase += step;
  }
  *si = iacc;
  *sq = qacc;
}

// 48 kHz (or any multiple of 8 kHz) to the 8 kHz rate fldigi demodulates at.
class Rate8k {
public:
  void reset() {
    factor_ = -1;
    phase_ = 0;
    pos_ = 0;
    bucket_ = 0.0;
    bucketCount_ = 0;
    held_ = 0.0;
    drop_ = 0;
    taps_.clear();
    hist_.clear();
  }

  void push(const float* samples, int count, int rate, std::vector<float>* out) {
    if (!out || !samples || count < 1 || rate < 1000)
      return;
    const int factor = (rate % 8000 == 0) ? rate / 8000 : 0;
    if (factor != factor_)
      arm(factor);
    for (int n = 0; n < count; ++n) {
      const float sample = samples[n];
      if (factor_ == 1) {
        out->push_back(sample);
        continue;
      }
      if (factor_ > 1) {
        const int taps = int(taps_.size());
        if (taps < 1)
          continue;
        hist_[static_cast<size_t>(pos_)] = sample;
        pos_ = (pos_ + 1) % taps;
        if (++phase_ < factor_)
          continue;
        phase_ = 0;
        double acc = 0.0;
        int p = pos_;
        for (int k = 0; k < taps; ++k) {
          acc += taps_[static_cast<size_t>(k)] * hist_[static_cast<size_t>(p)];
          p = (p + 1) % taps;
        }
        if (drop_ > 0) {
          --drop_;
          continue;
        }
        out->push_back(float(acc));
        continue;
      }
      bucket_ += sample;
      ++bucketCount_;
      held_ += 8000.0 / double(rate);
      if (held_ < 1.0)
        continue;
      const double mean = bucket_ / double(std::max(1, bucketCount_));
      bucket_ = 0.0;
      bucketCount_ = 0;
      held_ -= 1.0;
      out->push_back(float(mean));
    }
  }

private:
  void arm(int factor) {
    factor_ = factor;
    phase_ = 0;
    pos_ = 0;
    bucket_ = 0.0;
    bucketCount_ = 0;
    held_ = 0.0;
    drop_ = 0;
    taps_.clear();
    hist_.clear();
    if (factor <= 1)
      return;
    const int taps = 16 * factor + 1;
    const int half = taps / 2;
    constexpr double pi = 3.14159265358979323846;
    const double fc = 3600.0 / (8000.0 * double(factor));
    taps_.assign(static_cast<size_t>(taps), 0.0);
    double sum = 0.0;
    for (int k = 0; k < taps; ++k) {
      const double u = double(k - half);
      const double s = u == 0.0 ? 2.0 * fc : std::sin(2.0 * pi * fc * u) / (pi * u);
      const double w = 0.42 - 0.5 * std::cos(2.0 * pi * double(k) / double(taps - 1)) +
                       0.08 * std::cos(4.0 * pi * double(k) / double(taps - 1));
      taps_[static_cast<size_t>(k)] = s * w;
      sum += s * w;
    }
    if (sum != 0.0) {
      for (double& tap : taps_)
        tap /= sum;
    }
    hist_.assign(taps_.size(), 0.0);
    // Center of the 97-tap (16*factor+1) sinc. Dropping these outputs
    // lines the first 8 kHz sample up with the first input sample.
    drop_ = (taps / 2) / factor;
  }

  int factor_ = -1;
  int phase_ = 0;
  int pos_ = 0;
  double bucket_ = 0.0;
  int bucketCount_ = 0;
  double held_ = 0.0;
  int drop_ = 0;
  std::vector<double> taps_;
  std::vector<double> hist_;
};

// Per-symbol sample nudge and a slow frequency correction, in hertz.
class SymbolClock {
public:
  bool afcOn = false;

  void setAfcEnabled(bool on) {
    afcOn = on;
    if (!on)
      afcHz_ = 0.0;
  }

  void resetTrack() {
    sync_ = 0.0;
    afcHz_ = 0.0;
    prev_ = -1;
    tone_ = -1;
  }

  double afcHz() const { return afcOn ? afcHz_ : 0.0; }

  int tone() const { return tone_; }

  void setTone(int tone) {
    prev_ = tone_;
    tone_ = tone;
  }

  // +1 moves the next sample later. Only while a tone is actually present
  // and it changed, which is when the symbol edge is visible.
  int nudge(double early, double center, double late, int probe, bool strong, bool transition) {
    if (!strong || !transition || probe < 1)
      return 0;
    const double denom = early + center + late;
    if (denom < 1e-9)
      return 0;
    const double slope = (late - early) / denom;
    sync_ += slope * double(probe) * 4.0;
    const double limit = double(probe) * 2.0;
    if (sync_ > limit)
      sync_ = limit;
    if (sync_ < -limit)
      sync_ = -limit;
    if (sync_ >= double(probe)) {
      sync_ -= double(probe);
      return 1;
    }
    if (sync_ <= -double(probe)) {
      sync_ += double(probe);
      return -1;
    }
    return 0;
  }

  // Half-symbol phase slope. eps > 0 means the tone is sharp of the bin.
  void observeFreq(double si0, double sq0, double si1, double sq1, int half, int rate, double spacing,
                   bool repeat, bool strong) {
    if (!afcOn || !repeat || !strong || half < 4 || spacing <= 0.0 || rate < 1000)
      return;
    const double e0 = si0 * si0 + sq0 * sq0;
    const double e1 = si1 * si1 + sq1 * sq1;
    if (e0 < 1e-8 || e1 < 1e-8)
      return;
    constexpr double pi = 3.14159265358979323846;
    double delta = std::atan2(sq1, si1) - std::atan2(sq0, si0);
    if (delta > pi)
      delta -= 2.0 * pi;
    if (delta < -pi)
      delta += 2.0 * pi;
    const double eps = delta * double(rate) / (2.0 * pi * double(half));
    if (std::fabs(eps) > spacing * 0.5)
      return;
    afcHz_ = 0.8 * afcHz_ + 0.2 * eps;
    const double cap = spacing * 0.45;
    if (afcHz_ > cap)
      afcHz_ = cap;
    if (afcHz_ < -cap)
      afcHz_ = -cap;
  }

private:
  double sync_ = 0.0;
  double afcHz_ = 0.0;
  int prev_ = -1;
  int tone_ = -1;
};
