// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QWidget>

class QByteArray;

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

// Scrolling Feld Hell tape. New columns enter on the right.
class HellRaster : public QWidget {
  Q_OBJECT

public:
  explicit HellRaster(QWidget* parent = nullptr);
  void addColumn(const QByteArray& column);
  void clear();

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QImage tape_;
  bool heard_ = false;
};
