// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Line rate, IOC, and the ±400 Hz black/white swing follow Fldigi's WEFAX-576
// (Dave Freese W1HKJ and Remi Chateauneu, GPL-3). The demodulator mixes to the
// cursor, low-passes, then reads FM the way wefax.cxx does. APT start is 300 Hz
// for IOC 576 and 675 Hz for IOC 288. APT stop is 450 Hz. Receive only.
// Nothing here keys the radio or sends START.

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
#include <QtGlobal>

namespace {

constexpr double kPi = 3.141592653589793;
constexpr int kDeviationHz = 400;
constexpr int kAptStopHz = 450;

int blackHz(const WefaxSpec& spec) {
  return spec.centerHz - kDeviationHz;
}

int whiteHz(const WefaxSpec& spec) {
  return spec.centerHz + kDeviationHz;
}

int aptStartHz(const WefaxSpec& spec) {
  return spec.ioc < 400 ? 675 : 300;
}

int firTapsFor(int rate) {
  int taps = rate / 300;
  if (taps < 31)
    taps = 31;
  if (taps > 191)
    taps = 191;
  if ((taps & 1) == 0)
    ++taps;
  return taps;
}

// Slant is in parts per million of the line length. Sound cards are off
// by tens to a few hundred ppm; one percent would skew a 1810-pixel line
// by 18 pixels a row.
constexpr int kSlantMaxPpm = 20000;

double lineStep(int sampleRateHz, int lpm, double slantPpm) {
  const int nominal = wefaxSamplesPerLine(sampleRateHz, lpm);
  const double slant = std::clamp(slantPpm, -double(kSlantMaxPpm), double(kSlantMaxPpm));
  return std::max(32.0, double(nominal) * (1.0 + slant * 1e-6));
}

struct DemodState {
  double phase = 0.0;
  double iPrev = 0.0;
  double qPrev = 0.0;
  double smooth = 0.5;
  bool hasPrev = false;
  int rate = 0;
  int taps = 0;
  int fill = 0;
  int pos = 0;
  std::vector<double> coef;
  std::vector<double> iRing;
  std::vector<double> qRing;
};

void prepareFir(DemodState* state, int rate) {
  if (!state || (state->rate == rate && state->taps > 0))
    return;
  const int taps = firTapsFor(rate);
  const int mid = taps / 2;
  const double fc = 500.0 / double(rate);
  state->coef.assign(static_cast<size_t>(taps), 0.0);
  double sum = 0.0;
  for (int i = 0; i < taps; ++i) {
    const double n = double(i - mid);
    const double sinc = n == 0.0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * n) / (kPi * n);
    const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(taps - 1));
    state->coef[static_cast<size_t>(i)] = sinc * window;
    sum += state->coef[static_cast<size_t>(i)];
  }
  if (sum != 0.0) {
    for (double& coef : state->coef)
      coef /= sum;
  }
  state->iRing.assign(static_cast<size_t>(taps), 0.0);
  state->qRing.assign(static_cast<size_t>(taps), 0.0);
  state->taps = taps;
  state->fill = 0;
  state->pos = 0;
  state->rate = rate;
  state->hasPrev = false;
  state->smooth = 0.5;
  state->phase = 0.0;
}

void demodSamples(const float* samples, int count, int rate, const WefaxSpec& spec, DemodState* state,
                  std::vector<float>* lum) {
  if (!samples || count < 1 || rate < 1000 || !state || !lum)
    return;
  prepareFir(state, rate);
  const int taps = state->taps;
  if (taps < 5)
    return;
  const double omega = 2.0 * kPi * double(spec.centerHz) / double(rate);
  const double blend = 1.0 - std::exp(-2.0 * kPi * 1500.0 / double(rate));
  lum->reserve(lum->size() + static_cast<size_t>(count));
  for (int n = 0; n < count; ++n) {
    const double s = samples[n];
    state->phase += omega;
    if (state->phase > 2.0 * kPi)
      state->phase -= 2.0 * kPi;
    const double mixedI = s * std::cos(state->phase);
    const double mixedQ = s * std::sin(state->phase);
    state->iRing[static_cast<size_t>(state->pos)] = mixedI;
    state->qRing[static_cast<size_t>(state->pos)] = mixedQ;
    double iF = 0.0;
    double qF = 0.0;
    int idx = state->pos;
    for (int k = 0; k < taps; ++k) {
      iF += state->coef[static_cast<size_t>(k)] * state->iRing[static_cast<size_t>(idx)];
      qF += state->coef[static_cast<size_t>(k)] * state->qRing[static_cast<size_t>(idx)];
      if (--idx < 0)
        idx = taps - 1;
    }
    state->pos = (state->pos + 1) % taps;
    if (state->fill < taps)
      ++state->fill;
    if (state->fill >= taps && state->hasPrev) {
      const double power = iF * iF + qF * qF;
      if (power > 1e-8) {
        const double dphi = std::atan2(state->iPrev * qF - state->qPrev * iF,
                                       state->iPrev * iF + state->qPrev * qF);
        // Mixing sin/cos makes a tone above the cursor come out negative.
        const double df = -dphi * double(rate) / (2.0 * kPi);
        const double raw = (df + double(kDeviationHz)) / double(2 * kDeviationHz);
        state->smooth += blend * (raw - state->smooth);
      }
    }
    state->iPrev = iF;
    state->qPrev = qF;
    state->hasPrev = state->fill >= taps;
    lum->push_back(float(std::clamp(state->smooth, 0.0, 1.0)));
  }
}

void demod(const float* samples, int count, int rate, const WefaxSpec& spec, std::vector<float>* lum) {
  lum->clear();
  DemodState state;
  demodSamples(samples, count, rate, spec, &state, lum);
}

int findSync(const std::vector<float>& lum, int line, double* scoreOut) {
  if (scoreOut)
    *scoreOut = 0.0;
  if (line < 32 || int(lum.size()) < line * 2)
    return 0;
  const int bar = std::max(8, line * 45 / 1000);
  int best = 0;
  double bestScore = -1e9;
  const int step = std::max(1, line / 400);
  for (int off = 0; off < line; off += step) {
    double barSum = 0.0;
    double restSum = 0.0;
    int nBar = 0;
    int nRest = 0;
    bool complete = true;
    for (int lineNo = 0; lineNo < 2 && complete; ++lineNo) {
      const int origin = off + lineNo * line;
      if (origin < 0 || origin + line > int(lum.size())) {
        complete = false;
        break;
      }
      for (int i = 0; i < line; i += 4) {
        const float v = lum[static_cast<size_t>(origin + i)];
        if (i < bar) {
          barSum += v;
          ++nBar;
        } else {
          restSum += v;
          ++nRest;
        }
      }
    }
    if (!complete || nBar < 1 || nRest < 1)
      continue;
    const double barMean = barSum / double(nBar);
    const double restMean = restSum / double(nRest);
    // STD's test picture is a short black pulse on white. WMO phasing is a
    // short white pulse on black. The louder of those two wins.
    const double score = std::max(restMean - barMean, barMean - restMean);
    if (score > bestScore) {
      bestScore = score;
      best = off;
    }
  }
  if (scoreOut && bestScore > -1e8)
    *scoreOut = bestScore;
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
                                    const std::vector<unsigned char>& gray, int width, int height,
                                    double lineScale) {
  std::vector<float> out;
  if (width < 2 || height < 1 || int(gray.size()) < width * height || sampleRateHz < 1000)
    return out;
  if (lineScale < 0.5 || lineScale > 1.5)
    lineScale = 1.0;
  const int line = std::max(32, int(std::lround(double(wefaxSamplesPerLine(sampleRateHz, spec.lpm)) * lineScale)));
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

WefaxImage wefaxDecode(const WefaxSpec& spec, const float* samples, int count, int sampleRateHz,
                       double slantPpm) {
  WefaxImage picture;
  std::vector<float> lum;
  demod(samples, count, sampleRateHz, spec, &lum);
  const int drop = firTapsFor(sampleRateHz);
  if (int(lum.size()) <= drop)
    return picture;
  lum.erase(lum.begin(), lum.begin() + drop);
  const double step = lineStep(sampleRateHz, spec.lpm, slantPpm);
  const int span = std::max(32, int(std::lround(step)));
  const int pixels = wefaxPixelsPerLine(spec.ioc);
  if (int(lum.size()) < span * 2)
    return picture;
  const int sync = findSync(lum, span, nullptr);
  const int rows = int((double(int(lum.size()) - sync)) / step);
  if (rows < 1)
    return picture;
  picture.width = pixels;
  picture.height = rows;
  picture.gray.reserve(static_cast<size_t>(pixels * rows));
  double cursor = sync;
  for (int y = 0; y < rows; ++y) {
    paintLine(lum, int(std::lround(cursor)), span, pixels, &picture.gray);
    cursor += step;
  }
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
  enum class Phase { Hunt, Image };

  bool armed = false;
  WefaxSpec spec;
  int rate = 48000;
  int slant = 0;
  std::vector<float> lum;
  DemodState demod;
  double origin = 0.0;
  Phase phase = Phase::Hunt;
  int rows = 0;
  QImage image;
  int aptCount = 0;
  int aptRises = 0;
  bool aptHigh = false;
  int aptHist[4] = {};
  int aptN = 0;

  void resetApt() {
    aptCount = 0;
    aptRises = 0;
    aptHigh = false;
    aptN = 0;
    aptHist[0] = aptHist[1] = aptHist[2] = aptHist[3] = 0;
  }

  void resetFilter() {
    lum.clear();
    demod = DemodState();
    origin = 0.0;
    phase = Phase::Hunt;
    resetApt();
  }

  void reset() {
    resetFilter();
    image = QImage();
    rows = 0;
  }

  void noteApt(float value, int sampleRate) {
    const bool hot = value > 0.84f;
    const bool cold = value < 0.16f;
    if (hot && !aptHigh) {
      aptHigh = true;
      ++aptRises;
    } else if (cold && aptHigh) {
      aptHigh = false;
    }
    ++aptCount;
    const int window = std::max(1000, sampleRate / 2);
    if (aptCount < window)
      return;
    const int hz = int(std::lround(double(sampleRate) * double(aptRises) / double(aptCount)));
    aptHist[3] = aptHist[2];
    aptHist[2] = aptHist[1];
    aptHist[1] = aptHist[0];
    aptHist[0] = hz;
    if (aptN < 4)
      ++aptN;
    aptCount = 0;
    aptRises = 0;
  }

  bool twoNear(int target) const {
    if (aptN < 2)
      return false;
    return std::abs(aptHist[0] - target) <= 12 && std::abs(aptHist[1] - target) <= 12;
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

void WefaxEngine::setSlant(int ppm) {
  d_->slant = std::clamp(ppm, -kSlantMaxPpm, kSlantMaxPpm);
}

int WefaxEngine::slant() const {
  return d_->slant;
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
  if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
    emit imageSaved(QStringLiteral("selftest"));
    return QStringLiteral("selftest");
  }
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
  const int lumAt = int(d_->lum.size());
  demodSamples(samples, count, sampleRateHz, d_->spec, &d_->demod, &d_->lum);
  for (int i = lumAt; i < int(d_->lum.size()); ++i)
    d_->noteApt(d_->lum[static_cast<size_t>(i)], sampleRateHz);

  const double step = lineStep(sampleRateHz, d_->spec.lpm, d_->slant);
  const int span = std::max(32, int(std::lround(step)));
  const int pixels = wefaxPixelsPerLine(d_->spec.ioc);
  const int startHz = aptStartHz(d_->spec);
  if (d_->phase == Private::Phase::Image && d_->rows >= 4 &&
      (d_->twoNear(kAptStopHz) || d_->twoNear(startHz))) {
    if (d_->rows >= 4)
      saveNow();
    d_->image = QImage();
    d_->rows = 0;
    d_->resetFilter();
    emit imageChanged();
    return;
  }

  if (d_->phase == Private::Phase::Hunt) {
    while (int(d_->lum.size()) >= span * 2) {
      double score = 0.0;
      const int sync = findSync(d_->lum, span, &score);
      if (score >= 0.45) {
        d_->origin = sync;
        d_->phase = Private::Phase::Image;
        d_->resetApt();
        d_->image = QImage();
        d_->rows = 0;
        break;
      }
      if (int(d_->lum.size()) <= span * 4)
        break;
      const int drop = std::min(span, int(d_->lum.size()));
      d_->lum.erase(d_->lum.begin(), d_->lum.begin() + drop);
    }
  }

  bool grew = false;
  while (d_->phase == Private::Phase::Image && double(d_->lum.size()) - d_->origin >= step && d_->rows < 2400) {
    std::vector<unsigned char> row;
    paintLine(d_->lum, int(std::lround(d_->origin)), span, pixels, &row);
    if (d_->image.isNull() || d_->image.width() != pixels || d_->image.height() < 2400) {
      d_->image = QImage(pixels, 2400, QImage::Format_Grayscale8);
      d_->image.fill(0);
      d_->rows = 0;
    }
    if (unsigned char* dst = d_->image.scanLine(d_->rows))
      std::copy(row.begin(), row.end(), dst);
    ++d_->rows;
    d_->origin += step;
    grew = true;
  }
  if (d_->origin > step * 4.0 && d_->origin < double(d_->lum.size())) {
    const int drop = int(d_->origin);
    d_->lum.erase(d_->lum.begin(), d_->lum.begin() + drop);
    d_->origin -= drop;
  }
  if (grew)
    emit imageChanged();
}
