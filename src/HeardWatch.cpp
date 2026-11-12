// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HeardWatch.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <QTimeZone>

namespace {
// PSK Reporter asks clients not to query more often than every five minutes.
constexpr int kEveryMs = 5 * 60 * 1000;
// After a transmission, wait for the reports to come in before asking.
constexpr int kAfterTxMs = 90 * 1000;
// Asked only while we have transmitted this recently.
constexpr qint64 kTxWindowSecs = 30 * 60;
// Reports this old are dropped.
constexpr qint64 kKeepSecs = 15 * 60;
}  // namespace

HeardWatch::HeardWatch(QObject* parent) : QObject(parent) {
  net_ = new QNetworkAccessManager(this);
  timer_ = new QTimer(this);
  timer_->setInterval(kEveryMs);
  connect(timer_, &QTimer::timeout, this, &HeardWatch::poll);
  soon_ = new QTimer(this);
  soon_->setSingleShot(true);
  connect(soon_, &QTimer::timeout, this, &HeardWatch::poll);
  enabled_ = QSettings().value(QStringLiteral("pskr/heardMe"), false).toBool();
  if (enabled_)
    timer_->start();
}

void HeardWatch::setEnabled(bool on) {
  if (on == enabled_)
    return;
  enabled_ = on;
  QSettings().setValue(QStringLiteral("pskr/heardMe"), on);
  if (on) {
    timer_->start();
  } else {
    timer_->stop();
    soon_->stop();
    reports_.clear();
    lastError_.clear();
  }
  emit changed();
}

void HeardWatch::setCall(const QString& call) {
  const QString up = call.trimmed().toUpper();
  if (up == call_)
    return;
  call_ = up;
  reports_.clear();
  emit changed();
}

void HeardWatch::noteTransmit() {
  lastTx_ = QDateTime::currentDateTimeUtc();
  if (enabled_ && !soon_->isActive())
    soon_->start(kAfterTxMs);
}

QString HeardWatch::url(const QString& call) {
  QUrl url(QStringLiteral("https://retrieve.pskreporter.info/query"));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("senderCallsign"), call);
  q.addQueryItem(QStringLiteral("flowStartSeconds"), QString::number(-kKeepSecs));
  q.addQueryItem(QStringLiteral("rronly"), QStringLiteral("1"));
  q.addQueryItem(QStringLiteral("noactive"), QStringLiteral("1"));
  url.setQuery(q);
  return url.toString(QUrl::FullyEncoded);
}

QList<HeardReport> HeardWatch::parse(const QByteArray& xml, QString* error) {
  QList<HeardReport> out;
  QXmlStreamReader reader(xml);
  bool sawList = false;
  while (!reader.atEnd()) {
    if (!reader.readNextStartElement())
      continue;
    if (reader.name() == QLatin1String("receptionReports")) {
      sawList = true;
      continue;
    }
    if (reader.name() != QLatin1String("receptionReport")) {
      reader.skipCurrentElement();
      continue;
    }
    const QXmlStreamAttributes a = reader.attributes();
    HeardReport r;
    r.call = a.value(QLatin1String("receiverCallsign")).toString().trimmed().toUpper();
    r.locator = a.value(QLatin1String("receiverLocator")).toString().trimmed();
    r.country = a.value(QLatin1String("receiverDXCC")).toString().trimmed();
    r.mode = a.value(QLatin1String("mode")).toString().trimmed();
    r.freqHz = a.value(QLatin1String("frequency")).toLongLong();
    r.snr = a.value(QLatin1String("sNR")).toInt();
    r.time = QDateTime::fromSecsSinceEpoch(a.value(QLatin1String("flowStartSeconds")).toLongLong(),
                                           QTimeZone(QTimeZone::UTC));
    if (!r.call.isEmpty())
      out << r;
    reader.skipCurrentElement();
  }
  if ((reader.hasError() && reader.error() != QXmlStreamReader::PrematureEndOfDocumentError) || !sawList) {
    if (error)
      *error = QStringLiteral("PSK Reporter sent something that is not a report list.");
    return {};
  }
  if (error)
    error->clear();
  // Newest report per station.
  QHash<QString, int> at;
  QList<HeardReport> newest;
  for (const HeardReport& r : out) {
    const QString key = r.call + QLatin1Char('|') + QString::number(r.freqHz / 1000000);
    auto it = at.constFind(key);
    if (it == at.constEnd()) {
      at.insert(key, newest.size());
      newest << r;
    } else if (r.time > newest.at(it.value()).time) {
      newest[it.value()] = r;
    }
  }
  return newest;
}

void HeardWatch::setReports(const QList<HeardReport>& reports) {
  const QDateTime cut = QDateTime::currentDateTimeUtc().addSecs(-kKeepSecs);
  reports_.clear();
  for (const HeardReport& r : reports) {
    if (!r.time.isValid() || r.time >= cut)
      reports_ << r;
  }
  emit changed();
}

bool HeardWatch::due() const {
  if (!enabled_ || busy_ || call_.isEmpty() || !lastTx_.isValid())
    return false;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (lastTx_.secsTo(now) > kTxWindowSecs)
    return false;
  return !lastAsked_.isValid() || lastAsked_.msecsTo(now) >= kEveryMs - 5000;
}

void HeardWatch::poll() {
  if (qEnvironmentVariableIsSet("STD_SELFTEST") || !due())
    return;
  busy_ = true;
  lastAsked_ = QDateTime::currentDateTimeUtc();
  QNetworkRequest request{QUrl(url(call_))};
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("STD/") + QStringLiteral(STD_VERSION));
  request.setTransferTimeout(60000);
  QNetworkReply* reply = net_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    busy_ = false;
    checkedAt_ = QDateTime::currentDateTimeUtc();
    if (reply->error() != QNetworkReply::NoError) {
      lastError_ = QStringLiteral("PSK Reporter did not answer (%1).").arg(reply->errorString());
      emit changed();
      return;
    }
    QString error;
    const QList<HeardReport> list = parse(reply->readAll(), &error);
    lastError_ = error;
    if (error.isEmpty())
      setReports(list);
    else
      emit changed();
  });
}
