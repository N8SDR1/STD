// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SDRLogger+ log as ADIF, for the needed-station marks. SDRLogger+ runs
// a local web server while it is open; its /api/adif/export gives the whole
// log. STD asks 127.0.0.1 only, keeps the last copy on disk so the marks
// work while SDRLogger+ is closed, and sends nothing else. It does not key.

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

class LoggerFeed : public QObject {
  Q_OBJECT

public:
  explicit LoggerFeed(QObject* parent = nullptr);

  // On while the worked list comes from SDRLogger+.
  void setEnabled(bool on);
  bool enabled() const { return enabled_; }
  void setPort(int port);
  int port() const { return port_; }
  static QString url(int port);
  // The last copy of the log, kept between runs.
  static QString cachePath();

  // Ask now (after a QSO is logged, or when turned on). Skipped while a
  // request is out and under STD_SELFTEST.
  void refresh();
  QDateTime fetchedAt() const { return fetchedAt_; }
  QString lastError() const { return lastError_; }
  int lastRecords() const { return records_; }

signals:
  // A new copy is on disk.
  void updated();
  // SDRLogger+ did not answer; error says why.
  void failed(const QString& error);

private:
  QNetworkAccessManager* net_ = nullptr;
  QTimer* timer_ = nullptr;
  bool enabled_ = false;
  bool busy_ = false;
  int port_ = 5050;
  QDateTime fetchedAt_;
  QString lastError_;
  int records_ = 0;
};
