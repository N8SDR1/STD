// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

class WaterfallChrome;
class WaterfallGpu;

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
// inside that span and cannot cross its edges. Each pixel averages about
// three FFT bins, or blends the two bins it sits between. The trace is
// drawn with Qt RHI. Options → Graphics chooses the backend. Auto uses
// Vulkan. Direct3D 11, Direct3D 12, OpenGL, and Painter are explicit.
// Changing the choice destroys the old backend and creates the new one.
// A different graphics API restarts STD, because one window can drive
// only one QRhi backend.
// Contrast, brightness,
// speed, start, bandwidth, ticks, and palette recolor the trace only.
// Nothing here decodes a modem.
// Flatten: the passband shape under one waterfall line, one value per
// column. Each short stretch takes a low percentile (signals sit above it),
// then the stretches are joined by straight lines. MSHV's Flatten Display and
// WSJT-X's Flatten remove this so the band edges read like the middle.
QVector<float> waterfallBaseline(const QVector<float>& db);

// FT8, FT4, FT2, JT65, Q65, and JS8 start at the cursor and run up by
// widthHz. A carrier-centered mode (Hell, RTTY, PSK, MFSK, and the
// other keyboard tones) puts the cursor on the carrier and the band at
// center ± width/2.
inline void channelBandHz(int hz, int widthHz, bool centered, float* low, float* high) {
  const float width = float(widthHz < 1 ? 1 : widthHz);
  const float center = float(hz);
  if (!low || !high)
    return;
  if (!centered) {
    *low = center;
    *high = center + width;
    return;
  }
  *low = center - width / 2.0f;
  *high = center + width / 2.0f;
}

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
  // Loopback of the TCI TX frame, after TX gain. Mixed onto the trace
  // only while setTransmitting(true), at about the height of the other
  // traces, split or locked. Does not decode and does not key.
  void mixTxAudio(const float* samples, int count, int sampleRateHz, bool overFullScale);
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
  // Centered modes draw that width around the marker instead.
  void setChannelWidth(int rxHz, int txHz);
  void setChannelCentered(bool on);
  bool channelCentered() const { return channelCentered_; }
  // Radio PTT is not this. Pass true only when STD is transmitting.
  // This build never does. Receive levels are ignored.
  void setTransmitting(bool on);

  int rxHz() const { return rxHz_; }
  int txHz() const { return txHz_; }
  void setRxHz(int hz);
  void setTxHz(int hz);
  // Move TX to hz. RX moves too while Lock is on.
  void qsyAudio(int hz);
  // Floor is the dB level drawn as the bottom of the palette; Range is how
  // many dB the palette spans above it. With Auto on, Floor follows the
  // measured noise (a few dB under it) and floorDb() reports that level.
  int floorDb() const;
  int rangeDb() const { return rangeDb_; }
  bool autoFloor() const { return autoFloor_; }
  bool flatten() const { return flatten_; }
  // Puts the controls back without writing settings.
  void setTone(int floorDb, int rangeDb, bool autoFloor);
  // vulkan, d3d11, d3d12, opengl, or painter. Auto is Vulkan.
  // Options shows the backend that is actually in use. A different API
  // restarts STD so the window is created on that backend.
  void setGraphicsMode(const QString& mode);
  QString graphicsMode() const;
  // Creates the RHI widget before the top-level window is shown.
  void primeGraphics();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  void frequenciesChanged(int rxHz, int txHz);
  void toneChanged();
  void graphicsModeChanged(const QString& mode);
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
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  struct TraceRow {
    qint64 endSample = 0;
    float lo = 0.0f;
    float hi = 0.0f;
    int width = 0;
    int gen = 0;          // analysisGen_ when built; Flatten bumps it
    float noise = 0.0f;   // this line's noise level, dB
    QVector<float> level; // dB above noise (or above the flattened baseline)
  };
  float floorRelative(const TraceRow& row) const;
  void syncFloorReadout();

  friend class WaterfallChrome;

  void paintPlaceholder(QPainter& painter) const;
  void paintStandbyPlate(QPainter& painter) const;
  void paintAudio(QPainter& painter) const;
  void paintOverlays(QPainter& painter) const;
  QImage spectrumImage() const;
  void ensureGpu();
  void placeLayers();
  void releaseGpu(bool immediate = false);
  void gpuFallback(const QString& why);
  void paintMarkers(QPainter& painter) const;
  void paintRuler(QPainter& painter) const;
  void paintDial(QPainter& painter, int y) const;
  void pushTrace(qint64 endSample);
  void analyze(qint64 endSample, TraceRow* row);
  void noteTxSpill(bool spilling);
  void snapRxTx();
  void cycleRxTx();
  void showRxTxChip();
  void writeAudio(const float* samples, int count, int sampleRateHz, bool blendTx);
  void blendTxIntoRecent(int count);
  void noteRxLevel(const float* samples, int count);
  void stampTxTone(TraceRow* row);
  void refreshTxTone();
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
  WaterfallGpu* gpu_ = nullptr;
  QWidget* chrome_ = nullptr;
  QString graphicsChoice_ = QStringLiteral("vulkan");
  QString sessionMode_;
  bool steppedDown_ = false;
  bool gpuBroken_ = false;
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
  int floorDb_ = -100;
  int rangeDb_ = 30;
  bool autoFloor_ = true;
  bool flatten_ = false;
  bool haveNoise_ = false;
  float autoNoise_ = 0.0f;
  int shownFloor_ = 1 << 30;
  int analysisGen_ = 0;
  int palette_ = 0;
  int traceSpeed_ = 9;
  int tickHz_ = 500;
  int rxHz_ = 1200;
  int txHz_ = 1500;
  int rxWidthHz_ = 50;
  int txWidthHz_ = 50;
  bool channelCentered_ = false;
  bool locked_ = false;
  bool rxFollowsTx_ = true;
  bool rxTxClickPending_ = false;
  bool transmitting_ = false;
  QVector<float> txMix_;
  double txMixRead_ = 0.0;
  int txMixRate_ = 0;
  qint64 lastRxMs_ = 0;
  float rxPeak_ = 0.0f;
  // Display trim on the TX mix. Never raises the mix above full scale.
  float txTrim_ = 1.0f;
  bool txHot_ = false;
  int txHotStreak_ = 0;
  int txCoolStreak_ = 0;
};
