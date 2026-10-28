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
// top. The outer span is the receive band reported by TCI
// (rx_filter_band). The wheel, a drag, and the mode window can narrow
// inside that span and cannot cross its edges. Each pixel is one display
// bin from a short FFT, the same render as before the protocol markers.
// Contrast, brightness, speed, start, bandwidth, ticks, and palette sit
// on the top of this widget and recolor the trace only. A
// later renderer
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

  // Narrow the ruler. The result stays inside the reported receive band.
  void setFrequencyWindow(int startHz, int widthHz);

  // rx_filter_band edges, in Hz, as the radio sent them. This is the
  // outer limit. A later narrow view cannot cross it.
  void setReceiveBand(int lowHz, int highHz);
  void clearReceiveBand();

  // 1 is slow, 9 is the FT8 default and matches the earlier hop.
  void setTraceSpeed(int speed);
  // Width of the green RX band and the red TX band, in Hz.
  // FT8 is 50. The band starts at the marker and runs upward.
  void setChannelWidth(int rxHz, int txHz);
  // Radio PTT is not this. Pass true only when STD is transmitting.
  // This build never does. Receive levels are ignored.
  void setTransmitting(bool on);

  int rxHz() const { return rxHz_; }
  int txHz() const { return txHz_; }
  void setRxHz(int hz);
  void setTxHz(int hz);
  int contrast() const { return contrast_; }
  int brightness() const { return brightness_; }
  // Puts the sliders back without writing settings.
  void setTone(int contrast, int brightness);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  void frequenciesChanged(int rxHz, int txHz);
  void toneChanged(int contrast, int brightness);
  // True only while STD itself is transmitting and the TX spectrum is
  // wider than the red channel. Receive audio never raises this.
  void txHotChanged(bool hot);

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
  void paintMarkers(QPainter& painter) const;
  void paintRuler(QPainter& painter) const;
  void paintDial(QPainter& painter, int y) const;
  void pushTrace(qint64 endSample);
  void analyze(qint64 endSample, TraceRow* row);
  bool txSpectrumSpills(double df, int nfft, float noiseDb) const;
  void noteTxSpill(bool spilling);
  void clearTxHot();
  void paintTxHot(QPainter& painter) const;
  void scheduleRebuild();
  void rebuildSome();
  void buildToneBar();
  void placeToneBar();
  void setView(float loHz, float hiHz);
  void resetView();
  void applyFrequencyClick(int x, Qt::KeyboardModifiers mods);
  void rememberFrequencies();
  int clampTone(int hz) const;
  float limitLoHz() const;
  float limitHiHz() const;
  bool hasLimit() const;
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
  float viewHiHz_ = 0.0f;
  bool haveBand_ = false;
  float bandLoHz_ = 0.0f;
  float bandHiHz_ = 0.0f;
  bool zoomed_ = false;
  bool dragging_ = false;
  bool pressPending_ = false;
  int pendingClickX_ = 0;
  Qt::KeyboardModifiers pendingMods_ = Qt::NoModifier;
  int dragX0_ = 0;
  float dragLo_ = 0.0f;
  float dragHi_ = 0.0f;
  QWidget* toneBar_ = nullptr;
  int contrast_ = 50;
  int brightness_ = 50;
  int palette_ = 0;
  int traceSpeed_ = 9;
  int tickHz_ = 500;
  int rxHz_ = 1200;
  int txHz_ = 1500;
  int rxWidthHz_ = 50;
  int txWidthHz_ = 50;
  bool locked_ = false;
  bool transmitting_ = false;
  bool txHot_ = false;
  int txHotStreak_ = 0;
  int txCoolStreak_ = 0;
};
