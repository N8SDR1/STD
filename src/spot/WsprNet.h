// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// WSPRnet spot post, the same form WSJT-X sends from Network/wsprnet.cpp
// (Edson Pereira, PY2SDR). STD posts one heard station at a time.
// The receiver callsign and locator are Options → User. WSPRnet does not
// ask for a password or a key. This does not key and does not send START.

// application/x-www-form-urlencoded body, or empty when the spot must not
// be sent (no receiver identity, dial under 100 kHz, own call, unresolved
// hash, or a message that is not a WSPR spot). postedCall is the heard
// station with angle brackets removed.
QByteArray wsprnetFormBody(const QString& rxCall, const QString& rxGrid, qint64 dialHz,
                           double audioHz, const QString& dateUtc, const QString& timeUtc,
                           int snrDb, double dtSec, int drift, const QString& message,
                           const QString& version, QString* postedCall);

// Stations this callsign reported. No callsign: the query page, unfiltered.
QString wsprnetReportsUrl(const QString& call);

// Queues a heard WSPR decode and posts it to wsprnet.org.
// Does nothing while STD_SELFTEST is set, or while upload is off.
// status reports a post or a failure. It does not key and does not send START.
class WsprNetUploader : public QObject {
  Q_OBJECT

public:
  explicit WsprNetUploader(QObject* parent = nullptr);
  void note(const QString& rxCall, const QString& rxGrid, qint64 dialHz, double audioHz,
            const QString& dateUtc, const QString& timeUtc, int snrDb, double dtSec, int drift,
            const QString& message);

signals:
  void status(const QString& text);

private:
  void pump();
  void onReply(const QString& call);

  struct Item {
    QByteArray body;
    QString call;
  };

  QNetworkAccessManager* net_ = nullptr;
  QNetworkReply* busy_ = nullptr;
  QTimer* hold_ = nullptr;
  QVector<Item> queue_;
};
