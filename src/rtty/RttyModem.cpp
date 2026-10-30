// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mark/space pair from fldigi rtty.cxx (Dave Freese, W1HKJ, GPL-3.0-or-later).
// On DIGU with Reverse off, symbol 1 is mark at center + shift/2 and symbol 0
// is space at center - shift/2. Reverse flips that symbol before the tone is
// chosen. The start bit is space. The stop bit is mark. Baudot letters, US
// TTY figures, and ITA-2 figures are the tables in that file. Shaped keying
// is SymbolShaper plus two oscillators, also from that file.

#include "RttyModem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

namespace {

constexpr int kRate = 48000;
constexpr int kLetters = 0;
constexpr int kFigures = 1;
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 6.283185307179586;

double snapList(double asked, const double* list, int count, double fallback, double tol) {
  double best = fallback;
  double err = 1.0e9;
  for (int i = 0; i < count; ++i) {
    const double delta = std::fabs(list[i] - asked);
    if (delta < err) {
      err = delta;
      best = list[i];
    }
  }
  return err <= tol ? best : fallback;
}

double snapBaud(double baud) {
  return snapList(baud, kRttyBauds, kRttyBaudCount, 45.45, 0.02);
}

double snapShift(double shift) {
  return snapList(shift, kRttyShifts, kRttyShiftCount, 170.0, 0.6);
}

int filtLenFor(double baud) {
  // Fldigi FILTLEN[], same order as BAUD[]. 45 through 110 stay at 512.
  static const int kLens[] = {512, 512, 512, 512, 512, 512, 512, 256, 128, 64};
  const double snapped = snapBaud(baud);
  for (int i = 0; i < kRttyBaudCount; ++i) {
    if (std::fabs(kRttyBauds[i] - snapped) < 0.02)
      return kLens[i];
  }
  return 512;
}

const char kLettersTab[32] = {
    '\0', 'E',  '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
    'T',  'Z',  'L',  'W', 'H', 'Y', 'P', 'Q', 'O',  'B', 'G', ' ', 'M', 'X', 'V', ' ',
};

const char kFiguresTab[32] = {
    '\0', '3',  '\n', '-', ' ', '\a', '8', '7', '\r', '$', '4', '\'', ',', '!', ':', '(',
    '5',  '"',  ')',  '2', '#', '6',  '0', '1', '9',  '?', '&', ' ',  '.', '/', ';', ' ',
};

// Fldigi ITA2_figures. Indices 5, 9, 11, 17, and 30 differ from the US set.
const char kIta2Tab[32] = {
    '\0', '3',  '\n', '-', ' ', '\'', '8', '7', '\r', ' ', '4', '\a', ',', '!', ':', '(',
    '5',  '+',  ')',  '2', '#', '6',  '0', '1', '9',  '?', '&', ' ',  '.', '/', '=', ' ',
};

// Fldigi misc.h rcos, alpha 1. Raised-cosine pulse used by SymbolShaper.
double rttyRcos(double t, double width) {
  constexpr double alpha = 1.0;
  if (t == 0.0)
    return 1.0;
  const double taT = width / (2.0 * alpha);
  if (std::fabs(t) == taT)
    return (alpha / 2.0) * std::sin(kPi / (2.0 * alpha));
  return (std::sin(kPi * t / width) / (kPi * t / width)) * std::cos(alpha * kPi * t / width) /
         (1.0 - (t / taT) * (t / taT));
}

// Fldigi SymbolShaper. Six overlapping sinc integrators. At 48 kHz the table
// is several thousand samples, so a free integrator is one whose counter has
// reached the table length. Fldigi's reset uses 1024 because its modem rate
// is 8 kHz; that value is still inside the table here and would drop the
// first edge.
struct SymbolShaper {
  int tableSize = 1;
  std::vector<double> sinc;
  bool state = false;
  double acc = 0.0;
  int c0 = 1;
  int c1 = 1;
  int c2 = 1;
  int c3 = 1;
  int c4 = 1;
  int c5 = 1;
  double f0 = 0.0;
  double f1 = 0.0;
  double f2 = 0.0;
  double f3 = 0.0;
  double f4 = 0.0;
  double f5 = 0.0;

  void idle() {
    state = false;
    acc = 0.0;
    c0 = c1 = c2 = c3 = c4 = c5 = tableSize;
    f0 = f1 = f2 = f3 = f4 = f5 = 0.0;
  }

  void preset(double baud, double sampleRate) {
    baud = std::max(1.0, baud);
    sampleRate = std::max(1.0, sampleRate);
    tableSize = std::max(8, int(std::lround(sampleRate / baud * 5.49)));
    sinc.assign(static_cast<size_t>(tableSize), 0.0);
    const int offset = tableSize / 2;
    const double width = (1.0 / 1.568) * sampleRate / (baud * 2.0);
    long double sum = 0.0;
    for (int x = 0; x < tableSize; ++x) {
      sinc[static_cast<size_t>(x)] = rttyRcos(double(x - offset), width);
      sum += sinc[static_cast<size_t>(x)];
    }
    if (sum != 0.0) {
      for (double& value : sinc)
        value *= 1.0 / double(sum);
    }
    idle();
  }

  double update(bool next) {
    if (state != next) {
      state = next;
      const double factor = next ? 1.0 : -1.0;
      if (c0 >= tableSize) {
        c0 = 0;
        f0 = factor;
      } else if (c1 >= tableSize) {
        c1 = 0;
        f1 = factor;
      } else if (c2 >= tableSize) {
        c2 = 0;
        f2 = factor;
      } else if (c3 >= tableSize) {
        c3 = 0;
        f3 = factor;
      } else if (c4 >= tableSize) {
        c4 = 0;
        f4 = factor;
      } else if (c5 >= tableSize) {
        c5 = 0;
        f5 = factor;
      }
    }
    auto step = [&](int& counter, double factor) {
      if (counter < tableSize)
        acc += factor * sinc[static_cast<size_t>(counter++)];
    };
    step(c0, f0);
    step(c1, f1);
    step(c2, f2);
    step(c3, f3);
    step(c4, f4);
    step(c5, f5);
    return acc / std::sqrt(2.0);
  }
};

struct Oscillator {
  double phase = 0.0;
  double rate = 48000.0;

  double update(double frequency) {
    phase += frequency / rate * kTwoPi;
    if (phase > kTwoPi)
      phase -= kTwoPi;
    return std::sin(phase);
  }
};

struct Tx {
  std::vector<float> samples;
  std::vector<int> echoAt;
  double phase = 0.0;
  int shift = kLetters;
  int center = 1500;
  RttyOptions opt;
  const char* figures = kFiguresTab;
  double maxamp = 0.0;
  SymbolShaper shaper1;
  SymbolShaper shaper2;
  Oscillator osc1;
  Oscillator osc2;

  int bitCount() const { return std::max(1, int(std::lround(double(kRate) / opt.baud))); }

  int stopCount() const {
    return std::max(1, int(std::lround(1.5 * double(kRate) / opt.baud)));
  }

  void prepare() {
    opt.baud = snapBaud(opt.baud);
    opt.shiftHz = snapShift(opt.shiftHz);
    figures = opt.ita2 ? kIta2Tab : kFiguresTab;
    if (!opt.shaped)
      return;
    // Learn the peak on alternating symbols, then return both shapers to
    // idle and both oscillators to zero. Fldigi's Preset clears the global
    // peak; the peak learned here is kept so the first real edge is scaled.
    shaper1.preset(opt.baud, kRate);
    shaper2.preset(opt.baud, kRate);
    osc1.rate = kRate;
    osc2.rate = kRate;
    const double hi = double(center) + opt.shiftHz / 2.0;
    const double lo = double(center) - opt.shiftHz / 2.0;
    const int span = 3 * bitCount();
    bool sym = false;
    for (int j = 0; j < 100; ++j) {
      sym = !sym;
      for (int i = 0; i < span; ++i) {
        const double mark = shaper1.update(sym) * osc1.update(hi);
        const double space = shaper2.update(!sym) * osc2.update(lo);
        maxamp = std::max(maxamp, std::fabs(mark + space));
      }
    }
    shaper1.idle();
    shaper2.idle();
    osc1.phase = 0.0;
    osc2.phase = 0.0;
    if (maxamp < 1.0e-12)
      maxamp = 1.0;
  }

  void tone(bool mark, int count) {
    // Fldigi send_symbol: reverse flips the symbol, then the high tone is
    // that symbol. Shaped keying keeps the mark oscillator on the high tone
    // and the space oscillator on the low tone.
    bool symbol = mark;
    if (opt.reverse)
      symbol = !symbol;
    if (!opt.shaped) {
      const double hz = symbol ? double(center) + opt.shiftHz / 2.0 : double(center) - opt.shiftHz / 2.0;
      const double step = kTwoPi * hz / double(kRate);
      for (int i = 0; i < count; ++i) {
        samples.push_back(float(0.6 * std::sin(phase)));
        phase += step;
        if (phase > kTwoPi)
          phase -= kTwoPi;
      }
      return;
    }
    const double hi = double(center) + opt.shiftHz / 2.0;
    const double lo = double(center) - opt.shiftHz / 2.0;
    for (int i = 0; i < count; ++i) {
      const double markAmp = shaper1.update(symbol) * osc1.update(hi);
      const double spaceAmp = shaper2.update(!symbol) * osc2.update(lo);
      const double signal = markAmp + spaceAmp;
      maxamp = std::max(maxamp, std::fabs(signal));
      samples.push_back(float(0.6 * (maxamp > 0.0 ? signal / maxamp : 0.0)));
    }
  }

  void code(int five) {
    tone(false, bitCount());
    for (int i = 0; i < 5; ++i)
      tone(((five >> i) & 1) != 0, bitCount());
    tone(true, stopCount());
  }

  void put(int five) {
    if (five == 0x1F)
      shift = kLetters;
    else if (five == 0x1B)
      shift = kFigures;
    code(five);
  }
};

int encodeChar(QChar ch, int* shiftOut, const char* figures) {
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
    if (figures[i] == fig && figures[i] != '\0' && figures[i] != ' ' && figures[i] != '\n' &&
        figures[i] != '\r') {
      *shiftOut = kFigures;
      return i;
    }
  }
  return -1;
}

QString decodeCode(int code, int* shift, const char* figures) {
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
  const char glyph = *shift == kFigures ? figures[code] : kLettersTab[code];
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
const std::vector<double>& rttyTaps(double baud) {
  baud = snapBaud(baud);
  const int flen = filtLenFor(baud);
  const int key = flen * 100000 + int(std::lround(baud * 100.0));
  static std::mutex gate;
  static std::map<int, std::vector<double>> cache;
  std::lock_guard<std::mutex> lock(gate);
  const auto found = cache.find(key);
  if (found != cache.end())
    return found->second;
  constexpr double rate = 8000.0;
  const int half = flen / 2;
  const double f = 1.4 * baud / rate;
  auto build = [&]() {
    std::vector<double> hr(flen, 0.0);
    std::vector<double> hi(flen, 0.0);
    for (int i = 0; i < half; ++i) {
      const double x = double(i) / double(half);
      double dht = 1.0;
      if (x > 0.0) {
        if (x > 2.0 * f) {
          dht = 0.0;
        } else {
          dht = std::cos((kPi * x) / (f * 4.0));
          dht *= dht;
          if (i != 0) {
            const double arg = 2.0 * double(i) * f;
            const double s = std::sin(kPi * arg) / (kPi * arg);
            if (std::fabs(s) > 1e-12)
              dht /= s;
          }
        }
      }
      const double ang = double(i) * 0.5 * kPi;
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
        const double ang = kTwoPi * double(k) * double(n) / double(flen);
        acc += hr[static_cast<size_t>(k)] * std::cos(ang) - hi[static_cast<size_t>(k)] * std::sin(ang);
      }
      h[static_cast<size_t>(n)] = acc / double(flen);
    }
    return h;
  };
  return cache.emplace(key, build()).first->second;
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
  double baud = 45.45;
  double shiftHz = 170.0;
  bool reverse = false;
  bool ita2 = false;
  const char* figures = kFiguresTab;
  std::vector<unsigned char> bitBuf;

  void setOptions(const RttyOptions& options) {
    const double nextBaud = snapBaud(options.baud);
    const double nextShift = snapShift(options.shiftHz);
    const bool changed = markFir.taps.empty() || std::fabs(baud - nextBaud) > 1.0e-9 ||
                         std::fabs(shiftHz - nextShift) > 1.0e-6 || reverse != options.reverse ||
                         ita2 != options.ita2;
    baud = nextBaud;
    shiftHz = nextShift;
    reverse = options.reverse;
    ita2 = options.ita2;
    figures = ita2 ? kIta2Tab : kFiguresTab;
    symbollen = std::max(8, int(std::lround(8000.0 / baud)));
    if (changed)
      reset();
  }

  Rx() { setOptions(RttyOptions{}); }

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
    const std::vector<double>& taps = rttyTaps(baud);
    markFir.init(taps);
    spaceFir.init(taps);
    noiseFir.init(taps);
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
  // only; Reverse flips the bit before it reaches the state machine, not the
  // mixer. The cursor moves when the tone crosses a whole Hz.
  void trackTone() {
    if (!afcOn || !haveTone)
      return;
    if (!(metric > double(squelch) || !squelchOn))
      return;
    const double real = markZI * markPI + markZQ * markPQ;
    const double imag = markZI * markPQ - markZQ * markPI;
    double ferr = (kTwoPi * 8000.0 / baud) * std::atan2(imag, real);
    if (std::fabs(ferr) > baud / 2.0)
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
    const QString glyph = decodeCode(code, &shift, figures);
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
    mix(phaseM, tuneHz + shiftHz / 2.0, &mi, &mq);
    mix(phaseS, tuneHz - shiftHz / 2.0, &si, &sq);
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
    const bool bit = v3 > 0.0;
    return onBit(reverse ? !bit : bit);
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

RttyPlan planRtty(const QString& text, int centerHz, const RttyOptions& options) {
  Tx tx;
  tx.opt = options;
  tx.center = std::clamp(centerHz, 200, 3000);
  tx.prepare();
  tx.echoAt.assign(static_cast<size_t>(text.size()), -1);
  // Shaped keying rises over about three symbols. Hold mark long enough
  // that the first start bit is a settled edge. Hard FSK keeps two bits,
  // which is what the tone-power windows measure.
  tx.tone(true, tx.bitCount() * (tx.opt.shaped ? 6 : 2));
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
    const int five = encodeChar(ch, &want, tx.figures);
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
                   bool squelchOn, int squelchLevel, bool afcOn, const RttyOptions& options) {
  if (!samples || count < 1 || sampleRateHz < 1000)
    return {};
  Rx rx;
  rx.rate = sampleRateHz;
  rx.center = std::clamp(centerHz, 200, 3000);
  rx.setOptions(options);
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

void RttyEngine::setOptions(const RttyOptions& options) {
  d_->setOptions(options);
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
