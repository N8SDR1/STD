// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Feld Hell receive from fldigi 4.2.06, tag v4.2.06.
// feld.cxx mixer(), rx(), and rx_process() by Dave Freese, W1HKJ,
// GPL-3.0-or-later. The lowpass and Hilbert transforms are the
// fldigi filter classes compiled beside this file. Transmit is absent.

#include "HellEngine.h"

#include "config.h"
#include "fftfilt.h"
#include "filters.h"

#include <cmath>
#include <vector>

namespace {

constexpr int kSampleRate = 8000;
constexpr int kColumnLen = 20;
constexpr double kColumnRate = 17.5;
constexpr double kBandwidthHz = 245.0;

double clampd(double x, double lo, double hi) {
  if (x < lo)
    return lo;
  if (x > hi)
    return hi;
  return x;
}

}  // namespace

struct HellEngine::Private {
  bool armed = false;
  int toneHz = 1500;
  double held = 0.0;
  double bucket = 0.0;
  int bucketCount = 0;
  double rxphacc = 0.0;
  double rxcounter = 0.0;
  double peakval = 0.0;
  double peakhold = 0.0;
  double agc = 0.0;
  int colPointer = 0;
  int colData[2 * kColumnLen] = {};
  bool squelchOn = false;
  int squelchLevel = 10;
  const double downsampleInc = (kColumnLen * kColumnRate) / static_cast<double>(kSampleRate);
  C_FIR_filter* hilbert = nullptr;
  fftfilt* lowpass = nullptr;
  Cmovavg* average = nullptr;

  void open() {
    hilbert = new C_FIR_filter();
    hilbert->init_hilbert(37, 1);
    lowpass = new fftfilt(0, 0.5 * kBandwidthHz / kSampleRate, 1024);
    // feld::restart sets the averager to 500 * rate / (height * 17.5).
    average = new Cmovavg(200);
    average->setLength(500 * kSampleRate / static_cast<int>(kColumnLen * kColumnRate));
  }

  void close() {
    delete hilbert;
    delete lowpass;
    delete average;
    hilbert = nullptr;
    lowpass = nullptr;
    average = nullptr;
  }

  void reset() {
    close();
    open();
    held = 0.0;
    bucket = 0.0;
    bucketCount = 0;
    rxphacc = 0.0;
    rxcounter = 0.0;
    peakval = 0.0;
    peakhold = 0.0;
    agc = 0.0;
    colPointer = 0;
    for (int& sample : colData)
      sample = 0;
  }

  // feld::mixer. The phase step is the audio tone.
  cmplx mix(cmplx in) {
    cmplx z(std::cos(rxphacc), std::sin(rxphacc));
    z = z * in;
    rxphacc -= 2.0 * M_PI * static_cast<double>(toneHz) / kSampleRate;
    if (rxphacc < 0)
      rxphacc += 2.0 * M_PI;
    return z;
  }

  // feld::rx for amplitude Hell. One new pixel, then a 40-high column.
  bool takePixel(cmplx z, QByteArray* column) {
    double x = std::abs(z);
    if (x > peakval)
      peakval = x;
    const double avg = average->run(x);
    rxcounter += downsampleInc;
    if (rxcounter < 1.0)
      return false;
    rxcounter -= 1.0;

    x = peakval;
    peakval = 0.0;
    if (x > peakhold)
      peakhold = x;
    else
      peakhold *= (1.0 - 0.02 / kColumnLen);
    int ix = static_cast<int>(clampd(255.0 * x / (peakhold > 0.0 ? peakhold : 1.0), 0.0, 255.0));

    if (avg > agc)
      agc = avg;
    else
      agc *= (1.0 - 0.01 / kColumnLen);
    // feld::rx metric is 1000*agc, clamped 0..100, then compared with the slider.
    const double metric = clampd(1000.0 * agc, 0.0, 100.0);
    const bool gated = squelchOn && metric <= static_cast<double>(squelchLevel);

    // HellBlackboard is off, so a strong tone is ink.
    ix = 255 - ix;
    colData[colPointer + kColumnLen] = ix;
    ++colPointer;
    if (colPointer < kColumnLen)
      return false;
    column->resize(2 * kColumnLen);
    for (int i = 0; i < 2 * kColumnLen; ++i)
      (*column)[i] = static_cast<char>(colData[i]);
    colPointer = 0;
    for (int i = 0; i < kColumnLen; ++i)
      colData[i] = colData[i + kColumnLen];
    return !gated;
  }
};

HellEngine::HellEngine(QObject* parent) : QObject(parent), d_(new Private) {
  d_->open();
}

HellEngine::~HellEngine() {
  d_->close();
  delete d_;
}

void HellEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  if (armed)
    d_->reset();
}

void HellEngine::setSquelch(bool on, int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  d_->squelchOn = on;
  d_->squelchLevel = level;
}

void HellEngine::setToneHz(int hz) {
  if (hz < 200)
    hz = 200;
  if (hz > 4000)
    hz = 4000;
  d_->toneHz = hz;
}

void HellEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count <= 0 || sampleRateHz < 8000)
    return;
  const double ratio = static_cast<double>(kSampleRate) / static_cast<double>(sampleRateHz);
  std::vector<double> audio;
  audio.reserve(static_cast<size_t>(count * ratio + 2));
  for (int i = 0; i < count; ++i) {
    d_->bucket += samples[i];
    ++d_->bucketCount;
    d_->held += ratio;
    if (d_->held >= 1.0) {
      audio.push_back(d_->bucket / static_cast<double>(d_->bucketCount));
      d_->bucket = 0.0;
      d_->bucketCount = 0;
      d_->held -= 1.0;
    }
  }

  // feld::rx_process. Analytic pair, Hilbert, mix to baseband, lowpass.
  for (double sample : audio) {
    cmplx z(sample, sample);
    d_->hilbert->run(z, z);
    z = d_->mix(z);
    cmplx* filtered = nullptr;
    const int n = d_->lowpass->run(z, &filtered);
    for (int i = 0; i < n; ++i) {
      QByteArray column;
      if (d_->takePixel(filtered[i], &column))
        emit columnReady(column);
    }
  }
}
