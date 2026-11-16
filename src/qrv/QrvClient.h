// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "qrv/QrvProtocol.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Presence on qrvsdr.cc. The callsign is Options → User, never a second field.
// Opt-in is off until the operator turns it on. Heartbeats run only while the
// radio is connected. QRT runs on disable, disconnect, and exit.

class QrvClient : public QObject {
  Q_OBJECT

public:
  explicit QrvClient(QObject* parent = nullptr);

  bool optIn() const { return optIn_; }
  bool shareHz() const { return shareHz_; }
  QString note() const { return note_; }
  QString apiBase() const { return apiBase_; }
  QString lastError() const { return lastError_; }
  bool onAir() const { return onAir_; }
  const qrv::Roster& roster() const { return roster_; }

  void setOptIn(bool on);
  void setShareHz(bool on);
  void setNote(const QString& note);
  void setApiBase(const QString& url);
  void setRadioConnected(bool on);
  void setStationCall(const QString& call);
  void setActivity(qint64 hz, const QString& stdMode);
  void setRosterWatching(bool on);
  void flushQrtBlocking(int timeoutMs = 2500);

signals:
  void lastErrorChanged();
  void rosterChanged();
  void onAirChanged();

private:
  void load();
  void persist();
  void setError(const QString& error);
  void reconsider();
  bool canPublish() const;
  void maybeHeartbeat(bool force);
  void postHeartbeat();
  void postQrt(bool blocking, int timeoutMs);
  void fetchRoster();
  qrv::HeartbeatRequest currentHeartbeat() const;
  void onReplyFinished(QNetworkReply* reply, const QString& kind);

  QNetworkAccessManager* nam_ = nullptr;
  QTimer* heartbeatTimer_ = nullptr;
  QTimer* debounceTimer_ = nullptr;
  QTimer* rosterTimer_ = nullptr;

  bool optIn_ = false;
  bool shareHz_ = false;
  bool radioConnected_ = false;
  bool rosterWatching_ = false;
  bool onAir_ = false;
  bool inFlight_ = false;
  bool hold_ = false;
  bool qrtAfterFlight_ = false;
  QString note_;
  QString apiBase_;
  QString installKey_;
  QString boundCall_;
  QString stationCall_;
  QString lastError_;
  QString modeToken_;
  QString lastBand_;
  QString lastModeSent_;
  qint64 vfoHz_ = 0;
  qint64 lastPostMs_ = 0;
  qrv::Roster roster_;
};
