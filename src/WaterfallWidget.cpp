// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaterfallWidget.h"

#include <QAction>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
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

struct Stop {
  float pos;
  int r;
  int g;
  int b;
};

// Named waterfall patterns. Stops match Lyra's presets, weak to strong.
struct Palette {
  const char* name;
  Stop stops[8];
  int count;
};

constexpr Palette kPalettes[] = {
    {"Classic",
     {{0.00f, 4, 8, 16},
      {0.15f, 10, 24, 60},
      {0.35f, 20, 80, 180},
      {0.55f, 30, 180, 220},
      {0.75f, 230, 220, 120},
      {0.90f, 240, 100, 40},
      {1.00f, 255, 80, 80},
      {}},
     7},
    {"Inferno",
     {{0.00f, 0, 0, 4},
      {0.20f, 40, 11, 84},
      {0.40f, 101, 21, 110},
      {0.60f, 159, 42, 99},
      {0.75f, 212, 72, 66},
      {0.87f, 245, 125, 21},
      {0.95f, 250, 193, 39},
      {1.00f, 252, 255, 164}},
     8},
    {"Viridis",
     {{0.00f, 68, 1, 84},
      {0.25f, 59, 82, 139},
      {0.50f, 33, 145, 140},
      {0.75f, 94, 201, 98},
      {1.00f, 253, 231, 37},
      {},
      {},
      {}},
     5},
    {"Plasma",
     {{0.00f, 13, 8, 135},
      {0.20f, 84, 2, 163},
      {0.40f, 156, 23, 158},
      {0.60f, 218, 57, 105},
      {0.80f, 249, 149, 63},
      {1.00f, 240, 249, 33},
      {},
      {}},
     6},
    {"Rainbow",
     {{0.00f, 0, 0, 0},
      {0.15f, 0, 0, 128},
      {0.30f, 0, 128, 255},
      {0.45f, 0, 255, 128},
      {0.60f, 255, 255, 0},
      {0.80f, 255, 128, 0},
      {1.00f, 255, 0, 0},
      {}},
     7},
    {"Ocean",
     {{0.00f, 0, 0, 0},
      {0.25f, 8, 25, 64},
      {0.50f, 18, 95, 130},
      {0.75f, 80, 200, 220},
      {1.00f, 230, 245, 255},
      {},
      {},
      {}},
     5},
    {"Night",
     {{0.00f, 0, 0, 0},
      {0.30f, 40, 0, 0},
      {0.60f, 130, 10, 10},
      {0.85f, 220, 80, 30},
      {1.00f, 255, 180, 60},
      {},
      {},
      {}},
     5},
    {"Grayscale",
     {{0.00f, 0, 0, 0}, {1.00f, 255, 255, 255}, {}, {}, {}, {}, {}, {}},
     2},
    {"Amber",
     {{0.00f, 12, 6, 0},
      {0.35f, 90, 40, 0},
      {0.65f, 200, 110, 10},
      {0.85f, 245, 175, 40},
      {1.00f, 255, 230, 130},
      {},
      {},
      {}},
     5},
};

constexpr int kPaletteCount = int(sizeof(kPalettes) / sizeof(kPalettes[0]));

QRgb samplePalette(const Palette& palette, float u) {
  const Stop* left = &palette.stops[0];
  const Stop* right = &palette.stops[palette.count - 1];
  for (int i = 0; i + 1 < palette.count; ++i) {
    if (u >= palette.stops[i].pos && u <= palette.stops[i + 1].pos) {
      left = &palette.stops[i];
      right = &palette.stops[i + 1];
      break;
    }
  }
  float blend = 0.0f;
  if (right->pos > left->pos)
    blend = (u - left->pos) / (right->pos - left->pos);
  auto mix = [blend](int from, int to) {
    return std::clamp(int(from + (to - from) * blend + 0.5f), 0, 255);
  };
  return qRgb(mix(left->r, right->r), mix(left->g, right->g), mix(left->b, right->b));
}

QRgb traceColor(float dbAboveNoise, int contrast, int brightness, int palette) {
  const float threshold = 10.0f * (1.0f - float(brightness) / 100.0f);
  const float span = 12.0f - 8.0f * (float(contrast) / 100.0f);
  const float rise = (dbAboveNoise - threshold) / std::max(1.0f, span);
  if (!(rise > 0.0f))
    return kField;
  const float shaped = std::pow(rise > 1.0f ? 1.0f : rise, 1.8f);
  const int index = std::clamp(palette, 0, kPaletteCount - 1);
  return samplePalette(kPalettes[index], shaped);
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
  setToolTip(QStringLiteral(
      "Click sets RX. Shift+click sets TX. Ctrl+click sets both. Drag pans. This does not key."));
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
  if (hasLimit())
    setView(limitLoHz(), limitHiHz());
  else {
    viewLoHz_ = 0.0f;
    viewHiHz_ = 0.0f;
    update();
  }
}

float WaterfallWidget::limitLoHz() const {
  if (haveBand_)
    return bandLoHz_;
  return 0.0f;
}

float WaterfallWidget::limitHiHz() const {
  if (haveBand_)
    return bandHiHz_;
  if (rateHz_ > 0)
    return float(rateHz_) * 0.5f;
  return 0.0f;
}

bool WaterfallWidget::hasLimit() const {
  return limitHiHz() > limitLoHz() + 1.0f;
}

float WaterfallWidget::minSpanHz(int width) const {
  const int pixels = std::max(width, 1);
  if (rateHz_ <= 0)
    return kMinSpanFloor;
  const float fromRing = float(rateHz_) * float(pixels) / float(kRingSize / 2);
  return std::max(kMinSpanFloor, fromRing);
}

QRect WaterfallWidget::rulerRect() const {
  return QRect(0, kToneBarHeight, width(), kRulerHeight);
}

QRect WaterfallWidget::plotRect() const {
  const int top = kToneBarHeight + kRulerHeight;
  return QRect(0, top, width(), qMax(1, height() - top));
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
  if (hiHz < loHz)
    std::swap(loHz, hiHz);
  const float edgeLo = limitLoHz();
  const float edgeHi = limitHiHz();
  if (!hasLimit()) {
    viewLoHz_ = loHz;
    viewHiHz_ = hiHz;
    zoomed_ = hiHz > loHz + 1.0f;
    update();
    return;
  }
  const int pixels = std::max(1, plotRect().width());
  float span = hiHz - loHz;
  const float minimum = std::min(minSpanHz(pixels), edgeHi - edgeLo);
  if (span < minimum) {
    const float mid = 0.5f * (loHz + hiHz);
    span = minimum;
    loHz = mid - span * 0.5f;
    hiHz = loHz + span;
  }
  if (span >= (edgeHi - edgeLo) - 0.5f) {
    loHz = edgeLo;
    hiHz = edgeHi;
  }
  if (loHz < edgeLo) {
    hiHz += edgeLo - loHz;
    loHz = edgeLo;
  }
  if (hiHz > edgeHi) {
    loHz -= hiHz - edgeHi;
    hiHz = edgeHi;
  }
  if (loHz < edgeLo)
    loHz = edgeLo;
  if (hiHz > edgeHi)
    hiHz = edgeHi;
  viewLoHz_ = loHz;
  viewHiHz_ = hiHz;
  zoomed_ = viewLoHz_ > edgeLo + 0.5f || viewHiHz_ < edgeHi - 0.5f;
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
  const int speed = qBound(1, traceSpeed_, 9);
  return qBound(512, rateHz_ / (speed + 1), 8192);
}

void WaterfallWidget::setFrequencyWindow(int startHz, int widthHz) {
  float lo = float(std::max(0, startHz));
  float hi = lo + float(std::max(1, widthHz));
  const float edgeLo = limitLoHz();
  const float edgeHi = limitHiHz();
  if (edgeHi > edgeLo + 1.0f) {
    lo = std::max(lo, edgeLo);
    hi = std::min(hi, edgeHi);
    if (hi <= lo + 1.0f)
      return;
  }
  setView(lo, hi);
  scheduleRebuild();
}

void WaterfallWidget::setReceiveBand(int lowHz, int highHz) {
  float lo = float(std::min(lowHz, highHz));
  float hi = float(std::max(lowHz, highHz));
  float left = 0.0f;
  float right = 0.0f;
  if (lo < 0.0f && hi > 0.0f) {
    left = 0.0f;
    right = std::max(std::fabs(lo), std::fabs(hi));
  } else {
    left = std::min(std::fabs(lo), std::fabs(hi));
    right = std::max(std::fabs(lo), std::fabs(hi));
  }
  if (!(right > left + 1.0f))
    return;
  const bool keep = zoomed_;
  const float viewLo = viewLoHz_;
  const float viewHi = viewHiHz_;
  haveBand_ = true;
  bandLoHz_ = left;
  bandHiHz_ = right;
  const int rx = clampTone(rxHz_);
  const int tx = locked_ ? rx : clampTone(txHz_);
  if (rx != rxHz_ || tx != txHz_) {
    rxHz_ = rx;
    txHz_ = tx;
    rememberFrequencies();
    emit frequenciesChanged(rxHz_, txHz_);
  }
  if (keep)
    setView(viewLo, viewHi);
  else
    setView(left, right);
  scheduleRebuild();
}

void WaterfallWidget::clearReceiveBand() {
  haveBand_ = false;
  bandLoHz_ = 0.0f;
  bandHiHz_ = 0.0f;
  if (!zoomed_)
    resetView();
  else if (hasLimit())
    setView(viewLoHz_, viewHiHz_);
  scheduleRebuild();
}

void WaterfallWidget::setTraceSpeed(int speed) {
  traceSpeed_ = qBound(1, speed, 9);
}

void WaterfallWidget::setChannelWidth(int rxHz, int txHz) {
  rxWidthHz_ = std::clamp(rxHz, 6, 4000);
  txWidthHz_ = std::clamp(txHz, 6, 4000);
  update();
}

void WaterfallWidget::setRxHz(int hz) {
  rxHz_ = clampTone(hz);
  if (locked_)
    txHz_ = rxHz_;
  rememberFrequencies();
  emit frequenciesChanged(rxHz_, txHz_);
  update();
}

int WaterfallWidget::clampTone(int hz) const {
  int lo = 0;
  int hi = 5000;
  if (hasLimit()) {
    lo = int(limitLoHz());
    hi = int(limitHiHz());
  }
  if (hi < lo)
    std::swap(lo, hi);
  return std::clamp(hz, lo, hi);
}

void WaterfallWidget::rememberFrequencies() {
  QSettings settings;
  settings.setValue(QStringLiteral("waterfall/rxHz"), rxHz_);
  settings.setValue(QStringLiteral("waterfall/txHz"), txHz_);
  settings.setValue(QStringLiteral("waterfall/lock"), locked_);
}

void WaterfallWidget::applyFrequencyClick(int x, Qt::KeyboardModifiers mods) {
  if (!plotRect().contains(x, plotRect().center().y()) && x < 0)
    return;
  const int hz = clampTone(int(std::lround(hzAtX(x))));
  const bool both = locked_ || (mods & Qt::ControlModifier);
  if (both) {
    rxHz_ = hz;
    txHz_ = hz;
  } else if (mods & Qt::ShiftModifier) {
    txHz_ = hz;
  } else {
    rxHz_ = hz;
  }
  rememberFrequencies();
  update();
  emit frequenciesChanged(rxHz_, txHz_);
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
    else if (!haveBand_)
      setView(0.0f, float(sampleRateHz) * 0.5f);
    else
      setView(bandLoHz_, bandHiHz_);
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
  const int nfft = kMaxDisplayFft;
  if (endSample < nfft)
    return;

  if (fftRe_.size() < nfft) {
    fftRe_.resize(nfft);
    fftIm_.resize(nfft);
  }
  const qint64 start = endSample - nfft;
  for (int i = 0; i < nfft; ++i) {
    const double t = double(i) / double(nfft - 1);
    const double window = 0.35875 - 0.48829 * std::cos(6.283185307179586 * t) +
                          0.14128 * std::cos(12.566370614359172 * t) -
                          0.01168 * std::cos(18.84955592153876 * t);
    fftRe_[i] = float(double(sampleAt(start + i)) * window);
    fftIm_[i] = 0.0f;
  }
  fft(fftRe_.data(), fftIm_.data(), nfft);

  const double df = double(rateHz_) / double(nfft);
  QVector<float> db(width, -160.0f);
  QVector<float> heard;
  heard.reserve(nfft / 2);
  for (int bin = 1; bin < nfft / 2; ++bin) {
    const double freq = bin * df;
    if (freq < viewLoHz_ || freq >= viewHiHz_)
      continue;
    const int column = int((freq - viewLoHz_) / pixelHz);
    if (column < 0 || column >= width)
      continue;
    const float mag =
        std::sqrt(fftRe_[bin] * fftRe_[bin] + fftIm_[bin] * fftIm_[bin]) / float(nfft);
    const float value = 20.0f * std::log10(mag + 1.0e-12f);
    heard.push_back(value);
    if (value > db[column])
      db[column] = value;
  }
  float noise = -80.0f;
  if (!heard.isEmpty()) {
    const int rank = std::clamp(int(heard.size() * 40 / 100), 0, int(heard.size()) - 1);
    std::nth_element(heard.begin(), heard.begin() + rank, heard.end());
    noise = heard[rank];
  }
  for (int c = 0; c < width; ++c)
    row->level[c] = db[c] - noise;
}

void WaterfallWidget::buildToneBar() {
  QSettings settings;
  contrast_ = std::clamp(settings.value(QStringLiteral("waterfall/contrast"), 50).toInt(), 0, 100);
  brightness_ = std::clamp(settings.value(QStringLiteral("waterfall/brightness"), 50).toInt(), 0, 100);
  rxHz_ = std::clamp(settings.value(QStringLiteral("waterfall/rxHz"), rxHz_).toInt(), 0, 5000);
  txHz_ = std::clamp(settings.value(QStringLiteral("waterfall/txHz"), txHz_).toInt(), 0, 5000);
  locked_ = settings.value(QStringLiteral("waterfall/lock"), false).toBool();
  const int savedTick = settings.value(QStringLiteral("waterfall/tickHz"), tickHz_).toInt();
  tickHz_ = (savedTick == 100 || savedTick == 250 || savedTick == 500) ? savedTick : 500;
  if (locked_)
    txHz_ = rxHz_;
  palette_ = 0;
  const QString savedPalette =
      settings.value(QStringLiteral("waterfall/palette"), QStringLiteral("Classic")).toString();
  for (int i = 0; i < kPaletteCount; ++i) {
    if (savedPalette == QLatin1String(kPalettes[i].name))
      palette_ = i;
  }

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
    slider->setFixedWidth(72);
    slider->setFocusPolicy(Qt::NoFocus);
    slider->setAccessibleName(name);
    slider->setToolTip(name == QLatin1String("Contrast")
                            ? QStringLiteral("Contrast. Display only. Does not change the span.")
                            : QStringLiteral("Brightness. Display only. Does not change the span."));
    auto* readout = new QLabel(QString::number(value), toneBar_);
    readout->setObjectName(objectName + QStringLiteral("Value"));
    readout->setFont(face);
    readout->setMinimumWidth(24);
    layout->addWidget(label);
    layout->addWidget(slider);
    layout->addWidget(readout);
    connect(slider, &QSlider::valueChanged, this, [this, slot, readout](int next) {
      *slot = std::clamp(next, 0, 100);
      readout->setText(QString::number(*slot));
      update();
      emit toneChanged(contrast_, brightness_);
    });
  };

  addSlider(QStringLiteral("Contrast"), QStringLiteral("waterfallContrast"), contrast_, &contrast_);
  layout->addSpacing(18);
  addSlider(QStringLiteral("Brightness"), QStringLiteral("waterfallBrightness"), brightness_,
            &brightness_);
  layout->addSpacing(12);

  auto* lock = new QPushButton(toneBar_);
  lock->setObjectName("freqLock");
  lock->setCheckable(true);
  lock->setChecked(locked_);
  lock->setText(locked_ ? QStringLiteral("Unlock") : QStringLiteral("Lock"));
  lock->setFixedHeight(22);
  lock->setFocusPolicy(Qt::NoFocus);
  lock->setToolTip(QStringLiteral("Lock TX to RX. A click moves both markers. This does not key."));
  connect(lock, &QPushButton::toggled, this, [this, lock](bool on) {
    locked_ = on;
    if (locked_)
      txHz_ = rxHz_;
    lock->setText(on ? QStringLiteral("Unlock") : QStringLiteral("Lock"));
    rememberFrequencies();
    update();
    emit frequenciesChanged(rxHz_, txHz_);
  });
  auto* equal = new QPushButton(QStringLiteral("RX=TX"), toneBar_);
  equal->setObjectName("freqRxTx");
  equal->setFixedHeight(22);
  equal->setFocusPolicy(Qt::NoFocus);
  equal->setToolTip(QStringLiteral("Set TX to the RX frequency. This does not key."));
  connect(equal, &QPushButton::clicked, this, [this] {
    txHz_ = rxHz_;
    rememberFrequencies();
    update();
    emit frequenciesChanged(rxHz_, txHz_);
  });
  layout->addWidget(lock);
  layout->addWidget(equal);
  layout->addStretch(1);

  auto* tickLabel = new QLabel(QStringLiteral("Ticks"), toneBar_);
  tickLabel->setObjectName("waterfallToneLabel");
  QFont tickFace("Inter");
  tickFace.setPixelSize(12);
  tickLabel->setFont(tickFace);
  auto* tickButton = new QToolButton(toneBar_);
  tickButton->setObjectName("waterfallTicks");
  tickButton->setPopupMode(QToolButton::InstantPopup);
  tickButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  tickButton->setFocusPolicy(Qt::NoFocus);
  tickButton->setFont(tickFace);
  tickButton->setText(QStringLiteral("%1 Hz").arg(tickHz_));
  tickButton->setAccessibleName("Waterfall ticks");
  tickButton->setToolTip(QStringLiteral("Ruler marks. 100, 250, or 500 Hz. Does not change the span."));
  auto* tickMenu = new QMenu(tickButton);
  tickMenu->setObjectName("waterfallTicksMenu");
  const int tickSteps[] = {100, 250, 500};
  for (int step : tickSteps) {
    QAction* action = tickMenu->addAction(QStringLiteral("%1 Hz").arg(step));
    action->setCheckable(true);
    action->setChecked(step == tickHz_);
    action->setData(step);
  }
  connect(tickMenu, &QMenu::triggered, this, [this, tickButton](QAction* chosen) {
    if (!chosen)
      return;
    const int step = chosen->data().toInt();
    if (step != 100 && step != 250 && step != 500)
      return;
    tickHz_ = step;
    const auto actions = tickButton->menu()->actions();
    for (QAction* action : actions)
      action->setChecked(action == chosen);
    tickButton->setText(QStringLiteral("%1 Hz").arg(tickHz_));
    QSettings saved;
    saved.setValue(QStringLiteral("waterfall/tickHz"), tickHz_);
    update();
  });
  tickButton->setMenu(tickMenu);
  layout->addWidget(tickLabel);
  layout->addWidget(tickButton);
  layout->addSpacing(12);

  auto* paletteLabel = new QLabel(QStringLiteral("Palette"), toneBar_);
  paletteLabel->setObjectName("waterfallToneLabel");
  QFont face("Inter");
  face.setPixelSize(12);
  paletteLabel->setFont(face);
  auto* paletteButton = new QToolButton(toneBar_);
  paletteButton->setObjectName("waterfallPalette");
  paletteButton->setPopupMode(QToolButton::InstantPopup);
  paletteButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  paletteButton->setFocusPolicy(Qt::NoFocus);
  paletteButton->setFont(face);
  paletteButton->setText(QString::fromUtf8(kPalettes[palette_].name));
  paletteButton->setAccessibleName("Waterfall palette");
  paletteButton->setToolTip(QStringLiteral("Color pattern. Display only. Does not change the span."));
  auto* menu = new QMenu(paletteButton);
  menu->setObjectName("waterfallPaletteMenu");
  for (int i = 0; i < kPaletteCount; ++i) {
    QAction* action = menu->addAction(QString::fromUtf8(kPalettes[i].name));
    action->setCheckable(true);
    action->setChecked(i == palette_);
    action->setData(i);
  }
  connect(menu, &QMenu::triggered, this, [this, paletteButton](QAction* chosen) {
    if (!chosen)
      return;
    palette_ = std::clamp(chosen->data().toInt(), 0, kPaletteCount - 1);
    const auto actions = paletteButton->menu()->actions();
    for (QAction* action : actions)
      action->setChecked(action == chosen);
    paletteButton->setText(QString::fromUtf8(kPalettes[palette_].name));
    QSettings saved;
    saved.setValue(QStringLiteral("waterfall/palette"), paletteButton->text());
    update();
  });
  paletteButton->setMenu(menu);
  layout->addWidget(paletteLabel);
  layout->addWidget(paletteButton);
}

void WaterfallWidget::setTone(int contrast, int brightness) {
  contrast_ = std::clamp(contrast, 0, 100);
  brightness_ = std::clamp(brightness, 0, 100);
  const struct {
    const char* slider;
    const char* readout;
    int value;
  } rows[] = {{"waterfallContrast", "waterfallContrastValue", contrast_},
              {"waterfallBrightness", "waterfallBrightnessValue", brightness_}};
  for (const auto& row : rows) {
    if (auto* slider = findChild<QSlider*>(QLatin1String(row.slider))) {
      const QSignalBlocker block(slider);
      slider->setValue(row.value);
    }
    if (auto* readout = findChild<QLabel*>(QLatin1String(row.readout)))
      readout->setText(QString::number(row.value));
  }
  update();
}

void WaterfallWidget::placeToneBar() {
  if (!toneBar_)
    return;
  toneBar_->setGeometry(0, 0, std::max(1, width()), kToneBarHeight);
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
  surfaceContainer_->setGeometry(0, kToneBarHeight, width(), qMax(1, height() - kToneBarHeight));
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
    surfaceContainer_->setGeometry(0, kToneBarHeight, width(), qMax(1, height() - kToneBarHeight));
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
  const float step = float(tickHz_);
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
      dst[c] = traceColor(row.level[c], contrast_, brightness_, palette_);
  }
  painter.drawImage(plot.topLeft(), frame);
  paintMarkers(painter);
  paintRuler(painter);
  painter.fillRect(QRect(8, plot.top() + 4, 248, 18), QColor(0, 2, 12, 210));
  paintDial(painter, plot.top() + 5);
}

void WaterfallWidget::paintMarkers(QPainter& painter) const {
  const QRect plot = plotRect();
  if (plot.width() < 8)
    return;
  const int labelY = plot.top() + 26;

  auto draw = [&](int hz, int widthHz, const QColor& color, const QString& name, int y) {
    const float lo = float(hz);
    const float hi = float(hz + std::max(1, widthHz));
    if (hi < viewLoHz_ || lo > viewHiHz_)
      return;
    const int x0 = xAtHz(std::max(lo, viewLoHz_));
    const int x1 = xAtHz(std::min(hi, viewHiHz_));
    const int left = std::min(x0, x1);
    const int right = std::max(x0, x1);
    QColor fill = color;
    fill.setAlpha(56);
    painter.fillRect(QRect(left, plot.top(), std::max(2, right - left), plot.height()), fill);
    painter.setPen(QPen(color, 1));
    painter.drawLine(xAtHz(lo), plot.top(), xAtHz(lo), plot.bottom());
    painter.drawLine(xAtHz(hi), plot.top(), xAtHz(hi), plot.bottom());
    QFont mono("JetBrains Mono");
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(11);
    painter.setFont(mono);
    const QString label = QStringLiteral("%1 %2").arg(name).arg(hz);
    const int textWidth = painter.fontMetrics().horizontalAdvance(label) + 8;
    int textLeft = left + 4;
    if (textLeft + textWidth > plot.right())
      textLeft = std::max(plot.left(), left - textWidth - 2);
    painter.fillRect(QRect(textLeft, y, textWidth, 16), QColor(0, 2, 12, 210));
    painter.setPen(color);
    painter.drawText(textLeft + 4, y + 12, label);
  };

  const bool stack = std::abs(rxHz_ - txHz_) < rxWidthHz_ + txWidthHz_;
  draw(rxHz_, rxWidthHz_, QColor(0, 240, 0), QStringLiteral("RX"), labelY);
  draw(txHz_, txWidthHz_, QColor(250, 60, 60), QStringLiteral("TX"), stack ? labelY + 18 : labelY);
}

void WaterfallWidget::wheelEvent(QWheelEvent* event) {
  int delta = event->angleDelta().y();
  if (delta == 0)
    delta = event->pixelDelta().y();
  if (delta == 0) {
    event->ignore();
    return;
  }
  if (!hasLimit()) {
    event->ignore();
    return;
  }
  const float edgeLo = limitLoHz();
  const float edgeHi = limitHiHz();
  const float full = edgeHi - edgeLo;
  const float span = qMax(1.0f, viewHiHz_ - viewLoHz_);
  const float steps = float(delta) / 120.0f;
  const float factor = std::pow(1.25f, -steps);
  const float next = span * factor;
  if (next >= full - 1.0f) {
    setView(edgeLo, edgeHi);
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
    if (hasLimit())
      setView(limitLoHz(), limitHiHz());
    scheduleRebuild();
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  pressPending_ = true;
  dragging_ = false;
  dragX0_ = int(event->position().x());
  dragLo_ = viewLoHz_;
  dragHi_ = viewHiHz_;
  event->accept();
}

void WaterfallWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!pressPending_ && !dragging_) {
    setCursor(Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
    return;
  }
  const int x = int(event->position().x());
  if (pressPending_ && !dragging_ && std::abs(x - dragX0_) >= 5) {
    dragging_ = true;
    setCursor(Qt::ClosedHandCursor);
  }
  if (!dragging_) {
    event->accept();
    return;
  }
  const int width = std::max(1, plotRect().width());
  const float span = dragHi_ - dragLo_;
  const float shift = -float(x - dragX0_) * span / float(width);
  setView(dragLo_ + shift, dragHi_ + shift);
  scheduleRebuild();
  event->accept();
}

void WaterfallWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || (!pressPending_ && !dragging_)) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  const bool wasDrag = dragging_;
  const int x = int(event->position().x());
  const auto mods = event->modifiers();
  pressPending_ = false;
  dragging_ = false;
  setCursor(Qt::ArrowCursor);
  if (!wasDrag && plotRect().contains(event->position().toPoint())) {
    pendingClickX_ = x;
    pendingMods_ = mods;
    QTimer::singleShot(180, this, [this, x, mods] {
      if (pendingClickX_ != x || pendingMods_ != mods)
        return;
      pendingClickX_ = -1;
      applyFrequencyClick(x, mods);
    });
  }
  event->accept();
}

void WaterfallWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    pendingClickX_ = -1;
    pressPending_ = false;
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
    if (hasLimit())
      setView(limitLoHz(), limitHiHz());
    scheduleRebuild();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}
