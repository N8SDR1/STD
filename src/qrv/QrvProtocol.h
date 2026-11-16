// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// QRV board JSON. The live service is https://qrvsdr.cc.
// STD talks only to that HTTPS API. Discord is updated by the server.

namespace qrv {

inline constexpr auto kDefaultApiBase = "https://qrvsdr.cc";
inline constexpr auto kAppStd = "STD";
inline constexpr int kMinPostIntervalMs = 10000;
inline constexpr int kHeartbeatPeriodMs = 25000;
inline constexpr int kBandModeDebounceMs = 10000;
inline constexpr int kRosterPeriodMs = 25000;
inline constexpr int kHttpTimeoutMs = 12000;
inline constexpr int kNoteMaxChars = 40;

struct HeartbeatRequest {
  QString call;
  QString app = QStringLiteral("STD");
  QString band;
  QString mode;
  QString installKey;
  QString note;
  bool hasHz = false;
  qint64 hz = 0;
};

struct QrtRequest {
  QString call;
  QString app = QStringLiteral("STD");
  QString installKey;
};

struct ApiResult {
  bool ok = false;
  int httpStatus = 0;
  QString error;
  QString installKey;
  int staleS = 180;
};

struct RosterEntry {
  QString call;
  QString app;
  QString band;
  QString mode;
  QString lastSeen;
  QString note;
  bool hasHz = false;
  qint64 hz = 0;
};

struct Roster {
  bool ok = false;
  int httpStatus = 0;
  QString error;
  int staleS = 180;
  QList<RosterEntry> entries;
};

QString normalizeCall(const QString& raw);
QString sanitizeNote(const QString& raw);
QString bandForHz(qint64 hz);
bool hzInBand(qint64 hz, const QString& band);
bool intervalElapsed(qint64 lastMs, qint64 nowMs, int minMs);
QString normalizeApiBase(QString raw);
QString endpointUrl(const QString& apiBase, const QString& phpFile);

// STD Mode menu name to the token qrvsdr.cc stores. Empty when the board
// has no token for that mode. Q65A is the menu name for the contract's Q65.
QString qrvModeForStd(const QString& stdMode);
bool allowedMode(const QString& token);

QByteArray encodeHeartbeat(const HeartbeatRequest& req);
QByteArray encodeQrt(const QrtRequest& req);
ApiResult parseWriteResponse(const QByteArray& body, int httpStatus);
Roster parseRosterResponse(const QByteArray& body, int httpStatus);

}  // namespace qrv
