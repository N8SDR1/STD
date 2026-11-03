// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

// Linear resampler for the RADE rate changes: TCI 48/24/12/8 kHz,
// modem 8 kHz, speech 16 kHz.
struct RateConvert {
  int inRate = 0;
  int outRate = 0;
  double pos = 0;
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
    }
    buf.insert(buf.end(), in, in + n);
    if (buf.size() < 2)
      return;
    const double step = double(inRate) / double(outRate);
    while (pos + 1.0 < double(buf.size())) {
      const int i = int(pos);
      const double frac = pos - double(i);
      out.push_back(float((1.0 - frac) * buf[static_cast<size_t>(i)] +
                          frac * buf[static_cast<size_t>(i + 1)]));
      pos += step;
    }
    const int drop = int(pos);
    if (drop > 0 && drop < int(buf.size())) {
      buf.erase(buf.begin(), buf.begin() + drop);
      pos -= drop;
    }
  }
};
