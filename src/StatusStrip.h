// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

// Bottom strip: mode, dial, an on-air PTT lamp, and the radio line.
// The lamp follows the radio. It does not key.
class StatusStrip : public QWidget {
  Q_OBJECT

public:
  explicit StatusStrip(QWidget* parent = nullptr);

  void setMode(const QString& text);
  void setDigitalMode(const QString& mode);
  void setDial(const QString& text);
  void setPtt(bool onAir, bool connected);
  void setRadio(bool connected, const QString& line);
  // Shows "TX gain is too hot" beside the TX gain. Does not change it and does not key.
  void setTxHot(bool hot);
  // The clock chip: hidden unless the FT decodes say this PC's clock is off.
  // seconds > 0 is fast. mode names the mode the DTs came from.
  void setClockOff(bool off, double seconds, const QString& mode, int decodes);
  // samples are float -1..1. A null pointer clears the meter.
  void setRxLevel(const float* samples, int count);
  // Re-reads the spot chip from the Network Configuration settings.
  void refreshSpot();

signals:
  // The spot chip was clicked. key is the setting it changed.
  void spotToggled(const QString& key, bool on);
  // Right-click on the spot chip: "psk", "wsprnet", or "freedv".
  void spotMapRequested(const QString& service);

private:
  void refreshRxLevel();
  void applyDecoderControls(const QString& mode);
  void applySpot(const QString& mode);

  QString spotMode_;
  QWidget* spotSep_ = nullptr;

  double rxMeanSq_ = 0.0;
  qint64 rxHeardMs_ = 0;
};
