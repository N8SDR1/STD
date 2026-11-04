// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeMeters.h"

#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

#include <algorithm>

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
      "Gain, before this bar, raises the microphone before the leveler. "
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
