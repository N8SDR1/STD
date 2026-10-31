// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DigitalMode.h"
#include "TxWave.h"
#include "ft8/ft8_api.h"
#include "hell/HellFont.h"
#include "dominoex/DominoModem.h"
#include "thor/ThorModem.h"
#include "mfsk/MfskModem.h"
#include "mt63/Mt63Modem.h"
#include "olivia/OliviaModem.h"
#include "psk/PskModem.h"
#include "rtty/RttyModem.h"
#include "js8/Js8Modem.h"
#include "wspr/WsprModem.h"

#include <QCoreApplication>
#include <QString>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

constexpr int kRate = 48000;
// MSHV's TCI sender divides the generator ints by 8380000 (genft8 writes
// 8380000*sin, full 24-bit is 8388607). The same scale keeps a tone at
// ±1, which is what MSHV puts on the wire at TX level 100.
constexpr float kScale = 8380000.0f;

void fromInts(const int* samples, int count, std::vector<float>* out) {
  out->resize(static_cast<size_t>(std::max(0, count)));
  for (int i = 0; i < count; ++i)
    (*out)[static_cast<size_t>(i)] = std::clamp(samples[i] / kScale, -1.0f, 1.0f);
}

using GenerateFn = int (*)(const char*, double, int*, int);

GenerateFn generator(const char* name) {
  const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/ft8dec.dll");
  HMODULE module = GetModuleHandleW(reinterpret_cast<const wchar_t*>(path.utf16()));
  if (!module)
    module = LoadLibraryExW(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module)
    return nullptr;
  return reinterpret_cast<GenerateFn>(GetProcAddress(module, name));
}

bool buildFt(const char* exportName, int capacity, const QString& message, int toneHz,
             std::vector<float>* out) {
  GenerateFn generate = generator(exportName);
  if (!generate)
    return false;
  std::vector<int> wave(static_cast<size_t>(capacity), 0);
  const QByteArray utf8 = message.toUtf8();
  const int n = generate(utf8.constData(), double(toneHz), wave.data(), capacity);
  if (n <= 0)
    return false;
  fromInts(wave.data(), n, out);
  return true;
}

bool buildFt8(const QString& message, int toneHz, std::vector<float>* out) {
  return buildFt("ft8_generate", 840000, message, toneHz, out);
}

bool buildFt4(const QString& message, int toneHz, std::vector<float>* out) {
  return buildFt("ft4_generate", 420000, message, toneHz, out);
}

bool buildFt2(const QString& message, int toneHz, std::vector<float>* out) {
  return buildFt("ft2_generate", 220000, message, toneHz, out);
}

using Jt65GenerateFn = int (*)(const char*, int, int*, int);
using Q65GenerateFn = int (*)(const char*, double, int, int, int*, int);

bool buildJt65(const QString& mode, const QString& message, std::vector<float>* out) {
  const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/ft8dec.dll");
  HMODULE module = GetModuleHandleW(reinterpret_cast<const wchar_t*>(path.utf16()));
  if (!module)
    module = LoadLibraryExW(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module)
    return false;
  auto generate = reinterpret_cast<Jt65GenerateFn>(GetProcAddress(module, "jt65_generate"));
  if (!generate)
    return false;
  constexpr int kCap = 60 * kRate;
  std::vector<int> wave(static_cast<size_t>(kCap), 0);
  const QByteArray utf8 = message.toUtf8();
  const int n = generate(utf8.constData(), jt65Mode65(mode), wave.data(), kCap);
  if (n <= 0)
    return false;
  fromInts(wave.data(), n, out);
  return true;
}

bool buildQ65(const QString& mode, const QString& message, int toneHz, std::vector<float>* out) {
  const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/ft8dec.dll");
  HMODULE module = GetModuleHandleW(reinterpret_cast<const wchar_t*>(path.utf16()));
  if (!module)
    module = LoadLibraryExW(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module)
    return false;
  auto generate = reinterpret_cast<Q65GenerateFn>(GetProcAddress(module, "q65_generate"));
  if (!generate)
    return false;
  constexpr int kPeriod = 30;
  constexpr int kCap = kPeriod * kRate;
  std::vector<int> wave(static_cast<size_t>(kCap), 0);
  const QByteArray utf8 = message.toUtf8();
  const int n = generate(utf8.constData(), double(toneHz), q65Mq65(mode), kPeriod, wave.data(), kCap);
  if (n <= 0)
    return false;
  fromInts(wave.data(), n, out);
  return true;
}

// fldigi feld::get_font_data. -1 when this column and every column to its
// right are blank, which ends the character. Bit 0 of the result is the
// bottom row and is the first pixel in time.
int hellColumnBits(unsigned char c, int column) {
  if (column > 15 || c < ' ' || c > '~')
    return -1;
  const int index = int(c) - int(' ');
  const unsigned mask = 1u << (15 - column);
  unsigned all = 0;
  for (int row = 0; row < kHellFontRows; ++row)
    all |= kHellFont[index][row];
  int bits = 0;
  for (int row = 0; row < kHellFontRows; ++row) {
    if ((kHellFont[index][kHellFontRows - 1 - row] & mask) != 0)
      bits |= 1 << row;
  }
  const unsigned rest = (1u << (15 - column)) - 1u;
  if (bits == 0 && (all & rest) == 0)
    return -1;
  return bits;
}

struct HellTx {
  double columnsPerSec = 17.5;
  bool shaped = true;
  bool fsk = false;
  double shiftHz = 0.0;
};

HellTx hellTxFor(const QString& mode) {
  HellTx tx;
  tx.columnsPerSec = hellColumnRate(mode);
  if (mode == QLatin1String("Feld Hell X5") || mode == QLatin1String("Feld Hell X9"))
    tx.shaped = false;
  if (mode == QLatin1String("FSK Hell")) {
    tx.shaped = false;
    tx.fsk = true;
    tx.shiftHz = 122.5;
  } else if (mode == QLatin1String("FSK Hell-105")) {
    tx.shaped = false;
    tx.fsk = true;
    tx.shiftHz = 55.0;
  } else if (mode == QLatin1String("Hell 80")) {
    tx.shaped = false;
    tx.fsk = true;
    tx.shiftHz = 300.0;
  }
  return tx;
}

// fldigi initKeyWaveform, HellPulseFast default 1: 2 ms raised cosine.
// Index is an 8000 Hz sample. OnShape rises over the first 16. OffShape
// stays full for 16, then falls over the next 16.
double hellEdge(int sample, bool rising) {
  const double idx = double(sample) * 8000.0 / double(kRate);
  auto on = [](double i) {
    if (i <= 0.0)
      return 0.0;
    if (i >= 16.0)
      return 1.0;
    return 0.5 * (1.0 - std::cos(3.141592653589793 * i / 16.0));
  };
  if (rising)
    return on(idx);
  if (idx >= 32.0)
    return 0.0;
  const double src = 31.0 - idx;
  return src < 16.0 ? on(src) : 1.0;
}

bool buildHell(const QString& mode, const QString& message, int toneHz, std::vector<float>* out,
               std::vector<int>* echoAt) {
  // fldigi feld.cxx send_symbol / tx_char (Dave Freese, W1HKJ, GPL-3).
  // Amplitude Hell is on-off keying. Ink is the carrier, paper is silence.
  // The column is 14 pixels. Feld Hell is 17.5 columns/s, 245 pixels/s.
  // Feld and Slow use the 2 ms raised cosine. X5 and X9 are hard keyed.
  // FSK Hell, FSK Hell-105, and Hell 80 keep a tone up and move it by
  // half the shift. Reverse is off, so ink is the lower tone.
  const HellTx timing = hellTxFor(mode);
  const int tone = std::clamp(toneHz, 200, 3000);
  out->clear();
  if (echoAt)
    echoAt->clear();
  double phase = 0.0;
  double counter = 0.0;
  int previous = 0;
  const double step = timing.columnsPerSec * double(kHellFontRows) / double(kRate);
  // Fldigi's OffShape is 32 samples at 8 kHz from the start of the pixel,
  // which is the whole Feld Hell pixel. A Slow Hell pixel is 261 samples,
  // so Fldigi keys the last ink pixel of a stroke for 4 ms and leaves the
  // rest blank. Pixels longer than the shape put the 2 ms fall at the end
  // of the pixel instead. Feld Hell keeps the Fldigi shape sample for sample.
  const bool longPixel = 8000.0 / (timing.columnsPerSec * double(kHellFontRows)) > 40.0;
  auto sendPixel = [&](int current, int next) {
    int n = 0;
    const int length = std::max(1, int(std::ceil((1.0 - counter) / step - 1e-9)));
    for (;;) {
      double amp = 1.0;
      double freq = double(tone);
      if (timing.fsk) {
        freq += (current ? -1.0 : 1.0) * timing.shiftHz / 2.0;
      } else if (!timing.shaped) {
        amp = current ? 1.0 : 0.0;
      } else if (longPixel && current == 1) {
        // Rise over the first 2 ms after a gap, fall over the last 2 ms
        // before one. A lone pixel gets both.
        amp = 1.0;
        if (previous == 0)
          amp = std::min(amp, hellEdge(n, true));
        if (next == 0)
          amp = std::min(amp, hellEdge(length - 1 - n, true));
      } else if (previous == 0 && current == 1) {
        amp = hellEdge(n, true);
      } else if (current == 1 && next == 0) {
        amp = hellEdge(n, false);
      } else {
        amp = current ? 1.0 : 0.0;
      }
      out->push_back(float(0.6 * amp * std::sin(phase)));
      phase += 2.0 * 3.141592653589793 * freq / double(kRate);
      if (phase > 6.283185307179586)
        phase -= 6.283185307179586;
      ++n;
      counter += step;
      if (counter >= 1.0) {
        counter -= 1.0;
        break;
      }
    }
    previous = current;
  };
  auto sendColumn = [&](int bits) {
    for (int row = 0; row < kHellFontRows; ++row) {
      const int current = bits & 1;
      bits >>= 1;
      const int next = bits & 1;
      sendPixel(current, next);
    }
  };
  auto sendChar = [&](unsigned char c) {
    sendColumn(0);
    if (c == ' ') {
      sendColumn(0);
      sendColumn(0);
      sendColumn(0);
    } else {
      for (int column = 0;; ++column) {
        const int bits = hellColumnBits(c, column);
        if (bits < 0)
          break;
        sendColumn(bits);
      }
    }
    sendColumn(0);
  };
  for (int i = 0; i < 3; ++i)
    sendChar('.');
  for (const QChar ch : message) {
    if (echoAt)
      echoAt->push_back(int(out->size()));
    // feld::tx_process maps CR and LF to a space. Any other character
    // outside the font sends only the two blank columns of tx_char.
    if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
      sendChar(' ');
    } else if (ch.unicode() >= ' ' && ch.unicode() <= '~') {
      sendChar(static_cast<unsigned char>(ch.unicode()));
    } else {
      sendColumn(0);
      sendColumn(0);
    }
  }
  for (int i = 0; i < 3; ++i)
    sendChar('.');
  sendChar(' ');
  return !out->empty();
}

}  // namespace

bool buildTxWave(const QString& mode, const QString& message, int toneHz, std::vector<float>* out,
                 std::vector<int>* echoAt, const RttyOptions* rtty, bool startTones) {
  if (!out)
    return false;
  out->clear();
  if (echoAt)
    echoAt->clear();
  const QString text = message.trimmed();
  if (text.isEmpty())
    return false;
  const int tone = std::clamp(toneHz, 100, 5000);
  if (mode == QLatin1String("FT8"))
    return buildFt8(text, tone, out);
  if (mode == QLatin1String("FT4"))
    return buildFt4(text, tone, out);
  if (mode == QLatin1String("FT2"))
    return buildFt2(text, tone, out);
  if (jt65Mode(mode))
    return buildJt65(mode, text, out);
  if (q65Mode(mode))
    return buildQ65(mode, text, tone, out);
  if (hellFamily(mode))
    return buildHell(mode, text, tone, out, echoAt);
  if (rttyMode(mode)) {
    const RttyPlan plan = planRtty(text, tone, rtty ? *rtty : RttyOptions{});
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (keyedPsk(mode)) {
    const PskPlan plan = planPsk(mode, text, tone);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (mfskBandwidthHz(mode) > 0) {
    const MfskPlan plan = planMfsk(mode, text, tone);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (dominoBandwidthHz(mode) > 0) {
    const DominoPlan plan = planDomino(mode, text, tone);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (thorBandwidthHz(mode) > 0) {
    const ThorPlan plan = planThor(mode, text, tone);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (oliviaBandwidthHz(mode) > 0) {
    const OliviaPlan plan = planOlivia(mode, text, tone, startTones);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (js8Mode(mode)) {
    const Js8Plan plan = planJs8(text, tone);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (wsprMode(mode)) {
    const std::vector<float> wave = wsprModulate(text, wsprClampTxHz(tone));
    if (wave.empty())
      return false;
    *out = wave;
    if (echoAt)
      echoAt->clear();
    return true;
  }
  if (mt63BandwidthHz(mode) > 0) {
    const Mt63Plan plan = planMt63(mode, text, tone, startTones);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  return false;
}

int hellFirstInkRow(QChar ch, int column) {
  if (ch.unicode() < ' ' || ch.unicode() > '~')
    return -1;
  const int bits = hellColumnBits(static_cast<unsigned char>(ch.unicode()), column);
  if (bits <= 0)
    return -1;
  for (int row = 0; row < kHellFontRows; ++row) {
    if (((bits >> row) & 1) != 0)
      return row;
  }
  return -1;
}

int textModeSamplesPerChar(const QString& mode) {
  Q_UNUSED(mode);
  return 0;
}

namespace {

// Center of symbol s in the MSHV generator output. genft8 reads dphi
// starting one symbol in, so symbol s sits at (s + 0.5) * 7680.
int symbolCenter(int symbol) {
  return int((symbol + 0.5) * 7680.0);
}

int bestTone(const float* samples, int count, int toneHz, double* ratio) {
  int best = -1;
  double bestMag = 0.0;
  double second = 0.0;
  for (int tone = 0; tone < 8; ++tone) {
    const double freq = double(toneHz) + tone * 6.25;
    double re = 0.0;
    double im = 0.0;
    for (int i = 0; i < count; ++i) {
      const double w = 2.0 * 3.141592653589793 * freq * double(i) / double(kRate);
      const double sample = samples[i];
      re += sample * std::cos(w);
      im -= sample * std::sin(w);
    }
    const double mag = re * re + im * im;
    if (mag > bestMag) {
      second = bestMag;
      bestMag = mag;
      best = tone;
    } else if (mag > second) {
      second = mag;
    }
  }
  *ratio = second > 0.0 ? bestMag / second : 0.0;
  return best;
}

}  // namespace

bool txStampIsFirstHalf(const QString& mode, int minute, int second) {
  if (mode == QLatin1String("FT2")) {
    // A minute holds 16 periods, so the minute does not flip the half.
    Q_UNUSED(minute);
    // MSHV stamps floor(k * 3.75). Even k is the first half of the 7.5 s pair.
    const int marks[] = {0, 3, 7, 11, 15, 18, 22, 26, 30, 33, 37, 41, 45, 48, 52, 56};
    int sec = second;
    if (sec < 0)
      sec = 0;
    if (sec > 59)
      sec = 59;
    int index = 0;
    for (int i = 0; i < 16; ++i) {
      if (sec >= marks[i])
        index = i;
    }
    return (index % 2) == 0;
  }
  int cs = 0;
  int periodCs = 1500;
  if (mode == QLatin1String("FT4")) {
    periodCs = 750;
    if (second == 7 || second == 22 || second == 37 || second == 52)
      cs = 50;
  } else if (jt65Mode(mode)) {
    periodCs = 6000;
  } else if (q65Mode(mode)) {
    periodCs = 3000;
  }
  int pos = minute * 6000 + second * 100 + cs;
  const int pair = periodCs * 2;
  while (pos >= pair)
    pos -= pair;
  while (pos < 0)
    pos += pair;
  return pos < periodCs;
}

bool ft8TxMatchesMshv(const std::vector<float>& wave, int toneHz) {
  // 79 symbols * 7680 samples, plus MSHV's 4 s of trailing silence.
  constexpr int kNsps = 7680;
  constexpr int kSymbols = 79;
  if (toneHz < 200 || int(wave.size()) < kSymbols * kNsps)
    return false;
  const int costas[7] = {3, 1, 4, 0, 6, 5, 2};
  const int at[3] = {0, 36, 72};
  float peak = 0.0f;
  const int mid = symbolCenter(3);
  if (mid < 0 || mid >= int(wave.size()))
    return false;
  for (int i = -200; i < 200; ++i)
    peak = std::max(peak, std::fabs(wave[static_cast<size_t>(mid + i)]));
  // Same full-scale sine MSHV sends at TX level 100. A clipped or empty
  // buffer fails this before the tone test.
  if (peak < 0.5f || peak > 1.05f)
    return false;
  // One symbol is 7680 samples, so a full-symbol tone test resolves 6.25 Hz.
  for (int block = 0; block < 3; ++block) {
    for (int i = 0; i < 7; ++i) {
      const int symbol = at[block] + i;
      const int start = symbol * kNsps;
      if (start < 0 || start + kNsps > int(wave.size()))
        return false;
      double ratio = 0.0;
      const int tone = bestTone(wave.data() + start, kNsps, toneHz, &ratio);
      if (tone != costas[i] || ratio < 2.0)
        return false;
    }
  }
  return true;
}

namespace {

struct SequencedHit {
  QString message;
};

void takeSequencedHit(const char*, int, const char*, const char* message, int, void* user) {
  auto* hit = static_cast<SequencedHit*>(user);
  const QString text = QString::fromUtf8(message).trimmed();
  if (!text.isEmpty() && hit->message.isEmpty())
    hit->message = text;
}

// JT65 and Q65 keep large arrays on the decoder object and large frames
// in the decode itself. That runs on this thread, not the GUI thread.
class SequencedDecodeThread : public QThread {
public:
  QString mode;
  const std::vector<float>* wave = nullptr;
  bool allOptions = false;
  QString message;

  void run() override {
    if (!wave || wave->size() < static_cast<size_t>(48000))
      return;
    const bool jt = jt65Mode(mode);
    const int period = jt ? 60 : 30;
    const int count12 = period * 12000;
    std::vector<double> samples(static_cast<size_t>(count12), 0.0);
    const int n48 = int(wave->size());
    int out = 0;
    for (int i = 0; i + 3 < n48 && out < count12; i += 4, ++out)
      samples[static_cast<size_t>(out)] = double((*wave)[static_cast<size_t>(i)]) * 12.8;
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/ft8dec.dll");
    HMODULE module = GetModuleHandleW(reinterpret_cast<const wchar_t*>(path.utf16()));
    if (!module)
      module =
          LoadLibraryExW(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr,
                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
      return;
    auto setSlow = reinterpret_cast<decltype(&slow_set_options)>(GetProcAddress(module, "slow_set_options"));
    if (allOptions && !setSlow)
      return;
    if (setSlow) {
      SlowDecodeOptions slow{};
      slow.depth = allOptions ? 3 : 1;
      slow.ap = allOptions ? 1 : 0;
      slow.averaging = allOptions ? 1 : 0;
      slow.autoClearAverage = 1;
      slow.singleSignal = 0;
      slow.driftCorrection = allOptions ? 1 : 0;
      slow.vhfUhf = allOptions ? 1 : 0;
      slow.maxCandidates = allOptions ? 32 : 8;
      slow.clearAverage = 1;
      slow.emeDelay = 0;
      slow.deepSearch = allOptions ? 1 : 0;
      slow.deepSearchDb = allOptions ? "W1AW,FN31\nN8SDR,EM79\nK1JT,FN20" : "";
      slow.myCall = allOptions ? "W1AW" : "";
      slow.hisCall = "";
      slow.hisGrid = "";
      setSlow(&slow);
    }
    SequencedHit hit;
    const char* stamp = "000000";
    if (jt) {
      auto decode =
          reinterpret_cast<decltype(&jt65_decode_wave)>(GetProcAddress(module, "jt65_decode_wave"));
      if (!decode)
        return;
      decode(samples.data(), count12, 1270, 400, jt65Mode65(mode), stamp, takeSequencedHit, &hit);
    } else {
      auto decode =
          reinterpret_cast<decltype(&q65_decode_wave)>(GetProcAddress(module, "q65_decode_wave"));
      if (!decode)
        return;
      // The self-test cursor is 1500 Hz. Q65 follows that tone.
      decode(samples.data(), count12, 200, 4000, 1500, q65ModId(mode), period, stamp,
             takeSequencedHit, &hit);
    }
    message = hit.message;
    if (setSlow && allOptions) {
      SlowDecodeOptions defaults{};
      defaults.depth = 1;
      defaults.autoClearAverage = 1;
      defaults.maxCandidates = 8;
      defaults.clearAverage = 1;
      setSlow(&defaults);
    }
  }
};

}  // namespace

QString decodeSequencedMessage(const QString& mode, const std::vector<float>& wave48k, bool allOptions) {
  if (!jt65Mode(mode) && !q65Mode(mode))
    return {};
  SequencedDecodeThread thread;
  thread.mode = mode;
  thread.wave = &wave48k;
  thread.allOptions = allOptions;
  thread.setStackSize(64 * 1024 * 1024);
  thread.start();
  if (!thread.wait(180000))
    return {};
  return thread.message;
}
