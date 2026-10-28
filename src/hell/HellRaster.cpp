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
      "Feld Hell receive. The tape is the Fldigi column detector. DIGU. This does not key."));
}

void HellRaster::clear() {
  tape_.fill(kPaper);
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
    const auto ink = tone(static_cast<unsigned char>(column.at(i)));
    painter.fillRect(tape_.width() - shift, i * row, shift, row, QColor(ink));
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
  painter.fillRect(tapeRect, QColor(kPaper));
  painter.drawImage(tapeRect, tape_);
  painter.setPen(QColor("#2a1404"));
  if (!heard_) {
    painter.setPen(QColor("#5c5146"));
    painter.drawText(tapeRect, Qt::AlignCenter,
                     QStringLiteral("Feld Hell receive\nDIGU  ·  1500 Hz tone  ·  does not key"));
  }
}
