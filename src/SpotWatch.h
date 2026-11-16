// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QTimer;

// One DX cluster or RBN spot of a digital mode, as Spothole reports it.
struct DxSpot {
  QString call;      // the spotted station, upper case
  qint64 freqHz = 0;
  int metres = 0;    // band, 0 if outside the amateur bands STD knows
  QString mode;      // Spothole's mode: FT8, FT4, RTTY, PSK, JS8, DATA, ...
  bool modeGuessed = false;  // taken from the band plan, not the spot or its comment
  QString comment;
  QString spotter;
  QString source;    // "Cluster" or "RBN"
  QDateTime time;    // UTC
  double received = 0;  // Spothole's received_time, for the next query
};

// Cluster spots for the digital DXpeditions on the air, from Spothole
// (https://spothole.app), which gathers the DX clusters and the RBN. Off
// until Mark spotted DXpeditions is ticked. It asks only for digital spots
// (FT8, FT4, RTTY, PSK, ...) of the calls it is given, every 90 or 180
// seconds, and keeps each call's latest spot for 10, 20, or 30 minutes.
// It sends no callsign or login. Nothing here keys or sends START.
class SpotWatch : public QObject {
  Q_OBJECT

public:
  explicit SpotWatch(QObject* parent = nullptr);

  static QString apiUrl();  // https://spothole.app/api/v1/spots

  bool enabled() const { return enabled_; }
  void setEnabled(bool on);
  int pollSecs() const { return pollSecs_; }     // 90 or 180
  void setPollSecs(int secs);
  int keepMins() const { return keepMins_; }     // 10, 20, or 30
  void setKeepMins(int mins);

  // The calls to watch: full calls, upper case. A change asks again at once.
  void setWatch(const QStringList& calls);
  const QStringList& watch() const { return watch_; }

  // The latest spot of a watched call, or nullptr.
  const DxSpot* latest(const QString& call) const;
  int spottedCount() const { return int(latest_.size()); }
  QDateTime checkedAt() const { return checkedAt_; }
  QString lastError() const { return error_; }
  bool busy() const { return busy_; }

  // Parsing and matching, public for the self-test. parseSpots keeps only
  // the digital ones (mode_type DATA). lastReceived: the newest received time.
  static QList<DxSpot> parseSpots(const QByteArray& body, QString* error, double* lastReceived = nullptr);
  // The spotted call is the watched one, or one side of a slash is
  // ("K6VHF/HR9" for HR9 or K6VHF). A longer call that only starts the same
  // (3B9FR for 3B9KW) is not.
  static bool sameStation(const QString& spotted, const QString& watched);
  // STD's mode for a spot's mode, or empty when STD does not run it.
  static QString stdMode(const QString& spotMode);

  // Take spots as if Spothole sent them, then drop the ones older than the
  // keep time. The self-test feeds a canned list through this.
  void ingest(const QList<DxSpot>& spots, const QDateTime& nowUtc = QDateTime::currentDateTimeUtc());
  void prune(const QDateTime& nowUtc = QDateTime::currentDateTimeUtc());

signals:
  void changed();
  // A watched call has a spot newer than the one it had.
  void spotted(const QString& call, const DxSpot& spot);

private:
  void poll();
  void restartTimer();

  QNetworkAccessManager* net_ = nullptr;
  QTimer* timer_ = nullptr;
  bool enabled_ = false;
  int pollSecs_ = 90;
  int keepMins_ = 20;
  QStringList watch_;
  QHash<QString, DxSpot> latest_;
  double since_ = 0;  // received_time of the newest spot seen; 0 asks for the keep time
  QDateTime checkedAt_;
  QString error_;
  bool busy_ = false;
};
