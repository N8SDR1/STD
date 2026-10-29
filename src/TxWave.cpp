// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DigitalMode.h"
#include "TxWave.h"
#include "psk/PskModem.h"
#include "rtty/RttyModem.h"

#include <QCoreApplication>
#include <QString>

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

int glyphColumn(QChar ch, int column) {
  struct Glyph {
    char ch;
    unsigned char col[5];
  };
  // 5x7. Bit 0 is the top of the glyph. buildHell sends bit 6 first so the
  // bottom pixel leads, which is Fldigi get_font_data (b0 = bottom row).
  static const Glyph glyphs[] = {
      {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}, {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
      {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}}, {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
      {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}}, {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
      {'5', {0x27, 0x45, 0x45, 0x45, 0x39}}, {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
      {'7', {0x01, 0x71, 0x09, 0x05, 0x03}}, {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
      {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}}, {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
      {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}}, {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
      {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}}, {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
      {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}}, {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
      {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}}, {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
      {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}}, {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
      {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}}, {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
      {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}}, {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
      {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}}, {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
      {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}}, {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
      {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}}, {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
      {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}}, {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
      {'X', {0x63, 0x14, 0x08, 0x14, 0x63}}, {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
      {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}}, {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
      {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
      {'.', {0x00, 0x60, 0x60, 0x00, 0x00}}, {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
  };
  const QChar upper = ch.toUpper();
  const Glyph* found = &glyphs[0];
  for (const Glyph& glyph : glyphs) {
    if (QLatin1Char(glyph.ch) == upper) {
      found = &glyph;
      break;
    }
  }
  if (column < 0 || column >= 5)
    return 0;
  return found->col[column];
}

int firstInkRow(QChar ch, int column) {
  for (int row = 0; row < 20; ++row) {
    const int glyphRow = row - 6;
    if (glyphRow < 0 || glyphRow >= 7)
      continue;
    if (((glyphColumn(ch, column) >> (6 - glyphRow)) & 1) != 0)
      return row;
  }
  return -1;
}

bool buildHell(const QString& message, int toneHz, std::vector<float>* out) {
  // Fldigi Feld Hell transmit is on-off keying of one tone. Ink is the
  // carrier, paper is silence. The column is 20 pixels at 17.5 columns/s,
  // which is Fldigi's receive sampler (HellRcvHeight), so each pixel is
  // one raster sample. The bottom glyph bit goes out first.
  const double pixelsPerSecond = 20.0 * 17.5;
  const double pixelSamples = double(kRate) / pixelsPerSecond;
  const int tone = std::clamp(toneHz, 200, 3000);
  out->clear();
  double phase = 0.0;
  const double step = 2.0 * 3.141592653589793 * double(tone) / double(kRate);
  auto emitPixel = [&](bool ink) {
    const int n = std::max(1, int(std::lround(pixelSamples)));
    for (int i = 0; i < n; ++i) {
      out->push_back(ink ? float(0.6 * std::sin(phase)) : 0.0f);
      phase += step;
      if (phase > 6.283185307179586)
        phase -= 6.283185307179586;
    }
  };
  for (const QChar ch : message) {
    for (int column = 0; column < 5; ++column) {
      const int bits = glyphColumn(ch, column);
      for (int row = 0; row < 20; ++row) {
        const int glyphRow = row - 6;
        const bool ink = glyphRow >= 0 && glyphRow < 7 && ((bits >> (6 - glyphRow)) & 1) != 0;
        emitPixel(ink);
      }
    }
    for (int row = 0; row < 40; ++row)
      emitPixel(false);
  }
  for (int i = 0; i < kRate / 5; ++i)
    out->push_back(0.0f);
  return !out->empty();
}

}  // namespace

bool buildTxWave(const QString& mode, const QString& message, int toneHz, std::vector<float>* out,
                 std::vector<int>* echoAt) {
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
  if (hellFamily(mode))
    return buildHell(text, tone, out);
  if (rttyMode(mode)) {
    const RttyPlan plan = planRtty(text, tone);
    if (plan.samples.empty())
      return false;
    *out = plan.samples;
    if (echoAt)
      *echoAt = plan.echoAt;
    return true;
  }
  if (pskMode(mode) && pskBaud(mode) > 0.0) {
    const PskPlan plan = planPsk(mode, text, tone);
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
  return firstInkRow(ch, column);
}

int textModeSamplesPerChar(const QString& mode) {
  if (!hellFamily(mode))
    return 0;
  // Same grid as buildHell: 350 pixels/s, 5 columns of 20 rows, then
  // 40 blank pixels. The trailing fifth-of-a-second silence is not a
  // character. 140 * lround(48000/350) is 19180 samples, about 400 ms.
  constexpr double kPixelsPerSecond = 20.0 * 17.5;
  const int pixelSamples = std::max(1, int(std::lround(48000.0 / kPixelsPerSecond)));
  return 140 * pixelSamples;
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
