// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HellRaster.h"

#include <algorithm>

#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>

namespace {

constexpr int kColumnWidth = 2;
constexpr int kSampleScale = 3;
constexpr int kPaper = 0xf6f1e6;
constexpr int kInk = 0x1c140c;
constexpr int kMargin = 8;
constexpr int kRowGap = 4;
constexpr int kHistoryRows = 500;

QRgb tone(unsigned char value) {
  const int paperR = (kPaper >> 16) & 0xff;
  const int paperG = (kPaper >> 8) & 0xff;
  const int paperB = kPaper & 0xff;
  const int inkR = (kInk >> 16) & 0xff;
  const int inkG = (kInk >> 8) & 0xff;
  const int inkB = kInk & 0xff;
  const int keep = value;
  const int put = 255 - value;
  return qRgb((paperR * keep + inkR * put) / 255, (paperG * keep + inkG * put) / 255,
              (paperB * keep + inkB * put) / 255);
}

}  // namespace

HellRaster::HellRaster(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("hellReceive"));
  setMinimumHeight(96);
  setAutoFillBackground(false);
  tape_ = QImage(640, 40 * kSampleScale, QImage::Format_RGB32);
  tape_.fill(kPaper);
  setToolTip(QStringLiteral(
      "Feld Hell receive. Hell sends pictures of letters, so the tape is the copy; there is no text "
      "decode, the same as Fldigi. Full rows move up into the history: scroll up to read a long "
      "transmission. The first pixel of a column is the bottom. DIGU. This does not key and does not "
      "send START."));
  bar_ = new QScrollBar(Qt::Vertical, this);
  bar_->setObjectName(QStringLiteral("hellHistoryBar"));
  bar_->setRange(0, 0);
  bar_->hide();
  connect(bar_, &QScrollBar::valueChanged, this, [this](int) { update(); });
}

void HellRaster::fitTapeWidth() {
  // One tape pixel per screen pixel, so the strip is as wide as the pane.
  const int barWidth = bar_ && bar_->isVisible() ? bar_->width() : 0;
  const int want = std::max(64, width() - 2 * kMargin - barWidth);
  if (tape_.isNull() || tape_.width() == want)
    return;
  QImage next(want, tape_.height(), QImage::Format_RGB32);
  next.fill(reverse_ ? kInk : kPaper);
  // Keep the newest columns, right-aligned.
  const int keep = std::min(want, tape_.width());
  QPainter painter(&next);
  painter.drawImage(want - keep, 0, tape_, tape_.width() - keep, 0, keep, tape_.height());
  tape_ = next;
  filled_ = std::min(filled_, want / kColumnWidth);
}

void HellRaster::syncBar() {
  if (!bar_)
    return;
  const bool atBottom = bar_->value() >= bar_->maximum();
  const QSignalBlocker block(bar_);
  bar_->setRange(0, int(history_.size()));
  bar_->setPageStep(1);
  if (atBottom)
    bar_->setValue(bar_->maximum());
  const bool show = !history_.isEmpty();
  if (bar_->isVisible() != show) {
    bar_->setVisible(show);
    fitTapeWidth();
  }
  bar_->setGeometry(width() - 14, kMargin, 12, std::max(16, height() - 2 * kMargin));
}

void HellRaster::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  fitTapeWidth();
  syncBar();
}

void HellRaster::wheelEvent(QWheelEvent* event) {
  if (!bar_ || history_.isEmpty()) {
    QWidget::wheelEvent(event);
    return;
  }
  const int steps = event->angleDelta().y() / 120;
  bar_->setValue(bar_->value() - steps);
  event->accept();
}

QImage HellRaster::tapeImage() const {
  int width = tape_.width();
  for (const QImage& row : history_)
    width = std::max(width, row.width());
  const int rows = int(history_.size()) + 1;
  QImage out(width, rows * (tape_.height() + kRowGap), QImage::Format_RGB32);
  out.fill(reverse_ ? kInk : kPaper);
  QPainter painter(&out);
  int y = 0;
  for (const QImage& row : history_) {
    painter.drawImage(0, y, row);
    y += row.height() + kRowGap;
  }
  painter.drawImage(0, y, tape_);
  return out;
}

void HellRaster::invertTape() {
  if (tape_.isNull())
    return;
  constexpr int paperR = (kPaper >> 16) & 0xff;
  constexpr int paperG = (kPaper >> 8) & 0xff;
  constexpr int paperB = kPaper & 0xff;
  constexpr int inkR = (kInk >> 16) & 0xff;
  constexpr int inkG = (kInk >> 8) & 0xff;
  constexpr int inkB = kInk & 0xff;
  auto flip = [&](QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
      auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        const int r = std::clamp(paperR + inkR - qRed(line[x]), 0, 255);
        const int g = std::clamp(paperG + inkG - qGreen(line[x]), 0, 255);
        const int b = std::clamp(paperB + inkB - qBlue(line[x]), 0, 255);
        line[x] = qRgb(r, g, b);
      }
    }
  };
  flip(tape_);
  for (QImage& row : history_)
    flip(row);
}

void HellRaster::setVideoReverse(bool on) {
  if (reverse_ == on)
    return;
  reverse_ = on;
  invertTape();
  update();
}

void HellRaster::clear() {
  tape_.fill(reverse_ ? kInk : kPaper);
  history_.clear();
  filled_ = 0;
  heard_ = false;
  syncBar();
  update();
}

void HellRaster::addColumn(const QByteArray& column) {
  if (column.isEmpty() || tape_.isNull())
    return;
  const int shift = kColumnWidth;
  if (tape_.width() > shift) {
    const QImage kept = tape_.copy(shift, 0, tape_.width() - shift, tape_.height());
    QPainter painter(&tape_);
    painter.drawImage(0, 0, kept);
  }
  const int samples = column.size();
  const int row = std::max(1, tape_.height() / samples);
  QPainter painter(&tape_);
  for (int i = 0; i < samples; ++i) {
    unsigned char sample = static_cast<unsigned char>(column.at(i));
    if (reverse_)
      sample = static_cast<unsigned char>(255 - sample);
    const auto ink = tone(sample);
    const int y = hellTapeRowFromTop(i, samples) * row;
    painter.fillRect(tape_.width() - shift, y, shift, row, QColor(ink));
  }
  painter.end();
  heard_ = true;
  // A full width since the last move: that strip goes up into the history
  // and the live strip starts clean.
  if (++filled_ * kColumnWidth >= tape_.width()) {
    history_.append(tape_.copy());
    while (history_.size() > kHistoryRows)
      history_.removeFirst();
    tape_.fill(reverse_ ? kInk : kPaper);
    filled_ = 0;
    syncBar();
  }
  update();
}

void HellRaster::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#080d13"));
  if (tape_.isNull())
    return;
  const int barWidth = bar_ && bar_->isVisible() ? bar_->width() + 4 : 0;
  const QRect area = rect().adjusted(kMargin, kMargin, -kMargin - barWidth, -kMargin);
  // Bottom up: the live strip (unless scrolled back), then the history.
  const int hidden = bar_ ? bar_->maximum() - bar_->value() : 0;
  const int rowHeight = tape_.height();
  QVector<const QImage*> rows;
  for (const QImage& row : history_)
    rows.append(&row);
  rows.append(&tape_);
  int last = int(rows.size()) - 1 - hidden;
  if (history_.isEmpty()) {
    // One strip fills the pane, as before.
    painter.fillRect(area, QColor(reverse_ ? kInk : kPaper));
    painter.drawImage(area, tape_);
  } else {
    painter.save();
    painter.setClipRect(area);
    int y = area.bottom() + 1 - rowHeight;
    for (int i = last; i >= 0 && y + rowHeight > area.top(); --i) {
      painter.fillRect(QRect(area.left(), y, area.width(), rowHeight), QColor(reverse_ ? kInk : kPaper));
      painter.drawImage(area.left(), y, *rows[i]);
      y -= rowHeight + kRowGap;
    }
    painter.restore();
  }
  const QRect tapeRect = area;
  painter.setPen(QColor("#2a1404"));
  if (!heard_) {
    painter.setPen(QColor(reverse_ ? "#c4b8a8" : "#5c5146"));
    painter.drawText(tapeRect, Qt::AlignCenter,
                     QStringLiteral("Feld Hell receive\nDIGU  ·  1500 Hz tone  ·  does not key"));
  }
}
