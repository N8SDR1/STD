// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Varicode table from fldigi pskvaricode.cxx (Dave Freese, W1HKJ, GPL-3,
// adapted from gmfsk). A 0 reverses the phase. Characters are separated
// by 00, which never occurs inside a code.

#include "PskModem.h"

#include "DigitalMode.h"
#include "LiveTx.h"

#include <algorithm>
#include <climits>
#include <memory>
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

// Fldigi tx_carriers, one carrier: each symbol moves from the previous
// constellation point to the next with shapeA = 0.5*cos(pi*k/symbollen)+0.5.
// BPSK symbol 0 reverses (180 degrees) and symbol 2 holds (0 degrees). The
// last symbol falls to zero so the carrier does not stop abruptly.
class LivePsk final : public LiveTextTx {
public:
  LivePsk(double baud, bool qpsk, int centerHz)
      : qpsk_(qpsk),
        symbol_(std::max(8, int(std::lround(48000.0 / baud)))),
        dcd_(qpsk ? qpskPreamble(baud) : bpskDcdBits(baud)),
        step_(2.0 * 3.141592653589793 * double(std::clamp(centerHz, 200, 3000)) / 48000.0) {}

  void begin(std::vector<float>* out) override {
    // Fldigi starts on the reference point at full level (prevsymbol = 1);
    // a ramp up from zero here cost the QPSK receiver its first character.
    // QPSK sends raw symbol 0 here, not through the encoder, as Fldigi does.
    for (int i = 0; i < dcd_; ++i)
      symbol(0, out);
  }

  bool put(QChar ch, std::vector<float>* out) override {
    const ushort u = ch.unicode();
    if (u > 255)
      return false;
    for (const char* p = codeFor(static_cast<unsigned char>(u)); *p; ++p)
      bit(*p == '1' ? 1 : 0, out);
    bit(0, out);
    bit(0, out);
    return true;
  }

  // Fldigi tx_process GET_TX_CHAR_NODATA: tx_bit(0).
  void idle(std::vector<float>* out) override { bit(0, out); }

  void end(std::vector<float>* out) override {
    if (qpsk_) {
      // K=5 traceback is 60 symbols. Extra encoded zeros let a finite
      // buffer release the last character.
      const int flush = std::max(dcd_, 76);
      for (int i = 0; i < flush; ++i)
        symbol(enc_.encode(0), out);
    } else {
      // Unmodulated carrier: the far end drops DCD on it.
      for (int i = 0; i < dcd_; ++i)
        symbol(2, out);
    }
    move(i_, q_, 0.0, 0.0, out);
  }

private:
  void bit(int b, std::vector<float>* out) {
    if (qpsk_)
      symbol(enc_.encode(b), out);
    else
      symbol(b ? 2 : 0, out);
  }

  void symbol(int sym, std::vector<float>* out) {
    double ni = i_;
    double nq = q_;
    if (qpsk_) {
      rotateQpsk(sym, &ni, &nq);
    } else if (sym == 0) {
      ni = -ni;
      nq = -nq;
    }
    move(i_, q_, ni, nq, out);
    i_ = ni;
    q_ = nq;
  }

  void move(double fromI, double fromQ, double toI, double toQ, std::vector<float>* out) {
    out->reserve(out->size() + static_cast<size_t>(symbol_));
    for (int k = 0; k < symbol_; ++k) {
      const double shapeA = 0.5 * std::cos(3.141592653589793 * double(k) / double(symbol_)) + 0.5;
      const double shapeB = 1.0 - shapeA;
      const double ii = shapeA * fromI + shapeB * toI;
      const double qq = shapeA * fromQ + shapeB * toQ;
      out->push_back(float(0.6 * (ii * std::cos(carrier_) + qq * std::sin(carrier_))));
      carrier_ += step_;
      if (carrier_ > 6.283185307179586)
        carrier_ -= 6.283185307179586;
    }
  }

  bool qpsk_;
  int symbol_;
  int dcd_;
  double step_;
  double carrier_ = 0.0;
  double i_ = 1.0;
  double q_ = 0.0;
  QpskEncoder enc_;
};

#include "PskStream.inc"

}  // namespace

std::unique_ptr<LiveTextTx> makeLivePsk(const QString& mode, int centerHz) {
  const bool qpsk = qpskBaud(mode) > 0.0;
  const double baud = qpsk ? qpskBaud(mode) : pskBaud(mode);
  if (baud <= 0.0)
    return nullptr;
  return std::make_unique<LivePsk>(baud, qpsk, centerHz);
}

PskPlan planPsk(const QString& mode, const QString& text, int centerHz) {
  PskPlan plan;
  auto tx = makeLivePsk(mode, centerHz);
  if (!tx || text.isEmpty())
    return plan;
  tx->begin(&plan.samples);
  plan.echoAt.assign(static_cast<size_t>(text.size()), -1);
  for (int i = 0; i < text.size(); ++i) {
    const int at = int(plan.samples.size());
    if (tx->put(text.at(i), &plan.samples))
      plan.echoAt[static_cast<size_t>(i)] = at;
  }
  tx->end(&plan.samples);
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
