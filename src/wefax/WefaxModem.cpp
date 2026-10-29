// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Line rate, IOC, and the ±400 Hz black/white swing follow Fldigi's WEFAX-576
// (Dave Freese W1HKJ and Remi Chateauneu, GPL-3). The demodulator is STD's.
// Receive only. Nothing here keys the radio or sends START.

#include "WefaxModem.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr double kPi = 3.141592653589793;
constexpr int kDeviationHz = 400;

int blackHz(const WefaxSpec& spec) {
  return spec.centerHz - kDeviationHz;
}

int whiteHz(const WefaxSpec& spec) {
  return spec.centerHz + kDeviationHz;
}

struct DemodState {
  double prev = 0.0;
  double lastCross = -1.0;
  double seen = 0.0;
  double hold = 0.5;
  bool primed = false;
};

void demodSamples(const float* samples, int count, int rate, const WefaxSpec& spec, DemodState* state,
                  std::vector<float>* lum) {
  if (!samples || count < 1 || rate < 1000 || !state)
    return;
  const double black = double(blackHz(spec));
  const double white = double(whiteHz(spec));
  const double span = std::max(1.0, white - black);
  lum->reserve(lum->size() + static_cast<size_t>(count));
  for (int n = 0; n < count; ++n) {
    const double s = samples[n];
    const double at = state->seen + n;
    if (state->primed && ((state->prev <= 0.0 && s > 0.0) || (state->prev >= 0.0 && s < 0.0)) &&
        s != state->prev) {
      const double frac = (0.0 - state->prev) / (s - state->prev);
      const double cross = (at - 1.0) + frac;
      if (state->lastCross >= 0.0) {
        const double dt = cross - state->lastCross;
        if (dt > 1.0) {
          const double freq = double(rate) / (2.0 * dt);
          state->hold = std::clamp((freq - black) / span, 0.0, 1.0);
        }
      }
      state->lastCross = cross;
    }
    state->prev = s;
    state->primed = true;
    lum->push_back(float(state->hold));
  }
  state->seen += count;
}

void demod(const float* samples, int count, int rate, const WefaxSpec& spec, std::vector<float>* lum) {
  lum->clear();
  DemodState state;
  demodSamples(samples, count, rate, spec, &state, lum);
}

int findSync(const std::vector<float>& lum, int line) {
  if (line < 32 || int(lum.size()) < line * 2)
    return 0;
  const int bar = std::max(8, line * 45 / 1000);
  int best = 0;
  double bestScore = -1e9;
  const int step = std::max(1, line / 400);
  for (int off = 0; off < line; off += step) {
    double dark = 0.0;
    double rest = 0.0;
    int nDark = 0;
    int nRest = 0;
    bool complete = true;
    for (int lineNo = 0; lineNo < 2 && complete; ++lineNo) {
      const int origin = off + lineNo * line;
      if (origin < 0 || origin + line > int(lum.size())) {
        complete = false;
        break;
      }
      for (int i = 0; i < line; ++i) {
        const float v = lum[static_cast<size_t>(origin + i)];
        if (i < bar) {
          dark += v;
          ++nDark;
        } else {
          rest += v;
          ++nRest;
        }
      }
    }
    if (!complete || nDark < 1 || nRest < 1)
      continue;
    // Phasing is a short black pulse and a white line. A dark patch later in
    // the picture loses because the rest of that line is not white.
    const double score = rest / double(nRest) - dark / double(nDark);
    if (score > bestScore) {
      bestScore = score;
      best = off;
    }
  }
  return best;
}

void paintLine(const std::vector<float>& lum, int origin, int line, int pixels,
               std::vector<unsigned char>* gray) {
  for (int p = 0; p < pixels; ++p) {
    const double a0 = double(p) * double(line) / double(pixels);
    const double a1 = double(p + 1) * double(line) / double(pixels);
    int i0 = origin + int(std::floor(a0));
    int i1 = origin + int(std::ceil(a1));
    if (i1 <= i0)
      i1 = i0 + 1;
    double sum = 0.0;
    int n = 0;
    for (int i = i0; i < i1; ++i) {
      if (i < 0 || i >= int(lum.size()))
        continue;
      sum += lum[static_cast<size_t>(i)];
      ++n;
    }
    const int value = n > 0 ? int(std::lround(sum / double(n) * 255.0)) : 0;
    gray->push_back(static_cast<unsigned char>(std::clamp(value, 0, 255)));
  }
}

}  // namespace

int wefaxPixelsPerLine(int ioc) {
  if (ioc < 1)
    ioc = 576;
  return std::max(16, int(std::lround(double(ioc) * kPi)));
}

int wefaxSamplesPerLine(int sampleRateHz, int lpm) {
  if (sampleRateHz < 1000)
    sampleRateHz = 48000;
  if (lpm < 1)
    lpm = 120;
  return std::max(32, int(std::lround(double(sampleRateHz) * 60.0 / double(lpm))));
}

std::vector<float> wefaxModulateTest(const WefaxSpec& spec, int sampleRateHz,
                                    const std::vector<unsigned char>& gray, int width, int height) {
  std::vector<float> out;
  if (width < 2 || height < 1 || int(gray.size()) < width * height || sampleRateHz < 1000)
    return out;
  const int line = wefaxSamplesPerLine(sampleRateHz, spec.lpm);
  const double black = double(blackHz(spec));
  const double white = double(whiteHz(spec));
  out.resize(static_cast<size_t>(line * height));
  double phase = 0.0;
  for (int y = 0; y < height; ++y) {
    for (int n = 0; n < line; ++n) {
      const double pos = double(n) / double(line);
      int pixel = int(pos * double(width));
      if (pixel >= width)
        pixel = width - 1;
      if (pixel < 0)
        pixel = 0;
      const double g = double(gray[static_cast<size_t>(y * width + pixel)]) / 255.0;
      const double freq = black + g * (white - black);
      out[static_cast<size_t>(y * line + n)] = float(0.6 * std::sin(phase));
      phase += 2.0 * kPi * freq / double(sampleRateHz);
      if (phase > 2.0 * kPi)
        phase -= 2.0 * kPi;
    }
  }
  return out;
}

WefaxImage wefaxDecode(const WefaxSpec& spec, const float* samples, int count, int sampleRateHz) {
  WefaxImage picture;
  std::vector<float> lum;
  demod(samples, count, sampleRateHz, spec, &lum);
  const int drop = std::max(0, sampleRateHz / 50);
  if (int(lum.size()) <= drop)
    return picture;
  lum.erase(lum.begin(), lum.begin() + drop);
  const int line = wefaxSamplesPerLine(sampleRateHz, spec.lpm);
  const int pixels = wefaxPixelsPerLine(spec.ioc);
  if (int(lum.size()) < line * 2)
    return picture;
  const int sync = findSync(lum, line);
  const int rows = (int(lum.size()) - sync) / line;
  if (rows < 1)
    return picture;
  picture.width = pixels;
  picture.height = rows;
  picture.gray.reserve(static_cast<size_t>(pixels * rows));
  for (int y = 0; y < rows; ++y)
    paintLine(lum, sync + y * line, line, pixels, &picture.gray);
  return picture;
}

QString wefaxSaveDirectory() {
  QSettings store;
  QString dir = store.value(QStringLiteral("wefax/saveDir")).toString();
  if (dir.isEmpty()) {
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    dir = QDir(documents).filePath(QStringLiteral("STD/wefax"));
  }
  QDir().mkpath(dir);
  return dir;
}

void wefaxSetSaveDirectory(const QString& directory) {
  if (directory.trimmed().isEmpty())
    return;
  QDir().mkpath(directory);
  QSettings().setValue(QStringLiteral("wefax/saveDir"), directory);
}

struct WefaxEngine::Private {
  bool armed = false;
  WefaxSpec spec;
  int rate = 48000;
  std::vector<float> lum;
  DemodState demod;
  int origin = 0;
  bool locked = false;
  int rows = 0;
  QImage image;
  bool stopSaved = false;

  void resetFilter() {
    lum.clear();
    demod = DemodState();
    origin = 0;
    locked = false;
  }

  void reset() {
    resetFilter();
    image = QImage();
    rows = 0;
    stopSaved = false;
  }
};

WefaxEngine::WefaxEngine(QObject* parent) : QObject(parent), d_(new Private) {}

WefaxEngine::~WefaxEngine() {
  delete d_;
}

void WefaxEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  if (!armed)
    d_->resetFilter();
}

void WefaxEngine::setCenterHz(int hz) {
  const int next = std::clamp(hz, 600, 3000);
  if (d_->spec.centerHz == next)
    return;
  d_->spec.centerHz = next;
  d_->resetFilter();
}

void WefaxEngine::clear() {
  d_->reset();
  emit imageChanged();
}

const QImage& WefaxEngine::image() const {
  return d_->image;
}

int WefaxEngine::rowCount() const {
  return d_->rows;
}

QString WefaxEngine::saveNow() {
  if (d_->image.isNull() || d_->rows < 1)
    return {};
  const QImage frame = d_->image.copy(0, 0, d_->image.width(), d_->rows);
  const QString dir = wefaxSaveDirectory();
  QString path = QDir(dir).filePath(
      QStringLiteral("wefax-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
  if (QFileInfo::exists(path))
    path = QDir(dir).filePath(QStringLiteral("wefax-%1-%2.png")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
                                  .arg(d_->rows));
  if (!frame.save(path, "PNG"))
    return {};
  emit imageSaved(path);
  return path;
}

void WefaxEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || !samples || count < 1 || sampleRateHz < 1000)
    return;
  if (d_->rate != sampleRateHz) {
    d_->rate = sampleRateHz;
    d_->resetFilter();
  }
  demodSamples(samples, count, sampleRateHz, d_->spec, &d_->demod, &d_->lum);

  const int line = wefaxSamplesPerLine(sampleRateHz, d_->spec.lpm);
  const int pixels = wefaxPixelsPerLine(d_->spec.ioc);
  if (!d_->locked) {
    if (int(d_->lum.size()) < line * 2)
      return;
    d_->origin = findSync(d_->lum, line);
    d_->locked = true;
  }
  bool grew = false;
  while (int(d_->lum.size()) - d_->origin >= line && d_->rows < 2400) {
    std::vector<unsigned char> row;
    paintLine(d_->lum, d_->origin, line, pixels, &row);
    if (d_->image.isNull() || d_->image.width() != pixels || d_->image.height() < 2400) {
      d_->image = QImage(pixels, 2400, QImage::Format_Grayscale8);
      d_->image.fill(0);
      d_->rows = 0;
    }
    if (unsigned char* dst = d_->image.scanLine(d_->rows))
      std::copy(row.begin(), row.end(), dst);
    ++d_->rows;
    d_->origin += line;
    grew = true;
    if (!d_->stopSaved && d_->rows >= 8 && int(row.size()) == pixels) {
      int flips = 0;
      for (int i = 1; i < pixels; ++i) {
        if ((row[static_cast<size_t>(i - 1)] >= 128) != (row[static_cast<size_t>(i)] >= 128))
          ++flips;
      }
      if (flips > pixels / 10) {
        d_->stopSaved = true;
        saveNow();
      }
    }
  }
  if (d_->origin > line * 4) {
    d_->lum.erase(d_->lum.begin(), d_->lum.begin() + d_->origin);
    d_->origin = 0;
  }
  if (grew)
    emit imageChanged();
}
