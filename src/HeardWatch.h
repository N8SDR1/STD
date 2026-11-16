// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Who heard me: the stations that reported hearing this callsign to PSK
// Reporter. Off until the operator turns it on. Asks only while this
// station has transmitted in the last half hour, and no more than every five
// minutes, as PSK Reporter asks. The callsign is the one in Options → User.
// This only reads; it does not key.

#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

struct HeardReport {
  QString call;     // the station that heard us
  QString locator;  // its locator
  QString country;  // as PSK Reporter names it
  QString mode;
  qint64 freqHz = 0;
  int snr = 0;
  QDateTime time;  // UTC
};

class HeardWatch : public QObject {
  Q_OBJECT

public:
  explicit HeardWatch(QObject* parent = nullptr);

  // Persisted (pskr/heardMe). Off clears the reports.
  bool enabled() const { return enabled_; }
  void setEnabled(bool on);
  void setCall(const QString& call);
  // Our transmitter keyed: ask within a minute or two, then every five.
  void noteTransmit();

  static QString url(const QString& call);
  // PSK Reporter's XML. Empty with error set when it is not a report list.
  static QList<HeardReport> parse(const QByteArray& xml, QString* error = nullptr);

  // The reports of the last 15 minutes, newest kept per station.
  const QList<HeardReport>& reports() const { return reports_; }
  void setReports(const QList<HeardReport>& reports);
  QDateTime checkedAt() const { return checkedAt_; }
  QString lastError() const { return lastError_; }

  // Ask now when allowed. Skipped under STD_SELFTEST.
  void poll();

signals:
  void changed();

private:
  bool due() const;

  QNetworkAccessManager* net_ = nullptr;
  QTimer* timer_ = nullptr;
  QTimer* soon_ = nullptr;
  bool enabled_ = false;
  bool busy_ = false;
  QString call_;
  QDateTime lastTx_;
  QDateTime lastAsked_;
  QDateTime checkedAt_;
  QString lastError_;
  QList<HeardReport> reports_;
};
