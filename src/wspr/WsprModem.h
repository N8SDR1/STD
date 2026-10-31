// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <vector>

class QThread;
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

// WSPR occupies 200 Hz of audio, 1400 to 1600 Hz, on every band. WSJT-X
// and JTDX hold the WSPR Tx frequency inside that window. The receive side
// decodes 1390 to 1610 Hz, the wsprd default, wherever the RX marker sits.
constexpr int kWsprTxLowHz = 1400;
constexpr int kWsprTxHighHz = 1600;
int wsprClampTxHz(int hz);

// "37 dBm (5 W)". The watt figure is the WSJT-X approximation.
QString wsprPowerLabel(int dbm);

// Receive columns: Time, dB, DT, Frequency, Drift, Call, Locator, Report.
// Report is the dBm in the WSPR message. Call and locator are split out of it.
QString wsprActivityHeader();
QString wsprActivityLine(const QString& time, int snrDb, double dtSec, const QString& freqMhz, int drift,
                         const QString& message);
// call keeps angle brackets when the message used a hash. dbm is the report.
bool wsprSplitMessage(const QString& message, QString* call, QString* grid, int* dbm);

// "CALL GRID POWER" at 48 kHz. One second of silence, then 162 symbols.
// Empty when the call or the 4-character locator will not pack.
// toneHz is the centre of the four tones. Callers clamp it with wsprClampTxHz.
std::vector<float> wsprModulate(const QString& message, int toneHz);

struct WsprSpot {
  QString message;     // "CALL GRID dBm", "PFX/CALL dBm", or "<...> GRID6 dBm"
  int snrDb = 0;       // in 2500 Hz, as WSJT-X prints it
  double dtSec = 0.0;  // start time minus 1 s into the period
  double audioHz = 0.0;
  int drift = 0;       // Hz over the transmission
};

// Calls heard in type 1 and type 2 messages, by 15-bit hash, so a type 3
// message (hashed call, 6-character locator) prints as <CALL>. Saved as
// wspr_hashtable.txt in the STD data folder, the WSJT-X hashtable.txt format.
struct WsprHashTable {
  std::vector<QString> call = std::vector<QString>(32768);
  std::vector<QString> grid = std::vector<QString>(32768);
};
void wsprLoadHashTable(WsprHashTable* hash);
int wsprCallHash(const QString& call);
// Corrupts a known codeword and checks the OSD decoder repairs it.
bool wsprOsdSelfTest();
void wsprSaveHashTable(const WsprHashTable& hash);

// One 2-minute period of audio, sample 0 at the even UTC minute.
// A port of wsprd: candidate search, sync, soft decisions, Fano, OSD, subtraction.
// depth is the WSJT-X decode setting: 1 Fast, 2 Normal (default), 3 Deep.
// hash may be null; then a throwaway table is used.
std::vector<WsprSpot> wsprDecodePeriod(const float* samples, int count, int sampleRateHz, int depth = 2,
                                       WsprHashTable* hash = nullptr);

// First message of wsprDecodePeriod(), or empty.
QString wsprDecode(const float* samples, int count, int sampleRateHz);

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
  ~WsprEngine() override;

  void setArmed(bool armed);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  // periodStartMs is the even UTC minute the spot was sent in.
  void spotDecoded(const WsprSpot& spot, qint64 periodStartMs);
  void periodDecoded(int count, qint64 periodStartMs);

private:
  bool armed_ = false;
  int rate_ = 48000;
  int decodedPeriod_ = -1;
  qint64 epochMs_ = 0;
  std::vector<float> pending_;
  QThread* worker_ = nullptr;
  WsprHashTable hash_;
};

Q_DECLARE_METATYPE(WsprSpot)
