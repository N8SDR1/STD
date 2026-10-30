// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "spot/WsprNet.h"

#include "wspr/WsprModem.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

namespace {

bool digits(const QString& text, int count) {
  if (text.size() != count)
    return false;
  for (const QChar ch : text) {
    if (!ch.isDigit())
      return false;
  }
  return true;
}

bool gridOk(const QString& token) {
  static const QRegularExpression re(QStringLiteral("^[A-R]{2}[0-9]{2}(?:[A-X]{2})?$"));
  return re.match(token).hasMatch();
}

bool callOk(const QString& token) {
  static const QRegularExpression re(QStringLiteral(
      "^(?:[A-Z0-9]{1,4}/)?(?:[A-Z]{1,2}|[0-9][A-Z])[0-9][A-Z]{1,4}(?:/[A-Z0-9]{1,4})?$"));
  return re.match(token).hasMatch();
}

QString bareCall(QString call) {
  call = call.trimmed().toUpper();
  if (call.startsWith(QLatin1Char('<')) && call.endsWith(QLatin1Char('>')) && call.size() >= 2)
    call = call.mid(1, call.size() - 2).trimmed();
  return call;
}

QString mhz(double hz) {
  return QString::number(hz / 1.0e6, 'f', 6);
}

}  // namespace

QByteArray wsprnetFormBody(const QString& rxCall, const QString& rxGrid, qint64 dialHz,
                           double audioHz, const QString& dateUtc, const QString& timeUtc,
                           int snrDb, double dtSec, int drift, const QString& message,
                           const QString& version, QString* postedCall) {
  if (postedCall)
    postedCall->clear();
  const QString rx = rxCall.trimmed().toUpper();
  QString grid = rxGrid.trimmed().toUpper();
  if (grid.size() > 6)
    grid = grid.left(6);
  if (rx.isEmpty() || !gridOk(grid) || dialHz < 100000 || !digits(dateUtc, 6) || !digits(timeUtc, 4))
    return {};
  const QStringList words = message.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  bool powerOk = false;
  if (words.isEmpty())
    return {};
  const int power = words.last().toInt(&powerOk);
  if (!powerOk || power < 0 || power > 60)
    return {};
  QString heard;
  QString heardGrid;
  int dbm = 0;
  if (!wsprSplitMessage(message, &heard, &heardGrid, &dbm))
    return {};
  heard = bareCall(heard);
  if (!callOk(heard) || heard == rx)
    return {};
  heardGrid = heardGrid.trimmed().toUpper();
  if (heardGrid.size() > 6)
    heardGrid = heardGrid.left(6);
  if (!heardGrid.isEmpty() && !gridOk(heardGrid))
    heardGrid.clear();

  // Same names as WSJT-X urlEncodeSpot / decodeLine. mode 2 is WSPR-2.
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("function"), QStringLiteral("wspr"));
  query.addQueryItem(QStringLiteral("rcall"), rx);
  query.addQueryItem(QStringLiteral("rgrid"), grid);
  query.addQueryItem(QStringLiteral("rqrg"), mhz(double(dialHz) + 1500.0));
  query.addQueryItem(QStringLiteral("date"), dateUtc);
  query.addQueryItem(QStringLiteral("time"), timeUtc);
  query.addQueryItem(QStringLiteral("sig"), QString::number(snrDb));
  query.addQueryItem(QStringLiteral("dt"), QString::number(dtSec, 'f', 1));
  query.addQueryItem(QStringLiteral("drift"), QString::number(drift));
  query.addQueryItem(QStringLiteral("tqrg"), mhz(double(dialHz) + audioHz));
  query.addQueryItem(QStringLiteral("tcall"), heard);
  query.addQueryItem(QStringLiteral("tgrid"), heardGrid);
  query.addQueryItem(QStringLiteral("dbm"), QString::number(dbm));
  query.addQueryItem(QStringLiteral("version"), version.trimmed().isEmpty() ? QStringLiteral("STD") : version);
  query.addQueryItem(QStringLiteral("mode"), QStringLiteral("2"));
  if (postedCall)
    *postedCall = heard;
  return query.query(QUrl::FullyEncoded).toUtf8();
}

QString wsprnetReportsUrl(const QString& call) {
  const QString id = call.trimmed().toUpper();
  if (id.isEmpty())
    return QStringLiteral("https://www.wsprnet.org/drupal/wsprnet/spotquery");
  QUrl url(QStringLiteral("https://www.wsprnet.org/olddb"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("mode"), QStringLiteral("html"));
  query.addQueryItem(QStringLiteral("band"), QStringLiteral("all"));
  query.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
  query.addQueryItem(QStringLiteral("findreporter"), id);
  query.addQueryItem(QStringLiteral("sort"), QStringLiteral("date"));
  url.setQuery(query);
  return url.toString(QUrl::FullyEncoded);
}

WsprNetUploader::WsprNetUploader(QObject* parent) : QObject(parent) {
  hold_ = new QTimer(this);
  hold_->setSingleShot(true);
  connect(hold_, &QTimer::timeout, this, [this] { pump(); });
}

void WsprNetUploader::note(const QString& rxCall, const QString& rxGrid, qint64 dialHz, double audioHz,
                           const QString& dateUtc, const QString& timeUtc, int snrDb, double dtSec,
                           int drift, const QString& message) {
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (!QSettings().value(QStringLiteral("dx/wsprnetEnabled"), false).toBool())
    return;
  QString call;
  const QByteArray body =
      wsprnetFormBody(rxCall, rxGrid, dialHz, audioHz, dateUtc, timeUtc, snrDb, dtSec, drift, message,
                      QStringLiteral("STD ") + QLatin1String(STD_VERSION), &call);
  if (body.isEmpty())
    return;
  queue_.push_back(Item{body, call});
  if (!busy_ && !hold_->isActive())
    pump();
}

void WsprNetUploader::pump() {
  if (busy_ || queue_.isEmpty() || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (!net_)
    net_ = new QNetworkAccessManager(this);
  const Item item = queue_.takeFirst();
  QNetworkRequest request{QUrl(QStringLiteral("https://wsprnet.org/post/"))};
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/x-www-form-urlencoded"));
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("STD ") + QLatin1String(STD_VERSION));
  request.setTransferTimeout(20000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  busy_ = net_->post(request, item.body);
  const QString call = item.call;
  connect(busy_, &QNetworkReply::finished, this, [this, call] { onReply(call); });
}

void WsprNetUploader::onReply(const QString& call) {
  QNetworkReply* reply = busy_;
  busy_ = nullptr;
  if (!reply)
    return;
  const QString text = QString::fromUtf8(reply->readAll()).trimmed();
  const bool httpOk = reply->error() == QNetworkReply::NoError;
  const QString error = reply->errorString();
  reply->deleteLater();
  const bool accepted = httpOk && (text.contains(QLatin1String("spot(s) added")) ||
                                   text.contains(QLatin1String("spot(s) posted")) ||
                                   text.contains(QLatin1String("already"), Qt::CaseInsensitive));
  if (!httpOk) {
    queue_.clear();
    QString why = error.trimmed();
    if (why.size() > 80)
      why = why.left(80);
    emit status(QStringLiteral("WSPRnet upload failed: %1. START was not sent.").arg(why));
    return;
  }
  if (accepted) {
    emit status(QStringLiteral("WSPRnet posted %1. START was not sent.").arg(call));
  } else {
    QString why = text;
    if (why.size() > 80)
      why = why.left(80);
    if (why.isEmpty())
      why = QStringLiteral("the site did not accept the spot");
    emit status(QStringLiteral("WSPRnet did not accept %1: %2. START was not sent.").arg(call, why));
  }
  if (!queue_.isEmpty())
    hold_->start(1500);
}
