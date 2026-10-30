// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Varicode table from fldigi pskvaricode.cxx (Dave Freese, W1HKJ, GPL-3,
// adapted from gmfsk). A 0 reverses the phase. Characters are separated
// by 00, which never occurs inside a code.

#include "PskModem.h"

#include "DigitalMode.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace {

const char* kCode[256] = {
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
    "1011010111", "1110110101", "1110111101", "1110111111", "1111010101", "1111010111", "1111011011",
    "1111011101", "1111011111", "1111101011", "1111101101", "1111101111", "1111110101", "1111110111",
    "1111111011", "1111111101", "1111111111", "10101010101", "10101010111", "10101011011", "10101011101",
    "10101011111", "10101101011", "10101101101", "10101101111", "10101110101", "10101110111", "10101111011",
    "10101111101", "10101111111", "10110101011", "10110101101", "10110101111", "10110110101", "10110110111",
    "10110111011", "10110111101", "10110111111", "10111010101", "10111010111", "10111011011", "10111011101",
    "10111011111", "10111101011", "10111101101", "10111101111", "10111110101", "10111110111", "10111111011",
    "10111111101", "10111111111", "11010101011", "11010101101", "11010101111", "11010110101", "11010110111",
    "11010111011", "11010111101", "11010111111", "11011010101", "11011010111", "11011011011", "11011011101",
    "11011011111", "11011101011", "11011101101", "11011101111", "11011110101", "11011110111", "11011111011",
    "11011111101", "11011111111", "11101010101", "11101010111", "11101011011", "11101011101", "11101011111",
    "11101101011", "11101101101", "11101101111", "11101110101", "11101110111", "11101111011", "11101111101",
    "11101111111", "11110101011", "11110101101", "11110101111", "11110110101", "11110110111", "11110111011",
    "11110111101", "11110111111", "11111010101", "11111010111", "11111011011", "11111011101", "11111011111",
    "11111101011", "11111101101", "11111101111", "11111110101", "11111110111", "11111111011", "11111111101",
    "11111111111", "101010101011", "101010101101", "101010101111", "101010110101", "101010110111",
    "101010111011", "101010111101", "101010111111", "101011010101", "101011010111", "101011011011",
    "101011011101", "101011011111", "101011101011", "101011101101", "101011101111", "101011110101",
    "101011110111", "101011111011", "101011111101", "101011111111", "101101010101", "101101010111",
    "101101011011",
};

const char* codeFor(unsigned char c) {
  return kCode[c];
}

QString lookup(const QString& bits) {
  if (bits.isEmpty())
    return {};
  for (int i = 0; i < 256; ++i) {
    if (bits == QLatin1String(kCode[i])) {
      const unsigned char glyph = static_cast<unsigned char>(i);
      if (glyph == 0 || (glyph < 32 && glyph != '\n' && glyph != '\r'))
        return {};
      return QString(QChar::fromLatin1(char(glyph)));
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

// Fldigi QPSK: K=5, polynomials 0x17 and 0x19. Each varicode bit becomes
// one dibit. The dibit is a differential quarter-turn of the carrier.
// 8PSK uses a different constellation and is not built here.

class QpskEncoder {
public:
  QpskEncoder() { init(); }

  void init() {
    const int size = 1 << 5;
    output_.assign(static_cast<size_t>(size), 0);
    for (int i = 0; i < size; ++i) {
      const auto parity = [](unsigned word) {
        word ^= word >> 16;
        word ^= word >> 8;
        word ^= word >> 4;
        word ^= word >> 2;
        word ^= word >> 1;
        return int(word & 1u);
      };
      output_[static_cast<size_t>(i)] = parity(unsigned(0x17 & i)) | (parity(unsigned(0x19 & i)) << 1);
    }
    shreg_ = 0;
  }

  int encode(int bit) {
    shreg_ = (shreg_ << 1) | (bit ? 1u : 0u);
    return output_[shreg_ & 31u];
  }

private:
  std::vector<int> output_;
  unsigned shreg_ = 0;
};

class QpskViterbi {
public:
  QpskViterbi() { init(); }

  int decode(unsigned char sym0, unsigned char sym1) {
    const unsigned curr = ptr_;
    const unsigned prev = (curr - 1u) % 256u;
    int met[4];
    met[0] = mettab_[0][sym1] + mettab_[0][sym0];
    met[1] = mettab_[0][sym1] + mettab_[1][sym0];
    met[2] = mettab_[1][sym1] + mettab_[0][sym0];
    met[3] = mettab_[1][sym1] + mettab_[1][sym0];
    for (int n = 0; n < nstates_; ++n) {
      const int s0 = n;
      const int s1 = n + nstates_;
      const int p0 = s0 >> 1;
      const int p1 = s1 >> 1;
      const int m0 = cell(metrics_, prev, p0) + met[output_[static_cast<size_t>(s0)]];
      const int m1 = cell(metrics_, prev, p1) + met[output_[static_cast<size_t>(s1)]];
      if (m0 > m1) {
        cell(metrics_, curr, n) = m0;
        cell(history_, curr, n) = p0;
      } else {
        cell(metrics_, curr, n) = m1;
        cell(history_, curr, n) = p1;
      }
    }
    ptr_ = (ptr_ + 1u) % 256u;
    if ((ptr_ % 8u) == 0)
      return traceback();
    return -1;
  }

private:
  int& cell(std::vector<int>& data, unsigned row, int state) {
    return data[static_cast<size_t>(row) * static_cast<size_t>(nstates_) + static_cast<size_t>(state)];
  }

  void init() {
    nstates_ = 1 << 4;
    const int size = 1 << 5;
    output_.assign(static_cast<size_t>(size), 0);
    for (int i = 0; i < size; ++i) {
      const auto parity = [](unsigned word) {
        word ^= word >> 16;
        word ^= word >> 8;
        word ^= word >> 4;
        word ^= word >> 2;
        word ^= word >> 1;
        return int(word & 1u);
      };
      output_[static_cast<size_t>(i)] = parity(unsigned(0x17 & i)) | (parity(unsigned(0x19 & i)) << 1);
    }
    metrics_.assign(static_cast<size_t>(256 * nstates_), 0);
    history_.assign(static_cast<size_t>(256 * nstates_), 0);
    for (int i = 0; i < 256; ++i) {
      mettab_[0][i] = 128 - i;
      mettab_[1][i] = i - 128;
    }
    ptr_ = 0;
  }

  int traceback() {
    unsigned p = (ptr_ - 1u) % 256u;
    int bestMetric = INT_MIN;
    int bestState = 0;
    for (int i = 0; i < nstates_; ++i) {
      const int value = cell(metrics_, p, i);
      if (value > bestMetric) {
        bestMetric = value;
        bestState = i;
      }
    }
    std::vector<int> sequence(256, 0);
    sequence[p] = bestState;
    for (int i = 0; i < 60; ++i) {
      const unsigned prev = (p - 1u) % 256u;
      sequence[prev] = cell(history_, p, sequence[p]);
      p = prev;
    }
    unsigned bits = 0;
    for (int i = 0; i < 8; ++i) {
      bits = (bits << 1) | unsigned(sequence[p] & 1);
      p = (p + 1u) % 256u;
    }
    return int(bits);
  }

  int nstates_ = 0;
  unsigned ptr_ = 0;
  std::vector<int> output_;
  std::vector<int> metrics_;
  std::vector<int> history_;
  int mettab_[2][256] = {};
};

int qpskPreamble(double baud) {
  return std::max(32, int(std::lround(baud * 32.0 / 31.25)));
}

// fldigi psk::psk() dcdbits. PSK1000 is 128, not a straight doubling.
int bpskDcdBits(double baud) {
  if (baud >= 900.0)
    return 128;
  if (baud >= 400.0)
    return 512;
  if (baud >= 200.0)
    return 256;
  if (baud >= 100.0)
    return 128;
  if (baud >= 50.0)
    return 64;
  return 32;
}

void rotateQpsk(int sym, double* ui, double* uq) {
  // fldigi maps the dibit through (4 - sym) & 3 onto 180, 90, 0, 270.
  static const double rotI[4] = {-1.0, 0.0, 1.0, 0.0};
  static const double rotQ[4] = {0.0, 1.0, 0.0, -1.0};
  const int mapped = sym & 3;
  const double nextI = (*ui) * rotI[mapped] - (*uq) * rotQ[mapped];
  const double nextQ = (*ui) * rotQ[mapped] + (*uq) * rotI[mapped];
  *ui = nextI;
  *uq = nextQ;
}

PskPlan planQpsk(const QString& mode, const QString& text, int centerHz) {
  PskPlan plan;
  const double baud = qpskBaud(mode);
  if (baud <= 0.0 || text.isEmpty())
    return plan;
  const int rate = 48000;
  const int symbol = std::max(8, int(std::lround(double(rate) / baud)));
  const int center = std::clamp(centerHz, 200, 3000);
  const int preamble = qpskPreamble(baud);
  QpskEncoder enc;
  std::vector<int> syms;
  syms.reserve(static_cast<size_t>(preamble * 3 + text.size() * 16));
  for (int i = 0; i < preamble; ++i)
    syms.push_back(0);
  plan.echoAt.assign(static_cast<size_t>(text.size()), -1);
  for (int i = 0; i < text.size(); ++i) {
    const ushort u = text.at(i).unicode();
    if (u > 255)
      continue;
    plan.echoAt[static_cast<size_t>(i)] = int(syms.size()) * symbol;
    const char* code = codeFor(static_cast<unsigned char>(u));
    for (const char* p = code; *p; ++p)
      syms.push_back(enc.encode(*p == '1' ? 1 : 0));
    syms.push_back(enc.encode(0));
    syms.push_back(enc.encode(0));
  }
  // K=5 traceback is 60 symbols. The fldigi postamble is the DCD window.
  // Extra encoded zeros let a finite buffer release the last character.
  const int flush = std::max(preamble, 76);
  for (int i = 0; i < flush; ++i)
    syms.push_back(enc.encode(0));

  const int count = int(syms.size()) * symbol;
  plan.samples.resize(static_cast<size_t>(count));
  double ui = 1.0;
  double uq = 0.0;
  double carrier = 0.0;
  const double step = 2.0 * 3.141592653589793 * double(center) / double(rate);
  int at = 0;
  for (int sym : syms) {
    const double fromI = ui;
    const double fromQ = uq;
    rotateQpsk(sym, &ui, &uq);
    // fldigi tx_carriers: shapeA = 0.5*cos(i*pi/symbollen)+0.5 over the
    // whole symbol, then I = shapeA*previous + (1-shapeA)*next.
    for (int k = 0; k < symbol; ++k) {
      const double shapeA =
          0.5 * std::cos(3.141592653589793 * double(k) / double(symbol)) + 0.5;
      const double shapeB = 1.0 - shapeA;
      const double ii = shapeA * fromI + shapeB * ui;
      const double qq = shapeA * fromQ + shapeB * uq;
      plan.samples[static_cast<size_t>(at)] = float(0.6 * (ii * std::cos(carrier) + qq * std::sin(carrier)));
      carrier += step;
      if (carrier > 6.283185307179586)
        carrier -= 6.283185307179586;
      ++at;
    }
  }
  return plan;
}

#include "PskStream.inc"

}  // namespace

PskPlan planPsk(const QString& mode, const QString& text, int centerHz) {
  if (qpskBaud(mode) > 0.0)
    return planQpsk(mode, text, centerHz);
  PskPlan plan;
  const double baud = pskBaud(mode);
  if (baud <= 0.0 || text.isEmpty())
    return plan;
  const int rate = 48000;
  const int symbol = std::max(8, int(std::lround(double(rate) / baud)));
  const int center = std::clamp(centerHz, 200, 3000);
  // The painter's symbol 0 is the phase reference, not a varicode bit.
  // Each '0' is a reversal (fldigi tx_symbol(0)). Each '1' holds phase
  // (tx_symbol(2)), which is the unmodulated carrier the far end uses to
  // drop DCD. Preamble and postamble are both dcdbits long.
  const int dcd = bpskDcdBits(baud);
  QString bits(dcd, QLatin1Char('0'));
  plan.echoAt.assign(static_cast<size_t>(text.size()), -1);
  for (int i = 0; i < text.size(); ++i) {
    const ushort u = text.at(i).unicode();
    if (u > 255)
      continue;
    plan.echoAt[static_cast<size_t>(i)] = (bits.size() + 1) * symbol;
    bits.append(QLatin1String(codeFor(static_cast<unsigned char>(u))));
    bits.append(QLatin1String("00"));
  }
  bits.append(QString(dcd, QLatin1Char('1')));

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

QString pskDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                  int centerHz, bool squelchOn, int squelchLevel, bool afcOn) {
  PskStream rx;
  if (!rx.configure(mode, sampleRateHz, centerHz))
    return {};
  rx.setSquelch(squelchOn, squelchLevel);
  rx.afcOn = afcOn;
  return rx.push(samples, count);
}

struct PskEngine::Private {
  bool armed = false;
  QString mode = QStringLiteral("PSK31");
  int center = 1500;
  int rate = 0;
  PskStream rx;

  void retune(int sampleRateHz) {
    if (rate == sampleRateHz && rx.ready && rx.center == center)
      return;
    rate = sampleRateHz;
    const bool on = rx.squelchOn;
    const int level = rx.squelch;
    const bool afc = rx.afcOn;
    rx.configure(mode, sampleRateHz, center);
    rx.setSquelch(on, level);
    rx.afcOn = afc;
  }

  void reset() {
    const bool on = rx.squelchOn;
    const int level = rx.squelch;
    const bool afc = rx.afcOn;
    if (rate >= 1000)
      rx.configure(mode, rate, center);
    else
      rx.resetState();
    rx.setSquelch(on, level);
    rx.afcOn = afc;
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
  hz = std::clamp(hz, 200, 3000);
  if (hz == d_->center && std::abs(d_->rx.tuneHz - double(hz)) < 1.0)
    return;
  d_->center = hz;
  d_->rx.setCenter(d_->center);
}

void PskEngine::setSquelch(bool on, int level) {
  d_->rx.setSquelch(on, level);
}

void PskEngine::setAfc(bool on) {
  d_->rx.afcOn = on;
}

void PskEngine::clear() {
  d_->reset();
}

void PskEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  const double baud = pskBaud(d_->mode) > 0.0 ? pskBaud(d_->mode) : qpskBaud(d_->mode);
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000 || baud <= 0.0)
    return;
  d_->retune(sampleRateHz);
  const QString text = d_->rx.push(samples, count);
  if (!text.isEmpty())
    emit textReady(text);
  if (d_->rx.trackHz >= 0) {
    const int hz = d_->rx.trackHz;
    d_->rx.trackHz = -1;
    emit toneTracked(hz);
  }
}
