// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Feld Hell receive from fldigi 4.2.06, tag v4.2.06.
// feld.cxx mixer(), rx(), and rx_process() by Dave Freese, W1HKJ,
// GPL-3.0-or-later. The lowpass and Hilbert transforms are the
// fldigi filter classes compiled beside this file. TCIdigi carries the
// same feld.cxx unchanged. Transmit is in TxWave.cpp.

#include "HellEngine.h"

#include "config.h"
#include "fftfilt.h"
#include "filters.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kSampleRate = 8000;
constexpr int kColumnLen = 20;  // fldigi HellRcvHeight default

// fldigi progdefaults HELL_BW_* defaults, the receive filter bandwidth for
// each mode. rx_process builds the lowpass at half of it.
double rxBandwidthHz(double columnRate, double fskShift) {
  if (fskShift > 0.0) {
    if (std::fabs(fskShift - 55.0) < 1.0)
      return 120.0;  // FSK Hell-105
    if (std::fabs(fskShift - 300.0) < 1.0)
      return 450.0;  // Hell 80
    return 250.0;    // FSK Hell (122.5 Hz shift)
  }
  if (std::fabs(columnRate - 2.1875) < 0.01)
    return 30.0;     // Slow Hell
  if (std::fabs(columnRate - 87.5) < 0.01)
    return 1225.0;   // Feld Hell X5
  if (std::fabs(columnRate - 157.5) < 0.01)
    return 2205.0;   // Feld Hell X9
  if (std::fabs(columnRate - 17.5) < 0.01)
    return 245.0;    // Feld Hell
  return 14.0 * columnRate;
}

// Lowpass taps for an integer decimation to 8000 Hz, cut at 3600 Hz.
std::vector<double> decimatorTaps(int factor) {
  const int taps = 16 * factor + 1;
  const int half = taps / 2;
  const double fc = 3600.0 / (8000.0 * factor);
  std::vector<double> h(static_cast<size_t>(taps));
  double sum = 0.0;
  for (int k = 0; k < taps; ++k) {
    const double u = double(k - half);
    const double s = u == 0.0 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * u) / (M_PI * u);
    const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * k / (taps - 1)) +
                     0.08 * std::cos(4.0 * M_PI * k / (taps - 1));
    h[size_t(k)] = s * w;
    sum += s * w;
  }
  for (double& v : h)
    v /= sum;
  return h;
}

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
  double columnRate = 17.5;
  double fskShift = 0.0;
  double phi2freq = 0.0;
  double downsampleInc = (kColumnLen * 17.5) / static_cast<double>(kSampleRate);
  cmplx prev = cmplx(0.0, 0.0);
  int decimFactor = 0;
  std::vector<double> decimTaps;
  std::vector<double> decimHistory;
  int decimPhase = 0;
  C_FIR_filter* hilbert = nullptr;
  fftfilt* lowpass = nullptr;
  Cmovavg* average = nullptr;
  Cmovavg* bbfilt = nullptr;

  void applyFilter() {
    if (!lowpass)
      return;
    // feld::rx_process: lpfilt->create_filter(0, 0.5 * HELL_BW / samplerate)
    // with the mode's default HELL_BW. X5 and X9 need the wide filter;
    // Slow Hell gets the narrow one.
    phi2freq = fskShift > 0.0 ? static_cast<double>(kSampleRate) / M_PI / (fskShift / 2.0) : 0.0;
    lowpass->create_filter(0, 0.5 * rxBandwidthHz(columnRate, fskShift) / kSampleRate);
  }

  void applyAverager() {
    // feld::restart: average->setLength(500 * samplerate / rxpixrate).
    if (!average)
      return;
    const double pixels = std::max(1.0, kColumnLen * columnRate);
    average->setLength(std::max(1, static_cast<int>(500.0 * kSampleRate / pixels)));
  }

  void open() {
    hilbert = new C_FIR_filter();
    hilbert->init_hilbert(37, 1);
    lowpass = new fftfilt(0, 0.5 * rxBandwidthHz(columnRate, fskShift) / kSampleRate, 1024);
    average = new Cmovavg(200);
    bbfilt = new Cmovavg(8);
    applyAverager();
    applyFilter();
  }

  void close() {
    delete hilbert;
    delete lowpass;
    delete average;
    delete bbfilt;
    hilbert = nullptr;
    lowpass = nullptr;
    average = nullptr;
    bbfilt = nullptr;
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
    prev = cmplx(0.0, 0.0);
    decimHistory.assign(decimTaps.size(), 0.0);
    decimPhase = 0;
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

    // fldigi hellagc default 2, medium decay.
    if (avg > agc)
      agc = avg;
    else
      agc *= (1.0 - 0.075 / kColumnLen);
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

  // feld::FSKH_rx. The phase step between samples is the tone, not the loudness.
  bool takeFsk(cmplx z, QByteArray* column) {
    const cmplx step = std::conj(prev) * z;
    prev = z;
    double f = std::arg(step) * phi2freq;
    if (bbfilt)
      f = bbfilt->run(f);
    const double avg = average->run(std::abs(z));
    rxcounter += downsampleInc;
    if (rxcounter < 1.0)
      return false;
    rxcounter -= 1.0;

    if (avg > agc)
      agc = avg;
    else
      agc *= (1.0 - 0.075 / kColumnLen);
    const double metric = clampd(1000.0 * agc, 0.0, 100.0);
    const bool gated = squelchOn && metric <= static_cast<double>(squelchLevel);

    // Reverse off, blackboard off. A negative step is the lower tone, which is ink.
    const double vid = clampd(0.5 * (f + 1.0), 0.0, 1.0);
    const int ix = static_cast<int>(vid * 255.0);
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

void HellEngine::setColumnRate(double columnsPerSecond) {
  if (columnsPerSecond < 1.0)
    columnsPerSecond = 17.5;
  if (std::fabs(d_->columnRate - columnsPerSecond) < 1e-9)
    return;
  d_->columnRate = columnsPerSecond;
  d_->downsampleInc = (kColumnLen * d_->columnRate) / static_cast<double>(kSampleRate);
  if (d_->armed) {
    d_->reset();
  } else {
    d_->applyAverager();
    d_->applyFilter();
  }
}

void HellEngine::setFskShiftHz(double shiftHz) {
  if (shiftHz < 1.0)
    shiftHz = 0.0;
  if (std::fabs(d_->fskShift - shiftHz) < 0.01)
    return;
  d_->fskShift = shiftHz;
  if (d_->armed)
    d_->reset();
  else
    d_->applyFilter();
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
  // Fldigi resamples to 8000 Hz through libsamplerate. Integer ratios
  // (48000, 96000) use a windowed-sinc lowpass so 4 to 8 kHz noise does not
  // fold onto the tone. Other rates keep the plain block average.
  const int factor = sampleRateHz % kSampleRate == 0 ? sampleRateHz / kSampleRate : 0;
  if (factor != d_->decimFactor) {
    d_->decimFactor = factor;
    d_->decimTaps = factor > 1 ? decimatorTaps(factor) : std::vector<double>();
    d_->decimHistory.assign(d_->decimTaps.size(), 0.0);
    d_->decimPhase = 0;
  }
  if (factor == 1) {
    for (int i = 0; i < count; ++i)
      audio.push_back(samples[i]);
  } else if (factor > 1) {
    std::vector<double>& hist = d_->decimHistory;
    const std::vector<double>& h = d_->decimTaps;
    const size_t taps = h.size();
    for (int i = 0; i < count; ++i) {
      std::move(hist.begin() + 1, hist.end(), hist.begin());
      hist.back() = samples[i];
      if (++d_->decimPhase < factor)
        continue;
      d_->decimPhase = 0;
      double acc = 0.0;
      for (size_t k = 0; k < taps; ++k)
        acc += h[k] * hist[k];
      audio.push_back(acc);
    }
  }
  for (int i = 0; factor == 0 && i < count; ++i) {
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
      const bool printed = d_->fskShift > 0.0 ? d_->takeFsk(filtered[i], &column)
                                              : d_->takePixel(filtered[i], &column);
      if (printed)
        emit columnReady(column);
    }
  }
}
