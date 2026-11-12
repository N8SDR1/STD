// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MskPingDisplay.h"

#include <QDateTime>
#include <QPainter>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

constexpr int kRate = 12000;
// 256 samples at 12 kHz: 21.3 ms per column, 46.9 Hz per bin.
constexpr int kFft = 256;
constexpr double kBinHz = double(kRate) / kFft;
// 190 to 2810 Hz covers the 500 to 2500 Hz MSK144 band with room either side.
constexpr int kLowBin = 4;
constexpr int kHighBin = 60;
constexpr int kRows = kHighBin - kLowBin + 1;
constexpr int kRuler = 16;

void fft(std::vector<std::complex<float>>& a) {
  const int n = int(a.size());
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[size_t(i)], a[size_t(j)]);
  }
  for (int len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * 3.141592653589793 / len;
    const std::complex<float> wl(float(std::cos(ang)), float(std::sin(ang)));
    for (int i = 0; i < n; i += len) {
      std::complex<float> w(1.0f, 0.0f);
      for (int k = 0; k < len / 2; ++k) {
        const std::complex<float> u = a[size_t(i + k)];
        const std::complex<float> v = a[size_t(i + k + len / 2)] * w;
        a[size_t(i + k)] = u + v;
        a[size_t(i + k + len / 2)] = u - v;
        w *= wl;
      }
    }
  }
}

// Like MSHV: the noise floor stays near black with a faint navy grain,
// and only signal above it rises through blue and cyan to white.
QRgb shade(int level) {
  level = std::clamp(level, 0, 255);
  const double t = level / 255.0;
  const int r = int(std::clamp(255.0 * (t - 0.6) / 0.4, 0.0, 255.0));
  const int g = int(std::clamp(255.0 * (t - 0.35) / 0.55, 0.0, 255.0));
  const int b = int(std::clamp(12.0 + 243.0 * t / 0.55, 0.0, 255.0));
  return qRgb(r, g, b);
}

}  // namespace

MskPingDisplay::MskPingDisplay(QWidget* parent) : QWidget(parent), block_(size_t(kFft), 0.0f) {
  setObjectName(QStringLiteral("mskPingDisplay"));
  setMinimumHeight(2 * (kRuler + 60));
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setToolTip(QStringLiteral(
      "MSK144 ping display, as in MSHV. Time runs left to right across one T/R period and audio frequency "
      "runs up. The top strip is the period on the air now, the one below is the period before, each with "
      "its UTC start. A meteor ping is a bright burst; a yellow mark shows where each decode came from. "
      "It only shows receive audio and does not key."));
  floor_.assign(size_t(kRows), 0.0f);
  clear();
}

int MskPingDisplay::columnsPerPeriod() const {
  return int(qint64(periodMs_) * kRate / 1000 / kFft);
}

void MskPingDisplay::reset(Strip& strip, qint64 startMs) {
  strip.image = QImage(columnsPerPeriod(), kRows, QImage::Format_RGB32);
  strip.image.fill(QColor(0, 0, 0));
  strip.startMs = startMs;
  strip.pings.clear();
  strip.painted = QVector<bool>(columnsPerPeriod(), false);
  strip.cursor = -1;
  strip.stamp = startMs < 0 ? QString()
                            : QDateTime::fromMSecsSinceEpoch(startMs, QTimeZone::utc())
                                  .toString(QStringLiteral("hh:mm:ss"));
}

void MskPingDisplay::clear() {
  reset(current_, -1);
  reset(previous_, -1);
  fill_ = 0;
  update();
}

void MskPingDisplay::setPeriodMs(int periodMs) {
  if (periodMs < 1000 || periodMs == periodMs_)
    return;
  periodMs_ = periodMs;
  clear();
}

void MskPingDisplay::pushAudio(const float* samples, int count, int sampleRateHz) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 start = now - qint64(count) * 1000 / std::max(1, sampleRateHz);
  pushAudioAt(samples, count, sampleRateHz, start);
  if (now - lastRepaintMs_ > 100) {
    lastRepaintMs_ = now;
    update();
  }
}

void MskPingDisplay::pushAudioAt(const float* samples, int count, int sampleRateHz, qint64 startMs) {
  if (!samples || count <= 0 || sampleRateHz < 8000)
    return;
  if (rate_ != sampleRateHz) {
    rate_ = sampleRateHz;
    decim_ = 0;
    residual_ = 0.0;
  }
  const double msPerIn = 1000.0 / sampleRateHz;
  if (sampleRateHz % kRate == 0) {
    const int step = sampleRateHz / kRate;
    for (int i = 0; i < count; ++i) {
      if (decim_ == 0)
        feed(samples[i], startMs + qint64(i * msPerIn));
      decim_ = (decim_ + 1) % step;
    }
    return;
  }
  const double advance = double(sampleRateHz) / kRate;
  double cursor = residual_;
  while (cursor < count) {
    const int index = int(cursor);
    const float a = samples[index];
    const float b = samples[index + 1 < count ? index + 1 : index];
    feed(a + (b - a) * float(cursor - index), startMs + qint64(cursor * msPerIn));
    cursor += advance;
  }
  residual_ = cursor - count;
}

void MskPingDisplay::feed(float sample, qint64 sampleMs) {
  block_[size_t(fill_++)] = sample;
  if (fill_ < kFft)
    return;
  fill_ = 0;
  column(sampleMs);
}

void MskPingDisplay::column(qint64 endMs) {
  std::vector<std::complex<float>> a(static_cast<size_t>(kFft));
  for (int i = 0; i < kFft; ++i) {
    const float w = 0.5f - 0.5f * float(std::cos(2.0 * 3.141592653589793 * i / (kFft - 1)));
    a[size_t(i)] = std::complex<float>(block_[size_t(i)] * w, 0.0f);
  }
  fft(a);
  // The column sits at the middle of its 21 ms block.
  const qint64 at = endMs - qint64(kFft) * 1000 / kRate / 2;
  const qint64 start = (at / periodMs_) * periodMs_;
  if (start != current_.startMs) {
    if (current_.startMs >= 0 && start == current_.startMs + periodMs_)
      previous_ = current_, previous_.cursor = -1;
    else if (current_.startMs >= 0)
      reset(previous_, -1);
    reset(current_, start);
  }
  const int x = int(qint64(at - start) * columnsPerPeriod() / periodMs_);
  if (x < 0 || x >= current_.image.width())
    return;
  for (int row = 0; row < kRows; ++row) {
    const std::complex<float> c = a[size_t(kLowBin + row)];
    const float db = 10.0f * std::log10(std::norm(c) + 1e-12f);
    float& floor = floor_[size_t(row)];
    // The floor is the slow average of the noise; a ping is too short to
    // lift it. Noise sits below the floor + 3 dB and paints near black.
    if (floor == 0.0f)
      floor = db;
    else
      floor += 0.01f * (db - floor);
    const int level = int((db - floor - 3.0f) * 255.0f / 24.0f);
    // Row 0 is the top of the image, the highest frequency.
    current_.image.setPixel(x, kRows - 1 - row, shade(level));
  }
  current_.painted[x] = true;
  current_.cursor = x;
}

void MskPingDisplay::addPing(const QString& stamp, double t0) {
  QString digits;
  for (const QChar c : stamp) {
    if (c.isDigit())
      digits += c;
  }
  const auto matches = [&digits](const Strip& strip) {
    if (strip.stamp.isEmpty() || digits.size() < 6)
      return false;
    QString own = strip.stamp;
    own.remove(QLatin1Char(':'));
    return own == digits.left(6);
  };
  if (matches(current_))
    current_.pings.push_back(t0);
  else if (matches(previous_))
    previous_.pings.push_back(t0);
  else
    return;
  update();
}

int MskPingDisplay::pingCount(bool current) const {
  return current ? current_.pings.size() : previous_.pings.size();
}

QString MskPingDisplay::stamp(bool current) const {
  return current ? current_.stamp : previous_.stamp;
}

int MskPingDisplay::levelAt(double seconds, int hz) const {
  const int x = int(seconds * 1000.0 * columnsPerPeriod() / periodMs_);
  const int row = int(std::lround(hz / kBinHz)) - kLowBin;
  if (x < 0 || x >= current_.image.width() || row < 0 || row >= kRows)
    return -1;
  if (!current_.painted.value(x))
    return -1;
  const QRgb pixel = current_.image.pixel(x, kRows - 1 - row);
  return qBlue(pixel) + qGreen(pixel) + qRed(pixel);
}

void MskPingDisplay::paintStrip(QPainter& p, const QRect& area, const Strip& strip) const {
  const QRect ruler(area.left(), area.top(), area.width(), kRuler);
  const QRect picture(area.left(), area.top() + kRuler, area.width(), area.height() - kRuler);
  p.fillRect(ruler, QColor(10, 14, 20));
  p.setPen(QColor(200, 210, 220));
  QFont font = p.font();
  font.setPixelSize(11);
  p.setFont(font);
  const int seconds = std::max(1, periodMs_ / 1000);
  const int step = seconds > 15 ? 1 : 1;
  for (int s = 0; s <= seconds; s += step) {
    const int x = picture.left() + int(qint64(s) * 1000 * picture.width() / periodMs_);
    p.drawLine(x, ruler.bottom() - 4, x, ruler.bottom());
    const QString label = QString::number(s);
    const int w = p.fontMetrics().horizontalAdvance(label);
    p.drawText(std::clamp(x - w / 2, ruler.left() + 1, ruler.right() - w), ruler.top() + 11, label);
  }
  if (strip.image.isNull())
    p.fillRect(picture, Qt::black);
  else
    p.drawImage(picture, strip.image);
  // The sweep: where the period on the air has reached.
  if (strip.cursor >= 0 && strip.image.width() > 0) {
    const int x = picture.left() + int(qint64(strip.cursor + 1) * picture.width() / strip.image.width());
    p.setPen(QColor(90, 200, 120));
    p.drawLine(x, picture.top(), x, picture.bottom());
  }
  p.setPen(QColor(255, 210, 60));
  for (double t0 : strip.pings) {
    const int x = picture.left() + int(t0 * 1000.0 * picture.width() / periodMs_);
    p.drawLine(x, picture.top(), x, picture.top() + 6);
    p.drawLine(x, picture.bottom() - 6, x, picture.bottom());
  }
  if (!strip.stamp.isEmpty()) {
    p.setPen(QColor(235, 240, 245));
    p.drawText(picture.left() + 8, picture.bottom() - 6, strip.stamp);
  }
}

void MskPingDisplay::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), Qt::black);
  const int half = height() / 2;
  paintStrip(p, QRect(0, 0, width(), half - 1), current_);
  p.fillRect(QRect(0, half - 1, width(), 2), QColor(60, 70, 80));
  paintStrip(p, QRect(0, half + 1, width(), height() - half - 1), previous_);
}
