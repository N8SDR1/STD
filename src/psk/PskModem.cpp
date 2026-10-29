// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Varicode table from fldigi pskvaricode.cxx (Dave Freese, W1HKJ, GPL-3,
// adapted from gmfsk). A 0 reverses the phase. Characters are separated
// by 00, which never occurs inside a code.

#include "PskModem.h"

#include "DigitalMode.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

const char* kCode[128] = {
    "1010101011", "1011011011", "1011101101", "1101110111", "1011101011", "1101011111", "1011101111",
    "1011111101", "1011111111", "11101111",   "11101",      "1101101111", "1011011101", "11111",
    "1101110101", "1110101011", "1011110111", "1011110101", "1110101101", "1110101111", "1101011011",
    "1101101011", "1101101101", "1101010111", "1101111011", "1101111101", "1110110111", "1101010101",
    "1101011101", "1110111011", "1011111011", "1101111111", "1",          "111111111",  "101011111",
    "111110101",  "111011011",  "1011010101", "1010111011", "101111111",  "11111011",   "11110111",
    "101101111",  "111011111",  "1110101",    "110101",     "1010111",    "110101111",  "10110111",
    "10111101",   "11101101",   "11111111",   "101110111",  "101011011",  "101101011",  "110101101",
    "110101011",  "110110111",  "11110101",   "110111101",  "111101101",  "1010101",    "111010111",
    "1010101111", "1010111101", "1111101",    "11101011",   "10101101",   "10110101",   "1110111",
    "11011011",   "11111101",   "101010101",  "1111111",    "111111101",  "101111101",  "11010111",
    "10111011",   "11011101",   "10101011",   "11010101",   "111011101",  "10101111",   "1101111",
    "1101101",    "101010111",  "110110101",  "101011101",  "101110101",  "101111011",  "1010101101",
    "111110111",  "111101111",  "111111011",  "1010111111", "101101101",  "1011011111", "1011",
    "1011111",    "101111",     "101101",     "11",         "111101",     "1011011",    "101011",
    "1101",       "111101011",  "10111111",   "11011",      "111011",     "1111",       "111",
    "111111",     "110111111",  "10101",      "10111",      "101",        "110111",     "1111011",
    "1101011",    "11011111",   "1011101",    "111010101",  "1010110111", "110111011",  "1010110101",
    "1011010111", "1110110101",
};

const char* codeFor(unsigned char c) {
  return kCode[c];
}

QString lookup(const QString& bits) {
  if (bits.isEmpty())
    return {};
  for (int i = 0; i < 128; ++i) {
    if (bits == QLatin1String(kCode[i])) {
      const char glyph = char(i);
      if (glyph == '\0' || glyph < 32 && glyph != '\n' && glyph != '\r')
        return {};
      return QString(QChar::fromLatin1(glyph));
    }
  }
  return {};
}

QString codesFromBits(const QString& bits) {
  QString out;
  QString word;
  int zeros = 0;
  for (const QChar bit : bits) {
    if (bit == QLatin1Char('0')) {
      ++zeros;
      if (zeros == 2) {
        out += lookup(word);
        word.clear();
        zeros = 0;
        continue;
      }
      if (zeros == 1)
        continue;
    }
    if (zeros == 1)
      word.append(QLatin1Char('0'));
    zeros = 0;
    word.append(bit);
  }
  return out;
}

}  // namespace

PskPlan planPsk(const QString& mode, const QString& text, int centerHz) {
  PskPlan plan;
  const double baud = pskBaud(mode);
  if (baud <= 0.0 || text.isEmpty())
    return plan;
  const int rate = 48000;
  const int symbol = std::max(8, int(std::lround(double(rate) / baud)));
  const int center = std::clamp(centerHz, 200, 3000);
  // Symbol 0 is unmodulated carrier. The decoder uses it as the phase
  // reference and does not turn it into a varicode bit. Without it the
  // first preamble zero is eaten and an odd zero sticks to the character.
  QString bits(32, QLatin1Char('0'));
  plan.echoAt.assign(static_cast<size_t>(text.size()), -1);
  for (int i = 0; i < text.size(); ++i) {
    const ushort u = text.at(i).unicode();
    if (u > 127)
      continue;
    plan.echoAt[static_cast<size_t>(i)] = (bits.size() + 1) * symbol;
    bits.append(QLatin1String(codeFor(static_cast<unsigned char>(u))));
    bits.append(QLatin1String("00"));
  }
  bits.append(QString(16, QLatin1Char('0')));

  const int count = (bits.size() + 1) * symbol + symbol;
  std::vector<double> acc(static_cast<size_t>(count), 0.0);
  int sign = 1;
  auto paint = [&](int symbolIndex) {
    const int origin = symbolIndex * symbol - symbol / 2;
    for (int k = 0; k < 2 * symbol; ++k) {
      const int idx = origin + k;
      if (idx < 0 || idx >= count)
        continue;
      const double w = std::sin(3.141592653589793 * double(k) / double(2 * symbol));
      acc[static_cast<size_t>(idx)] += double(sign) * w * w;
    }
  };
  paint(0);
  for (int b = 0; b < bits.size(); ++b) {
    if (bits.at(b) == QLatin1Char('0'))
      sign = -sign;
    paint(b + 1);
  }
  plan.samples.resize(static_cast<size_t>(count));
  double phase = 0.0;
  const double step = 2.0 * 3.141592653589793 * double(center) / double(rate);
  for (int i = 0; i < count; ++i) {
    plan.samples[static_cast<size_t>(i)] = float(0.6 * acc[static_cast<size_t>(i)] * std::sin(phase));
    phase += step;
    if (phase > 6.283185307179586)
      phase -= 6.283185307179586;
  }
  return plan;
}

QString pskDecode(const QString& mode, const float* samples, int count, int sampleRateHz, int centerHz) {
  const double baud = pskBaud(mode);
  if (!samples || count < 32 || sampleRateHz < 1000 || baud <= 0.0)
    return {};
  const int symbol = std::max(8, int(std::lround(double(sampleRateHz) / baud)));
  const int center = std::clamp(centerHz, 200, 3000);
  const double a = 1.0 - std::exp(-2.0 * 3.141592653589793 * baud / double(sampleRateHz));
  std::vector<double> iAcc(static_cast<size_t>(count));
  std::vector<double> qAcc(static_cast<size_t>(count));
  double ii = 0.0;
  double qq = 0.0;
  double phase = 0.0;
  const double step = 2.0 * 3.141592653589793 * double(center) / double(sampleRateHz);
  for (int n = 0; n < count; ++n) {
    ii += a * (samples[n] * std::cos(phase) - ii);
    qq += a * (samples[n] * std::sin(phase) - qq);
    iAcc[static_cast<size_t>(n)] = ii;
    qAcc[static_cast<size_t>(n)] = qq;
    phase += step;
    if (phase > 6.283185307179586)
      phase -= 6.283185307179586;
  }

  int bestOff = symbol / 2;
  int bestScore = -1;
  double bestAmp = -1.0;
  const int probe = std::min(24, (count - symbol) / symbol);
  for (int off = 0; off < symbol; ++off) {
    int score = 0;
    double amp = 0.0;
    double prevI = 0.0;
    double prevQ = 0.0;
    bool have = false;
    for (int s = 0; s < probe; ++s) {
      const int c = off + s * symbol;
      if (c < 0 || c >= count)
        break;
      const double iiNow = iAcc[static_cast<size_t>(c)];
      const double qqNow = qAcc[static_cast<size_t>(c)];
      amp += iiNow * iiNow + qqNow * qqNow;
      if (have) {
        const double dot = prevI * iiNow + prevQ * qqNow;
        if (dot < 0.0)
          ++score;
      }
      prevI = iiNow;
      prevQ = qqNow;
      have = true;
    }
    if (score > bestScore || (score == bestScore && amp > bestAmp)) {
      bestScore = score;
      bestAmp = amp;
      bestOff = off;
    }
  }

  QString bits;
  double prevI = 0.0;
  double prevQ = 0.0;
  bool have = false;
  for (int c = bestOff; c < count; c += symbol) {
    const double iiNow = iAcc[static_cast<size_t>(c)];
    const double qqNow = qAcc[static_cast<size_t>(c)];
    if (!have) {
      prevI = iiNow;
      prevQ = qqNow;
      have = true;
      continue;
    }
    const double dot = prevI * iiNow + prevQ * qqNow;
    bits.append(dot < 0.0 ? QLatin1Char('0') : QLatin1Char('1'));
    prevI = iiNow;
    prevQ = qqNow;
  }
  return codesFromBits(bits);
}

struct PskEngine::Private {
  bool armed = false;
  bool squelchOn = false;
  int squelch = 10;
  QString mode = QStringLiteral("PSK31");
  int center = 1500;
  int rate = 48000;
  std::vector<float> pending;
  QString shown;

  void reset() {
    pending.clear();
    shown.clear();
  }
};

PskEngine::PskEngine(QObject* parent) : QObject(parent), d_(new Private) {}

PskEngine::~PskEngine() {
  delete d_;
}

void PskEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  d_->reset();
}

void PskEngine::setMode(const QString& mode) {
  if (d_->mode == mode)
    return;
  d_->mode = mode;
  d_->reset();
}

void PskEngine::setToneHz(int hz) {
  d_->center = std::clamp(hz, 200, 3000);
}

void PskEngine::setSquelch(bool on, int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  d_->squelchOn = on;
  d_->squelch = level;
}

void PskEngine::clear() {
  d_->reset();
}

void PskEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000 || pskBaud(d_->mode) <= 0.0)
    return;
  if (d_->squelchOn && d_->squelch >= 90)
    return;
  d_->rate = sampleRateHz;
  d_->pending.insert(d_->pending.end(), samples, samples + count);
  const int symbol = std::max(8, int(std::lround(double(sampleRateHz) / pskBaud(d_->mode))));
  const int keep = symbol * 80;
  if (int(d_->pending.size()) < keep)
    return;
  const QString text = pskDecode(d_->mode, d_->pending.data(), int(d_->pending.size()), sampleRateHz,
                                 d_->center);
  if (int(d_->pending.size()) > keep * 2)
    d_->pending.erase(d_->pending.begin(), d_->pending.end() - keep);
  if (text.isEmpty())
    return;
  if (d_->shown.isEmpty()) {
    emit textReady(text);
    d_->shown = text;
    return;
  }
  if (text.startsWith(d_->shown)) {
    const QString extra = text.mid(d_->shown.size());
    if (!extra.isEmpty())
      emit textReady(extra);
    d_->shown = text;
    return;
  }
  // The buffer slid. The new decode overlaps what was already shown.
  d_->shown = text;
}
