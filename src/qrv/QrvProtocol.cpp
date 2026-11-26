// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qrv/QrvProtocol.h"

#include "DigitalMode.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace qrv {
namespace {

struct BandRange {
  const char* name;
  qint64 lo;
  qint64 hi;
};

// Same edges as the QRV contract. GEN is last so a ham band wins.
constexpr BandRange kBands[] = {
    {"160m", 1800000, 2000000}, {"80m", 3500000, 4000000},   {"60m", 5330000, 5407000},
    {"40m", 7000000, 7300000},  {"30m", 10100000, 10150000}, {"20m", 14000000, 14350000},
    {"17m", 18068000, 18168000}, {"15m", 21000000, 21450000}, {"12m", 24890000, 24990000},
    {"10m", 28000000, 29700000}, {"6m", 50000000, 54000000},  {"2m", 144000000, 148000000},
    {"70cm", 420000000, 450000000}, {"GEN", 1, 54000000},
};

// Exact tokens the deployed allow-list stores.
constexpr const char* kModes[] = {
    "LSB",     "USB",    "CWL",      "CWU",     "DSB",     "AM",      "SAM",     "FM",
    "DIGU",    "DIGL",   "SPEC",     "DRM",     "SSB",     "NFM",     "PSK31",   "JT65",
    "JT9",     "DIGI",   "DATA",     "VARAC",   "PACKET",  "RTTYR",   "FT8",     "FT4",
    "FT2",     "JT65A",  "JT65B",    "JT65C",   "Q65",     "Q65C",    "Q65D",    "Q65E",
    "MSK144",  "MSKMS",  "JS8",      "JS8CX2",  "WSPR",    "FST4",    "FST4W",   "HELL",
    "RTTY",    "PSK",    "QPSK",     "OLIVIA",  "CONTESTIA", "MFSK",  "DOMINOEX", "THOR",
    "MT63",    "VARA",   "VARAHF",  "SSTV",
};

QString jsonError(const QJsonObject& object) {
  const QJsonValue error = object.value(QStringLiteral("error"));
  if (error.isString())
    return error.toString();
  return {};
}

}  // namespace

QString normalizeCall(const QString& raw) {
  QString call = raw.trimmed().toUpper();
  call.remove(QLatin1Char(' '));
  if (call.isEmpty() || call.size() > 16)
    return {};
  static const QRegularExpression re(QStringLiteral("^[A-Z0-9]{3,11}(/[A-Z0-9]{1,5})?$"));
  if (!re.match(call).hasMatch())
    return {};
  const QString base = call.section(QLatin1Char('/'), 0, 0);
  bool hasLetter = false;
  bool hasDigit = false;
  for (const QChar c : base) {
    if (c.isLetter())
      hasLetter = true;
    if (c.isDigit())
      hasDigit = true;
  }
  if (!hasLetter || !hasDigit)
    return {};
  return call;
}

QString sanitizeNote(const QString& raw) {
  if (raw.isEmpty())
    return {};
  QString cleaned;
  cleaned.reserve(raw.size());
  for (const QChar c : raw) {
    const ushort u = c.unicode();
    if (u < 0x20 || u == 0x7F)
      continue;
    if (c == QLatin1Char('`') || c == QLatin1Char('*') || c == QLatin1Char('_') ||
        c == QLatin1Char('|') || c == QLatin1Char('<') || c == QLatin1Char('>'))
      continue;
    cleaned.append(c);
  }
  static const QRegularExpression url(QStringLiteral("https?://\\S+"),
                                      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression www(QStringLiteral("www\\.\\S+"),
                                      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression at(QStringLiteral("@[\\w.]+"));
  cleaned.replace(url, QString());
  cleaned.replace(www, QString());
  cleaned.replace(at, QString());
  cleaned = cleaned.trimmed();
  if (cleaned.size() > kNoteMaxChars)
    cleaned = cleaned.left(kNoteMaxChars);
  return cleaned;
}

QString bandForHz(qint64 hz) {
  if (hz <= 0)
    return {};
  for (const BandRange& band : kBands) {
    if (QLatin1String(band.name) == QLatin1String("GEN"))
      continue;
    if (hz >= band.lo && hz <= band.hi)
      return QString::fromLatin1(band.name);
  }
  for (const BandRange& band : kBands) {
    if (QLatin1String(band.name) == QLatin1String("GEN") && hz >= band.lo && hz <= band.hi)
      return QStringLiteral("GEN");
  }
  return {};
}

bool hzInBand(qint64 hz, const QString& band) {
  if (hz <= 0)
    return false;
  for (const BandRange& row : kBands) {
    if (band.compare(QLatin1String(row.name), Qt::CaseInsensitive) == 0)
      return hz >= row.lo && hz <= row.hi;
  }
  return false;
}

bool intervalElapsed(qint64 lastMs, qint64 nowMs, int minMs) {
  if (lastMs <= 0)
    return true;
  return (nowMs - lastMs) >= minMs;
}

QString normalizeApiBase(QString raw) {
  QString base = raw.trimmed();
  while (base.endsWith(QLatin1Char('/')))
    base.chop(1);
  if (base.endsWith(QLatin1String("/v1"), Qt::CaseInsensitive))
    base.chop(3);
  while (base.endsWith(QLatin1Char('/')))
    base.chop(1);
  if (base.isEmpty())
    base = QString::fromUtf8(kDefaultApiBase);
  return base;
}

QString endpointUrl(const QString& apiBase, const QString& phpFile) {
  return normalizeApiBase(apiBase) + QStringLiteral("/v1/") + phpFile;
}

bool allowedMode(const QString& token) {
  for (const char* mode : kModes) {
    if (token == QLatin1String(mode))
      return true;
  }
  return false;
}

QString qrvModeForStd(const QString& stdMode) {
  const QString mode = stdMode.trimmed();
  if (mode.isEmpty() || radeMode(mode) || wefaxMode(mode))
    return {};
  if (sstvMode(mode))
    return QStringLiteral("SSTV");
  if (mode == QLatin1String("Q65A"))
    return QStringLiteral("Q65");
  if (mode == QLatin1String("Q65B"))
    return {};
  if (hellFamily(mode))
    return QStringLiteral("HELL");
  if (rttyMode(mode))
    return QStringLiteral("RTTY");
  if (qpskMode(mode))
    return QStringLiteral("QPSK");
  if (pskMode(mode))
    return QStringLiteral("PSK");
  if (mode.startsWith(QLatin1String("Olivia"), Qt::CaseInsensitive))
    return QStringLiteral("OLIVIA");
  if (mode.startsWith(QLatin1String("Contestia"), Qt::CaseInsensitive))
    return QStringLiteral("CONTESTIA");
  if (mode.startsWith(QLatin1String("MFSK"), Qt::CaseInsensitive))
    return QStringLiteral("MFSK");
  if (mode.startsWith(QLatin1String("DominoEX"), Qt::CaseInsensitive))
    return QStringLiteral("DOMINOEX");
  if (mode.startsWith(QLatin1String("Thor"), Qt::CaseInsensitive))
    return QStringLiteral("THOR");
  if (mode.startsWith(QLatin1String("MT63"), Qt::CaseInsensitive))
    return QStringLiteral("MT63");
  if (mode.startsWith(QLatin1String("JS8C"), Qt::CaseInsensitive))
    return QStringLiteral("JS8CX2");
  QString token = mode.toUpper();
  token.remove(QRegularExpression(QStringLiteral("[^A-Z0-9]")));
  if (token == QLatin1String("VARAHFSTD"))
    token = QStringLiteral("VARAHF");
  if (token.size() > 12)
    token = token.left(12);
  if (!allowedMode(token))
    return {};
  return token;
}

QByteArray encodeHeartbeat(const HeartbeatRequest& req) {
  QJsonObject object;
  object.insert(QStringLiteral("call"), req.call);
  object.insert(QStringLiteral("app"), req.app);
  object.insert(QStringLiteral("band"), req.band);
  object.insert(QStringLiteral("mode"), req.mode);
  if (!req.installKey.isEmpty())
    object.insert(QStringLiteral("install_key"), req.installKey);
  if (req.hasHz)
    object.insert(QStringLiteral("hz"), req.hz);
  if (!req.note.isEmpty())
    object.insert(QStringLiteral("note"), req.note);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray encodeQrt(const QrtRequest& req) {
  QJsonObject object;
  object.insert(QStringLiteral("call"), req.call);
  object.insert(QStringLiteral("app"), req.app);
  object.insert(QStringLiteral("install_key"), req.installKey);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

ApiResult parseWriteResponse(const QByteArray& body, int httpStatus) {
  ApiResult result;
  result.httpStatus = httpStatus;
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    result.ok = false;
    result.error = httpStatus > 0 ? QStringLiteral("http_%1").arg(httpStatus)
                                   : QStringLiteral("invalid_json");
    return result;
  }
  const QJsonObject object = doc.object();
  result.ok = object.value(QStringLiteral("ok")).toBool(false);
  result.error = jsonError(object);
  result.installKey = object.value(QStringLiteral("install_key")).toString();
  if (object.contains(QStringLiteral("stale_s")))
    result.staleS = object.value(QStringLiteral("stale_s")).toInt(180);
  if (!result.ok && result.error.isEmpty())
    result.error = QStringLiteral("http_%1").arg(httpStatus);
  return result;
}

Roster parseRosterResponse(const QByteArray& body, int httpStatus) {
  Roster roster;
  roster.httpStatus = httpStatus;
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    roster.ok = false;
    roster.error = httpStatus > 0 ? QStringLiteral("http_%1").arg(httpStatus)
                                   : QStringLiteral("invalid_json");
    return roster;
  }
  const QJsonObject object = doc.object();
  roster.ok = object.value(QStringLiteral("ok")).toBool(false);
  roster.error = jsonError(object);
  if (object.contains(QStringLiteral("stale_s")))
    roster.staleS = object.value(QStringLiteral("stale_s")).toInt(180);
  const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
  for (const QJsonValue& value : entries) {
    if (!value.isObject())
      continue;
    const QJsonObject entry = value.toObject();
    RosterEntry row;
    row.call = entry.value(QStringLiteral("call")).toString();
    row.app = entry.value(QStringLiteral("app")).toString();
    row.band = entry.value(QStringLiteral("band")).toString();
    row.mode = entry.value(QStringLiteral("mode")).toString();
    row.lastSeen = entry.value(QStringLiteral("last_seen")).toString();
    row.note = entry.value(QStringLiteral("note")).toString();
    if (entry.contains(QStringLiteral("hz")) && entry.value(QStringLiteral("hz")).isDouble()) {
      row.hasHz = true;
      row.hz = static_cast<qint64>(entry.value(QStringLiteral("hz")).toDouble());
    }
    roster.entries.append(row);
  }
  if (!roster.ok && roster.error.isEmpty())
    roster.error = QStringLiteral("http_%1").arg(httpStatus);
  return roster;
}

}  // namespace qrv
