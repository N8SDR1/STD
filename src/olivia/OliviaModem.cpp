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

class BlockDecoder {
public:
  explicit BlockDecoder(const Spec& spec) : spec_(spec) {
    buffer_.assign(static_cast<size_t>(spec.symbolsPerBlock * spec.bits), 0.0);
  }

  int character(int freqBit) const {
    const int block = spec_.symbolsPerBlock;
    const int bits = spec_.bits;
    const int len = block * bits;
    const int wrap = block - 1;
    std::vector<double> buffer(static_cast<size_t>(block), 0.0);
    int ptr = ptr_;
    int rotate = freqBit;
    int codeBit = (freqBit * spec_.shift) & wrap;
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
    double peak = 0.0;
    int peakPos = 0;
    for (int time = 0; time < block; ++time) {
      const double signal = buffer[static_cast<size_t>(time)];
      if (std::fabs(signal) > std::fabs(peak)) {
        peak = signal;
        peakPos = time;
      }
    }
    int ch = peakPos;
    if (peak < 0.0)
      ch += block;
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
  Spec spec_;
  std::vector<double> buffer_;
  int ptr_ = 0;
  bool escape_ = false;
};

double lowHz(const Spec& spec, int centerHz) {
  return double(centerHz) - (double(spec.tones - 1) * spec.spacing) / 2.0;
}

}  // namespace

int oliviaBandwidthHz(const QString& mode) {
  return specFor(mode).bandwidth;
}

OliviaPlan planOlivia(const QString& mode, const QString& text, int centerHz) {
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
  for (int s = 0; s < int(symbols.size()); ++s) {
    const int tone = grayCode(symbols[static_cast<size_t>(s)]);
    const double step = twoPi * (low + double(tone) * spec.spacing) / double(kRate);
    double phase = 0.0;
    for (int t = 0; t < pulse; ++t) {
      const int idx = s * separ + t;
      if (idx >= count)
        break;
      const double shape = 1.0 - std::cos(twoPi * double(t) / double(pulse));
      wave[static_cast<size_t>(idx)] += std::sin(phase) * shape;
      phase += step;
    }
  }
  double peak = 0.0;
  for (double sample : wave)
    peak = std::max(peak, std::fabs(sample));
  plan.samples.resize(wave.size());
  const double scale = peak > 0.0 ? 0.6 / peak : 0.0;
  for (size_t i = 0; i < wave.size(); ++i)
    plan.samples[i] = float(wave[i] * scale);
  return plan;
}

QString oliviaDecode(const QString& mode, const float* samples, int count, int sampleRateHz,
                     int centerHz) {
  const Spec spec = specFor(mode);
  if (spec.tones < 2 || !samples || count < 32 || sampleRateHz < 1000)
    return {};
  const int separ = separAt(spec, sampleRateHz);
  if (separ < 8)
    return {};
  BlockDecoder decoder(spec);
  QString text;
  double energy[256];
  double bits[8];
  const double low = lowHz(spec, centerHz);
  // Symbol s is integrated over [s*separ + separ/2, s*separ + 3*separ/2).
  // The modulator's last pulse ends one symbol after the last symbol start.
  const int n = (count - separ / 2) / separ;
  int fed = 0;
  for (int s = 0; s < n; ++s) {
    toneEnergy(samples, count, s * separ + separ / 2, separ, low, spec.spacing, spec.tones,
               sampleRateHz, energy);
    softBits(energy, spec, bits);
    decoder.push(bits);
    ++fed;
    if (fed % spec.symbolsPerBlock == 0)
      text += decoder.take();
  }
  return text;
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
  std::unique_ptr<BlockDecoder> rx;
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
    rx.reset();
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
  if (int(d_->pending.size()) < need)
    return;
  double energy[256];
  double bits[8];
  const auto lowNow = [&] { return lowHz(spec, d_->center) + d_->clock.afcHz(); };
  if (!d_->locked) {
    if (d_->sinceDecode < sampleRateHz / 2)
      return;
    d_->sinceDecode = 0;
    double best = -1.0;
    int bestAt = 0;
    const int step = std::max(1, separ / 4);
    const int probe = std::min(16, spec.symbolsPerBlock);
    const double low = lowNow();
    for (int off = 0; off < separ; off += step) {
      double score = 0.0;
      int used = 0;
      for (int s = 0; s < probe; ++s) {
        const int begin = off + s * separ + separ / 2;
        if (begin + separ > int(d_->pending.size()))
          break;
        toneEnergy(d_->pending.data(), int(d_->pending.size()), begin, separ, low, spec.spacing, spec.tones,
                   rate, energy);
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
    if (best < 4.0)
      return;
    d_->locked = true;
    d_->origin = bestAt;
    d_->fed = 0;
    d_->quality = 0.0;
    d_->rx = std::make_unique<BlockDecoder>(spec);
  }
  if (!d_->rx)
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
    softBits(energy, spec, bits);
    d_->rx->push(bits);
    d_->origin += separ;
    ++d_->fed;
    if (d_->fed % spec.symbolsPerBlock == 0) {
      QString text = d_->rx->take();
      if (d_->squelchOn && d_->quality < double(d_->squelch))
        text.clear();
      if (!text.isEmpty())
        emit textReady(text);
    }
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
