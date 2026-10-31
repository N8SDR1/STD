// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thor rewritten from fldigi (Dave Freese W1HKJ, GPL-3). The slow speeds
// use the NASA K=7 polynomials 0x6d and 0x4f, the IZ8BLY varicode, a 4 by 10
// diagonal interleaver, and 18-tone incremental keying. Thor 25x4, 50x1,
// 50x2, and 100 use the IEEE K=15 polynomials and a depth-50 interleaver.
// The fldigi sources are not compiled into STD.

#include "ThorModem.h"

#include "mfsk/MfskModem.h"
#include "modem/ToneClock.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr int kTones = 18;
constexpr int kPath = 256;
constexpr int kBits = 4;
constexpr int kNasaPoly1 = 0x6d;
constexpr int kNasaPoly2 = 0x4f;
// fldigi thor.h IEEE polynomials, octal.
constexpr int kIeeePoly1 = 044735;
constexpr int kIeeePoly2 = 063057;

struct Spec {
  const char* name;
  double baud;
  int doubled;
  int depth;
  int flushChars;
  int constraint;
  int poly1;
  int poly2;
  int traceback;
};

const Spec kSlow{nullptr, 0, 0, 10, 4, 7, kNasaPoly1, kNasaPoly2, 45};
const Spec kFast{nullptr, 0, 0, 50, 40, 15, kIeeePoly1, kIeeePoly2, 15 * 12};

Spec modeSpec(const char* name, double baud, int doubled, const Spec& code, int flushChars) {
  Spec spec = code;
  spec.name = name;
  spec.baud = baud;
  spec.doubled = doubled;
  spec.flushChars = flushChars;
  return spec;
}

// fldigi thor.cxx flushlength. Thor 11 and 16 need 8, Thor 22 needs 16.
const Spec kModes[] = {
    modeSpec("Thor 4", 8000.0 / 2048.0, 2, kSlow, 4),
    modeSpec("Thor 5", 11025.0 / 2048.0, 2, kSlow, 4),
    modeSpec("Thor 8", 8000.0 / 1024.0, 2, kSlow, 4),
    modeSpec("Thor 11", 11025.0 / 1024.0, 1, kSlow, 8),
    modeSpec("Thor 16", 8000.0 / 512.0, 1, kSlow, 8),
    modeSpec("Thor 22", 11025.0 / 512.0, 1, kSlow, 16),
    modeSpec("Thor 25x4", 8000.0 / 320.0, 4, kFast, 40),
    modeSpec("Thor 50x1", 8000.0 / 160.0, 1, kFast, 40),
    modeSpec("Thor 50x2", 8000.0 / 160.0, 2, kFast, 40),
    modeSpec("Thor 100", 8000.0 / 80.0, 1, kFast, 40),
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
  ConvEncoder(int constraint, int poly1, int poly2) : poly1_(poly1), poly2_(poly2) { init(constraint); }

  void init(int constraint) {
    const int size = 1 << constraint;
    output_.assign(static_cast<size_t>(size), 0);
    for (int i = 0; i < size; ++i)
      output_[static_cast<size_t>(i)] = parity(unsigned(poly1_ & i)) | (parity(unsigned(poly2_ & i)) << 1);
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
  int poly1_ = 0;
  int poly2_ = 0;
};

class Interleave {
public:
  Interleave(bool reverse, int depth) : reverse_(reverse), depth_(depth) {
    table_.assign(static_cast<size_t>(kBits * kBits * depth_), reverse ? 128 : 0);
  }

  unsigned char& at(int layer, int row, int col) {
    return table_[static_cast<size_t>((kBits * kBits * layer) + (kBits * row) + col)];
  }

  void symbols(unsigned char* psyms) {
    for (int layer = 0; layer < depth_; ++layer) {
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
  int depth_ = 10;
  std::vector<unsigned char> table_;
};

class Viterbi {
public:
  Viterbi(int constraint, int poly1, int poly2, int traceback)
      : constraint_(constraint), poly1_(poly1), poly2_(poly2), traceback_(traceback) {
    init();
  }

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
    nstates_ = 1 << (constraint_ - 1);
    const int size = 1 << constraint_;
    output_.assign(static_cast<size_t>(size), 0);
    for (int i = 0; i < size; ++i)
      output_[static_cast<size_t>(i)] = parity(unsigned(poly1_ & i)) | (parity(unsigned(poly2_ & i)) << 1);
    metrics_.assign(static_cast<size_t>(kPath * nstates_), 0);
    history_.assign(static_cast<size_t>(kPath * nstates_), 0);
    for (int i = 0; i < 256; ++i) {
      mettab_[0][i] = 128 - i;
      mettab_[1][i] = i - 128;
    }
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
  int constraint_ = 7;
  int poly1_ = 0;
  int poly2_ = 0;
  int traceback_ = 45;
  unsigned ptr_ = 0;
  std::vector<int> output_;
  std::vector<int> metrics_;
  std::vector<int> history_;
  int mettab_[2][256] = {};
};

class ToneTx {
public:
  explicit ToneTx(const Spec& spec)
      : spec_(spec), enc_(spec.constraint, spec.poly1, spec.poly2), inlv_(false, spec.depth) {}

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
    for (int i = 0; i < spec_.flushChars; ++i)
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

  Spec spec_;
  ConvEncoder enc_;
  Interleave inlv_;
  unsigned bitshreg_ = 0;
  int bitstate_ = 0;
  int prev_ = 0;
  std::vector<int> symbols_;
};

class ToneRx {
public:
  explicit ToneRx(const Spec& spec)
      : inlv_(true, spec.depth), dec_(spec.constraint, spec.poly1, spec.poly2, spec.traceback) {}

  void setPrevBin(int bin) { prev_ = bin; }

  void setSquelch(bool on, int level) {
    squelchOn_ = on;
    squelch_ = std::clamp(level, 0, 100);
  }

  // A negative tone is a puncture: no carrier, so the interleaver and the
  // K=7 traceback keep moving without a new nibble. fldigi does this on a
  // flat bin. A finished buffer has no bins after the last tone, and the
  // last characters sit in that delay until something clocks them out.
  // bin is a pickIfk index, or negative to puncture. One tone step is kIfkPaths bins.
  QString pushTone(int bin) {
    if (bin < 0)
      return pushSoft(nullptr);
    const double diff = double(bin - prev_) / double(kIfkPaths);
    prev_ = bin;
    const int raw = int(std::floor(diff + 0.5));
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
    met_ = ((double(metric) - met_) / 50.0) + met_;
    const double shown = std::clamp((met_ - 60.0) * 0.5, 0.0, 100.0);
    const QString text = takeBit(bit);
    if (squelchOn_ && shown < double(squelch_))
      return {};
    return text;
  }

  QString takeBit(int bit) {
    datashreg_ = (datashreg_ << 1) | (bit ? 1u : 0u);
    if ((datashreg_ & 7u) != 1u)
      return {};
    const int ch = mfskVaridec(datashreg_ >> 1);
    datashreg_ = 1;
    return latin1Rx(ch);
  }

  Interleave inlv_;
  Viterbi dec_;
  unsigned char pair_[2] = {};
  int symcounter_ = 0;
  unsigned datashreg_ = 1;
  double met_ = 0.0;
  bool squelchOn_ = false;
  int squelch_ = 0;
  int prev_ = ifkToneBin(0, kTones);
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

QString textFromAudio(const Spec& spec, const float* samples, int count, int sampleRateHz, int centerHz) {
  const int separ = std::max(8, int(std::lround(double(sampleRateHz) / spec.baud)));
  const int symbols = count / separ;
  ToneRx rx(spec);
  QString text;
  const double low = lowHz(spec, centerHz);
  const double spacing = spacingHz(spec);
  for (int s = 0; s < symbols; ++s) {
    const IfkPick ifk = pickIfk(samples, count, s * separ, separ, low, spacing, kTones, sampleRateHz);
    text += rx.pushTone(ifk.bin);
  }
  // The K=7 traceback is 45 and the 4 by 10 interleaver holds the tail.
  // K=15 uses a traceback of 180 and a depth of 50. fldigi keeps
  // demodulating after the tones. Punctures release that delay.
  const int extra = spec.constraint > 7 ? 220 : 48;
  for (int i = 0; i < extra; ++i)
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
  ToneTx tx(*spec);
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
  // fldigi THOR_RISETIME default is 4 ms, raised cosine on each symbol edge.
  const int rise = std::max(1, int(std::lround(48000.0 * 4.0 / 1000.0)));
  const int edge = rise * 2 < separ ? rise : std::max(1, separ / 4);
  for (int symbol : tx.symbols()) {
    const double step = twoPi * (low + double(symbol) * spacing) / 48000.0;
    const int num2 = separ - edge;
    for (int i = 0; i < separ; ++i) {
      double coeff = 1.0;
      if (i < edge)
        coeff = 0.5 * (1.0 - std::cos(3.14159265358979323846 * double(i) / double(edge)));
      else if (i > num2)
        coeff = 0.5 * (1.0 - std::cos(3.14159265358979323846 * double(separ - i) / double(edge)));
      plan.samples.push_back(float(0.6 * coeff * std::cos(phase)));
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
  Rate8k decim;
  SymbolClock clock;

  void reset() {
    locked = false;
    lock = 0;
    next = 0;
    since = 0;
    pending.clear();
    rx.reset();
    decim.reset();
    clock.resetTrack();
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

void ThorEngine::setAfc(bool on) {
  d_->clock.setAfcEnabled(on);
}

void ThorEngine::clear() {
  d_->reset();
}

void ThorEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000 || thorBandwidthHz(d_->mode) < 1)
    return;
  const Spec* spec = specFor(d_->mode);
  if (!spec)
    return;
  d_->decim.push(samples, count, sampleRateHz, &d_->pending);
  constexpr int rate = 8000;
  const int separ = std::max(8, int(std::lround(double(rate) / spec->baud)));
  const double spacing = spacingHz(*spec);
  d_->since += count;
  if (int(d_->pending.size()) < separ * 12)
    return;
  double magnitude[kTones];
  const auto lowNow = [&] { return lowHz(*spec, d_->center) + d_->clock.afcHz(); };
  if (!d_->locked) {
    if (d_->since < sampleRateHz / 4)
      return;
    d_->since = 0;
    const double low = lowNow();
    const int pending = int(d_->pending.size());
    const SymbolLock symbolLock = lockSymbolPhase(separ, pending, [&](int pos) {
      toneMagnitude(d_->pending.data(), pending, pos, separ, low, spacing, rate, 1, magnitude);
      double peak = 0.0;
      double second = 0.0;
      for (int tone = 0; tone < kTones; ++tone) {
        if (magnitude[tone] > peak) {
          second = peak;
          peak = magnitude[tone];
        } else if (magnitude[tone] > second)
          second = magnitude[tone];
      }
      if (peak < 1.0)
        return -1.0;
      return (peak - second) / peak;
    });
    if (symbolLock.rank < 0.45)
      return;
    d_->locked = true;
    d_->lock = symbolLock.at;
    d_->next = 0;
    d_->rx = std::make_unique<ToneRx>(*spec);
    if (symbolLock.at >= separ) {
      toneMagnitude(d_->pending.data(), pending, symbolLock.at - separ, separ, low, spacing, rate, 1, magnitude);
      const IfkPick seeded =
          pickIfk(d_->pending.data(), pending, symbolLock.at - separ, separ, low, spacing, kTones, rate);
      d_->rx->setPrevBin(seeded.bin);
    }
  }
  if (!d_->rx)
    return;
  d_->rx->setSquelch(d_->squelchOn, d_->squelch);
  const int probe = std::max(1, separ / 8);
  const int size = int(d_->pending.size());
  while (d_->lock + (d_->next + 1) * separ <= size) {
    int at = d_->lock + d_->next * separ;
    const auto peakOnly = [&](int pos) {
      if (pos < 0 || pos + separ > size)
        return 0.0;
      toneMagnitude(d_->pending.data(), size, pos, separ, lowNow(), spacing, rate, 1, magnitude);
      double peak = 0.0;
      for (int tone = 0; tone < kTones; ++tone)
        peak = std::max(peak, magnitude[tone]);
      return peak;
    };
    const double early = peakOnly(at - probe);
    const double late = peakOnly(at + probe);
    toneMagnitude(d_->pending.data(), size, at, separ, lowNow(), spacing, rate, 1, magnitude);
    BinPick bin = pickBin(magnitude, kTones);
    const bool strong = binStrong(bin, kTones);
    const bool transition = d_->clock.tone() < 0 || bin.index != d_->clock.tone();
    const int shift = d_->clock.nudge(early, bin.peak, late, probe, strong, transition);
    if (shift != 0 && at + shift >= 0 && at + shift + separ <= size) {
      at += shift;
      d_->lock += shift;
      toneMagnitude(d_->pending.data(), size, at, separ, lowNow(), spacing, rate, 1, magnitude);
      bin = pickBin(magnitude, kTones);
    }
    const bool repeat = d_->clock.tone() >= 0 && bin.index == d_->clock.tone();
    if (repeat && binStrong(bin, kTones)) {
      const int half = std::max(4, separ / 2);
      const double freq = lowNow() + double(bin.index) * spacing;
      double si0 = 0.0;
      double sq0 = 0.0;
      double si1 = 0.0;
      double sq1 = 0.0;
      toneIq(d_->pending.data(), size, at, half, freq, rate, &si0, &sq0);
      toneIq(d_->pending.data(), size, at + half, half, freq, rate, &si1, &sq1);
      d_->clock.observeFreq(si0, sq0, si1, sq1, half, rate, spacing, true, true);
    }
    d_->clock.setTone(bin.index);
    const bool flat = bin.sum < 1e-6 || bin.peak * double(kTones) < bin.sum * 1.2;
    const IfkPick ifk = pickIfk(d_->pending.data(), size, at, separ, lowNow(), spacing, kTones, rate);
    const QString text = d_->rx->pushTone(flat ? -1 : ifk.bin);
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
