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
#include <cstdlib>
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

double rttyDecay(double average, double input, double weight) {
  if (weight <= 1.0)
    return input;
  return average + (input - average) / weight;
}

// Impulse response of fldigi fftfilt::rtty_filter at 8 kHz, 512 taps.
// Raised cosine, cutoff scaled by 1.4, the setting with the lowest
// character error rate at -9 dB S/N.
const std::vector<double>& rttyTaps() {
  static const std::vector<double> taps = [] {
    constexpr int flen = 512;
    constexpr double baud = 45.45;
    constexpr double rate = 8000.0;
    constexpr double pi = 3.141592653589793;
    const int half = flen / 2;
    const double f = 1.4 * baud / rate;
    std::vector<double> hr(flen, 0.0);
    std::vector<double> hi(flen, 0.0);
    for (int i = 0; i < half; ++i) {
      const double x = double(i) / double(half);
      double dht = 1.0;
      if (x > 0.0) {
        if (x > 2.0 * f) {
          dht = 0.0;
        } else {
          dht = std::cos((pi * x) / (f * 4.0));
          dht *= dht;
          if (i != 0) {
            const double arg = 2.0 * double(i) * f;
            const double s = std::sin(pi * arg) / (pi * arg);
            if (std::fabs(s) > 1e-12)
              dht /= s;
          }
        }
      }
      const double ang = double(i) * 0.5 * pi;
      const double c = std::cos(ang);
      const double sn = std::sin(ang);
      hr[static_cast<size_t>(i)] = dht * c;
      hi[static_cast<size_t>(i)] = -dht * sn;
      const int j = (flen - i) % flen;
      if (j != i) {
        hr[static_cast<size_t>(j)] = dht * c;
        hi[static_cast<size_t>(j)] = dht * sn;
      }
    }
    std::vector<double> h(flen, 0.0);
    for (int n = 0; n < flen; ++n) {
      double acc = 0.0;
      for (int k = 0; k < flen; ++k) {
        const double ang = 2.0 * pi * double(k) * double(n) / double(flen);
        acc += hr[static_cast<size_t>(k)] * std::cos(ang) - hi[static_cast<size_t>(k)] * std::sin(ang);
      }
      h[static_cast<size_t>(n)] = acc / double(flen);
    }
    return h;
  }();
  return taps;
}

struct ToneFir {
  std::vector<double> taps;
  std::vector<double> bi;
  std::vector<double> bq;
  int pos = 0;

  void init(const std::vector<double>& in) {
    taps = in;
    bi.assign(taps.size(), 0.0);
    bq.assign(taps.size(), 0.0);
    pos = 0;
  }

  void run(double i, double q, double* oi, double* oq) {
    if (taps.empty()) {
      *oi = i;
      *oq = q;
      return;
    }
    bi[static_cast<size_t>(pos)] = i;
    bq[static_cast<size_t>(pos)] = q;
    const int n = int(taps.size());
    pos = (pos + 1) % n;
    double si = 0.0;
    double sq = 0.0;
    int p = pos;
    for (int k = 0; k < n; ++k) {
      si += taps[static_cast<size_t>(k)] * bi[static_cast<size_t>(p)];
      sq += taps[static_cast<size_t>(k)] * bq[static_cast<size_t>(p)];
      p = (p + 1) % n;
    }
    *oi = si;
    *oq = sq;
  }
};

// fldigi rtty::rx at 8 kHz. Mark and space are mixed to baseband and low-passed
// with the same raised-cosine FIR. Optimal ATC (the v3 expression) decides
// the bit. A one-symbol shift register confirms the start bit before a
// character is accepted.
struct Rx {
  enum State { Idle, Start, Data, Stop };

  int rate = kRate;
  int center = 1500;
  bool armed = true;
  bool squelchOn = false;
  bool afcOn = false;
  int squelch = 10;
  double tuneHz = 1500.0;
  double freqerr = 0.0;
  bool haveTone = false;
  double markZI = 0.0;
  double markZQ = 0.0;
  double markPI = 0.0;
  double markPQ = 0.0;
  int trackHz = -1;
  int symbollen = 176;
  int decimFactor = -1;
  int decimPhase = 0;
  int decimPos = 0;
  std::vector<double> decimTaps;
  std::vector<double> decimHist;
  double bucket = 0.0;
  int bucketCount = 0;
  double held = 0.0;
  double phaseM = 0.0;
  double phaseS = 0.0;
  double phaseN = 0.0;
  ToneFir markFir;
  ToneFir spaceFir;
  ToneFir noiseFir;
  double markEnv = 0.0;
  double spaceEnv = 0.0;
  double markNoise = 1.0;
  double spaceNoise = 1.0;
  double sigTrack = 1e-8;
  double noiseTrack = 1e-8;
  double metric = 0.0;
  State state = Idle;
  int counter = 0;
  int bitCount = 0;
  int rxData = 0;
  int shift = kLetters;
  int lastChar = 0;
  std::vector<unsigned char> bitBuf;

  Rx() {
    symbollen = std::max(8, int(std::lround(8000.0 / kBaud)));
    const std::vector<double>& taps = rttyTaps();
    markFir.init(taps);
    spaceFir.init(taps);
    noiseFir.init(taps);
    bitBuf.assign(static_cast<size_t>(symbollen), 1);
  }

  void armDecimator() {
    decimFactor = (rate % 8000 == 0) ? rate / 8000 : 0;
    decimPhase = 0;
    decimPos = 0;
    bucket = 0.0;
    bucketCount = 0;
    held = 0.0;
    if (decimFactor > 1) {
      const int taps = 16 * decimFactor + 1;
      const int half = taps / 2;
      constexpr double pi = 3.141592653589793;
      const double fc = 3600.0 / (8000.0 * double(decimFactor));
      decimTaps.assign(static_cast<size_t>(taps), 0.0);
      double sum = 0.0;
      for (int k = 0; k < taps; ++k) {
        const double u = double(k - half);
        const double s = u == 0.0 ? 2.0 * fc : std::sin(2.0 * pi * fc * u) / (pi * u);
        const double w = 0.42 - 0.5 * std::cos(2.0 * pi * double(k) / double(taps - 1)) +
                         0.08 * std::cos(4.0 * pi * double(k) / double(taps - 1));
        decimTaps[static_cast<size_t>(k)] = s * w;
        sum += s * w;
      }
      if (sum != 0.0) {
        for (double& v : decimTaps)
          v /= sum;
      }
      decimHist.assign(decimTaps.size(), 0.0);
    } else {
      decimTaps.clear();
      decimHist.clear();
    }
  }

  void reset() {
    phaseM = phaseS = phaseN = 0.0;
    markFir.init(rttyTaps());
    spaceFir.init(rttyTaps());
    noiseFir.init(rttyTaps());
    markEnv = spaceEnv = 0.0;
    markNoise = spaceNoise = 1.0;
    sigTrack = noiseTrack = 1e-8;
    metric = 0.0;
    state = Idle;
    counter = 0;
    bitCount = 0;
    rxData = 0;
    shift = kLetters;
    lastChar = 0;
    bitBuf.assign(static_cast<size_t>(symbollen), 1);
    decimPhase = 0;
    decimPos = 0;
    bucket = 0.0;
    bucketCount = 0;
    held = 0.0;
    if (!decimHist.empty())
      std::fill(decimHist.begin(), decimHist.end(), 0.0);
    tuneHz = double(center);
    freqerr = 0.0;
    haveTone = false;
    markZI = markZQ = markPI = markPQ = 0.0;
    trackHz = -1;
  }

  // fldigi freqerr after a good character. Slow AFC (weight 8). Mark phase
  // only; Reverse is not on the menu. The cursor moves when the tone crosses
  // a whole Hz.
  void trackTone() {
    if (!afcOn || !haveTone)
      return;
    if (!(metric > double(squelch) || !squelchOn))
      return;
    const double real = markZI * markPI + markZQ * markPQ;
    const double imag = markZI * markPQ - markZQ * markPI;
    double ferr = (2.0 * 3.141592653589793 * 8000.0 / kBaud) * std::atan2(imag, real);
    if (std::fabs(ferr) > kBaud / 2.0)
      ferr = 0.0;
    freqerr = rttyDecay(freqerr, ferr / 8.0, 8.0);
    tuneHz -= freqerr;
    if (tuneHz < 200.0)
      tuneHz = 200.0;
    if (tuneHz > 3000.0)
      tuneHz = 3000.0;
    const int shown = int(std::lround(tuneHz));
    if (shown != center) {
      center = shown;
      trackHz = shown;
    }
  }

  QString accept(int code) {
    const QString glyph = decodeCode(code, &shift);
    if (glyph.isEmpty())
      return {};
    const QChar ch = glyph.at(0);
    if (ch == QLatin1Char('\r')) {
      lastChar = '\r';
      return {};
    }
    if (ch == QLatin1Char('\n')) {
      if (lastChar == '\n')
        return {};
      lastChar = '\n';
      return QStringLiteral("\n");
    }
    lastChar = ch.unicode();
    return glyph;
  }

  bool straddles(int* correction) const {
    if (!bitBuf.front() || bitBuf.back())
      return false;
    int marks = 0;
    for (unsigned char bit : bitBuf)
      marks += bit ? 1 : 0;
    const int mid = symbollen / 2;
    if (std::abs(mid - marks) >= 6)
      return false;
    *correction = marks;
    return true;
  }

  bool middleMark() const { return bitBuf[static_cast<size_t>(symbollen / 2)] != 0; }

  QString onBit(bool mark) {
    for (int i = 1; i < symbollen; ++i)
      bitBuf[static_cast<size_t>(i - 1)] = bitBuf[static_cast<size_t>(i)];
    bitBuf.back() = mark ? 1 : 0;
    switch (state) {
    case Idle: {
      int correction = 0;
      if (straddles(&correction)) {
        state = Start;
        counter = correction;
      }
      break;
    }
    case Start:
      if (--counter == 0) {
        if (!middleMark()) {
          state = Data;
          counter = symbollen;
          bitCount = 0;
          rxData = 0;
        } else {
          state = Idle;
        }
      }
      break;
    case Data:
      if (--counter == 0) {
        if (middleMark())
          rxData |= 1 << bitCount;
        ++bitCount;
        counter = symbollen;
      }
      if (bitCount == 5)
        state = Stop;
      break;
    case Stop:
      if (--counter == 0) {
        QString out;
        if (middleMark() && (!squelchOn || metric >= double(squelch))) {
          out = accept(rxData);
          trackTone();
        }
        state = Idle;
        return out;
      }
      break;
    }
    return {};
  }

  QString on8k(double sample) {
    constexpr double pi = 3.141592653589793;
    constexpr double twoPi = 2.0 * pi;
    auto mix = [&](double& phase, double hz, double* i, double* q) {
      const double c = std::cos(phase);
      const double s = std::sin(phase);
      phase -= twoPi * hz / 8000.0;
      if (phase < -twoPi)
        phase += twoPi;
      *i = sample * c;
      *q = sample * s;
    };
    double mi = 0, mq = 0, si = 0, sq = 0, ni = 0, nq = 0;
    mix(phaseM, tuneHz + kShift / 2.0, &mi, &mq);
    mix(phaseS, tuneHz - kShift / 2.0, &si, &sq);
    mix(phaseN, tuneHz, &ni, &nq);
    markFir.run(mi, mq, &mi, &mq);
    spaceFir.run(si, sq, &si, &sq);
    noiseFir.run(ni, nq, &ni, &nq);
    const double markMag = std::sqrt(mi * mi + mq * mq);
    const double spaceMag = std::sqrt(si * si + sq * sq);
    const double noiseMag = std::sqrt(ni * ni + nq * nq);
    const double atk = std::max(1.0, double(symbollen) / 4.0);
    markEnv = rttyDecay(markEnv, markMag, markMag > markEnv ? atk : double(symbollen) * 16.0);
    spaceEnv = rttyDecay(spaceEnv, spaceMag, spaceMag > spaceEnv ? atk : double(symbollen) * 16.0);
    markNoise = rttyDecay(markNoise, markMag, markMag < markNoise ? atk : double(symbollen) * 48.0);
    spaceNoise = rttyDecay(spaceNoise, spaceMag, spaceMag < spaceNoise ? atk : double(symbollen) * 48.0);
    const double floor = std::min(markNoise, spaceNoise);
    double markClip = markMag > markEnv ? markEnv : markMag;
    double spaceClip = spaceMag > spaceEnv ? spaceEnv : spaceMag;
    if (markClip < floor)
      markClip = floor;
    if (spaceClip < floor)
      spaceClip = floor;
    const double markRise = markEnv - floor;
    const double spaceRise = spaceEnv - floor;
    const double v3 = (markClip - floor) * markRise - (spaceClip - floor) * spaceRise -
                      0.25 * (markRise * markRise - spaceRise * spaceRise);
    const double sig = markMag * markMag + spaceMag * spaceMag;
    const double noise = noiseMag * noiseMag + 1e-18;
    sigTrack = rttyDecay(sigTrack, sig, sig > sigTrack ? 64.0 : 256.0);
    noiseTrack = rttyDecay(noiseTrack, noise, 512.0);
    metric = sigTrack / noiseTrack;
    if (metric < 0.0)
      metric = 0.0;
    if (metric > 100.0)
      metric = 100.0;
    markPI = markZI;
    markPQ = markZQ;
    markZI = mi;
    markZQ = mq;
    haveTone = true;
    return onBit(v3 > 0.0);
  }

  QString push(float sample) {
    const int factor = (rate % 8000 == 0) ? rate / 8000 : 0;
    if (factor != decimFactor)
      armDecimator();
    if (factor == 1)
      return on8k(sample);
    if (factor > 1) {
      const int taps = int(decimTaps.size());
      if (taps < 1)
        return {};
      decimHist[static_cast<size_t>(decimPos)] = sample;
      decimPos = (decimPos + 1) % taps;
      if (++decimPhase < factor)
        return {};
      decimPhase = 0;
      double acc = 0.0;
      int p = decimPos;
      for (int k = 0; k < taps; ++k) {
        acc += decimTaps[static_cast<size_t>(k)] * decimHist[static_cast<size_t>(p)];
        p = (p + 1) % taps;
      }
      return on8k(acc);
    }
    bucket += sample;
    ++bucketCount;
    held += 8000.0 / double(std::max(1, rate));
    if (held < 1.0)
      return {};
    const double mean = bucket / double(std::max(1, bucketCount));
    bucket = 0.0;
    bucketCount = 0;
    held -= 1.0;
    return on8k(mean);
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

QString rttyDecode(const float* samples, int count, int sampleRateHz, int centerHz,
                   bool squelchOn, int squelchLevel, bool afcOn) {
  if (!samples || count < 1 || sampleRateHz < 1000)
    return {};
  Rx rx;
  rx.rate = sampleRateHz;
  rx.center = std::clamp(centerHz, 200, 3000);
  rx.tuneHz = double(rx.center);
  rx.afcOn = afcOn;
  rx.squelchOn = squelchOn;
  if (squelchLevel < 0)
    squelchLevel = 0;
  if (squelchLevel > 100)
    squelchLevel = 100;
  rx.squelch = squelchLevel;
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
  hz = std::clamp(hz, 200, 3000);
  if (hz == d_->center && std::abs(d_->tuneHz - double(hz)) < 1.0)
    return;
  d_->center = hz;
  d_->tuneHz = double(hz);
  d_->freqerr = 0.0;
}

void RttyEngine::setSquelch(bool on, int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  d_->squelchOn = on;
  d_->squelch = level;
}

void RttyEngine::setAfc(bool on) {
  d_->afcOn = on;
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
  if (d_->trackHz >= 0) {
    const int hz = d_->trackHz;
    d_->trackHz = -1;
    emit toneTracked(hz);
  }
}
