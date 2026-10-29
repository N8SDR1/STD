// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mark/space pair from fldigi rtty.cxx (Dave Freese, W1HKJ, GPL-3.0-or-later).
// On DIGU with Reverse off, symbol 1 is mark at center + shift/2 and symbol 0
// is space at center - shift/2. The start bit is space. The stop bit is mark.
// Baudot letters and US TTY figures are the tables in that file.

#include "RttyModem.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kBaud = 45.45;
constexpr double kShift = 170.0;
constexpr int kRate = 48000;
constexpr int kLetters = 0;
constexpr int kFigures = 1;

const char kLettersTab[32] = {
    '\0', 'E',  '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
    'T',  'Z',  'L',  'W', 'H', 'Y', 'P', 'Q', 'O',  'B', 'G', ' ', 'M', 'X', 'V', ' ',
};

const char kFiguresTab[32] = {
    '\0', '3',  '\n', '-', ' ', '\a', '8', '7', '\r', '$', '4', '\'', ',', '!', ':', '(',
    '5',  '"',  ')',  '2', '#', '6',  '0', '1', '9',  '?', '&', ' ',  '.', '/', ';', ' ',
};

int bitSamples(int rate) {
  return std::max(1, int(std::lround(double(rate) / kBaud)));
}

int stopSamples(int rate) {
  return std::max(1, int(std::lround(1.5 * double(rate) / kBaud)));
}

int tonePair(int center, bool mark) {
  const int half = int(kShift / 2.0);
  return mark ? center + half : center - half;
}

struct Tx {
  std::vector<float> samples;
  std::vector<int> echoAt;
  double phase = 0.0;
  int shift = kLetters;
  int center = 1500;

  void tone(bool mark, int count) {
    const double step = 2.0 * 3.141592653589793 * double(tonePair(center, mark)) / double(kRate);
    for (int i = 0; i < count; ++i) {
      samples.push_back(float(0.6 * std::sin(phase)));
      phase += step;
      if (phase > 6.283185307179586)
        phase -= 6.283185307179586;
    }
  }

  void code(int five) {
    tone(false, bitSamples(kRate));
    for (int i = 0; i < 5; ++i)
      tone(((five >> i) & 1) != 0, bitSamples(kRate));
    tone(true, stopSamples(kRate));
  }

  void put(int five) {
    if (five == 0x1F)
      shift = kLetters;
    else if (five == 0x1B)
      shift = kFigures;
    code(five);
  }
};

int encodeChar(QChar ch, int* shiftOut) {
  if (ch == QLatin1Char(' ')) {
    *shiftOut = kLetters;
    return 0x04;
  }
  const QChar upper = ch.toUpper();
  const char ascii = upper.unicode() < 128 ? char(upper.unicode()) : 0;
  if (ascii == 0)
    return -1;
  for (int i = 0; i < 32; ++i) {
    if (kLettersTab[i] == ascii && kLettersTab[i] != '\0' && kLettersTab[i] != ' ') {
      *shiftOut = kLetters;
      return i;
    }
  }
  const char fig = ch.unicode() < 128 ? char(ch.unicode()) : 0;
  for (int i = 0; i < 32; ++i) {
    if (kFiguresTab[i] == fig && kFiguresTab[i] != '\0' && kFiguresTab[i] != ' ' &&
        kFiguresTab[i] != '\n' && kFiguresTab[i] != '\r') {
      *shiftOut = kFigures;
      return i;
    }
  }
  return -1;
}

QString decodeCode(int code, int* shift) {
  code &= 0x1F;
  if (code == 0x1F) {
    *shift = kLetters;
    return {};
  }
  if (code == 0x1B) {
    *shift = kFigures;
    return {};
  }
  if (code == 0x04) {
    *shift = kLetters;
    return QStringLiteral(" ");
  }
  const char glyph = *shift == kFigures ? kFiguresTab[code] : kLettersTab[code];
  if (glyph == '\0' || glyph == '\a')
    return {};
  return QString(QChar::fromLatin1(glyph));
}

struct Rx {
  int rate = kRate;
  int center = 1500;
  bool armed = true;
  bool squelchOn = false;
  int squelch = 10;
  double phaseM = 0.0;
  double phaseS = 0.0;
  double mI = 0.0;
  double mQ = 0.0;
  double sI = 0.0;
  double sQ = 0.0;
  bool prevMark = true;
  bool inChar = false;
  int samplesIn = 0;
  int nextAt = 0;
  int bitN = 0;
  int code = 0;
  int shift = kLetters;
  double agc = 1e-8;
  double charPower = 0.0;
  int charSamples = 0;

  bool markNow() const {
    const double mark = mI * mI + mQ * mQ;
    const double space = sI * sI + sQ * sQ;
    return mark >= space;
  }

  QString push(float sample) {
    const double a = 1.0 - std::exp(-2.0 * 3.141592653589793 * 60.0 / double(std::max(1, rate)));
    const double stepM = 2.0 * 3.141592653589793 * double(tonePair(center, true)) / double(rate);
    const double stepS = 2.0 * 3.141592653589793 * double(tonePair(center, false)) / double(rate);
    const double cM = std::cos(phaseM);
    const double sM = std::sin(phaseM);
    const double cS = std::cos(phaseS);
    const double sS = std::sin(phaseS);
    phaseM += stepM;
    phaseS += stepS;
    if (phaseM > 6.283185307179586)
      phaseM -= 6.283185307179586;
    if (phaseS > 6.283185307179586)
      phaseS -= 6.283185307179586;
    mI += a * (sample * cM - mI);
    mQ += a * (sample * sM - mQ);
    sI += a * (sample * cS - sI);
    sQ += a * (sample * sS - sQ);
    const bool mark = markNow();
    const double power = mI * mI + mQ * mQ + sI * sI + sQ * sQ;
    if (power > agc)
      agc = power;
    else
      agc *= 0.9995;

    if (!inChar) {
      if (prevMark && !mark && power > agc * 0.05)
        begin();
      prevMark = mark;
      return {};
    }

    ++samplesIn;
    charPower += power;
    ++charSamples;
    if (samplesIn < nextAt) {
      prevMark = mark;
      return {};
    }
    if (bitN < 5) {
      if (mark)
        code |= 1 << bitN;
      ++bitN;
      if (bitN == 5)
        nextAt = (bitSamples(rate) * 27) / 4;
      else
        nextAt += bitSamples(rate);
      prevMark = mark;
      return {};
    }
    inChar = false;
    prevMark = mark;
    if (!mark)
      return {};
    const double mean = charSamples > 0 ? charPower / double(charSamples) : 0.0;
    const int metric = int(std::lround(100.0 * mean / agc));
    if (squelchOn && metric <= squelch)
      return {};
    return decodeCode(code, &shift);
  }

  void begin() {
    inChar = true;
    samplesIn = 0;
    bitN = 0;
    code = 0;
    nextAt = (3 * bitSamples(rate)) / 2;
    charPower = 0.0;
    charSamples = 0;
  }

  void reset() {
    phaseM = phaseS = 0.0;
    mI = mQ = sI = sQ = 0.0;
    prevMark = true;
    inChar = false;
    samplesIn = 0;
    shift = kLetters;
    agc = 1e-8;
    charPower = 0.0;
    charSamples = 0;
  }
};

}  // namespace

RttyPlan planRtty(const QString& text, int centerHz) {
  Tx tx;
  tx.center = std::clamp(centerHz, 200, 3000);
  tx.echoAt.assign(static_cast<size_t>(text.size()), -1);
  tx.tone(true, bitSamples(kRate) * 2);
  tx.put(0x1F);
  tx.put(0x1F);
  for (int i = 0; i < text.size(); ++i) {
    const QChar ch = text.at(i);
    tx.echoAt[static_cast<size_t>(i)] = int(tx.samples.size());
    if (ch == QLatin1Char('\r')) {
      tx.put(0x08);
      continue;
    }
    if (ch == QLatin1Char('\n')) {
      tx.put(0x02);
      continue;
    }
    int want = kLetters;
    const int five = encodeChar(ch, &want);
    if (five < 0)
      continue;
    if (ch == QLatin1Char(' ')) {
      tx.put(0x1F);
      tx.put(0x04);
      continue;
    }
    if (want != tx.shift)
      tx.put(want == kFigures ? 0x1B : 0x1F);
    tx.put(five);
  }
  tx.put(0x08);
  tx.put(0x02);
  RttyPlan plan;
  plan.samples.swap(tx.samples);
  plan.echoAt.swap(tx.echoAt);
  return plan;
}

QString rttyDecode(const float* samples, int count, int sampleRateHz, int centerHz) {
  if (!samples || count < 1 || sampleRateHz < 1000)
    return {};
  Rx rx;
  rx.rate = sampleRateHz;
  rx.center = std::clamp(centerHz, 200, 3000);
  QString out;
  for (int i = 0; i < count; ++i)
    out += rx.push(samples[i]);
  return out;
}

struct RttyEngine::Private : Rx {};

RttyEngine::RttyEngine(QObject* parent) : QObject(parent), d_(new Private) {
  d_->armed = false;
}

RttyEngine::~RttyEngine() {
  delete d_;
}

void RttyEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  d_->reset();
}

void RttyEngine::setToneHz(int hz) {
  d_->center = std::clamp(hz, 200, 3000);
}

void RttyEngine::setSquelch(bool on, int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  d_->squelchOn = on;
  d_->squelch = level;
}

void RttyEngine::clear() {
  d_->reset();
}

void RttyEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000)
    return;
  d_->rate = sampleRateHz;
  QString got;
  for (int i = 0; i < count; ++i)
    got += d_->push(samples[i]);
  if (!got.isEmpty())
    emit textReady(got);
}
