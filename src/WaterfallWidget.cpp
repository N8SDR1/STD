// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaterfallWidget.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kFft = 2048;
constexpr int kColumns = 512;
constexpr int kMaxRows = 420;
constexpr float kSpanHz = 4000.0f;

void fft(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float angle = -6.28318530718f / float(len);
    const float wlenRe = std::cos(angle);
    const float wlenIm = std::sin(angle);
    for (int i = 0; i < n; i += len) {
      float wr = 1.0f;
      float wi = 0.0f;
      for (int k = 0; k < len / 2; ++k) {
        const int even = i + k;
        const int odd = even + len / 2;
        const float tr = wr * re[odd] - wi * im[odd];
        const float ti = wr * im[odd] + wi * re[odd];
        re[odd] = re[even] - tr;
        im[odd] = im[even] - ti;
        re[even] += tr;
        im[even] += ti;
        const float nextRe = wr * wlenRe - wi * wlenIm;
        wi = wr * wlenIm + wi * wlenRe;
        wr = nextRe;
      }
    }
  }
}

QRgb colorForDb(float db) {
  float t = (db + 108.0f) / 90.0f;
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;
  int r = 8;
  int g = 12;
  int b = 20;
  if (t < 0.30f) {
    const float u = t / 0.30f;
    r = int(8.0f + u * 48.0f);
    g = int(12.0f + u * 6.0f);
    b = int(20.0f + u * 62.0f);
  } else if (t < 0.62f) {
    const float u = (t - 0.30f) / 0.32f;
    r = int(56.0f + u * 98.0f);
    g = int(18.0f + u * 28.0f);
    b = int(82.0f + u * 80.0f);
  } else if (t < 0.84f) {
    const float u = (t - 0.62f) / 0.22f;
    r = int(154.0f - u * 90.0f);
    g = int(46.0f + u * 150.0f);
    b = int(162.0f + u * 50.0f);
  } else {
    const float u = (t - 0.84f) / 0.16f;
    r = int(64.0f + u * 191.0f);
    g = int(196.0f + u * 40.0f);
    b = int(212.0f - u * 150.0f);
  }
  return qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

}  // namespace

WaterfallWidget::WaterfallWidget(QWidget* parent) : QWidget(parent) {
  setObjectName("waterfall");
  setAttribute(Qt::WA_StyledBackground, false);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::NoFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAccessibleName("Waterfall");
  setAccessibleDescription("No signal. Radio not connected.");
}

void WaterfallWidget::setRadio(bool connected, const QString& dial) {
  radioConnected_ = connected;
  dialText_ = dial.isEmpty() ? QString::fromUtf8("—") : dial;
  if (filled_ > 0)
    setAccessibleDescription(QStringLiteral("Receiver audio. Nothing is decoded."));
  else
    setAccessibleDescription(connected ? QStringLiteral("No signal. Connected.")
                                        : QStringLiteral("No signal. Radio not connected."));
  update();
}

void WaterfallWidget::clearAudio() {
  pending_.clear();
  filled_ = 0;
  rateHz_ = 0;
  columns_ = QImage();
  update();
}

void WaterfallWidget::pushRealAudio(const float* samples, int count, int sampleRateHz) {
  if (!samples || count <= 0 || sampleRateHz < 1000 || surfaceContainer_)
    return;
  if (rateHz_ != sampleRateHz) {
    rateHz_ = sampleRateHz;
    pending_.clear();
    filled_ = 0;
    columns_ = QImage();
  }
  const int room = kFft * 8 - pending_.size();
  if (room <= 0)
    pending_.remove(0, pending_.size() - kFft);
  else if (count > room)
    samples += count - room, count = room;
  pending_.reserve(pending_.size() + count);
  for (int i = 0; i < count; ++i)
    pending_.push_back(samples[i]);
  bool drew = false;
  while (pending_.size() >= kFft) {
    appendSpectrum(pending_.constData(), kFft);
    pending_.remove(0, kFft);
    drew = true;
  }
  if (drew) {
    setAccessibleDescription(QStringLiteral("Receiver audio. Nothing is decoded."));
    update();
  }
}

void WaterfallWidget::appendSpectrum(const float* samples, int count) {
  Q_UNUSED(count);
  float re[kFft];
  float im[kFft];
  for (int i = 0; i < kFft; ++i) {
    const float window = 0.5f - 0.5f * std::cos(6.28318530718f * float(i) / float(kFft - 1));
    const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
    re[i] = sample * window;
    im[i] = 0.0f;
  }
  fft(re, im, kFft);

  if (columns_.isNull() || columns_.width() != kColumns || columns_.height() != kMaxRows)
    columns_ = QImage(kColumns, kMaxRows, QImage::Format_RGB32);
  if (filled_ > 0) {
    const int keep = qMin(filled_, kMaxRows - 1);
    std::memmove(columns_.scanLine(1), columns_.scanLine(0), size_t(keep) * size_t(columns_.bytesPerLine()));
  }
  QRgb* row = reinterpret_cast<QRgb*>(columns_.scanLine(0));
  const float span = rateHz_ >= 8000 ? kSpanHz : float(rateHz_) * 0.5f;
  const float scale = 2.0f / float(kFft);
  const int nyquist = kFft / 2 - 1;
  for (int c = 0; c < kColumns; ++c) {
    const float freq = span * float(c) / float(kColumns - 1);
    const float bin = freq * float(kFft) / float(rateHz_);
    const int i0 = qBound(0, int(bin), nyquist);
    const int i1 = qMin(i0 + 1, nyquist);
    const float frac = bin - float(i0);
    const float m0 = std::sqrt(re[i0] * re[i0] + im[i0] * im[i0]) * scale;
    const float m1 = std::sqrt(re[i1] * re[i1] + im[i1] * im[i1]) * scale;
    const float mag = m0 + (m1 - m0) * frac;
    const float db = 20.0f * std::log10(mag + 1.0e-12f);
    row[c] = colorForDb(db);
  }
  if (filled_ < kMaxRows)
    ++filled_;
}

void WaterfallWidget::setRenderSurface(QWindow* surface) {
  if (surfaceContainer_) {
    surfaceContainer_->hide();
    surfaceContainer_->deleteLater();
    surfaceContainer_ = nullptr;
  }
  if (!surface) {
    update();
    return;
  }

  surfaceContainer_ = QWidget::createWindowContainer(surface, this);
  surfaceContainer_->setFocusPolicy(Qt::NoFocus);
  surfaceContainer_->setGeometry(rect());
  surfaceContainer_->show();
  surfaceContainer_->raise();
}

QSize WaterfallWidget::sizeHint() const {
  return QSize(800, 380);
}

QSize WaterfallWidget::minimumSizeHint() const {
  return QSize(280, 180);
}

void WaterfallWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (surfaceContainer_)
    return;
  QPainter painter(this);
  if (filled_ > 0 && !columns_.isNull())
    paintAudio(painter);
  else
    paintPlaceholder(painter);
}

void WaterfallWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (surfaceContainer_)
    surfaceContainer_->setGeometry(rect());
}

void WaterfallWidget::paintPlaceholder(QPainter& painter) const {
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRect area = rect();

  QLinearGradient ground(0, 0, 0, area.height());
  ground.setColorAt(0.0, QColor("#070c12"));
  ground.setColorAt(1.0, QColor("#05080c"));
  painter.fillRect(area, ground);

  QLinearGradient sheen(0, 0, 0, 22);
  sheen.setColorAt(0.0, QColor(0, 229, 255, 26));
  sheen.setColorAt(1.0, QColor(0, 229, 255, 0));
  painter.fillRect(0, 0, area.width(), 22, sheen);

  painter.setPen(QColor(39, 65, 80, 120));
  constexpr int kGrid = 8;
  for (int i = 1; i < kGrid; ++i) {
    const int x = area.width() * i / kGrid;
    painter.drawLine(x, 28, x, area.height() - 26);
  }
  for (int y = 32; y < area.height() - 26; y += 16)
    painter.drawLine(10, y, area.width() - 10, y);

  const int center = area.width() / 2;
  painter.setPen(QPen(QColor(0, 229, 255, 170), 1));
  painter.drawLine(center, 28, center, area.height() - 26);

  QFont mono("JetBrains Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(12);
  painter.setFont(mono);

  paintDial(painter);

  painter.setPen(QColor("#8399ac"));
  const QStringList ticks = {
    QString::fromUtf8("−2"),
    QString::fromUtf8("−1"),
    QStringLiteral("0"),
    QStringLiteral("+1"),
    QStringLiteral("+2"),
  };
  for (int i = 0; i < ticks.size(); ++i) {
    const int x = area.width() * (i + 1) / (ticks.size() + 1);
    painter.drawText(QRect(x - 20, area.height() - 20, 40, 16), Qt::AlignCenter, ticks[i]);
  }
  painter.setPen(QColor("#5a7080"));
  painter.drawText(QRect(area.width() - 52, area.height() - 20, 42, 16),
                   Qt::AlignRight | Qt::AlignVCenter, "kHz");

  const QRect plate(area.center().x() - 148, area.center().y() - 34, 296, 68);
  painter.setPen(QPen(QColor("#274150"), 1));
  painter.setBrush(QColor(8, 12, 18, 220));
  painter.drawRoundedRect(plate, 8, 8);

  QFont title = mono;
  title.setPixelSize(16);
  title.setBold(true);
  painter.setFont(title);
  painter.setPen(QColor("#cdd9e5"));
  painter.drawText(plate.adjusted(0, 10, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "No signal");

  painter.setFont(mono);
  painter.setPen(QColor("#e08a3d"));
  painter.drawText(plate.adjusted(0, 36, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                   radioConnected_ ? QStringLiteral("Waiting for audio")
                                   : QStringLiteral("Radio not connected"));
}

void WaterfallWidget::paintDial(QPainter& painter) const {
  QFont mono("JetBrains Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(12);
  painter.setFont(mono);
  painter.setPen(QColor("#6d8496"));
  painter.drawText(QRect(12, 8, 42, 16), Qt::AlignLeft | Qt::AlignVCenter, "DIAL");
  painter.setPen(QColor("#00e5ff"));
  painter.drawText(QRect(54, 8, 220, 16), Qt::AlignLeft | Qt::AlignVCenter, dialText_);
}

void WaterfallWidget::paintAudio(QPainter& painter) const {
  const QRect area = rect();
  painter.fillRect(area, QColor("#05080c"));
  const QRect plot(8, 28, qMax(1, area.width() - 16), qMax(1, area.height() - 52));
  painter.drawImage(plot, columns_, QRect(0, 0, kColumns, filled_));

  QLinearGradient sheen(0, 0, 0, 22);
  sheen.setColorAt(0.0, QColor(0, 229, 255, 26));
  sheen.setColorAt(1.0, QColor(0, 229, 255, 0));
  painter.fillRect(0, 0, area.width(), 22, sheen);
  paintDial(painter);

  QFont mono("JetBrains Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(12);
  painter.setFont(mono);
  painter.setPen(QColor("#8399ac"));
  const QStringList ticks = {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"),
                             QStringLiteral("3"), QStringLiteral("4")};
  for (int i = 0; i < ticks.size(); ++i) {
    const int x = plot.left() + plot.width() * i / (ticks.size() - 1);
    painter.drawText(QRect(x - 16, area.height() - 20, 32, 16), Qt::AlignCenter, ticks[i]);
  }
  painter.setPen(QColor("#5a7080"));
  painter.drawText(QRect(area.width() - 52, area.height() - 20, 42, 16),
                   Qt::AlignRight | Qt::AlignVCenter, "kHz");
}
