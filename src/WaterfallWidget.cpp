// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaterfallWidget.h"

#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxRows = 480;
constexpr int kRingBits = 22;
constexpr int kRingSize = 1 << kRingBits;
constexpr int kRulerHeight = 32;
constexpr int kToneBarHeight = 40;
constexpr int kMaxDisplayFft = 8192;
constexpr float kFullHz = 4000.0f;
constexpr float kMinSpanFloor = 40.0f;
constexpr QRgb kField = qRgb(0, 2, 12);

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

int nextPow2(int n) {
  int p = 64;
  while (p < n && p < kMaxDisplayFft)
    p <<= 1;
  return p;
}

float tickStepHz(float span) {
  const float raw = span / 8.0f;
  const float steps[] = {10.0f, 20.0f, 25.0f, 50.0f, 100.0f, 200.0f,
                          250.0f, 500.0f, 1000.0f, 2000.0f};
  for (float step : steps) {
    if (step >= raw)
      return step;
  }
  return 2000.0f;
}

QRgb traceColor(float dbAboveNoise, int contrast, int brightness) {
  const float threshold = 18.0f * (1.0f - float(brightness) / 100.0f);
  const float span = 22.0f - 16.0f * (float(contrast) / 100.0f);
  const float rise = (dbAboveNoise - threshold) / span;
  if (!(rise > 0.0f))
    return kField;
  const float u = rise > 1.0f ? 1.0f : rise;
  if (u < 0.35f) {
    const float k = u / 0.35f;
    return qRgb(0, int(8.0f + 36.0f * k), int(36.0f + 90.0f * k));
  }
  if (u < 0.72f) {
    const float k = (u - 0.35f) / 0.37f;
    return qRgb(0, int(44.0f + 170.0f * k), int(126.0f + 110.0f * k));
  }
  const float k = (u - 0.72f) / 0.28f;
  return qRgb(int(30.0f + 225.0f * k), int(214.0f + 41.0f * k), 255);
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
  rebuildTimer_ = new QTimer(this);
  rebuildTimer_->setInterval(0);
  connect(rebuildTimer_, &QTimer::timeout, this, [this] { rebuildSome(); });
  buildToneBar();
  placeToneBar();
}

void WaterfallWidget::setRadio(bool connected, const QString& dial) {
  radioConnected_ = connected;
  dialText_ = dial.isEmpty() ? QString::fromUtf8("—") : dial;
  if (rowCount_ > 0)
    setAccessibleDescription(QStringLiteral("Receiver audio. Nothing is decoded."));
  else
    setAccessibleDescription(connected ? QStringLiteral("No signal. Connected.")
                                        : QStringLiteral("No signal. Radio not connected."));
  update();
}

void WaterfallWidget::clearAudio() {
  pending_.clear();
  ring_.clear();
  ringWrite_ = 0;
  ringFilled_ = 0;
  totalSamples_ = 0;
  rows_.clear();
  newest_ = -1;
  rowCount_ = 0;
  rateHz_ = 0;
  dragging_ = false;
  if (rebuildTimer_)
    rebuildTimer_->stop();
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

float WaterfallWidget::minSpanHz(int width) const {
  const int pixels = std::max(width, 1);
  if (rateHz_ <= 0)
    return kMinSpanFloor;
  const float fromRing = float(rateHz_) * float(pixels) / float(kRingSize / 2);
  return std::max(kMinSpanFloor, fromRing);
}

QRect WaterfallWidget::rulerRect() const {
  return QRect(0, 0, width(), kRulerHeight);
}

QRect WaterfallWidget::plotRect() const {
  return QRect(0, kRulerHeight, width(), qMax(1, height() - kRulerHeight - kToneBarHeight));
}

float WaterfallWidget::hzAtX(int x) const {
  const QRect ruler = rulerRect();
  const float span = qMax(1.0f, viewHiHz_ - viewLoHz_);
  const float t = float(x - ruler.left()) / float(qMax(1, ruler.width() - 1));
  return viewLoHz_ + qBound(0.0f, t, 1.0f) * span;
}

int WaterfallWidget::xAtHz(float hz) const {
  const QRect ruler = rulerRect();
  const float span = qMax(1.0f, viewHiHz_ - viewLoHz_);
  const float t = qBound(0.0f, (hz - viewLoHz_) / span, 1.0f);
  return ruler.left() + int(t * float(qMax(1, ruler.width() - 1)));
}

void WaterfallWidget::setView(float loHz, float hiHz) {
  const float full = fullSpanHz();
  const int pixels = std::max(1, plotRect().width());
  float span = hiHz - loHz;
  const float minimum = minSpanHz(pixels);
  if (span < minimum) {
    const float mid = 0.5f * (loHz + hiHz);
    span = minimum;
    loHz = mid - span * 0.5f;
    hiHz = loHz + span;
  }
  if (span >= full - 0.5f) {
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
  update();
}

float WaterfallWidget::sampleAt(qint64 absolute) const {
  if (ring_.isEmpty() || absolute < 0 || absolute >= totalSamples_)
    return 0.0f;
  const qint64 age = totalSamples_ - 1 - absolute;
  if (age < 0 || age >= ringFilled_)
    return 0.0f;
  const int mask = ring_.size() - 1;
  return ring_[(ringWrite_ - 1 - int(age)) & mask];
}

int WaterfallWidget::hopSamples() const {
  if (rateHz_ <= 0)
    return 2048;
  return qBound(512, rateHz_ / 10, 8192);
}

WaterfallWidget::TraceRow& WaterfallWidget::rowAt(int age) {
  int index = newest_ - age;
  index %= kMaxRows;
  if (index < 0)
    index += kMaxRows;
  return rows_[index];
}

const WaterfallWidget::TraceRow& WaterfallWidget::rowAt(int age) const {
  int index = newest_ - age;
  index %= kMaxRows;
  if (index < 0)
    index += kMaxRows;
  return rows_[index];
}

bool WaterfallWidget::rowMatches(const TraceRow& row) const {
  return row.width == plotRect().width() && row.level.size() == row.width &&
         std::fabs(row.lo - viewLoHz_) < 0.05f && std::fabs(row.hi - viewHiHz_) < 0.05f;
}

void WaterfallWidget::pushRealAudio(const float* samples, int count, int sampleRateHz) {
  if (!samples || count <= 0 || sampleRateHz < 1000 || surfaceContainer_)
    return;
  if (rateHz_ != sampleRateHz) {
    const bool keepZoom = zoomed_;
    const float lo = viewLoHz_;
    const float hi = viewHiHz_;
    clearAudio();
    rateHz_ = sampleRateHz;
    if (keepZoom)
      setView(lo, hi);
    else {
      viewLoHz_ = 0.0f;
      viewHiHz_ = fullSpanHz();
    }
  }
  if (ring_.size() != kRingSize)
    ring_ = QVector<float>(kRingSize, 0.0f);
  const int mask = kRingSize - 1;
  for (int i = 0; i < count; ++i) {
    const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
    ring_[ringWrite_] = sample;
    ringWrite_ = (ringWrite_ + 1) & mask;
    if (ringFilled_ < kRingSize)
      ++ringFilled_;
  }
  totalSamples_ += count;
  pending_.reserve(pending_.size() + count);
  for (int i = 0; i < count; ++i)
    pending_.push_back(std::isfinite(samples[i]) ? samples[i] : 0.0f);

  const int hop = hopSamples();
  bool drew = false;
  while (pending_.size() >= hop) {
    const qint64 end = totalSamples_ - qint64(pending_.size() - hop);
    pushTrace(end);
    pending_.remove(0, hop);
    drew = true;
  }
  if (drew) {
    setAccessibleDescription(QStringLiteral("Receiver audio. Nothing is decoded."));
    update();
  }
}

void WaterfallWidget::pushTrace(qint64 endSample) {
  if (rows_.size() != kMaxRows)
    rows_.resize(kMaxRows);
  if (rowCount_ < kMaxRows)
    ++rowCount_;
  newest_ = (newest_ + 1) % kMaxRows;
  TraceRow& row = rows_[newest_];
  row.endSample = endSample;
  analyze(endSample, &row);
}

void WaterfallWidget::analyze(qint64 endSample, TraceRow* row) {
  const int width = std::max(0, plotRect().width());
  row->lo = viewLoHz_;
  row->hi = viewHiHz_;
  row->width = width;
  row->level = QVector<float>(std::max(width, 0), -80.0f);
  if (width < 8 || rateHz_ <= 0 || endSample <= 0)
    return;

  const double span = std::max(1.0, double(viewHiHz_ - viewLoHz_));
  const double pixelHz = span / double(width);
  const int nfft = nextPow2(std::max(width, 64));
  int groups = int(std::ceil(double(rateHz_) / (pixelHz * double(nfft))));
  if (groups < 1)
    groups = 1;
  int aperture = groups * nfft;
  if (aperture > kRingSize / 2) {
    groups = std::max(1, (kRingSize / 2) / nfft);
    aperture = groups * nfft;
  }
  // Samples before the stream started are gone. Keep this row dark for
  // the current span so the rebuild timer does not spin on it.
  if (endSample < aperture)
    return;

  if (fftRe_.size() < nfft) {
    fftRe_.resize(nfft);
    fftIm_.resize(nfft);
  }
  std::fill(fftRe_.begin(), fftRe_.begin() + nfft, 0.0f);
  std::fill(fftIm_.begin(), fftIm_.begin() + nfft, 0.0f);

  const double rotate = -6.283185307179586 * double(viewLoHz_) / double(rateHz_);
  double cs = 1.0;
  double sn = 0.0;
  const double dcs = std::cos(rotate);
  const double dsn = std::sin(rotate);
  const qint64 start = endSample - aperture;
  int n = 0;
  for (int i = 0; i < nfft; ++i) {
    double accRe = 0.0;
    double accIm = 0.0;
    for (int g = 0; g < groups; ++g, ++n) {
      const double hann = 0.5 - 0.5 * std::cos(6.283185307179586 * double(n) / double(aperture - 1));
      const double x = double(sampleAt(start + n)) * hann;
      accRe += x * cs;
      accIm += x * sn;
      const double nextCs = cs * dcs - sn * dsn;
      sn = cs * dsn + sn * dcs;
      cs = nextCs;
      if ((n & 1023) == 0) {
        const double gain = 1.0 / std::max(1.0e-12, std::hypot(cs, sn));
        cs *= gain;
        sn *= gain;
      }
    }
    fftRe_[i] = float(accRe);
    fftIm_[i] = float(accIm);
  }
  fft(fftRe_.data(), fftIm_.data(), nfft);

  const double df = double(rateHz_) / (double(groups) * double(nfft));
  QVector<float> db(width, -160.0f);
  int lastBin = -1;
  for (int c = 0; c < width; ++c) {
    const double freq = (double(c) + 0.5) * pixelHz;
    int bin = int(std::lround(freq / df));
    if (bin <= lastBin)
      bin = lastBin + 1;
    if (bin >= nfft)
      bin = nfft - 1;
    lastBin = bin;
    const float mag = std::sqrt(fftRe_[bin] * fftRe_[bin] + fftIm_[bin] * fftIm_[bin]) / float(nfft);
    db[c] = 20.0f * std::log10(mag + 1.0e-12f);
  }
  const int rank = std::clamp(width * 40 / 100, 0, width - 1);
  QVector<float> order = db;
  std::nth_element(order.begin(), order.begin() + rank, order.end());
  const float noise = order[rank];
  for (int c = 0; c < width; ++c)
    row->level[c] = db[c] - noise;
}

void WaterfallWidget::buildToneBar() {
  QSettings settings;
  contrast_ = std::clamp(settings.value(QStringLiteral("waterfall/contrast"), 50).toInt(), 0, 100);
  brightness_ = std::clamp(settings.value(QStringLiteral("waterfall/brightness"), 50).toInt(), 0, 100);

  toneBar_ = new QWidget(this);
  toneBar_->setObjectName("waterfallTone");
  toneBar_->setFixedHeight(kToneBarHeight);
  toneBar_->setAutoFillBackground(true);

  auto* layout = new QHBoxLayout(toneBar_);
  layout->setContentsMargins(12, 4, 12, 4);
  layout->setSpacing(8);

  auto addSlider = [this, layout](const QString& name, const QString& objectName, int value,
                                   int* slot) {
    auto* label = new QLabel(name, toneBar_);
    label->setObjectName("waterfallToneLabel");
    QFont face("Inter");
    face.setPixelSize(12);
    label->setFont(face);
    auto* slider = new QSlider(Qt::Horizontal, toneBar_);
    slider->setObjectName(objectName);
    slider->setRange(0, 100);
    slider->setValue(value);
    slider->setFixedWidth(160);
    slider->setFocusPolicy(Qt::NoFocus);
    slider->setAccessibleName(name);
    slider->setToolTip(name == QLatin1String("Contrast")
                            ? QStringLiteral("Contrast. Display only. Does not change the span.")
                            : QStringLiteral("Brightness. Display only. Does not change the span."));
    auto* readout = new QLabel(QString::number(value), toneBar_);
    readout->setObjectName("waterfallToneValue");
    readout->setFont(face);
    readout->setMinimumWidth(24);
    layout->addWidget(label);
    layout->addWidget(slider);
    layout->addWidget(readout);
    connect(slider, &QSlider::valueChanged, this, [this, slot, readout](int next) {
      *slot = std::clamp(next, 0, 100);
      readout->setText(QString::number(*slot));
      QSettings saved;
      saved.setValue(slot == &contrast_ ? QStringLiteral("waterfall/contrast")
                                         : QStringLiteral("waterfall/brightness"),
                     *slot);
      update();
    });
  };

  addSlider(QStringLiteral("Contrast"), QStringLiteral("waterfallContrast"), contrast_, &contrast_);
  layout->addSpacing(18);
  addSlider(QStringLiteral("Brightness"), QStringLiteral("waterfallBrightness"), brightness_,
            &brightness_);
  layout->addStretch(1);
}

void WaterfallWidget::placeToneBar() {
  if (!toneBar_)
    return;
  toneBar_->setGeometry(0, std::max(0, height() - kToneBarHeight), std::max(1, width()), kToneBarHeight);
  toneBar_->raise();
}

void WaterfallWidget::scheduleRebuild() {
  if (rowCount_ <= 0 || !rebuildTimer_)
    return;
  if (!rebuildTimer_->isActive())
    rebuildTimer_->start();
}

void WaterfallWidget::rebuildSome() {
  int budget = 1;
  for (int age = 0; age < rowCount_ && budget > 0; ++age) {
    TraceRow& row = rowAt(age);
    if (rowMatches(row))
      continue;
    analyze(row.endSample, &row);
    --budget;
    update();
  }
  bool pending = false;
  for (int age = 0; age < rowCount_; ++age) {
    if (!rowMatches(rowAt(age))) {
      pending = true;
      break;
    }
  }
  if (!pending && rebuildTimer_)
    rebuildTimer_->stop();
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
  surfaceContainer_->setGeometry(0, 0, width(), qMax(1, height() - kToneBarHeight));
  surfaceContainer_->show();
  placeToneBar();
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
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  if (rowCount_ > 0)
    paintAudio(painter);
  else
    paintPlaceholder(painter);
}

void WaterfallWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (surfaceContainer_)
    surfaceContainer_->setGeometry(0, 0, width(), qMax(1, height() - kToneBarHeight));
  placeToneBar();
  if (rowCount_ > 0)
    scheduleRebuild();
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
}

void WaterfallWidget::paintAudio(QPainter& painter) const {
  painter.fillRect(rect(), QColor("#00020c"));
  const QRect plot = plotRect();
  const int shown = std::min(rowCount_, plot.height());
  QImage frame(plot.width(), std::max(shown, 1), QImage::Format_RGB32);
  frame.fill(kField);
  for (int y = 0; y < shown; ++y) {
    const TraceRow& row = rowAt(y);
    if (!rowMatches(row))
      continue;
    auto* dst = reinterpret_cast<QRgb*>(frame.scanLine(y));
    const int columns = std::min(int(row.level.size()), plot.width());
    for (int c = 0; c < columns; ++c)
      dst[c] = traceColor(row.level[c], contrast_, brightness_);
  }
  painter.drawImage(plot.topLeft(), frame);
  paintRuler(painter);
  painter.fillRect(QRect(8, plot.top() + 4, 248, 18), QColor(0, 2, 12, 210));
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
  const float next = span * factor;
  if (next >= full - 1.0f) {
    setView(0.0f, full);
  } else {
    const float anchor = hzAtX(int(event->position().x()));
    const float place = qBound(0.0f, (anchor - viewLoHz_) / span, 1.0f);
    const float lo = anchor - place * next;
    setView(lo, lo + next);
  }
  scheduleRebuild();
  event->accept();
}

void WaterfallWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton) {
    setView(0.0f, fullSpanHz());
    scheduleRebuild();
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  dragging_ = true;
  dragX0_ = int(event->position().x());
  dragLo_ = viewLoHz_;
  dragHi_ = viewHiHz_;
  setCursor(Qt::ClosedHandCursor);
  event->accept();
}

void WaterfallWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_) {
    setCursor(Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
    return;
  }
  const int width = std::max(1, plotRect().width());
  const float span = dragHi_ - dragLo_;
  const float shift = -float(int(event->position().x()) - dragX0_) * span / float(width);
  setView(dragLo_ + shift, dragHi_ + shift);
  scheduleRebuild();
  event->accept();
}

void WaterfallWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (!dragging_ || event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  dragging_ = false;
  setCursor(Qt::ArrowCursor);
  event->accept();
}

void WaterfallWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
    setView(0.0f, fullSpanHz());
    scheduleRebuild();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}
