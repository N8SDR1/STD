// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

#include <vector>

class QWidget;

// WSPR-2 from WSJT-X and from JTDX-improved (the same WSPR tab).
// Not MSHV. Callsign and locator stay in Options → User.
//
// Studied from JTDX-improved, which carries the WSJT-X WSPR sources:
//   lib/wsprd/wsprsim_utils.c   pack, sync, interleave (K1JT / K9AN)
//   lib/wsprd/fano.c            K=32 Layland-Lushbaugh (KA9Q / K1JT)
//   lib/wsprd/wsprd_utils.c     unpack, deinterleave
//   WsprTxScheduler.cpp         Tx Pct
//   WSPRBandHopping.cpp         Schedule
//   FrequencyList.cpp           dials
//   doc/user_guide/en/wspr.adoc power, Tx Pct, Band Hopping, Schedule

// "CALL GRID POWER" at 48 kHz. One second of silence, then 162 symbols.
// Empty when the call or the 4-character locator will not pack.
std::vector<float> wsprModulate(const QString& message, int toneHz);

// Hard decision of a clean WSPR-2 frame. Empty when the frame does not
// check. A noisy on-air spot needs the Fano decoder; that is later.
QString wsprDecode(const float* samples, int count, int sampleRateHz, int toneHz);

// WSJT-X / JTDX Tx Pct. Looks 30 s ahead, same as WsprTxScheduler.
// Zero never transmits.
bool wsprTxThisSlot(int txPercent);

// User-guide table. Minute 00 is 160 m, 02 is 80 m, ... 18 is 10 m.
int wsprBandIndex(int utcMinute);
qint64 wsprDialHz(int bandIndex);
QString wsprBandName(int bandIndex);

// 0 sunrise grayline, 1 day, 2 sunset grayline, 3 night.
// grid is the locator from Options → User.
int wsprGrayPeriod(const QString& grid, int grayMinutes, const QDateTime& utc);

// bits is 6 x 10, row-major: sunrise, day, sunset, night, tune, rx only.
// Coordinated band wins when that box is checked. Otherwise one enabled band.
// -1 when nothing in this period is checked.
int wsprPickBand(int period, int coordinated, bool wantTx, const bool bits[60]);
bool wsprTuneBand(int bandIndex, const bool bits[60]);
void wsprLoadBands(bool bits[60]);
int wsprGrayMinutes();

void showWsprBandHopping(QWidget* parent);

class WsprEngine : public QObject {
  Q_OBJECT

public:
  explicit WsprEngine(QObject* parent = nullptr);

  void setArmed(bool armed);
  void setToneHz(int hz);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  void textReady(const QString& text);

private:
  bool armed_ = false;
  int toneHz_ = 1500;
  int rate_ = 48000;
  int decodedPeriod_ = -1;
  qint64 epochMs_ = 0;
  std::vector<float> pending_;
  QString last_;
};
