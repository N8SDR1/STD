// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QWidget>

class QByteArray;

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
