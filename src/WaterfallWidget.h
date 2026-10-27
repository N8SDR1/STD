// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

class QMouseEvent;
class QPainter;
class QTimer;
class QWheelEvent;
class QWindow;

// Waterfall for one real RX-audio channel.
//
// Time runs downward, one pixel per spectrum. Frequency runs across the
// top. The default span is the full receive audio, about 0–4 kHz. The
// wheel zooms around the cursor and a drag pans. Each pixel of the
// visible span is its own FFT bin. Contrast and brightness sit on the
// bottom of this widget and recolor the trace only. A later renderer
// can pass a QWindow
// — including a QVulkanWindow — to setRenderSurface(). Vulkan is not
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
  struct TraceRow {
    qint64 endSample = 0;
    float lo = 0.0f;
    float hi = 0.0f;
    int width = 0;
    QVector<float> level;
  };

  void paintPlaceholder(QPainter& painter) const;
  void paintAudio(QPainter& painter) const;
  void paintRuler(QPainter& painter) const;
  void paintDial(QPainter& painter, int y) const;
  void pushTrace(qint64 endSample);
  void analyze(qint64 endSample, TraceRow* row);
  void scheduleRebuild();
  void rebuildSome();
  void buildToneBar();
  void placeToneBar();
  void setView(float loHz, float hiHz);
  void resetView();
  float fullSpanHz() const;
  float minSpanHz(int width) const;
  bool rowMatches(const TraceRow& row) const;
  QRect rulerRect() const;
  QRect plotRect() const;
  float hzAtX(int x) const;
  int xAtHz(float hz) const;
  float sampleAt(qint64 absolute) const;
  int hopSamples() const;
  TraceRow& rowAt(int age);
  const TraceRow& rowAt(int age) const;

  QWidget* surfaceContainer_ = nullptr;
  QTimer* rebuildTimer_ = nullptr;
  bool radioConnected_ = false;
  QString dialText_ = QString::fromUtf8("—");
  QVector<float> pending_;
  QVector<float> ring_;
  QVector<float> fftRe_;
  QVector<float> fftIm_;
  QVector<TraceRow> rows_;
  int ringWrite_ = 0;
  int ringFilled_ = 0;
  int newest_ = -1;
  int rowCount_ = 0;
  int rateHz_ = 0;
  qint64 totalSamples_ = 0;
  float viewLoHz_ = 0.0f;
  float viewHiHz_ = 4000.0f;
  bool zoomed_ = false;
  bool dragging_ = false;
  int dragX0_ = 0;
  float dragLo_ = 0.0f;
  float dragHi_ = 0.0f;
  QWidget* toneBar_ = nullptr;
  int contrast_ = 50;
  int brightness_ = 50;
};
