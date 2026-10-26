// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPainter;
class QWheelEvent;
class QWindow;

// Waterfall for one real RX-audio channel.
//
// With no samples it paints a graticule and an empty state. Type-1 TCI
// audio scrolls under a frequency ruler. The default span is the full
// receive audio, about 0–4 kHz. The wheel zooms that span, and a drag on
// the ruler sets it. A later renderer can pass a QWindow — including a
// QVulkanWindow — to setRenderSurface(). This widget embeds that surface
// with QWidget::createWindowContainer and stops painting. Vulkan is not
// linked here. Nothing here decodes a modem.
class WaterfallWidget : public QWidget {
  Q_OBJECT
public:
  explicit WaterfallWidget(QWidget* parent = nullptr);

  // Takes ownership of surface. Pass nullptr to drop it and paint the
  // placeholder again. The container deletes the window when it is destroyed.
  void setRenderSurface(QWindow* surface);

  // dial is the VFO text. connected changes the standby line under "No signal".
  void setRadio(bool connected, const QString& dial);

  // Real mono samples, already one channel. sampleRateHz is the frame rate.
  void pushRealAudio(const float* samples, int count, int sampleRateHz);
  void clearAudio();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  void paintPlaceholder(QPainter& painter) const;
  void paintAudio(QPainter& painter) const;
  void paintRuler(QPainter& painter) const;
  void paintDial(QPainter& painter, int y) const;
  void appendSpectrum(const float* samples, int count);
  void rebuildImage();
  void setView(float loHz, float hiHz);
  void resetView();
  float fullSpanHz() const;
  QRect rulerRect() const;
  QRect plotRect() const;
  float hzAtX(int x) const;
  int xAtHz(float hz) const;
  const float* spectrumRow(int age) const;

  QWidget* surfaceContainer_ = nullptr;
  bool radioConnected_ = false;
  QString dialText_ = QString::fromUtf8("—");
  QVector<float> pending_;
  QVector<float> spectra_;
  QImage columns_;
  int rateHz_ = 0;
  int filled_ = 0;
  int head_ = 0;
  float viewLoHz_ = 0.0f;
  float viewHiHz_ = 4000.0f;
  bool zoomed_ = false;
  bool dragging_ = false;
  int dragX0_ = 0;
  int dragX1_ = 0;
};
