// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeMeters.h"

#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kHalf = 0.5f;
constexpr float kSnrFloor = -10.0f;
constexpr float kSnrCeil = 35.0f;
constexpr qint64 kSnrSpanMs = 180000;

}  // namespace

RadeMicBar::RadeMicBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("radeMicLevel"));
  setFixedSize(108, 26);
  setToolTip(QStringLiteral(
      "Speech peaks after the microphone leveler, from 0 to 1. "
      "This is the same scale as FreeDV's Frm Mic plot. "
      "The mark is 0.5, half scale. The FreeDV manual says the level should average about half scale, "
      "and the peaks should not clip at the top. "
      "Gain, before this bar, raises or cuts the microphone before the leveler. "
      "The Drive slider is the modem level. It does not move this bar. "
      "This does not key. START is never sent."));
  decay_ = new QTimer(this);
  decay_->setInterval(50);
  connect(decay_, &QTimer::timeout, this, [this] {
    if (level_ <= 0.004f) {
      level_ = 0.0f;
      decay_->stop();
    } else {
      level_ *= 0.995f;
    }
    update();
  });
}

void RadeMicBar::note(float peak) {
  peak = std::clamp(peak, 0.0f, 1.0f);
  if (peak > level_)
    level_ = peak;
  if (level_ > 0.0f && !decay_->isActive())
    decay_->start();
  update();
}

void RadeMicBar::clear() {
  level_ = 0.0f;
  if (decay_)
    decay_->stop();
  update();
}

void RadeMicBar::paintEvent(QPaintEvent*) {
  QPainter paint(this);
  const QRect track(0, height() - 12, width(), 10);
  paint.fillRect(track, QColor(18, 30, 42));
  paint.setPen(QColor(58, 78, 94));
  paint.drawRect(track.adjusted(0, 0, -1, -1));
  const int inner = std::max(1, track.width() - 2);
  const int fillW = int(std::clamp(level_, 0.0f, 1.0f) * float(inner));
  QColor fill(72, 196, 186);
  if (level_ >= 0.92f)
    fill = QColor(214, 92, 78);
  if (fillW > 0)
    paint.fillRect(track.left() + 1, track.top() + 1, fillW, track.height() - 2, fill);
  const int mark = track.left() + 1 + int(kHalf * float(inner));
  paint.setPen(QColor(232, 214, 160));
  paint.drawLine(mark, track.top() - 2, mark, track.bottom() + 1);
  paint.setPen(QColor(186, 198, 208));
  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(10);
  paint.setFont(font);
  const QString label = QStringLiteral("0.5");
  const int textW = paint.fontMetrics().horizontalAdvance(label);
  paint.drawText(std::clamp(mark - textW / 2, 0, width() - textW), 10, label);
}

RadeRxBar::RadeRxBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("radeRxLevel"));
  setFixedSize(148, 26);
  setToolTip(QStringLiteral(
      "Decoder input, in dBFS, from -60 to 0. "
      "CLIP means the modem audio reached full scale. "
      "Auto RX, beside this bar, turns that level down when it clips "
      "and brings it back when the peaks stay low. "
      "This does not key. START is never sent."));
  decay_ = new QTimer(this);
  decay_->setInterval(80);
  connect(decay_, &QTimer::timeout, this, [this] {
    peak_ = std::max(db_, peak_ - 0.6f);
    if (peak_ <= -60.0f && !clip_)
      decay_->stop();
    update();
  });
}

void RadeRxBar::setLevel(float db, bool clip) {
  db_ = db;
  clip_ = clip;
  if (db > peak_)
    peak_ = db;
  if ((peak_ > -60.0f || clip_) && decay_ && !decay_->isActive())
    decay_->start();
  update();
}

void RadeRxBar::paintEvent(QPaintEvent*) {
  QPainter paint(this);
  const QRect track(0, height() - 14, width() - 46, 12);
  paint.fillRect(track, QColor(18, 30, 42));
  paint.setPen(QColor(58, 78, 94));
  paint.drawRect(track.adjusted(0, 0, -1, -1));
  const float shown = std::clamp(db_, -60.0f, 0.0f);
  const int inner = std::max(1, track.width() - 2);
  const int fillW = db_ <= -100.0f ? 0 : int(((shown + 60.0f) / 60.0f) * float(inner));
  if (fillW > 0)
    paint.fillRect(track.left() + 1, track.top() + 1, fillW, track.height() - 2,
                   clip_ ? QColor(214, 92, 78) : QColor(72, 196, 186));
  if (peak_ > -60.0f && peak_ <= 0.0f) {
    const int mark = track.left() + 1 + int(((std::clamp(peak_, -60.0f, 0.0f) + 60.0f) / 60.0f) * float(inner));
    paint.setPen(QColor(232, 214, 160));
    paint.drawLine(mark, track.top(), mark, track.bottom());
  }
  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(11);
  font.setBold(clip_);
  paint.setFont(font);
  paint.setPen(clip_ ? QColor(214, 92, 78) : QColor(186, 198, 208));
  const QString label = clip_ ? QStringLiteral("CLIP")
                              : (db_ <= -100.0f ? QStringLiteral("--")
                                                : QString::number(int(std::lround(db_))));
  paint.drawText(track.right() + 6, height() - 3, label);
}

RadeSnrBar::RadeSnrBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("radeSnrBar"));
  setFixedSize(132, 26);
  setToolTip(QStringLiteral(
      "Live SNR, from -10 to 40 dB. Red is below 0, amber is below 6, and green is 6 and above. "
      "The trace beside it is still the last 3 minutes. "
      "It does not mute. This does not key. START is never sent."));
}

void RadeSnrBar::setSnr(bool sync, float db) {
  sync_ = sync;
  db_ = db;
  update();
}

void RadeSnrBar::paintEvent(QPaintEvent*) {
  QPainter paint(this);
  const QRect track(0, 4, width(), 12);
  paint.fillRect(track, QColor(18, 30, 42));
  paint.setPen(QColor(58, 78, 94));
  paint.drawRect(track.adjusted(0, 0, -1, -1));
  if (sync_) {
    const float norm = std::clamp((db_ + 10.0f) / 50.0f, 0.0f, 1.0f);
    QColor fill(61, 204, 122);
    if (db_ < 0.0f)
      fill = QColor(214, 92, 78);
    else if (db_ < 6.0f)
      fill = QColor(239, 179, 64);
    const int fillW = int(norm * float(std::max(1, track.width() - 2)));
    if (fillW > 0)
      paint.fillRect(track.left() + 1, track.top() + 1, fillW, track.height() - 2, fill);
  }
  paint.setPen(QColor(90, 110, 124));
  for (int db = -10; db <= 40; db += 10) {
    const int x = track.left() + int((db + 10) / 50.0 * double(track.width() - 1));
    paint.drawLine(x, track.bottom() + 1, x, track.bottom() + 4);
  }
}

RadeSyncLamp::RadeSyncLamp(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("radeSyncLamp"));
  setFixedSize(14, 14);
  setToolTip(QStringLiteral(
      "Green while synced. Dark red while not. "
      "This does not key. START is never sent."));
}

void RadeSyncLamp::setSynced(bool on) {
  if (on_ == on)
    return;
  on_ = on;
  update();
}

void RadeSyncLamp::paintEvent(QPaintEvent*) {
  QPainter paint(this);
  paint.setRenderHint(QPainter::Antialiasing, true);
  paint.setPen(Qt::NoPen);
  paint.setBrush(on_ ? QColor(52, 199, 89) : QColor(120, 32, 32));
  paint.drawEllipse(rect().adjusted(1, 1, -1, -1));
}

RadeSnrTrace::RadeSnrTrace(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("radeSnrTrace"));
  setFixedSize(168, 36);
  setFocusPolicy(Qt::NoFocus);
  setToolTip(QStringLiteral(
      "SNR for the last 3 minutes. The scale runs from -10 to 35 dB, "
      "the same range as the FreeDV SNR plot. Idle sits on the bottom. "
      "It does not mute. This does not key. START is never sent."));
}

void RadeSnrTrace::push(bool sync, float snrDb) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!samples_.isEmpty() && now - lastMs_ < 500)
    return;
  lastMs_ = now;
  const float y = sync ? std::clamp(snrDb, kSnrFloor, kSnrCeil) : kSnrFloor;
  samples_.append(qMakePair(now, y));
  const qint64 cutoff = now - kSnrSpanMs;
  while (!samples_.isEmpty() && samples_.first().first < cutoff)
    samples_.removeFirst();
  update();
}

void RadeSnrTrace::paintEvent(QPaintEvent*) {
  QPainter paint(this);
  const QRect plot = rect().adjusted(1, 2, -2, -2);
  paint.fillRect(rect(), QColor(14, 24, 34));
  paint.setPen(QColor(48, 66, 80));
  paint.drawRect(rect().adjusted(0, 0, -1, -1));
  if (plot.width() < 2 || samples_.size() < 2)
    return;
  const qint64 end = samples_.last().first;
  const qint64 begin = end - kSnrSpanMs;
  const auto xOf = [&](qint64 t) {
    return plot.left() + int((t - begin) * plot.width() / kSnrSpanMs);
  };
  const auto yOf = [&](float db) {
    const float span = kSnrCeil - kSnrFloor;
    const float norm = (db - kSnrFloor) / span;
    return plot.bottom() - int(norm * float(plot.height()));
  };
  paint.setPen(QColor(40, 58, 70));
  const int zero = yOf(0.0f);
  paint.drawLine(plot.left(), zero, plot.right(), zero);
  paint.setPen(QColor(94, 196, 168));
  QPoint previous(xOf(samples_.first().first), yOf(samples_.first().second));
  for (int i = 1; i < samples_.size(); ++i) {
    const QPoint next(xOf(samples_.at(i).first), yOf(samples_.at(i).second));
    paint.drawLine(previous, next);
    previous = next;
  }
}
