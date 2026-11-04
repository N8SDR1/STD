// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPair>
#include <QVector>
#include <QWidget>

// Speech peaks after the microphone leveler, on FreeDV's 0..1 scale.
// The mark is half scale. Drive is not part of this reading.
class RadeMicBar : public QWidget {
  Q_OBJECT

public:
  explicit RadeMicBar(QWidget* parent = nullptr);
  void note(float peak);
  void clear();

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  float level_ = 0.0f;
  class QTimer* decay_ = nullptr;
};

// Last three minutes of SNR, from -10 to 35 dB, as the FreeDV SNR plot.
class RadeSnrTrace : public QWidget {
  Q_OBJECT

public:
  explicit RadeSnrTrace(QWidget* parent = nullptr);
  void push(bool sync, float snrDb);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QVector<QPair<qint64, float>> samples_;
  qint64 lastMs_ = 0;
};
