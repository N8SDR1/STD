// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaterfallWidget.h"

#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kFft = 2048;
constexpr int kSpecBins = kFft / 2;
constexpr int kColumns = 512;
constexpr int kMaxRows = 420;
constexpr int kRulerHeight = 32;
constexpr float kFullHz = 4000.0f;
constexpr float kMinSpanHz = 200.0f;

float tickStepHz(float span) {
  const float raw = span / 8.0f;
  const float steps[] = {10.0f, 20.0f, 25.0f, 50.0f, 100.0f, 200.0f, 250.0f, 500.0f, 1000.0f, 2000.0f};
  for (float step : steps) {
    if (step >= raw)
      return step;
  }
  return 2000.0f;
}

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
  setFocusPolicy(Qt::WheelFocus);
  setMouseTracking(true);
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
  spectra_.clear();
  columns_ = QImage();
  filled_ = 0;
  head_ = 0;
  rateHz_ = 0;
  dragging_ = false;
  resetView();
}

void WaterfallWidget::resetView() {
  zoomed_ = false;
  viewLoHz_ = 0.0f;
  viewHiHz_ = kFullHz;
  update();
}

float WaterfallWidget::fullSpanHz() const {
  if (rateHz_ >= 8000)
    return kFullHz;
  if (rateHz_ > 0)
    return float(rateHz_) * 0.5f;
  return kFullHz;
}

QRect WaterfallWidget::rulerRect() const {
  return QRect(0, 0, width(), kRulerHeight);
}

QRect WaterfallWidget::plotRect() const {
  return QRect(0, kRulerHeight, width(), qMax(1, height() - kRulerHeight));
}

float WaterfallWidget::hzAtX(int x) const {
  const QRect plot = rulerRect();
  const float span = qMax(1.0f, viewHiHz_ - viewLoHz_);
  const float t = float(x - plot.left()) / float(qMax(1, plot.width() - 1));
  const float clamped = qBound(0.0f, t, 1.0f);
  return viewLoHz_ + clamped * span;
}

int WaterfallWidget::xAtHz(float hz) const {
  const QRect plot = rulerRect();
  const float span = qMax(1.0f, viewHiHz_ - viewLoHz_);
  const float t = qBound(0.0f, (hz - viewLoHz_) / span, 1.0f);
  return plot.left() + int(t * float(plot.width() - 1));
}

void WaterfallWidget::setView(float loHz, float hiHz) {
  const float full = fullSpanHz();
  if (hiHz < loHz)
    std::swap(loHz, hiHz);
  if (hiHz - loHz < kMinSpanHz) {
    const float mid = 0.5f * (loHz + hiHz);
    loHz = mid - kMinSpanHz * 0.5f;
    hiHz = loHz + kMinSpanHz;
  }
  if (hiHz - loHz >= full) {
    loHz = 0.0f;
    hiHz = full;
  }
  if (loHz < 0.0f) {
    hiHz -= loHz;
    loHz = 0.0f;
  }
  if (hiHz > full) {
    loHz -= hiHz - full;
    hiHz = full;
  }
  if (loHz < 0.0f)
    loHz = 0.0f;
  viewLoHz_ = loHz;
  viewHiHz_ = hiHz;
  zoomed_ = viewLoHz_ > 0.5f || viewHiHz_ < full - 0.5f;
  rebuildImage();
  update();
}

const float* WaterfallWidget::spectrumRow(int age) const {
  int index = head_ - 1 - age;
  index %= kMaxRows;
  if (index < 0)
    index += kMaxRows;
  return spectra_.constData() + index * kSpecBins;
}

void WaterfallWidget::pushRealAudio(const float* samples, int count, int sampleRateHz) {
  if (!samples || count <= 0 || sampleRateHz < 1000 || surfaceContainer_)
    return;
  if (rateHz_ != sampleRateHz) {
    rateHz_ = sampleRateHz;
    pending_.clear();
    filled_ = 0;
    head_ = 0;
    columns_ = QImage();
    spectra_.clear();
    zoomed_ = false;
    viewLoHz_ = 0.0f;
    viewHiHz_ = fullSpanHz();
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

  if (spectra_.size() != kMaxRows * kSpecBins)
    spectra_ = QVector<float>(kMaxRows * kSpecBins, -120.0f);
  float* row = spectra_.data() + head_ * kSpecBins;
  const float scale = 2.0f / float(kFft);
  for (int i = 0; i < kSpecBins; ++i) {
    const float mag = std::sqrt(re[i] * re[i] + im[i] * im[i]) * scale;
    row[i] = 20.0f * std::log10(mag + 1.0e-12f);
  }
  head_ = (head_ + 1) % kMaxRows;
  if (filled_ < kMaxRows)
    ++filled_;
  if (!zoomed_) {
    viewLoHz_ = 0.0f;
    viewHiHz_ = fullSpanHz();
  }
  rebuildImage();
}

void WaterfallWidget::rebuildImage() {
  if (filled_ <= 0 || spectra_.size() != kMaxRows * kSpecBins || rateHz_ <= 0) {
    columns_ = QImage();
    return;
  }
  columns_ = QImage(kColumns, filled_, QImage::Format_RGB32);
  const float span = qMax(1.0f, viewHiHz_ - viewLoHz_);
  const int lastBin = kSpecBins - 1;
  for (int y = 0; y < filled_; ++y) {
    const float* spec = spectrumRow(y);
    auto* dst = reinterpret_cast<QRgb*>(columns_.scanLine(y));
    for (int c = 0; c < kColumns; ++c) {
      const float hz = viewLoHz_ + span * float(c) / float(kColumns - 1);
      const float bin = hz * float(kFft) / float(rateHz_);
      const float clamped = qBound(0.0f, bin, float(lastBin));
      const int i0 = int(clamped);
      const int i1 = qMin(i0 + 1, lastBin);
      const float frac = clamped - float(i0);
      const float db = spec[i0] + (spec[i1] - spec[i0]) * frac;
      dst[c] = colorForDb(db);
    }
  }
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

  paintDial(painter, 8);

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

void WaterfallWidget::paintDial(QPainter& painter, int y) const {
  QFont mono("JetBrains Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(12);
  painter.setFont(mono);
  painter.setPen(QColor("#6d8496"));
  painter.drawText(QRect(12, y, 42, 16), Qt::AlignLeft | Qt::AlignVCenter, "DIAL");
  painter.setPen(QColor("#00e5ff"));
  painter.drawText(QRect(54, y, 220, 16), Qt::AlignLeft | Qt::AlignVCenter, dialText_);
}

void WaterfallWidget::paintRuler(QPainter& painter) const {
  const QRect ruler = rulerRect();
  painter.fillRect(ruler, QColor("#0c141c"));
  painter.setPen(QColor("#274150"));
  painter.drawLine(ruler.bottomLeft(), ruler.bottomRight());

  QFont mono("JetBrains Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(12);
  painter.setFont(mono);
  const QFontMetrics metrics(mono);
  const float step = tickStepHz(qMax(1.0f, viewHiHz_ - viewLoHz_));
  const float first = std::ceil(viewLoHz_ / step) * step;
  int lastRight = -1000;
  const int hzReserve = metrics.horizontalAdvance(QStringLiteral("Hz")) + 8;
  for (float hz = first; hz <= viewHiHz_ + 0.01f; hz += step) {
    const int x = xAtHz(hz);
    painter.setPen(QColor("#3d5a6e"));
    painter.drawLine(x, ruler.bottom() - 7, x, ruler.bottom());
    const QString label = QString::number(int(hz + 0.5f));
    const int textWidth = metrics.horizontalAdvance(label);
    QRect box(x - textWidth / 2 - 2, ruler.top(), textWidth + 4, ruler.height() - 8);
    if (box.left() < ruler.left())
      box.moveLeft(ruler.left());
    if (box.right() > ruler.right() - hzReserve)
      box.moveRight(ruler.right() - hzReserve);
    if (box.left() < lastRight + 6)
      continue;
    painter.setPen(QColor("#9fb4c4"));
    painter.drawText(box, Qt::AlignCenter, label);
    lastRight = box.right();
  }
  painter.setPen(QColor("#5a7080"));
  painter.drawText(QRect(ruler.right() - hzReserve, ruler.top(), hzReserve - 4, ruler.height() - 6),
                   Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Hz"));

  if (dragging_) {
    const int left = qBound(ruler.left(), qMin(dragX0_, dragX1_), ruler.right());
    const int right = qBound(ruler.left(), qMax(dragX0_, dragX1_), ruler.right());
    painter.fillRect(QRect(left, ruler.top(), qMax(1, right - left), ruler.height()),
                     QColor(0, 229, 255, 48));
    const QRect plot = plotRect();
    painter.fillRect(QRect(left, plot.top(), qMax(1, right - left), plot.height()),
                     QColor(0, 229, 255, 28));
  }
}

void WaterfallWidget::paintAudio(QPainter& painter) const {
  painter.fillRect(rect(), QColor("#05080c"));
  const QRect plot = plotRect();
  if (!columns_.isNull() && filled_ > 0)
    painter.drawImage(plot, columns_, QRect(0, 0, columns_.width(), columns_.height()));
  paintRuler(painter);
  painter.fillRect(QRect(8, plot.top() + 4, 248, 18), QColor(5, 8, 12, 190));
  paintDial(painter, plot.top() + 5);
}

void WaterfallWidget::wheelEvent(QWheelEvent* event) {
  int delta = event->angleDelta().y();
  if (delta == 0)
    delta = event->pixelDelta().y();
  if (delta == 0) {
    event->ignore();
    return;
  }
  const float full = fullSpanHz();
  const float span = qMax(1.0f, viewHiHz_ - viewLoHz_);
  const float steps = float(delta) / 120.0f;
  const float factor = std::pow(1.25f, -steps);
  float next = span * factor;
  if (next >= full - 1.0f) {
    setView(0.0f, full);
  } else {
    next = qMax(next, kMinSpanHz);
    const float anchor = hzAtX(int(event->position().x()));
    const float place = qBound(0.0f, (anchor - viewLoHz_) / span, 1.0f);
    const float lo = anchor - place * next;
    setView(lo, lo + next);
  }
  event->accept();
}

void WaterfallWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton && rulerRect().contains(event->position().toPoint())) {
    setView(0.0f, fullSpanHz());
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton || filled_ <= 0 ||
      !rulerRect().contains(event->position().toPoint())) {
    QWidget::mousePressEvent(event);
    return;
  }
  dragging_ = true;
  dragX0_ = int(event->position().x());
  dragX1_ = dragX0_;
  event->accept();
  update();
}

void WaterfallWidget::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    dragX1_ = int(event->position().x());
    event->accept();
    update();
    return;
  }
  const bool onRuler = filled_ > 0 && rulerRect().contains(event->position().toPoint());
  setCursor(onRuler ? Qt::SplitHCursor : Qt::ArrowCursor);
  QWidget::mouseMoveEvent(event);
}

void WaterfallWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (!dragging_ || event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  dragging_ = false;
  dragX1_ = int(event->position().x());
  const float lo = hzAtX(qMin(dragX0_, dragX1_));
  const float hi = hzAtX(qMax(dragX0_, dragX1_));
  if (std::fabs(hi - lo) < 40.0f || std::abs(dragX1_ - dragX0_) < 6)
    update();
  else
    setView(lo, hi);
  event->accept();
}

void WaterfallWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && rulerRect().contains(event->position().toPoint())) {
    dragging_ = false;
    setView(0.0f, fullSpanHz());
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}
