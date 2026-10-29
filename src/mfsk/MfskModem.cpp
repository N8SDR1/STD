// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MFSK modem rewritten from fldigi (Dave Freese, W1HKJ, GPL-3), which
// adapted gmfsk (Tomi Manninen) and the IZ8BLY varicode. NASA K=7
// polynomials 0x6d and 0x4f, diagonal interleaver, Gray-coded tones.
// The fldigi sources are not compiled into STD.

#include "MfskModem.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr int kPath = 256;
constexpr int kConstraint = 7;
constexpr int kPoly1 = 0x6d;
constexpr int kPoly2 = 0x4f;

struct Spec {
  const char* name;
  int tones;
  int bits;
  int depth;
  int preamble;
  double baud;
};

const Spec kModes[] = {
    {"MFSK4", 32, 5, 5, 107, 8000.0 / 2048.0},
    {"MFSK8", 32, 5, 5, 107, 8000.0 / 1024.0},
    {"MFSK11", 16, 4, 10, 107, 11025.0 / 1024.0},
    {"MFSK16", 16, 4, 10, 107, 8000.0 / 512.0},
    {"MFSK22", 16, 4, 10, 107, 11025.0 / 512.0},
    {"MFSK31", 8, 3, 10, 107, 8000.0 / 256.0},
    {"MFSK32", 16, 4, 10, 107, 8000.0 / 256.0},
    {"MFSK64", 16, 4, 10, 180, 8000.0 / 128.0},
    {"MFSK128", 16, 4, 20, 214, 8000.0 / 64.0},
    {"MFSK64L", 16, 4, 400, 2500, 8000.0 / 128.0},
    {"MFSK128L", 16, 4, 800, 5000, 8000.0 / 64.0},
};

const Spec* specFor(const QString& mode) {
  for (const Spec& spec : kModes) {
    if (mode == QLatin1String(spec.name))
      return &spec;
  }
  return nullptr;
}

int grayEncode(int binary) {
  return binary ^ (binary >> 1);
}

int grayDecode(int gray) {
  unsigned value = unsigned(gray);
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;
  return int(value);
}

int parity(unsigned long word) {
  word ^= word >> 16;
  word ^= word >> 8;
  word ^= word >> 4;
  word ^= word >> 2;
  word ^= word >> 1;
  return int(word & 1u);
}

// IZ8BLY varicode, 0..255. Codes never contain the 001 tail the decoder uses.
const char* kCode[256] = {
    "11101011100", "11101100000", "11101101000", "11101101100", "11101110000", "11101110100", "11101111000", "11101111100",
    "10101000", "11110000000", "11110100000", "11110101000", "11110101100", "10101100", "11110110000", "11110110100",
    "11110111000", "11110111100", "11111000000", "11111010000", "11111010100", "11111011000", "11111011100", "11111100000",
    "11111101000", "11111101100", "11111110000", "11111110100", "11111111000", "11111111100", "100000000000", "101000000000",
    "100", "111000000", "111111100", "1011011000", "1010101000", "1010100000", "1000000000", "110111100",
    "111110100", "111110000", "1010110100", "111100000", "10100000", "111011000", "111010100", "111101000",
    "11100000", "11110000", "101000000", "101010100", "101110100", "101100000", "101101100", "110100000",
    "110000000", "110101100", "111101100", "111111000", "1011000000", "111011100", "1010111100", "111010000",
    "1010000000", "10111100", "100000000", "11010100", "11011100", "10111000", "11111000", "101010000",
    "101011000", "11000000", "110110100", "101111100", "11110100", "11101000", "11111100", "11010000",
    "11101100", "110110000", "11011000", "10110100", "10110000", "101011100", "110101000", "101101000",
    "101110000", "101111000", "110111000", "1011101000", "1011010000", "1011101100", "1011010100", "1010110000",
    "1010101100", "10100", "1100000", "111000", "110100", "1000", "1010000", "1011000",
    "110000", "11000", "10000000", "1110000", "101100", "1000000", "11100", "10000",
    "1010100", "1111000", "100000", "101000", "1100", "111100", "1101100", "1101000",
    "1110100", "1011100", "1111100", "1011011100", "1010111000", "1011100000", "1011110000", "101010000000",
    "101010100000", "101010101000", "101010101100", "101010110000", "101010110100", "101010111000", "101010111100", "101011000000",
    "101011010000", "101011010100", "101011011000", "101011011100", "101011100000", "101011101000", "101011101100", "101011110000",
    "101011110100", "101011111000", "101011111100", "101100000000", "101101000000", "101101010000", "101101010100", "101101011000",
    "101101011100", "101101100000", "101101101000", "101101101100", "101101110000", "101101110100", "101101111000", "101101111100",
    "1011110100", "1011111000", "1011111100", "1100000000", "1101000000", "1101010000", "1101010100", "1101011000",
    "1101011100", "1101100000", "1101101000", "1101101100", "1101110000", "1101110100", "1101111000", "1101111100",
    "1110000000", "1110100000", "1110101000", "1110101100", "1110110000", "1110110100", "1110111000", "1110111100",
    "1111000000", "1111010000", "1111010100", "1111011000", "1111011100", "1111100000", "1111101000", "1111101100",
    "1111110000", "1111110100", "1111111000", "1111111100", "10000000000", "10100000000", "10101000000", "10101010000",
    "10101010100", "10101011000", "10101011100", "10101100000", "10101101000", "10101101100", "10101110000", "10101110100",
    "10101111000", "10101111100", "10110000000", "10110100000", "10110101000", "10110101100", "10110110000", "10110110100",
    "10110111000", "10110111100", "10111000000", "10111010000", "10111010100", "10111011000", "10111011100", "10111100000",
    "10111101000", "10111101100", "10111110000", "10111110100", "10111111000", "10111111100", "11000000000", "11010000000",
    "11010100000", "11010101000", "11010101100", "11010110000", "11010110100", "11010111000", "11010111100", "11011000000",
    "11011010000", "11011010100", "11011011000", "11011011100", "11011100000", "11011101000", "11011101100", "11011110000",
    "11011110100", "11011111000", "11011111100", "11100000000", "11101000000", "11101010000", "11101010100", "11101011000",
};

unsigned codeBits(const char* text) {
  unsigned value = 0;
  for (const char* p = text; *p; ++p)
    value = (value << 1) | (*p == '1' ? 1u : 0u);
  return value;
}

int varidec(unsigned symbol) {
  for (int i = 0; i < 256; ++i) {
    if (symbol == codeBits(kCode[i]))
      return i;
  }
  return -1;
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
  Interleave(int size, int depth, bool reverse) : size_(size), depth_(depth), reverse_(reverse) {
    table_.assign(static_cast<size_t>(size * size * depth), reverse ? 128 : 0);
  }

  unsigned char& at(int layer, int row, int col) {
    return table_[static_cast<size_t>((size_ * size_ * layer) + (size_ * row) + col)];
  }

  void symbols(unsigned char* psyms) {
    for (int layer = 0; layer < depth_; ++layer) {
      for (int row = 0; row < size_; ++row) {
        for (int col = 0; col < size_ - 1; ++col)
          at(layer, row, col) = at(layer, row, col + 1);
      }
      for (int row = 0; row < size_; ++row)
        at(layer, row, size_ - 1) = psyms[row];
      for (int row = 0; row < size_; ++row) {
        if (!reverse_)
          psyms[row] = at(layer, row, size_ - row - 1);
        else
          psyms[row] = at(layer, row, row);
      }
    }
  }

  void bits(unsigned* pbits) {
    std::vector<unsigned char> syms(static_cast<size_t>(size_), 0);
    for (int i = 0; i < size_; ++i)
      syms[static_cast<size_t>(i)] = (*pbits >> (size_ - i - 1)) & 1u;
    symbols(syms.data());
    *pbits = 0;
    for (int i = 0; i < size_; ++i)
      *pbits = (*pbits << 1) | syms[static_cast<size_t>(i)];
  }

private:
  int size_ = 0;
  int depth_ = 0;
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
    if ((ptr_ % unsigned(chunk_)) == 0)
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
    // fldigi TRACEPAIR trace is 45, not the 12-constraint-length default.
    traceback_ = 45;
    chunk_ = 1;
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
    unsigned bits = 0;
    for (int i = 0; i < chunk_; ++i) {
      bits = (bits << 1) | unsigned(sequence[p] & 1);
      p = (p + 1u) % unsigned(kPath);
    }
    if (metric)
      *metric = cell(metrics_, p, sequence[p]) - base;
    return int(bits);
  }

  int nstates_ = 0;
  int traceback_ = 0;
  int chunk_ = 1;
  unsigned ptr_ = 0;
  std::vector<int> output_;
  std::vector<int> metrics_;
  std::vector<int> history_;
  int mettab_[2][256] = {};
};

class ToneTx {
public:
  explicit ToneTx(const Spec& spec)
      : spec_(spec), inlv_(spec.bits, spec.depth, false) {}

  void sendBit(int bit) {
    const int data = enc_.encode(bit);
    for (int i = 0; i < 2; ++i) {
      bitshreg_ = (bitshreg_ << 1) | unsigned((data >> i) & 1);
      ++bitstate_;
      if (bitstate_ == spec_.bits) {
        inlv_.bits(&bitshreg_);
        symbols_.push_back(grayEncode(int(bitshreg_ & unsigned(spec_.tones - 1))));
        bitstate_ = 0;
        bitshreg_ = 0;
      }
    }
  }

  void sendChar(int ch) {
    const char* code = kCode[(ch >= 0 && ch < 256) ? ch : 0];
    while (*code)
      sendBit(*code++ - '0');
  }

  void clearBits() {
    const int data = enc_.encode(0);
    for (int k = 0; k < spec_.preamble; ++k) {
      for (int i = 0; i < 2; ++i) {
        bitshreg_ = (bitshreg_ << 1) | unsigned((data >> i) & 1);
        ++bitstate_;
        if (bitstate_ == spec_.bits) {
          inlv_.bits(&bitshreg_);
          bitstate_ = 0;
          bitshreg_ = 0;
        }
      }
    }
  }

  void flushTx() {
    sendBit(1);
    for (int i = 0; i < spec_.preamble; ++i)
      sendBit(0);
    bitstate_ = 0;
  }

  const std::vector<int>& symbols() const { return symbols_; }

private:
  Spec spec_;
  ConvEncoder enc_;
  Interleave inlv_;
  unsigned bitshreg_ = 0;
  int bitstate_ = 0;
  std::vector<int> symbols_;
};

class ToneRx {
public:
  explicit ToneRx(const Spec& spec)
      : spec_(spec), inlv_(spec.bits, spec.depth, true), dec1_(), dec2_() {}

  QString push(const double* magnitude) {
    double sum = 0.0;
    for (int tone = 0; tone < spec_.tones; ++tone)
      sum += magnitude[tone];
    if (sum < 1e-12)
      sum = 1e-12;
    double bits[8] = {};
    for (int tone = 0; tone < spec_.tones; ++tone) {
      const int symbol = grayDecode(tone);
      const double mag = magnitude[tone];
      int mask = 1 << (spec_.bits - 1);
      for (int bit = 0; bit < spec_.bits; ++bit) {
        bits[bit] += (symbol & mask) ? mag : -mag;
        mask >>= 1;
      }
    }
    unsigned char soft[8] = {};
    for (int bit = 0; bit < spec_.bits; ++bit) {
      const double scaled = 128.0 + (bits[bit] / sum) * 256.0;
      const double clamped = std::clamp(scaled, 0.0, 255.0);
      soft[bit] = static_cast<unsigned char>(clamped);
    }
    inlv_.symbols(soft);
    QString text;
    for (int bit = 0; bit < spec_.bits; ++bit)
      text += takeSymbol(soft[bit]);
    return text;
  }

private:
  QString takeSymbol(unsigned char symbol) {
    pair_[0] = pair_[1];
    pair_[1] = symbol;
    symcounter_ = symcounter_ ? 0 : 1;
    int metric = 0;
    int bit = -1;
    const bool odd = spec_.bits == 5 || spec_.bits == 3;
    if (odd) {
      if (symcounter_) {
        bit = dec1_.decode(pair_[0], pair_[1], &metric);
        if (bit < 0)
          return {};
        met1_ = ((double(metric) - met1_) / 50.0) + met1_;
        if (met1_ < met2_)
          return {};
      } else {
        bit = dec2_.decode(pair_[0], pair_[1], &metric);
        if (bit < 0)
          return {};
        met2_ = ((double(metric) - met2_) / 50.0) + met2_;
        if (met2_ < met1_)
          return {};
      }
    } else {
      if (symcounter_)
        return {};
      bit = dec2_.decode(pair_[0], pair_[1], &metric);
      if (bit < 0)
        return {};
    }
    return takeBit(bit);
  }

  QString takeBit(int bit) {
    datashreg_ = (datashreg_ << 1) | (bit ? 1u : 0u);
    if ((datashreg_ & 7u) != 1u)
      return {};
    const int ch = varidec(datashreg_ >> 1);
    datashreg_ = 1;
    if (ch < 32 || ch >= 127)
      return {};
    return QString(QChar::fromLatin1(char(ch)));
  }

  Spec spec_;
  Interleave inlv_;
  Viterbi dec1_;
  Viterbi dec2_;
  unsigned char pair_[2] = {128, 128};
  int symcounter_ = 0;
  double met1_ = 0.0;
  double met2_ = 0.0;
  unsigned datashreg_ = 0;
};

void toneMagnitude(const float* samples, int count, int begin, int length, double lowHz, double spacing,
                   int tones, int sampleRateHz, int stride, double* magnitude) {
  for (int tone = 0; tone < tones; ++tone)
    magnitude[tone] = 0.0;
  if (!samples || length < 8 || begin < 0 || begin + length > count)
    return;
  const int stepSamples = std::max(1, stride);
  const double stepBase = 2.0 * 3.14159265358979323846 / double(sampleRateHz);
  for (int tone = 0; tone < tones; ++tone) {
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
  const double width = double(spec.tones - 1) * spec.baud;
  return double(centerHz) - width / 2.0;
}

QString textFromAudio(const Spec& spec, const float* samples, int count, int sampleRateHz, int centerHz) {
  const int separ = std::max(8, int(std::lround(double(sampleRateHz) / spec.baud)));
  const int symbols = count / separ;
  ToneRx rx(spec);
  QString text;
  double magnitude[32];
  const double low = lowHz(spec, centerHz);
  for (int s = 0; s < symbols; ++s) {
    toneMagnitude(samples, count, s * separ, separ, low, spec.baud, spec.tones, sampleRateHz, 1, magnitude);
    text += rx.push(magnitude);
  }
  return text;
}

}  // namespace

const char* mfskVaricode(int ch) {
  if (ch < 0 || ch > 255)
    ch = 0;
  return kCode[ch];
}

int mfskVaridec(unsigned symbol) {
  return varidec(symbol);
}

int mfskBandwidthHz(const QString& mode) {
  const Spec* spec = specFor(mode);
  if (!spec)
    return 0;
  return std::max(1, int(std::lround(double(spec->tones - 1) * spec->baud)));
}

MfskPlan planMfsk(const QString& mode, const QString& text, int centerHz) {
  MfskPlan plan;
  const Spec* spec = specFor(mode);
  if (!spec || text.isEmpty())
    return plan;
  const int separ = std::max(8, int(std::lround(48000.0 / spec->baud)));
  ToneTx tx(*spec);
  tx.clearBits();
  for (int i = 0; i < spec->preamble / 3; ++i)
    tx.sendBit(0);
  tx.sendChar('\r');
  tx.sendChar(2);
  tx.sendChar('\r');
  const QByteArray latin = text.toLatin1();
  plan.echoAt.assign(static_cast<size_t>(latin.size()), 0);
  for (int i = 0; i < latin.size(); ++i) {
    plan.echoAt[static_cast<size_t>(i)] = int(tx.symbols().size()) * separ;
    tx.sendChar(unsigned char(latin.at(i)));
  }
  tx.sendChar('\r');
  tx.sendChar(4);
  tx.sendChar('\r');
  tx.flushTx();
  const double low = lowHz(*spec, std::clamp(centerHz, 200, 3000));
  const double twoPi = 2.0 * 3.14159265358979323846;
  double phase = 0.0;
  plan.samples.reserve(tx.symbols().size() * static_cast<size_t>(separ));
  for (int symbol : tx.symbols()) {
    const double step = twoPi * (low + double(symbol) * spec->baud) / 48000.0;
    for (int i = 0; i < separ; ++i) {
      plan.samples.push_back(float(0.6 * std::cos(phase)));
      phase -= step;
      if (phase < 0.0)
        phase += twoPi;
    }
  }
  return plan;
}

QString mfskDecode(const QString& mode, const float* samples, int count, int sampleRateHz, int centerHz) {
  const Spec* spec = specFor(mode);
  if (!spec || !samples || count < 32 || sampleRateHz < 1000)
    return {};
  return textFromAudio(*spec, samples, count, sampleRateHz, centerHz);
}

struct MfskEngine::Private {
  bool armed = false;
  bool squelchOn = false;
  int squelch = 10;
  QString mode = QStringLiteral("MFSK16");
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

MfskEngine::MfskEngine(QObject* parent) : QObject(parent), d_(new Private) {}

MfskEngine::~MfskEngine() {
  delete d_;
}

void MfskEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  d_->reset();
}

void MfskEngine::setMode(const QString& mode) {
  if (d_->mode == mode)
    return;
  d_->mode = mode;
  d_->reset();
}

void MfskEngine::setCenterHz(int hz) {
  hz = std::clamp(hz, 200, 3000);
  if (d_->center == hz)
    return;
  d_->center = hz;
  d_->reset();
}

void MfskEngine::setSquelch(bool on, int level) {
  d_->squelchOn = on;
  d_->squelch = std::clamp(level, 0, 100);
}

void MfskEngine::clear() {
  d_->reset();
}

void MfskEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000 || mfskBandwidthHz(d_->mode) < 1)
    return;
  if (d_->squelchOn && d_->squelch >= 90)
    return;
  const Spec* spec = specFor(d_->mode);
  if (!spec)
    return;
  const int separ = std::max(8, int(std::lround(double(sampleRateHz) / spec->baud)));
  d_->pending.insert(d_->pending.end(), samples, samples + count);
  d_->since += count;
  const int need = separ * 12;
  if (int(d_->pending.size()) < need)
    return;
  if (!d_->locked) {
    if (d_->since < separ * 4)
      return;
    d_->since = 0;
    double best = -1.0;
    int bestAt = 0;
    const int step = std::max(1, separ / 4);
    double magnitude[32];
    const double low = lowHz(*spec, d_->center);
    for (int off = 0; off + separ <= int(d_->pending.size()) && off < separ; off += step) {
      toneMagnitude(d_->pending.data(), int(d_->pending.size()), off, separ, low, spec->baud, spec->tones,
                    sampleRateHz, 4, magnitude);
      double peak = 0.0;
      double second = 0.0;
      for (int tone = 0; tone < spec->tones; ++tone) {
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
    d_->rx = std::make_unique<ToneRx>(*spec);
  }
  if (!d_->rx)
    return;
  const double low = lowHz(*spec, d_->center);
  double magnitude[32];
  while (d_->lock + (d_->next + 1) * separ <= int(d_->pending.size())) {
    toneMagnitude(d_->pending.data(), int(d_->pending.size()), d_->lock + d_->next * separ, separ, low,
                  spec->baud, spec->tones, sampleRateHz, 1, magnitude);
    const QString text = d_->rx->push(magnitude);
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
