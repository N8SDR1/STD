// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QVector>
#include <QWidget>

class QByteArray;
class QScrollBar;

// Fldigi Raster::data puts sample 0 at the bottom of the column. Sample 0
// is the first audio in time, and Feld Hell sends the bottom pixel first.
inline int hellTapeRowFromTop(int index, int count) {
  if (count < 1)
    return 0;
  if (index < 0)
    index = 0;
  if (index >= count)
    index = count - 1;
  return count - 1 - index;
}

// Scrolling Feld Hell tape. New columns enter on the right. Each time the
// strip has filled a full width, a copy moves up into the history above it,
// so a long transmission can be read back with the scroll bar or the wheel.
// Hell has no text decode: the tape is the copy, as in Fldigi.
class HellRaster : public QWidget {
  Q_OBJECT

public:
  explicit HellRaster(QWidget* parent = nullptr);
  void addColumn(const QByteArray& column);
  void clear();
  // Fldigi Configure → Modems → Feld Hell → Reverse video
  // (progdefaults.HellBlackboard). Off is black on white. On is white
  // on black, so an inverted image reads. It does not move the carrier.
  void setVideoReverse(bool on);
  bool videoReverse() const { return reverse_; }
  // Full rows kept above the live strip, oldest first.
  int historyRows() const { return int(history_.size()); }
  // The history and the live strip as one picture, for saving.
  QImage tapeImage() const;

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  void invertTape();
  void fitTapeWidth();
  void syncBar();

  QImage tape_;
  QVector<QImage> history_;
  QScrollBar* bar_ = nullptr;
  int filled_ = 0;  // columns written since the strip last moved up
  bool heard_ = false;
  bool reverse_ = false;
};
