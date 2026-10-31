// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Olivia and Contestia block modem. The FEC, scramble, Gray tones, and
// symbol shape follow Pawel Jalocha, pj_mfsk.h in fldigi (GPL-3, Dave
// Freese W1HKJ). This is a rewrite for STD's 48 kHz TCI audio. It is
// not a copy of that header, and the header is not built in.

#include "OliviaModem.h"

#include "modem/ToneClock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr int kRate = 48000;
constexpr uint64_t kScrambleOlivia = 0xE257E6D0291574ECLL;
constexpr uint64_t kScrambleContestia = 0xEDB88320LL;

struct Spec {
  bool contestia = false;
  int tones = 0;
  int bits = 0;
  int bandwidth = 0;
  int symbolsPerBlock = 0;
  int shift = 0;
  uint64_t scramble = 0;
  double baud = 0.0;
  double spacing = 0.0;
};

int log2Floor(int value) {
  int n = 0;
  while (value > 1) {
    value >>= 1;
    ++n;
  }
  return n;
}

Spec specFor(const QString& mode) {
  Spec spec;
  const QString name = mode.trimmed();
  QString body;
  if (name.startsWith(QLatin1String("Olivia "), Qt::CaseInsensitive))
    body = name.mid(7);
  else if (name.startsWith(QLatin1String("Contestia "), Qt::CaseInsensitive)) {
    spec.contestia = true;
    body = name.mid(10);
  } else
    return spec;
  const int slash = body.indexOf(QLatin1Char('/'));
  if (slash < 1)
    return spec;
  bool tonesOk = false;
  bool bwOk = false;
  const int tones = body.left(slash).toInt(&tonesOk);
  const int bandwidth = body.mid(slash + 1).toInt(&bwOk);
  if (!tonesOk || !bwOk || tones < 2 || tones > 256 || (tones & (tones - 1)) != 0)
    return spec;
  if (bandwidth < 125 || bandwidth > 2000 || bandwidth % 125 != 0)
    return spec;
  const int bwSteps = bandwidth / 125;
  if ((bwSteps & (bwSteps - 1)) != 0)
    return spec;
  const int bits = log2Floor(tones);
  const int bwLog = log2Floor(bwSteps);
  const int symbolBits = bits + 7 - bwLog;
  if (symbolBits < 3 || symbolBits > 16)
    return spec;
  const int symbolLen = 1 << symbolBits;
  const int separ8k = symbolLen / 2;
  spec.tones = tones;
  spec.bits = bits;
  spec.bandwidth = bandwidth;
  spec.symbolsPerBlock = spec.contestia ? 32 : 64;
  spec.shift = spec.contestia ? 5 : 13;
  spec.scramble = spec.contestia ? kScrambleContestia : kScrambleOlivia;
  spec.baud = 8000.0 / double(separ8k);
  spec.spacing = 2.0 * (8000.0 / double(symbolLen));
  return spec;
}

int separAt(const Spec& spec, int sampleRateHz) {
  if (spec.baud <= 0.0 || sampleRateHz < 1000)
    return 0;
  return std::max(8, int(std::lround(double(sampleRateHz) / spec.baud)));
}

void ifht(std::vector<double>* data) {
  const int len = int(data->size());
  for (int step = len / 2; step >= 1; step /= 2) {
    for (int ptr = 0; ptr < len; ptr += 2 * step) {
      for (int i = ptr; i < ptr + step; ++i) {
        const double a = (*data)[static_cast<size_t>(i)];
        const double b = (*data)[static_cast<size_t>(i + step)];
        (*data)[static_cast<size_t>(i)] = a - b;
        (*data)[static_cast<size_t>(i + step)] = a + b;
      }
    }
  }
}

void fht(std::vector<double>* data) {
  const int len = int(data->size());
  for (int step = 1; step < len; step *= 2) {
    for (int ptr = 0; ptr < len; ptr += 2 * step) {
      for (int i = ptr; i < ptr + step; ++i) {
        const double a = (*data)[static_cast<size_t>(i)];
        const double b = (*data)[static_cast<size_t>(i + step)];
        (*data)[static_cast<size_t>(i)] = b + a;
        (*data)[static_cast<size_t>(i + step)] = b - a;
      }
    }
  }
}

int grayCode(int binary) {
  return binary ^ (binary >> 1);
}

int binaryCode(int gray) {
  unsigned value = unsigned(gray);
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;
  return int(value);
}

int contestiaCode(int ch) {
  if (ch >= 'a' && ch <= 'z')
    ch += 'A' - 'a';
  if (ch == ' ')
    return 59;
  if (ch == '\r')
    return 60;
  if (ch == '\n')
    return 0;
  if (ch >= 33 && ch <= 90)
    return ch - 32;
  if (ch == 8)
    return 61;
  if (ch == 0)
    return 0;
  return '?' - 32;
}

int contestiaChar(int code) {
  if (code <= 0)
    return 0;
  if (code == 59)
    return ' ';
  if (code == 60)
    return '\r';
  if (code == 61)
    return 8;
  return code + 32;
}

void encodeCharacter(int ch, const Spec& spec, std::vector<double>* buffer) {
  const int block = spec.symbolsPerBlock;
  const int mask = (block << 1) - 1;
  if (spec.contestia)
    ch = contestiaCode(ch);
  else
    ch &= mask;
  std::fill(buffer->begin(), buffer->end(), 0.0);
  if (ch < block)
    (*buffer)[static_cast<size_t>(ch)] = 1.0;
  else
    (*buffer)[static_cast<size_t>(ch - block)] = -1.0;
  ifht(buffer);
}

void encodeBlock(const int* chars, const Spec& spec, std::vector<int>* symbols) {
  const int block = spec.symbolsPerBlock;
  const int bits = spec.bits;
  const int wrap = block - 1;
  std::vector<double> buffer(static_cast<size_t>(block), 0.0);
  std::vector<int> out(static_cast<size_t>(block), 0);
  for (int freq = 0; freq < bits; ++freq) {
    encodeCharacter(chars[freq], spec, &buffer);
    int codeBit = (freq * spec.shift) & wrap;
    for (int time = 0; time < block; ++time) {
      const uint64_t codeMask = uint64_t(1) << codeBit;
      if ((spec.scramble & codeMask) != 0)
        buffer[static_cast<size_t>(time)] = -buffer[static_cast<size_t>(time)];
      codeBit = (codeBit + 1) & wrap;
    }
    int rotate = 0;
    for (int time = 0; time < block; ++time) {
      if (buffer[static_cast<size_t>(time)] < 0.0) {
        int bit = freq + rotate;
        if (bit >= bits)
          bit -= bits;
        out[static_cast<size_t>(time)] |= 1 << bit;
      }
      ++rotate;
      if (rotate >= bits)
        rotate -= bits;
    }
  }
  symbols->insert(symbols->end(), out.begin(), out.end());
}

void toneEnergy(const float* samples, int count, int begin, int length, double lowHz, double spacing,
                int tones, int sampleRateHz, double* energy) {
  for (int tone = 0; tone < tones; ++tone)
    energy[tone] = 0.0;
  if (!samples || length < 8 || begin < 0 || begin + length > count)
    return;
  const double stepBase = 2.0 * 3.14159265358979323846 / double(sampleRateHz);
  for (int tone = 0; tone < tones; ++tone) {
    const double step = stepBase * (lowHz + double(tone) * spacing);
    double phase = 0.0;
    double si = 0.0;
    double sq = 0.0;
    for (int i = 0; i < length; ++i) {
      const double sample = samples[begin + i];
      si += sample * std::sin(phase);
      sq += sample * std::cos(phase);
      phase += step;
      if (phase > 6.283185307179586)
        phase -= 6.283185307179586;
    }
    energy[tone] = si * si + sq * sq;
  }
}

void softBits(const double* energy, const Spec& spec, double* bits) {
  double total = 0.0;
  for (int tone = 0; tone < spec.tones; ++tone) {
    const double power = energy[tone] * energy[tone];
    total += power;
  }
  for (int bit = 0; bit < spec.bits; ++bit)
    bits[bit] = 0.0;
  if (total <= 0.0)
    return;
  for (int tone = 0; tone < spec.tones; ++tone) {
    const int symbol = binaryCode(tone);
    const double power = energy[tone] * energy[tone];
    int mask = 1;
    for (int bit = 0; bit < spec.bits; ++bit) {
      if ((symbol & mask) != 0)
        bits[bit] -= power;
      else
        bits[bit] += power;
      mask <<= 1;
    }
  }
  for (int bit = 0; bit < spec.bits; ++bit)
    bits[bit] /= total;
}

struct BlockMetrics {
  double signal = 0.0;
  double noise = 0.0;
};

class BlockDecoder {
public:
  explicit BlockDecoder(const Spec& spec) : spec_(spec) {
    buffer_.assign(static_cast<size_t>(spec.symbolsPerBlock * spec.bits), 0.0);
  }

  // fldigi MFSK_SoftDecoder::DecodeCharacter: peak magnitude over the
  // mean power of the other FHT bins. Sync S/N is signal / sqrt(noise).
  BlockMetrics metrics() const {
    BlockMetrics out;
    if (spec_.bits < 1)
      return out;
    for (int freq = 0; freq < spec_.bits; ++freq) {
      const FecHit hit = fecHit(freq);
      out.signal += std::fabs(hit.peak);
      out.noise += hit.noise;
    }
    out.signal /= double(spec_.bits);
    out.noise /= double(spec_.bits);
    return out;
  }

  int character(int freqBit) const {
    int ch = fecHit(freqBit).ch;
    if (spec_.contestia)
      ch = contestiaChar(ch);
    return ch;
  }

  QString take() {
    QString text;
    for (int freq = 0; freq < spec_.bits; ++freq) {
      int ch = character(freq);
      if (!spec_.contestia) {
        if (escape_) {
          escape_ = false;
          ch = (ch & 127) + 128;
        } else if (ch == 127) {
          escape_ = true;
          continue;
        }
      }
      if (ch == 8) {
        if (!text.isEmpty())
          text.chop(1);
        continue;
      }
      text += latin1Rx(ch);
    }
    return text;
  }

  void push(const double* bits) {
    for (int bit = 0; bit < spec_.bits; ++bit) {
      buffer_[static_cast<size_t>(ptr_)] = bits[bit];
      ++ptr_;
    }
    const int len = spec_.symbolsPerBlock * spec_.bits;
    if (ptr_ >= len)
      ptr_ -= len;
  }

private:
  struct FecHit {
    int ch = 0;
    double peak = 0.0;
    double noise = 0.0;
  };

  FecHit fecHit(int freqBit) const {
    FecHit hit;
    const int block = spec_.symbolsPerBlock;
    const int bits = spec_.bits;
    const int len = block * bits;
    const int wrap = block - 1;
    if (block < 2 || bits < 1 || freqBit < 0 || freqBit >= bits)
      return hit;
    std::vector<double> buffer(static_cast<size_t>(block), 0.0);
    int ptr = ptr_;
    int rotate = freqBit;
    int codeBit = (freqBit * spec_.shift) & wrap;
    double sumSq = 0.0;
    for (int time = 0; time < block; ++time) {
      double bit = buffer_[static_cast<size_t>(ptr + rotate)];
      const uint64_t codeMask = uint64_t(1) << codeBit;
      if ((spec_.scramble & codeMask) != 0)
        bit = -bit;
      buffer[static_cast<size_t>(time)] = bit;
      codeBit = (codeBit + 1) & wrap;
      ++rotate;
      if (rotate >= bits)
        rotate -= bits;
      ptr += bits;
      if (ptr >= len)
        ptr -= len;
    }
    fht(&buffer);
    for (int time = 0; time < block; ++time) {
      const double signal = buffer[static_cast<size_t>(time)];
      sumSq += signal * signal;
      if (std::fabs(signal) > std::fabs(hit.peak)) {
        hit.peak = signal;
        hit.ch = time;
      }
    }
    if (hit.peak < 0.0)
      hit.ch += block;
    sumSq -= hit.peak * hit.peak;
    hit.noise = sumSq / double(block - 1);
    return hit;
  }

  Spec spec_;
  std::vector<double> buffer_;
  int ptr_ = 0;
  bool escape_ = false;
};

// fldigi FirstCarrier = int((SymbolLen/16) * (f - fc_offset)/500) + 1
// at the 8 kHz processor. fc_offset = BW * (1 - 0.5/tones) / 2, so the
// tone set sits about a quarter of a spacing off a cursor-centred comb.
// The carrier frequency is that FFT bin times 8000/SymbolLen.
double lowHz(const Spec& spec, int centerHz) {
  if (spec.spacing <= 0.0 || spec.tones < 2 || spec.bandwidth < 125)
    return double(centerHz);
  const double fcOffset = double(spec.bandwidth) * (1.0 - 0.5 / double(spec.tones)) / 2.0;
  const double symbolLen = 16000.0 / spec.spacing;
  const double scaled = (symbolLen / 16.0) * ((double(centerHz) - fcOffset) / 500.0);
  const int first = std::max(1, int(scaled) + 1);
  return double(first) * (8000.0 / symbolLen);
}

constexpr int kSyncMargin = 8;
constexpr int kSyncInteg = 4;
constexpr double kSyncSnr = 3.2;

double syncSnr(double signal, double noise) {
  if (noise <= 1e-18)
    return signal > 0.2 ? 80.0 : 0.0;
  return signal / std::sqrt(noise);
}

struct BlockScore {
  double signal = 0.0;
  double noise = 0.0;
  int blocks = 0;
};

BlockScore scorePhase(const Spec& spec, const std::vector<std::array<double, 8>>& symbols, int phase) {
  BlockScore score;
  const int block = spec.symbolsPerBlock;
  const int have = int(symbols.size()) - phase;
  const int blocks = have / block;
  if (phase < 0 || blocks < 1)
    return score;
  const int use = std::min(blocks, kSyncInteg);
  const int start = phase + (blocks - use) * block;
  BlockDecoder decoder(spec);
  int got = 0;
  for (int s = start; s < start + use * block; ++s) {
    decoder.push(symbols[static_cast<size_t>(s)].data());
    if (++got % block == 0) {
      const BlockMetrics metrics = decoder.metrics();
      score.signal += metrics.signal;
      score.noise += metrics.noise;
      ++score.blocks;
    }
  }
  return score;
}

double phaseSnr(const BlockScore& score) {
  if (score.blocks < 1)
    return 0.0;
  return syncSnr(score.signal / double(score.blocks), score.noise / double(score.blocks));
}

QString textAtPhase(const Spec& spec, const std::vector<std::array<double, 8>>& symbols, int phase) {
  const int block = spec.symbolsPerBlock;
  BlockDecoder decoder(spec);
  QString text;
  int got = 0;
  for (int s = phase; s < int(symbols.size()); ++s) {
    decoder.push(symbols[static_cast<size_t>(s)].data());
    if (++got % block == 0)
      text += decoder.take();
  }
  return text;
}

// One symbol of soft bits at each tune offset. The ring holds one FEC
// block. Printing waits until a (phase, offset) pair holds S/N >= 3.2
// across up to four blocks, which is fldigi's SyncThreshold / SyncIntegLen.
class OliviaSync {
public:
  explicit OliviaSync(const Spec& spec) : spec_(spec), printer_(spec) {
    cap_ = std::max(8, spec.symbolsPerBlock);
    nOff_ = kSyncMargin * 2 + 1;
    soft_.assign(static_cast<size_t>(nOff_), std::vector<double>(static_cast<size_t>(cap_ * spec.bits), 0.0));
    hist_.assign(static_cast<size_t>(cap_ * nOff_), Hist{});
  }

  QString push(const double* soft, double quality, bool squelchOn, int squelch) {
    if (!soft || spec_.symbolsPerBlock < 8 || spec_.bits < 1)
      return {};
    const int block = spec_.symbolsPerBlock;
    const int bits = spec_.bits;
    const int s = symbols_;
    ++symbols_;
    for (int off = 0; off < nOff_; ++off) {
      double* slot = soft_[static_cast<size_t>(off)].data() + (s % cap_) * bits;
      const double* src = soft + off * bits;
      for (int bit = 0; bit < bits; ++bit)
        slot[bit] = src[bit];
    }
    if (s < block - 1)
      return {};
    const int phase = (s + 1) % block;
    const int start = s - block + 1;
    for (int off = 0; off < nOff_; ++off) {
      BlockDecoder decoder(spec_);
      for (int i = start; i <= s; ++i)
        decoder.push(soft_[static_cast<size_t>(off)].data() + (i % cap_) * bits);
      const BlockMetrics metrics = decoder.metrics();
      Hist& hist = hist_[static_cast<size_t>(phase * nOff_ + off)];
      hist.sig[hist.pos] = metrics.signal;
      hist.noi[hist.pos] = metrics.noise;
      hist.pos = (hist.pos + 1) % kSyncInteg;
      if (hist.n < kSyncInteg)
        ++hist.n;
    }
    int globalPhase = 0;
    int globalOff = kSyncMargin;
    double global = -1.0;
    double second = -1.0;
    for (int p = 0; p < block; ++p) {
      for (int off = 0; off < nOff_; ++off) {
        const Hist& hist = hist_[static_cast<size_t>(p * nOff_ + off)];
        if (hist.n < 2)
          continue;
        const double snr = snrOf(hist);
        if (snr > global) {
          second = global;
          global = snr;
          globalPhase = p;
          globalOff = off;
        } else if (snr > second) {
          second = snr;
        }
      }
    }
    // A lone false block can clear 3.2. Print when this pair is in a
    // different class from the runner-up, which is what a real FEC block does.
    const bool distinct = global >= kSyncSnr && (second < kSyncSnr || global > second * 2.0);
    if (!distinct)
      global = -1.0;
    // Hold the pair we are printing. A one-bin wiggle must not reset the
    // escape state between the two characters of a Latin-1 byte.
    if (global >= kSyncSnr && (latchedPhase_ < 0 || global > latchedSnr_ + 0.5)) {
      latchedPhase_ = globalPhase;
      latchedOff_ = globalOff;
    }
    if (latchedPhase_ < 0)
      return {};
    latchedSnr_ = snrOf(hist_[static_cast<size_t>(latchedPhase_ * nOff_ + latchedOff_)]);
    if (latchedSnr_ < kSyncSnr || phase != latchedPhase_)
      return {};
    if (hist_[static_cast<size_t>(phase * nOff_ + latchedOff_)].n < 2)
      return {};
    if (printerPhase_ != phase || printerOff_ != latchedOff_ || s - lastPrinted_ != block)
      printer_ = BlockDecoder(spec_);
    for (int i = start; i <= s; ++i)
      printer_.push(soft_[static_cast<size_t>(latchedOff_)].data() + (i % cap_) * bits);
    printerPhase_ = phase;
    printerOff_ = latchedOff_;
    lastPrinted_ = s;
    QString text = printer_.take();
    if (squelchOn && quality < double(squelch))
      text.clear();
    return text;
  }

private:
  struct Hist {
    double sig[kSyncInteg] = {};
    double noi[kSyncInteg] = {};
    int n = 0;
    int pos = 0;
  };

  double snrOf(const Hist& hist) const {
    if (hist.n < 1)
      return 0.0;
    double signal = 0.0;
    double noise = 0.0;
    for (int i = 0; i < hist.n; ++i) {
      signal += hist.sig[i];
      noise += hist.noi[i];
    }
    return syncSnr(signal / double(hist.n), noise / double(hist.n));
  }

  Spec spec_;
  BlockDecoder printer_;
  int cap_ = 0;
  int nOff_ = 0;
  int symbols_ = 0;
  int printerPhase_ = -1;
  int printerOff_ = -1;
  int latchedPhase_ = -1;
  int latchedOff_ = kSyncMargin;
  double latchedSnr_ = 0.0;
  int lastPrinted_ = -100000;
  std::vector<std::vector<double>> soft_;
  std::vector<Hist> hist_;
};

}  // namespace

int oliviaBandwidthHz(const QString& mode) {
  return specFor(mode).bandwidth;
}

int oliviaToneSamples(int sampleRateHz) {
  // fldigi TONE_DURATION is 8192 samples at 8 kHz, 1.024 seconds.
  return std::max(4, int(std::lround(1.024 * double(sampleRateHz))));
}

std::vector<float> edgeTones(const Spec& spec, int centerHz, int sampleRateHz) {
  const int total = oliviaToneSamples(sampleRateHz);
  const int quarter = std::max(1, total / 4);
  const int rise = std::max(1, quarter / 8);
  const double bw = double(spec.bandwidth);
  const double freqA = double(centerHz) - bw / 2.0;
  const double freqB = double(centerHz) + bw / 2.0;
  const double twoPi = 2.0 * 3.14159265358979323846;
  const double freqs[4] = {freqA, freqB, freqA, freqB};
  std::vector<float> out(static_cast<size_t>(quarter * 4), 0.0f);
  for (int part = 0; part < 4; ++part) {
    const double step = twoPi * freqs[part] / double(sampleRateHz);
    double phase = 0.0;
    for (int i = 0; i < quarter; ++i) {
      double env = 1.0;
      if (i < rise)
        env = 0.5 * (1.0 - std::cos(3.14159265358979323846 * double(i) / double(rise)));
      else if (i >= quarter - rise)
        env = 0.5 * (1.0 - std::cos(3.14159265358979323846 * double(quarter - 1 - i) / double(rise)));
      out[static_cast<size_t>(part * quarter + i)] = float(0.6 * env * std::sin(phase));
      phase += step;
    }
  }
  return out;
}

OliviaPlan planOlivia(const QString& mode, const QString& text, int centerHz, bool startTones) {
  OliviaPlan plan;
  const Spec spec = specFor(mode);
  const QString body = text;
  if (spec.tones < 2 || body.isEmpty())
    return plan;
  const int separ = separAt(spec, kRate);
  if (separ < 8)
    return plan;
  const QByteArray latin = body.toLatin1();
  struct Byte {
    unsigned char value;
    int source;
  };
  std::vector<Byte> bytes;
  bytes.reserve(static_cast<size_t>(std::max(0, int(latin.size()) * 2)));
  for (int i = 0; i < latin.size(); ++i) {
    const auto u = static_cast<unsigned char>(latin.at(i));
    if (!spec.contestia && u >= 128) {
      bytes.push_back(Byte{127, -1});
      bytes.push_back(Byte{static_cast<unsigned char>(u & 127), i});
    } else {
      bytes.push_back(Byte{u, i});
    }
  }
  std::vector<int> symbols;
  const int idle[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  encodeBlock(idle, spec, &symbols);
  plan.echoAt.assign(static_cast<size_t>(latin.size()), 0);
  for (int i = 0; i < int(bytes.size());) {
    int block[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const int blockIndex = 1 + i / spec.bits;
    const int at = blockIndex * spec.symbolsPerBlock * separ;
    for (int bit = 0; bit < spec.bits && i < int(bytes.size()); ++bit, ++i) {
      block[bit] = bytes[static_cast<size_t>(i)].value;
      const int source = bytes[static_cast<size_t>(i)].source;
      if (source >= 0)
        plan.echoAt[static_cast<size_t>(source)] = at;
    }
    encodeBlock(block, spec, &symbols);
  }
  const int pulse = separ * 2;
  const int count = (int(symbols.size()) + 1) * separ;
  std::vector<double> wave(static_cast<size_t>(count), 0.0);
  const double low = lowHz(spec, std::clamp(centerHz, 200, 3000));
  const double twoPi = 2.0 * 3.14159265358979323846;
  // fldigi MFSK_Modulator::Send keeps SymbolPhase across the hop and adds
  // ±90 degrees. Resetting the phase on every symbol cancels a repeated tone
  // in the 50% overlap.
  double phase = 0.0;
  bool plus = true;
  for (int s = 0; s < int(symbols.size()); ++s) {
    const int tone = grayCode(symbols[static_cast<size_t>(s)]);
    const double step = twoPi * (low + double(tone) * spec.spacing) / double(kRate);
    phase -= step * (double(separ) / 2.0);
    double cursor = phase;
    for (int t = 0; t < pulse; ++t) {
      const int idx = s * separ + t;
      if (idx >= count)
        break;
      const double shape = 1.0 - std::cos(twoPi * double(t) / double(pulse));
      wave[static_cast<size_t>(idx)] += std::sin(cursor) * shape;
      cursor += step;
    }
    phase += step * (double(separ) / 2.0 + double(pulse) / 2.0);
    const double differ = step * (double(pulse) / 4.0);
    phase += plus ? differ : -differ;
    plus = !plus;
    phase = std::fmod(phase, twoPi);
    if (phase < 0.0)
      phase += twoPi;
  }
  double peak = 0.0;
  for (double sample : wave)
    peak = std::max(peak, std::fabs(sample));
  plan.samples.resize(wave.size());
  const double scale = peak > 0.0 ? 0.6 / peak : 0.0;
  for (size_t i = 0; i < wave.size(); ++i)
    plan.samples[i] = float(wave[i] * scale);
  if (startTones) {
    const std::vector<float> tones = edgeTones(spec, std::clamp(centerHz, 200, 3000), kRate);
    for (int& at : plan.echoAt)
      at += int(tones.size());
    plan.samples.insert(plan.samples.begin(), tones.begin(), tones.end());
    plan.samples.insert(plan.samples.end(), tones.begin(), tones.end());
  }
  return plan;
}

int edgeToneLead(const Spec& spec, const float* samples, int count, int sampleRateHz, int centerHz) {
  const int lead = oliviaToneSamples(sampleRateHz);
  const int n = std::min(lead / 4, std::max(32, sampleRateHz / 5));
  if (!samples || count < lead + 64 || n < 32)
    return 0;
  const double freqA = double(centerHz) - spec.bandwidth / 2.0;
  const double mid = double(centerHz);
  auto mag = [&](double freq) {
    const double step = 2.0 * 3.14159265358979323846 * freq / double(sampleRateHz);
    double phase = 0.0;
    double si = 0.0;
    double sq = 0.0;
    for (int i = 0; i < n; ++i) {
      const double sample = samples[i];
      si += sample * std::sin(phase);
      sq += sample * std::cos(phase);
      phase += step;
    }
    return std::sqrt(si * si + sq * sq);
  };
  const double edge = mag(freqA);
  const double center = mag(mid);
  if (edge > 1.0 && edge > center * 3.0)
    return lead;
  return 0;
}

QString oliviaDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                     int centerHz) {
  const Spec spec = specFor(mode);
  if (spec.tones < 2 || !samples || count < 32 || sampleRateHz < 1000)
    return {};
  const int separ = separAt(spec, sampleRateHz);
  if (separ < 8)
    return {};
  double energy[256];
  const double base = lowHz(spec, centerHz);
  const double binHz = spec.spacing / 2.0;
  const int lead = edgeToneLead(spec, samples, count, sampleRateHz, centerHz);
  // Symbol s is integrated over [s*separ + separ/2, s*separ + 3*separ/2).
  // The modulator's last pulse ends one symbol after the last symbol start.
  const int n = (count - lead - separ / 2) / separ;
  if (n < spec.symbolsPerBlock)
    return {};
  const int nOff = kSyncMargin * 2 + 1;
  std::vector<std::vector<std::array<double, 8>>> syms(static_cast<size_t>(nOff));
  for (int off = 0; off < nOff; ++off) {
    syms[static_cast<size_t>(off)].resize(static_cast<size_t>(n));
    const double low = base + double(off - kSyncMargin) * binHz;
    for (int s = 0; s < n; ++s) {
      toneEnergy(samples, count, lead + s * separ + separ / 2, separ, low, spec.spacing, spec.tones,
                 sampleRateHz, energy);
      softBits(energy, spec, syms[static_cast<size_t>(off)][static_cast<size_t>(s)].data());
    }
  }
  int bestPhase = 0;
  int bestOff = 0;
  double bestSnr = -1.0;
  for (int off = 0; off < nOff; ++off) {
    for (int phase = 0; phase < spec.symbolsPerBlock; ++phase) {
      const double snr = phaseSnr(scorePhase(spec, syms[static_cast<size_t>(off)], phase));
      if (snr > bestSnr) {
        bestSnr = snr;
        bestPhase = phase;
        bestOff = off;
      }
    }
  }
  if (bestSnr < kSyncSnr)
    return {};
  return textAtPhase(spec, syms[static_cast<size_t>(bestOff)], bestPhase);
}

struct OliviaEngine::Private {
  bool armed = false;
  bool squelchOn = false;
  int squelch = 10;
  QString mode = QStringLiteral("Olivia 8/250");
  int center = 1500;
  int rate = kRate;
  bool locked = false;
  int lock = 0;
  int sinceDecode = 0;
  int origin = 0;
  int fed = 0;
  double quality = 0.0;
  std::vector<float> pending;
  std::unique_ptr<OliviaSync> sync;
  Rate8k decim;
  SymbolClock clock;

  void reset() {
    locked = false;
    lock = 0;
    sinceDecode = 0;
    origin = 0;
    fed = 0;
    quality = 0.0;
    pending.clear();
    sync.reset();
    decim.reset();
    clock.resetTrack();
  }
};

OliviaEngine::OliviaEngine(QObject* parent) : QObject(parent), d_(new Private) {}

OliviaEngine::~OliviaEngine() {
  delete d_;
}

void OliviaEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  d_->reset();
}

void OliviaEngine::setMode(const QString& mode) {
  if (d_->mode == mode)
    return;
  d_->mode = mode;
  d_->reset();
}

void OliviaEngine::setCenterHz(int hz) {
  hz = std::clamp(hz, 200, 3000);
  if (d_->center == hz)
    return;
  d_->center = hz;
  d_->reset();
}

void OliviaEngine::setSquelch(bool on, int level) {
  d_->squelchOn = on;
  d_->squelch = std::clamp(level, 0, 100);
}

void OliviaEngine::setAfc(bool on) {
  d_->clock.setAfcEnabled(on);
}

void OliviaEngine::clear() {
  d_->reset();
}

void OliviaEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000)
    return;
  if (oliviaBandwidthHz(d_->mode) < 125)
    return;
  const Spec spec = specFor(d_->mode);
  if (spec.tones < 2)
    return;
  d_->decim.push(samples, count, sampleRateHz, &d_->pending);
  constexpr int rate = 8000;
  d_->rate = rate;
  const int separ = separAt(spec, rate);
  if (separ < 8)
    return;
  d_->sinceDecode += count;
  const int need = (spec.symbolsPerBlock + 4) * separ;
  // The long window is only for finding symbol timing. Once locked, a
  // trimmed buffer is still decoded a symbol at a time.
  if (!d_->locked && int(d_->pending.size()) < need)
    return;
  double energy[256];
  const auto lowNow = [&] { return lowHz(spec, d_->center) + d_->clock.afcHz(); };
  if (!d_->locked) {
    const int step = std::max(1, separ / 4);
    const int probe = std::min(16, spec.symbolsPerBlock);
    const double binHz = spec.spacing / 2.0;
    const int nOff = kSyncMargin * 2 + 1;
    while (!d_->locked && int(d_->pending.size()) >= need) {
      double best = -1.0;
      int bestAt = 0;
      const int sizeNow = int(d_->pending.size());
      for (int bin = 0; bin < nOff; ++bin) {
        const double low = lowNow() + double(bin - kSyncMargin) * binHz;
        for (int off = 0; off < separ; off += step) {
          double score = 0.0;
          int used = 0;
          for (int s = 0; s < probe; ++s) {
            const int begin = off + s * separ + separ / 2;
            if (begin + separ > sizeNow)
              break;
            toneEnergy(d_->pending.data(), sizeNow, begin, separ, low, spec.spacing, spec.tones, rate, energy);
            double peak = 0.0;
            double second = 0.0;
            for (int tone = 0; tone < spec.tones; ++tone) {
              const double power = energy[tone];
              if (power > peak) {
                second = peak;
                peak = power;
              } else if (power > second)
                second = power;
            }
            if (peak > 0.0)
              score += (peak - second) / peak;
            ++used;
          }
          if (used >= 8 && score > best + 0.5) {
            best = score;
            bestAt = off;
          }
        }
      }
      if (best >= 4.0) {
        d_->locked = true;
        d_->origin = bestAt;
        d_->fed = 0;
        d_->quality = 0.0;
        d_->sync = std::make_unique<OliviaSync>(spec);
        break;
      }
      // Start tones and silence are not an FEC symbol. Drop the window
      // just scored so the next pass can see the text that follows.
      const int drop = std::min(sizeNow, probe * separ);
      if (drop < 1)
        break;
      d_->pending.erase(d_->pending.begin(), d_->pending.begin() + drop);
    }
    if (!d_->locked)
      return;
  }
  if (!d_->sync)
    return;
  const int size = int(d_->pending.size());
  const int edge = std::max(1, separ / 8);
  while (d_->origin + separ + separ / 2 <= size) {
    int begin = d_->origin + separ / 2;
    const auto peakOnly = [&](int pos) {
      if (pos < 0 || pos + separ > size)
        return 0.0;
      toneEnergy(d_->pending.data(), size, pos, separ, lowNow(), spec.spacing, spec.tones, rate, energy);
      double peak = 0.0;
      for (int tone = 0; tone < spec.tones; ++tone)
        peak = std::max(peak, energy[tone]);
      return peak;
    };
    const double early = peakOnly(begin - edge);
    const double late = peakOnly(begin + edge);
    toneEnergy(d_->pending.data(), size, begin, separ, lowNow(), spec.spacing, spec.tones, rate, energy);
    BinPick bin = pickBin(energy, spec.tones);
    const bool strong = binStrong(bin, spec.tones);
    const bool transition = d_->clock.tone() < 0 || bin.index != d_->clock.tone();
    const int shift = d_->clock.nudge(early, bin.peak, late, edge, strong, transition);
    if (shift != 0 && begin + shift >= 0 && begin + shift + separ <= size) {
      begin += shift;
      d_->origin += shift;
      toneEnergy(d_->pending.data(), size, begin, separ, lowNow(), spec.spacing, spec.tones, rate, energy);
      bin = pickBin(energy, spec.tones);
    }
    const bool repeat = d_->clock.tone() >= 0 && bin.index == d_->clock.tone();
    if (repeat && binStrong(bin, spec.tones)) {
      const int half = std::max(4, separ / 2);
      const double freq = lowNow() + double(bin.index) * spec.spacing;
      double si0 = 0.0;
      double sq0 = 0.0;
      double si1 = 0.0;
      double sq1 = 0.0;
      toneIq(d_->pending.data(), size, begin, half, freq, rate, &si0, &sq0);
      toneIq(d_->pending.data(), size, begin + half, half, freq, rate, &si1, &sq1);
      d_->clock.observeFreq(si0, sq0, si1, sq1, half, rate, spec.spacing, true, true);
    }
    d_->clock.setTone(bin.index);
    const double instant = toneQuality(bin.peak, bin.sum, spec.tones);
    d_->quality += (instant - d_->quality) / 16.0;
    const int nOff = kSyncMargin * 2 + 1;
    const double binHz = spec.spacing / 2.0;
    const double base = lowNow();
    std::vector<double> packed(static_cast<size_t>(nOff * spec.bits), 0.0);
    softBits(energy, spec, packed.data() + kSyncMargin * spec.bits);
    for (int off = 0; off < nOff; ++off) {
      if (off == kSyncMargin)
        continue;
      toneEnergy(d_->pending.data(), size, begin, separ, base + double(off - kSyncMargin) * binHz,
                 spec.spacing, spec.tones, rate, energy);
      softBits(energy, spec, packed.data() + off * spec.bits);
    }
    const QString text = d_->sync->push(packed.data(), d_->quality, d_->squelchOn, d_->squelch);
    d_->origin += separ;
    if (!text.isEmpty())
      emit textReady(text);
  }
  const int margin = separ * 4;
  if (d_->origin > margin * 8) {
    const int drop = d_->origin - margin;
    d_->pending.erase(d_->pending.begin(), d_->pending.begin() + drop);
    d_->origin -= drop;
    if (d_->origin < 0)
      d_->reset();
  }
}
