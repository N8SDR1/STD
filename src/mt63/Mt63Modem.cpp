// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MT63 transmit and receive from Pawel Jalocha, SP9VRC, as maintained in
// fldigi by Dave Freese, W1HKJ (GPL-3). The Walsh code, interleaver, and
// the 64-carrier modulator are those sources. STD does not invent them.

#include "Mt63Modem.h"

#include "dsp.h"
#include "mt63base.h"

#include <immintrin.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Qt's graphics startup leaves flush-to-zero set. Jalocha's filters were
// written for gradual underflow; a denormal tap that flushes to zero moves
// the 8 kHz wave off the shape the receiver can print.
struct IeeeGuard {
  unsigned int saved;
  IeeeGuard() : saved(_mm_getcsr()) {
    _mm_setcsr(0x1F80);
    // First libm use in this process can run before the filters. Touch it
    // while flush-to-zero is off, then put the SSE control word back.
    const volatile double warm = std::sin(0.01);
    (void)warm;
    _mm_setcsr(0x1F80);
  }
  ~IeeeGuard() { _mm_setcsr(saved); }
};

constexpr int kModemRate = 8000;
constexpr int kPlayRate = 48000;
constexpr int kUp = kPlayRate / kModemRate;

int bandwidthOf(const QString& mode) {
  if (!mode.startsWith(QLatin1String("MT63")))
    return 0;
  if (mode.contains(QLatin1String("2000")))
    return 2000;
  if (mode.contains(QLatin1String("1000")))
    return 1000;
  if (mode.contains(QLatin1String("500")))
    return 500;
  return 0;
}

void takeSymbol(MT63tx& tx, std::vector<double>* audio) {
  const int n = tx.Comb.Output.Len;
  if (n <= 0 || !tx.Comb.Output.Data)
    return;
  audio->insert(audio->end(), tx.Comb.Output.Data, tx.Comb.Output.Data + n);
}

QString printable(const MT63rx& rx) {
  QString text;
  for (int i = 0; i < rx.Output.Len; ++i) {
    const unsigned char c = static_cast<unsigned char>(rx.Output.Data[i]);
    if (c >= 32 && c < 127)
      text.append(QLatin1Char(static_cast<char>(c)));
  }
  return text;
}

QString decode8k(const QString& mode, const double* samples, int count, int centerHz) {
  const IeeeGuard ieee;
  const int bandwidth = bandwidthOf(mode);
  if (bandwidth <= 0 || !samples || count <= 0)
    return {};
  MT63rx rx;
  if (rx.Preset(centerHz, bandwidth, mt63LongInterleave(mode) ? 1 : 0, 16, nullptr))
    return {};
  double_buff in;
  QString text;
  int pos = 0;
  while (pos < count) {
    const int n = std::min(800, count - pos);
    if (in.EnsureSpace(n))
      break;
    for (int i = 0; i < n; ++i)
      in.Data[i] = samples[pos + i];
    in.Len = n;
    rx.Process(&in);
    text.append(printable(rx));
    pos += n;
  }
  return text;
}

std::vector<double> downsample(const float* samples, int count, int sampleRateHz) {
  std::vector<double> out;
  if (!samples || count <= 0 || sampleRateHz < kModemRate)
    return out;
  if (sampleRateHz % kModemRate != 0)
    return out;
  const int ratio = sampleRateHz / kModemRate;
  out.reserve(static_cast<size_t>(count / ratio));
  for (int i = 0; i + ratio <= count; i += ratio) {
    double sum = 0.0;
    for (int k = 0; k < ratio; ++k)
      sum += samples[i + k];
    out.push_back(sum / static_cast<double>(ratio));
  }
  return out;
}

}  // namespace

int mt63BandwidthHz(const QString& mode) {
  return bandwidthOf(mode);
}

bool mt63LongInterleave(const QString& mode) {
  return bandwidthOf(mode) > 0 && mode.endsWith(QLatin1Char('L'));
}

Mt63Plan planMt63(const QString& mode, const QString& text, int centerHz) {
  const IeeeGuard ieee;
  Mt63Plan plan;
  const int bandwidth = bandwidthOf(mode);
  if (bandwidth <= 0 || text.isEmpty())
    return plan;
  int center = centerHz;
  if (center < 200)
    center = 200;
  if (center > 4000)
    center = 4000;
  MT63tx tx;
  if (tx.Preset(center, bandwidth, mt63LongInterleave(mode) ? 1 : 0))
    return plan;
  _mm_setcsr(0x1F80);

  std::vector<double> audio;
  std::vector<int> echo8;
  for (int i = 0; i < tx.DataInterleave; ++i) {
    tx.SendChar(0);
    takeSymbol(tx, &audio);
  }
  for (const QChar ch : text) {
    unsigned char c = ' ';
    if (ch.unicode() >= 32 && ch.unicode() < 127)
      c = static_cast<unsigned char>(ch.unicode());
    echo8.push_back(static_cast<int>(audio.size()));
    tx.SendChar(static_cast<char>(c));
    takeSymbol(tx, &audio);
  }
  for (int i = 0; i < tx.DataInterleave + 48; ++i) {
    tx.SendChar(0);
    takeSymbol(tx, &audio);
  }
  tx.SendSilence();
  takeSymbol(tx, &audio);
  if (audio.empty())
    return plan;

  double peak = 0.0;
  for (double sample : audio)
    peak = std::max(peak, std::fabs(sample));
  if (peak <= 0.0)
    return plan;
  const double scale = 0.8 / peak;
  plan.samples.reserve(audio.size() * static_cast<size_t>(kUp));
  for (double sample : audio) {
    const float value = static_cast<float>(sample * scale);
    for (int i = 0; i < kUp; ++i)
      plan.samples.push_back(value);
  }
  plan.echoAt.reserve(echo8.size());
  for (int at : echo8)
    plan.echoAt.push_back(at * kUp);
  return plan;
}

QString mt63Decode(const QString& mode, const float* samples, int count, int sampleRateHz,
                   int centerHz) {
  const std::vector<double> audio = downsample(samples, count, sampleRateHz);
  if (audio.empty())
    return {};
  int center = centerHz;
  if (center < 200)
    center = 200;
  if (center > 4000)
    center = 4000;
  return decode8k(mode, audio.data(), static_cast<int>(audio.size()), center);
}

struct Mt63Engine::Private {
  bool armed = false;
  bool squelchOn = false;
  int squelchLevel = 10;
  int centerHz = 1500;
  QString mode;
  MT63rx* rx = nullptr;
  double bucket = 0.0;
  int bucketCount = 0;
  int ratio = kUp;

  void closeRx() {
    delete rx;
    rx = nullptr;
  }

  void openRx() {
    closeRx();
    bucket = 0.0;
    bucketCount = 0;
    ratio = kUp;
    const int bandwidth = bandwidthOf(mode);
    if (bandwidth <= 0)
      return;
    rx = new MT63rx();
    if (rx->Preset(centerHz, bandwidth, mt63LongInterleave(mode) ? 1 : 0, 16, nullptr))
      closeRx();
  }
};

Mt63Engine::Mt63Engine(QObject* parent) : QObject(parent), d_(new Private) {}

Mt63Engine::~Mt63Engine() {
  d_->closeRx();
  delete d_;
}

void Mt63Engine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  if (armed)
    d_->openRx();
  else
    d_->closeRx();
}

void Mt63Engine::setMode(const QString& mode) {
  if (d_->mode == mode)
    return;
  d_->mode = mode;
  if (d_->armed)
    d_->openRx();
}

void Mt63Engine::setCenterHz(int hz) {
  if (hz < 200)
    hz = 200;
  if (hz > 4000)
    hz = 4000;
  if (d_->centerHz == hz)
    return;
  d_->centerHz = hz;
  if (d_->armed)
    d_->openRx();
}

void Mt63Engine::setSquelch(bool on, int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  d_->squelchOn = on;
  d_->squelchLevel = level;
}

void Mt63Engine::clear() {
  if (d_->armed)
    d_->openRx();
}

void Mt63Engine::pushAudio(const float* samples, int count, int sampleRateHz) {
  const IeeeGuard ieee;
  if (!d_->armed || !d_->rx || !samples || count <= 0 || sampleRateHz < kModemRate ||
      sampleRateHz % kModemRate != 0)
    return;
  d_->ratio = sampleRateHz / kModemRate;
  std::vector<double> audio;
  audio.reserve(static_cast<size_t>(count / d_->ratio + 2));
  for (int i = 0; i < count; ++i) {
    d_->bucket += samples[i];
    ++d_->bucketCount;
    if (d_->bucketCount >= d_->ratio) {
      audio.push_back(d_->bucket / static_cast<double>(d_->ratio));
      d_->bucket = 0.0;
      d_->bucketCount = 0;
    }
  }
  if (audio.empty())
    return;
  double_buff in;
  if (in.EnsureSpace(static_cast<int>(audio.size())))
    return;
  for (int i = 0; i < static_cast<int>(audio.size()); ++i)
    in.Data[i] = audio[static_cast<size_t>(i)];
  in.Len = static_cast<int>(audio.size());
  d_->rx->Process(&in);
  if (d_->squelchOn && d_->rx->FEC_SNR() < d_->squelchLevel)
    return;
  const QString text = printable(*d_->rx);
  if (!text.isEmpty())
    emit textReady(text);
}
