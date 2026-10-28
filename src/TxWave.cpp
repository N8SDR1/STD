// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TxWave.h"

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
constexpr float kScale = 8388607.0f;

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
    module = LoadLibraryW(reinterpret_cast<const wchar_t*>(path.utf16()));
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

int glyphColumn(QChar ch, int column) {
  struct Glyph {
    char ch;
    unsigned char col[5];
  };
  // 5x7, bit 0 at the bottom.
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

bool buildHell(const QString& message, int toneHz, std::vector<float>* out) {
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

bool buildTxWave(const QString& mode, const QString& message, int toneHz, std::vector<float>* out) {
  if (!out)
    return false;
  out->clear();
  const QString text = message.trimmed();
  if (text.isEmpty())
    return false;
  const int tone = std::clamp(toneHz, 100, 5000);
  if (mode == QLatin1String("FT8"))
    return buildFt8(text, tone, out);
  if (mode == QLatin1String("FT4"))
    return buildFt4(text, tone, out);
  if (mode == QLatin1String("Hell"))
    return buildHell(text, tone, out);
  return false;
}
