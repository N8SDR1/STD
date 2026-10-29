// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thor rewritten from fldigi (Dave Freese W1HKJ, GPL-3). The K=7 speeds
// use the NASA polynomials 0x6d and 0x4f, the IZ8BLY varicode, a 4 by 10
// diagonal interleaver, and 18-tone incremental keying. The K=15 speeds
// are a different code and are not modulated here. The fldigi sources
// are not compiled into STD.

#include "ThorModem.h"

#include "mfsk/MfskModem.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr int kTones = 18;
constexpr int kConstraint = 7;
constexpr int kPoly1 = 0x6d;
constexpr int kPoly2 = 0x4f;
constexpr int kPath = 256;
constexpr int kBits = 4;
constexpr int kDepth = 10;

struct Spec {
  const char* name;
  double baud;
  int doubled;
};

const Spec kModes[] = {
    {"Thor 4", 8000.0 / 2048.0, 2},  {"Thor 5", 11025.0 / 2048.0, 2},
    {"Thor 8", 8000.0 / 1024.0, 2},  {"Thor 11", 11025.0 / 1024.0, 1},
    {"Thor 16", 8000.0 / 512.0, 1},  {"Thor 22", 11025.0 / 512.0, 1},
};

const Spec* specFor(const QString& mode) {
  for (const Spec& spec : kModes) {
    if (mode == QLatin1String(spec.name))
      return &spec;
  }
  return nullptr;
}

double spacingHz(const Spec& spec) {
  return spec.baud * double(spec.doubled);
}

int parity(unsigned long word) {
  word ^= word >> 16;
  word ^= word >> 8;
  word ^= word >> 4;
  word ^= word >> 2;
  word ^= word >> 1;
  return int(word & 1u);
}

class ConvEncoder {
public:
  ConvEncoder() { init(); }

  void init() {
    const int size = 1 << kConstraint;
    output_.assign(static_cast<size_t>(size), 0);
    for (int i = 0; i < size; ++i)
      output_[static_cast<size_t>(i)] = parity(unsigned(kPoly1 & i)) | (parity(unsigned(kPoly2 & i)) << 1);
    shreg_ = 0;
    mask_ = unsigned(size - 1);
  }

  int encode(int bit) {
    shreg_ = (shreg_ << 1) | (bit ? 1u : 0u);
    return output_[shreg_ & mask_];
  }

private:
  std::vector<int> output_;
  unsigned shreg_ = 0;
  unsigned mask_ = 0;
};

class Interleave {
public:
  Interleave(bool reverse) : reverse_(reverse) {
    table_.assign(static_cast<size_t>(kBits * kBits * kDepth), reverse ? 128 : 0);
  }

  unsigned char& at(int layer, int row, int col) {
    return table_[static_cast<size_t>((kBits * kBits * layer) + (kBits * row) + col)];
  }

  void symbols(unsigned char* psyms) {
    for (int layer = 0; layer < kDepth; ++layer) {
      for (int row = 0; row < kBits; ++row) {
        for (int col = 0; col < kBits - 1; ++col)
          at(layer, row, col) = at(layer, row, col + 1);
      }
      for (int row = 0; row < kBits; ++row)
        at(layer, row, kBits - 1) = psyms[row];
      for (int row = 0; row < kBits; ++row)
        psyms[row] = reverse_ ? at(layer, row, row) : at(layer, row, kBits - row - 1);
    }
  }

  void bits(unsigned* pbits) {
    unsigned char syms[kBits];
    for (int i = 0; i < kBits; ++i)
      syms[i] = static_cast<unsigned char>((*pbits >> (kBits - i - 1)) & 1u);
    symbols(syms);
    *pbits = 0;
    for (int i = 0; i < kBits; ++i)
      *pbits = (*pbits << 1) | syms[i];
  }

private:
  bool reverse_ = false;
  std::vector<unsigned char> table_;
};

class Viterbi {
public:
  Viterbi() { init(); }

  int decode(unsigned char sym0, unsigned char sym1, int* metric) {
    const unsigned curr = ptr_;
    const unsigned prev = (curr - 1u) % unsigned(kPath);
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
    ptr_ = (ptr_ + 1u) % unsigned(kPath);
    if ((ptr_ % 1u) == 0)
      return traceback(metric);
    return -1;
  }

private:
  int& cell(std::vector<int>& data, unsigned row, int state) {
    return data[static_cast<size_t>(row) * static_cast<size_t>(nstates_) + static_cast<size_t>(state)];
  }

  void init() {
    nstates_ = 1 << (kConstraint - 1);
    const int size = 1 << kConstraint;
    output_.assign(static_cast<size_t>(size), 0);
    for (int i = 0; i < size; ++i)
      output_[static_cast<size_t>(i)] = parity(unsigned(kPoly1 & i)) | (parity(unsigned(kPoly2 & i)) << 1);
    metrics_.assign(static_cast<size_t>(kPath * nstates_), 0);
    history_.assign(static_cast<size_t>(kPath * nstates_), 0);
    for (int i = 0; i < 256; ++i) {
      mettab_[0][i] = 128 - i;
      mettab_[1][i] = i - 128;
    }
    traceback_ = 45;
    ptr_ = 0;
  }

  int traceback(int* metric) {
    unsigned p = (ptr_ - 1u) % unsigned(kPath);
    int bestMetric = INT_MIN;
    int bestState = 0;
    for (int i = 0; i < nstates_; ++i) {
      const int value = cell(metrics_, p, i);
      if (value > bestMetric) {
        bestMetric = value;
        bestState = i;
      }
    }
    std::vector<int> sequence(kPath, 0);
    sequence[p] = bestState;
    for (int i = 0; i < traceback_; ++i) {
      const unsigned prev = (p - 1u) % unsigned(kPath);
      sequence[prev] = cell(history_, p, sequence[p]);
      p = prev;
    }
    int base = 0;
    if (metric)
      base = cell(metrics_, p, sequence[p]);
    const unsigned bits = unsigned(sequence[p] & 1);
    p = (p + 1u) % unsigned(kPath);
    if (metric)
      *metric = cell(metrics_, p, sequence[p]) - base;
    return int(bits);
  }

  int nstates_ = 0;
  int traceback_ = 45;
  unsigned ptr_ = 0;
  std::vector<int> output_;
  std::vector<int> metrics_;
  std::vector<int> history_;
  int mettab_[2][256] = {};
};

class ToneTx {
public:
  void sendRaw(int nibble) {
    const int tone = (prev_ + 2 + (nibble & 15)) % kTones;
    prev_ = tone;
    symbols_.push_back(tone);
  }

  void sendChar(int ch) {
    const char* code = mfskVaricode(ch);
    while (*code)
      sendBit(*code++ - '0');
  }

  void clearBits() {
    const int data = enc_.encode(0);
    for (int k = 0; k < 1400; ++k) {
      for (int i = 0; i < 2; ++i) {
        bitshreg_ = (bitshreg_ << 1) | unsigned((data >> i) & 1);
        ++bitstate_;
        if (bitstate_ == kBits) {
          inlv_.bits(&bitshreg_);
          bitstate_ = 0;
          bitshreg_ = 0;
        }
      }
    }
  }

  void flushTail() {
    for (int i = 0; i < 4; ++i)
      sendChar(0);
    bitstate_ = 0;
  }

  const std::vector<int>& symbols() const { return symbols_; }

private:
  void sendBit(int bit) {
    const int data = enc_.encode(bit);
    for (int i = 0; i < 2; ++i) {
      bitshreg_ = (bitshreg_ << 1) | unsigned((data >> i) & 1);
      ++bitstate_;
      if (bitstate_ == kBits) {
        inlv_.bits(&bitshreg_);
        sendRaw(int(bitshreg_ & 15u));
        bitstate_ = 0;
        bitshreg_ = 0;
      }
    }
  }

  ConvEncoder enc_;
  Interleave inlv_{false};
  unsigned bitshreg_ = 0;
  int bitstate_ = 0;
  int prev_ = 0;
  std::vector<int> symbols_;
};

class ToneRx {
public:
  void setPrev(int tone) { prev_ = tone; }

  // A negative tone is a puncture: no carrier, so the interleaver and the
  // K=7 traceback keep moving without a new nibble. fldigi does this on a
  // flat bin. A finished buffer has no bins after the last tone, and the
  // last characters sit in that delay until something clocks them out.
  QString pushTone(int tone) {
    if (tone < 0)
      return pushSoft(nullptr);
    const int diff = tone - prev_;
    prev_ = tone;
    const int raw = int(std::floor(double(diff) + 0.5));
    if (preambleDetect(raw)) {
      flushRx();
      return {};
    }
    int nibble = raw - 2;
    if (nibble < 0)
      nibble += kTones;
    unsigned char hard[kBits];
    if (nibble < 0 || nibble > 17) {
      for (int i = 0; i < kBits; ++i)
        hard[i] = 128;
    } else {
      int n = nibble;
      hard[3] = (n & 1) ? 255 : 0;
      n /= 2;
      hard[2] = (n & 1) ? 255 : 0;
      n /= 2;
      hard[1] = (n & 1) ? 255 : 0;
      n /= 2;
      hard[0] = (n & 1) ? 255 : 0;
    }
    return pushSoft(hard);
  }

private:
  // fldigi preambledetect: a run of +2 tone steps and one -16 step is the
  // 16-symbol idle that is not convolutional. Clocking those into the
  // deinterleaver shifts it off the code.
  bool preambleDetect(int c) {
    if (twoCount_ > 14)
      twoCount_ = 0;
    if (c == -16 && twoCount_ > 2)
      neg16_ = true;
    else if (c != 2)
      neg16_ = false;
    else
      ++twoCount_;
    if (c != -16 && c != 2 && twoCount_ > 1)
      twoCount_ -= 2;
    if (twoCount_ > 4 && neg16_) {
      if (++preambleCheck_ > 4)
        return true;
    } else {
      preambleCheck_ = 0;
    }
    return false;
  }

  void flushRx() {
    unsigned char buf[kBits];
    for (int i = 0; i < kBits; ++i)
      buf[i] = 128;
    for (int i = 0; i < 90; ++i)
      inlv_.symbols(buf);
    for (int i = 0; i < 128; ++i)
      dec_.decode(128, 128, nullptr);
  }

  QString pushSoft(const unsigned char* hard) {
    unsigned char syms[kBits];
    for (int i = 0; i < kBits; ++i)
      syms[i] = hard ? hard[i] : 128;
    inlv_.symbols(syms);
    QString text;
    for (int i = 0; i < kBits; ++i)
      text += takeSymbol(syms[i]);
    return text;
  }

  QString takeSymbol(unsigned char symbol) {
    pair_[0] = pair_[1];
    pair_[1] = symbol;
    symcounter_ = symcounter_ ? 0 : 1;
    if (symcounter_)
      return {};
    int metric = 0;
    const int bit = dec_.decode(pair_[0], pair_[1], &metric);
    if (bit < 0)
      return {};
    return takeBit(bit);
  }

  QString takeBit(int bit) {
    datashreg_ = (datashreg_ << 1) | (bit ? 1u : 0u);
    if ((datashreg_ & 7u) != 1u)
      return {};
    const int ch = mfskVaridec(datashreg_ >> 1);
    datashreg_ = 1;
    if (ch < 32 || ch >= 127)
      return {};
    return QString(QChar::fromLatin1(char(ch)));
  }

  Interleave inlv_{true};
  Viterbi dec_;
  unsigned char pair_[2] = {};
  int symcounter_ = 0;
  unsigned datashreg_ = 1;
  int prev_ = 0;
  int twoCount_ = 0;
  int preambleCheck_ = 0;
  bool neg16_ = false;
};

void toneMagnitude(const float* samples, int count, int begin, int length, double lowHz, double spacing,
                   int sampleRateHz, int stride, double* magnitude) {
  for (int tone = 0; tone < kTones; ++tone)
    magnitude[tone] = 0.0;
  if (!samples || length < 8 || begin < 0 || begin + length > count)
    return;
  const int stepSamples = std::max(1, stride);
  const double stepBase = 2.0 * 3.14159265358979323846 / double(sampleRateHz);
  for (int tone = 0; tone < kTones; ++tone) {
    const double step = stepBase * (lowHz + double(tone) * spacing) * double(stepSamples);
    double phase = 0.0;
    double si = 0.0;
    double sq = 0.0;
    for (int i = 0; i < length; i += stepSamples) {
      const double sample = samples[begin + i];
      si += sample * std::sin(phase);
      sq += sample * std::cos(phase);
      phase += step;
    }
    magnitude[tone] = std::sqrt(si * si + sq * sq);
  }
}

double lowHz(const Spec& spec, int centerHz) {
  return double(centerHz) - double(kTones - 1) * spacingHz(spec) / 2.0;
}

int peakTone(const double* magnitude) {
  int best = 0;
  for (int tone = 1; tone < kTones; ++tone) {
    if (magnitude[tone] > magnitude[best])
      best = tone;
  }
  return best;
}

QString textFromAudio(const Spec& spec, const float* samples, int count, int sampleRateHz, int centerHz) {
  const int separ = std::max(8, int(std::lround(double(sampleRateHz) / spec.baud)));
  const int symbols = count / separ;
  ToneRx rx;
  QString text;
  double magnitude[kTones];
  const double low = lowHz(spec, centerHz);
  const double spacing = spacingHz(spec);
  for (int s = 0; s < symbols; ++s) {
    toneMagnitude(samples, count, s * separ, separ, low, spacing, sampleRateHz, 1, magnitude);
    text += rx.pushTone(peakTone(magnitude));
  }
  // Traceback is 45 and the 4 by 10 interleaver holds the tail. fldigi
  // keeps demodulating after the tones. Forty-eight punctures release it.
  for (int i = 0; i < 48; ++i)
    text += rx.pushTone(-1);
  return text;
}

}  // namespace

int thorBandwidthHz(const QString& mode) {
  const Spec* spec = specFor(mode);
  if (!spec)
    return 0;
  return std::max(1, int(std::lround(double(kTones) * spacingHz(*spec))));
}

ThorPlan planThor(const QString& mode, const QString& text, int centerHz) {
  ThorPlan plan;
  const Spec* spec = specFor(mode);
  if (!spec || text.isEmpty())
    return plan;
  const int separ = std::max(8, int(std::lround(48000.0 / spec->baud)));
  ToneTx tx;
  tx.clearBits();
  for (int i = 0; i < 16; ++i)
    tx.sendRaw(0);
  tx.sendChar(0);
  tx.sendChar('\r');
  tx.sendChar(2);
  tx.sendChar('\r');
  const QByteArray latin = text.toLatin1();
  plan.echoAt.assign(static_cast<size_t>(latin.size()), 0);
  for (int i = 0; i < latin.size(); ++i) {
    plan.echoAt[static_cast<size_t>(i)] = int(tx.symbols().size()) * separ;
    tx.sendChar(static_cast<unsigned char>(latin.at(i)));
  }
  tx.sendChar('\r');
  tx.sendChar(4);
  tx.sendChar('\r');
  tx.flushTail();
  const double low = lowHz(*spec, std::clamp(centerHz, 200, 3000));
  const double twoPi = 2.0 * 3.14159265358979323846;
  const double spacing = spacingHz(*spec);
  double phase = 0.0;
  plan.samples.reserve(tx.symbols().size() * static_cast<size_t>(separ));
  for (int symbol : tx.symbols()) {
    const double step = twoPi * (low + double(symbol) * spacing) / 48000.0;
    for (int i = 0; i < separ; ++i) {
      plan.samples.push_back(float(0.6 * std::cos(phase)));
      phase -= step;
      if (phase < 0.0)
        phase += twoPi;
    }
  }
  return plan;
}

QString thorDecode(const QString& mode, const float* samples, int count, int sampleRateHz, int centerHz) {
  const Spec* spec = specFor(mode);
  if (!spec || !samples || count < 32 || sampleRateHz < 1000)
    return {};
  return textFromAudio(*spec, samples, count, sampleRateHz, centerHz);
}

struct ThorEngine::Private {
  bool armed = false;
  bool squelchOn = false;
  int squelch = 10;
  QString mode = QStringLiteral("Thor 16");
  int center = 1500;
  bool locked = false;
  int lock = 0;
  int next = 0;
  int since = 0;
  std::vector<float> pending;
  std::unique_ptr<ToneRx> rx;

  void reset() {
    locked = false;
    lock = 0;
    next = 0;
    since = 0;
    pending.clear();
    rx.reset();
  }
};

ThorEngine::ThorEngine(QObject* parent) : QObject(parent), d_(new Private) {}

ThorEngine::~ThorEngine() {
  delete d_;
}

void ThorEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  d_->reset();
}

void ThorEngine::setMode(const QString& mode) {
  if (d_->mode == mode)
    return;
  d_->mode = mode;
  d_->reset();
}

void ThorEngine::setCenterHz(int hz) {
  hz = std::clamp(hz, 200, 3000);
  if (d_->center == hz)
    return;
  d_->center = hz;
  d_->reset();
}

void ThorEngine::setSquelch(bool on, int level) {
  d_->squelchOn = on;
  d_->squelch = std::clamp(level, 0, 100);
}

void ThorEngine::clear() {
  d_->reset();
}

void ThorEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000 || thorBandwidthHz(d_->mode) < 1)
    return;
  if (d_->squelchOn && d_->squelch >= 90)
    return;
  const Spec* spec = specFor(d_->mode);
  if (!spec)
    return;
  const int separ = std::max(8, int(std::lround(double(sampleRateHz) / spec->baud)));
  d_->pending.insert(d_->pending.end(), samples, samples + count);
  d_->since += count;
  if (int(d_->pending.size()) < separ * 12)
    return;
  const double low = lowHz(*spec, d_->center);
  const double spacing = spacingHz(*spec);
  double magnitude[kTones];
  if (!d_->locked) {
    if (d_->since < separ * 4)
      return;
    d_->since = 0;
    double best = -1.0;
    int bestAt = 0;
    const int step = std::max(1, separ / 4);
    for (int off = 0; off + separ <= int(d_->pending.size()) && off < separ; off += step) {
      toneMagnitude(d_->pending.data(), int(d_->pending.size()), off, separ, low, spacing, sampleRateHz, 4,
                    magnitude);
      double peak = 0.0;
      double second = 0.0;
      for (int tone = 0; tone < kTones; ++tone) {
        if (magnitude[tone] > peak) {
          second = peak;
          peak = magnitude[tone];
        } else if (magnitude[tone] > second)
          second = magnitude[tone];
      }
      const double score = peak > 0.0 ? (peak - second) / peak : 0.0;
      if (score > best) {
        best = score;
        bestAt = off;
      }
    }
    if (best < 0.45)
      return;
    d_->locked = true;
    d_->lock = bestAt;
    d_->next = 0;
    d_->rx = std::make_unique<ToneRx>();
    if (bestAt >= separ) {
      toneMagnitude(d_->pending.data(), int(d_->pending.size()), bestAt - separ, separ, low, spacing,
                    sampleRateHz, 1, magnitude);
      d_->rx->setPrev(peakTone(magnitude));
    }
  }
  if (!d_->rx)
    return;
  while (d_->lock + (d_->next + 1) * separ <= int(d_->pending.size())) {
    toneMagnitude(d_->pending.data(), int(d_->pending.size()), d_->lock + d_->next * separ, separ, low, spacing,
                  sampleRateHz, 1, magnitude);
    double sum = 0.0;
    double peak = 0.0;
    for (int tone = 0; tone < kTones; ++tone) {
      sum += magnitude[tone];
      if (magnitude[tone] > peak)
        peak = magnitude[tone];
    }
    const bool flat = sum < 1e-6 || peak * double(kTones) < sum * 1.2;
    const QString text = d_->rx->pushTone(flat ? -1 : peakTone(magnitude));
    ++d_->next;
    if (!text.isEmpty())
      emit textReady(text);
  }
  const int keep = separ * 80;
  if (int(d_->pending.size()) > keep * 2) {
    const int drop = int(d_->pending.size()) - keep;
    d_->pending.erase(d_->pending.begin(), d_->pending.begin() + drop);
    d_->lock -= drop;
    if (d_->lock < 0) {
      const int skip = (-d_->lock + separ - 1) / separ;
      d_->next -= skip;
      d_->lock += skip * separ;
      if (d_->next < 0 || d_->lock < 0)
        d_->reset();
    }
  }
}
