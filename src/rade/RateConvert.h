// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Band-limited resampling for RADE: TCI 48/24/12/8 kHz, modem 8 kHz,
// speech 16 kHz. A Blackman-windowed sinc low-passes at 90 % of the lower
// Nyquist, so going down to 8 kHz does not fold 4-24 kHz noise onto the
// modem, and going up does not leave images above the passband. The kernel
// is tabled at 256 fractional phases.
struct SincTable {
  static constexpr int kPhases = 256;
  double fc = 0;
  int hw = 0;
  std::vector<float> taps;  // kPhases rows of 2*hw taps, each row sums to 1

  void build(int inRate, int outRate) {
    const double cut = 0.45 * std::min(1.0, double(outRate) / double(inRate));
    if (cut == fc && !taps.empty())
      return;
    fc = cut;
    hw = std::clamp(int(std::ceil(8.0 / (2.0 * fc))), 4, 128);
    const double pi = 3.14159265358979323846;
    taps.assign(static_cast<size_t>(kPhases) * size_t(2 * hw), 0.0f);
    for (int p = 0; p < kPhases; ++p) {
      const double frac = double(p) / kPhases;
      float* row = &taps[static_cast<size_t>(p) * size_t(2 * hw)];
      double sum = 0.0;
      for (int j = 0; j < 2 * hw; ++j) {
        // Tap j multiplies input sample floor(pos) - hw + 1 + j.
        const double t = frac + double(hw - 1 - j);
        const double u = t / double(hw);
        double h = 0.0;
        if (u > -1.0 && u < 1.0) {
          const double arg = 2.0 * fc * t;
          const double sinc = std::fabs(arg) < 1e-9 ? 1.0 : std::sin(pi * arg) / (pi * arg);
          h = sinc * (0.42 + 0.5 * std::cos(pi * u) + 0.08 * std::cos(2.0 * pi * u));
        }
        row[j] = float(h);
        sum += h;
      }
      for (int j = 0; j < 2 * hw; ++j)
        row[j] = float(row[j] / sum);
    }
  }

  // Value at fractional input position pos. Samples outside [0, n) count as 0.
  float at(const float* x, int n, double pos) const {
    const int base = int(std::floor(pos));
    int p = int((pos - double(base)) * kPhases + 0.5);
    int first = base - hw + 1;
    if (p >= kPhases) {
      p = 0;
      ++first;
    }
    const float* row = &taps[static_cast<size_t>(p) * size_t(2 * hw)];
    float acc = 0.0f;
    const int lo = std::max(0, -first);
    const int hi = std::min(2 * hw, n - first);
    for (int j = lo; j < hi; ++j)
      acc += row[j] * x[first + j];
    return acc;
  }
};

struct RateConvert {
  int inRate = 0;
  int outRate = 0;
  double pos = 0;
  SincTable kernel;
  std::vector<float> buf;

  void reset() {
    inRate = 0;
    outRate = 0;
    pos = 0;
    buf.clear();
  }

  void process(const float* in, int n, int newIn, int newOut, std::vector<float>& out) {
    out.clear();
    if (!in || n < 1 || newIn < 1 || newOut < 1)
      return;
    if (newIn != inRate || newOut != outRate) {
      reset();
      inRate = newIn;
      outRate = newOut;
      kernel.build(inRate, outRate);
      // Silence behind the first sample primes the filter.
      buf.assign(static_cast<size_t>(kernel.hw), 0.0f);
      pos = double(kernel.hw);
    }
    buf.insert(buf.end(), in, in + n);
    const double step = double(inRate) / double(outRate);
    while (pos + double(kernel.hw) < double(buf.size())) {
      out.push_back(kernel.at(buf.data(), int(buf.size()), pos));
      pos += step;
    }
    // Keep hw samples of history behind the read position.
    const int drop = int(pos) - kernel.hw;
    if (drop > 0 && drop < int(buf.size())) {
      buf.erase(buf.begin(), buf.begin() + drop);
      pos -= drop;
    }
  }
};
