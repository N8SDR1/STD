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

// Decoder input, from -60 to 0 dBFS. CLIP means the modem audio reached full scale.
class RadeRxBar : public QWidget {
  Q_OBJECT

public:
  explicit RadeRxBar(QWidget* parent = nullptr);
  void setLevel(float db, bool clip);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  float db_ = -120.0f;
  float peak_ = -120.0f;
  bool clip_ = false;
  class QTimer* decay_ = nullptr;
};

// Live SNR from -10 to 40 dB. Red below 0, amber below 6, green from 6 up.
class RadeSnrBar : public QWidget {
  Q_OBJECT

public:
  explicit RadeSnrBar(QWidget* parent = nullptr);
  void setSnr(bool sync, float db);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  bool sync_ = false;
  float db_ = -10.0f;
};

// Green while the modem is locked, dark red while it is not.
class RadeSyncLamp : public QWidget {
  Q_OBJECT

public:
  explicit RadeSyncLamp(QWidget* parent = nullptr);
  void setSynced(bool on);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  bool on_ = false;
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
