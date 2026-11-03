// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// A callbook entry, trimmed to what the QSO bar holds.
struct CallbookRecord {
  bool found = false;
  QString call;
  QString name;   // first name or nickname, "Fred"
  QString qth;    // city, "Scottsdale"
  QString state;  // US state, "AZ"
  QString grid;   // "DM32af"
};

// Looks a call up on HamQTH (free) or QRZ.com (XML Data; a subscription
// gives the full record, without one QRZ sends little more than the name).
// Set in Options → Lookup. Off by default. One session is kept and renewed
// when it expires. Answers are kept for the run, so a call is asked once.
class Callbook : public QObject {
  Q_OBJECT

public:
  explicit Callbook(QObject* parent = nullptr);

  bool enabled() const;
  // Read Options → Lookup again; a new login is made on the next lookup.
  void reload();
  void lookup(const QString& call);
  QString serviceName() const;

  // A call with a slash is portable or abroad: /P, /M, /4, VE3/...
  static bool portable(const QString& call);
  // The home call inside a portable one: "VE3/K8AB/P" -> "K8AB".
  static QString homeCall(const QString& call);

  // Each service's reply. Public for the self-test.
  static CallbookRecord parseQrz(const QByteArray& xml, QString* key, QString* error);
  static QString parseHamqthToken(const QByteArray& json, QString* error);
  static CallbookRecord parseHamqth(const QByteArray& json, QString* error);

signals:
  // asked is the call that was looked up, as given to lookup().
  void answered(const QString& asked, const CallbookRecord& record, const QString& error);

private:
  void login(const QString& call);
  void query(const QString& call, bool retried);
  void finish(const QString& call, const CallbookRecord& r, const QString& error);

  QNetworkAccessManager* net_ = nullptr;
  QString source_;
  QString user_;
  QString password_;
  QString session_;
  QHash<QString, CallbookRecord> cache_;
};
