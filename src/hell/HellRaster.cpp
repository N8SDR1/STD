// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HellRaster.h"

#include <algorithm>

#include <QPainter>
#include <QPaintEvent>

namespace {

constexpr int kColumnWidth = 2;
constexpr int kSampleScale = 3;
constexpr int kPaper = 0xf6f1e6;
constexpr int kInk = 0x1c140c;

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
      "Feld Hell receive. The first pixel of a column is the bottom, same as Fldigi. "
      "A tone is ink and silence is paper. DIGU. This does not key and does not send START."));
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
  for (int y = 0; y < tape_.height(); ++y) {
    auto* line = reinterpret_cast<QRgb*>(tape_.scanLine(y));
    for (int x = 0; x < tape_.width(); ++x) {
      const int r = std::clamp(paperR + inkR - qRed(line[x]), 0, 255);
      const int g = std::clamp(paperG + inkG - qGreen(line[x]), 0, 255);
      const int b = std::clamp(paperB + inkB - qBlue(line[x]), 0, 255);
      line[x] = qRgb(r, g, b);
    }
  }
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
  heard_ = false;
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
  heard_ = true;
  update();
}

void HellRaster::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#080d13"));
  if (tape_.isNull())
    return;
  const QRect tapeRect = rect().adjusted(8, 8, -8, -8);
  painter.fillRect(tapeRect, QColor(reverse_ ? kInk : kPaper));
  painter.drawImage(tapeRect, tape_);
  painter.setPen(QColor("#2a1404"));
  if (!heard_) {
    painter.setPen(QColor(reverse_ ? "#c4b8a8" : "#5c5146"));
    painter.drawText(tapeRect, Qt::AlignCenter,
                     QStringLiteral("Feld Hell receive\nDIGU  ·  1500 Hz tone  ·  does not key"));
  }
}
