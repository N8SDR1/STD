// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpotWatch.h"

#include "DxpedFeed.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cstdlib>
#include <initializer_list>

namespace {

int nearestOf(int value, std::initializer_list<int> allowed) {
  int best = *allowed.begin();
  for (int a : allowed)
    if (std::abs(a - value) < std::abs(best - value))
      best = a;
  return best;
}

}  // namespace

SpotWatch::SpotWatch(QObject* parent) : QObject(parent) {
  QSettings store;
  enabled_ = store.value(QStringLiteral("spots/enabled"), false).toBool();
  pollSecs_ = nearestOf(store.value(QStringLiteral("spots/pollSecs"), 90).toInt(), {90, 180});
  keepMins_ = nearestOf(store.value(QStringLiteral("spots/keepMins"), 20).toInt(), {10, 20, 30});
  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, [this] { poll(); });
  restartTimer();
}

QString SpotWatch::apiUrl() {
  return QStringLiteral("https://spothole.app/api/v1/spots");
}

void SpotWatch::setEnabled(bool on) {
  if (on == enabled_)
    return;
  enabled_ = on;
  QSettings().setValue(QStringLiteral("spots/enabled"), on);
  if (!on) {
    latest_.clear();
    since_ = 0;
    error_.clear();
  }
  restartTimer();
  if (on)
    poll();
  emit changed();
}

void SpotWatch::setPollSecs(int secs) {
  secs = nearestOf(secs, {90, 180});
  if (secs == pollSecs_)
    return;
  pollSecs_ = secs;
  QSettings().setValue(QStringLiteral("spots/pollSecs"), secs);
  restartTimer();
  emit changed();
}

void SpotWatch::setKeepMins(int mins) {
  mins = nearestOf(mins, {10, 20, 30});
  if (mins == keepMins_)
    return;
  keepMins_ = mins;
  QSettings().setValue(QStringLiteral("spots/keepMins"), mins);
  prune();
  emit changed();
}

void SpotWatch::setWatch(const QStringList& calls) {
  QStringList list;
  for (const QString& c : calls) {
    const QString up = c.trimmed().toUpper();
    if (!up.isEmpty() && !list.contains(up))
      list << up;
  }
  list.sort();
  if (list == watch_)
    return;
  watch_ = list;
  // Spots of calls no longer watched go; the new list is asked for at once,
  // over the whole keep time.
  for (auto it = latest_.begin(); it != latest_.end();) {
    if (watch_.contains(it.key()))
      ++it;
    else
      it = latest_.erase(it);
  }
  since_ = 0;
  restartTimer();
  if (enabled_)
    poll();
  emit changed();
}

const DxSpot* SpotWatch::latest(const QString& call) const {
  auto it = latest_.constFind(call.trimmed().toUpper());
  return it == latest_.constEnd() ? nullptr : &it.value();
}

void SpotWatch::restartTimer() {
  timer_->setInterval(pollSecs_ * 1000);
  if (enabled_ && !watch_.isEmpty())
    timer_->start();
  else
    timer_->stop();
}

QList<DxSpot> SpotWatch::parseSpots(const QByteArray& body, QString* error, double* lastReceived) {
  QList<DxSpot> out;
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (!doc.isArray()) {
    if (error)
      *error = QStringLiteral("Spothole sent something that is not a spot list.");
    return out;
  }
  if (error)
    error->clear();
  for (const QJsonValue& v : doc.array()) {
    const QJsonObject o = v.toObject();
    const double received = o.value(QLatin1String("received_time")).toDouble();
    if (lastReceived && received > *lastReceived)
      *lastReceived = received;
    // Digital spots only: a CW or phone spot never reaches the panel.
    if (o.value(QLatin1String("mode_type")).toString() != QLatin1String("DATA"))
      continue;
    if (o.value(QLatin1String("qrt")).toBool())
      continue;
    DxSpot s;
    s.call = o.value(QLatin1String("dx_call")).toString().trimmed().toUpper();
    s.freqHz = qint64(o.value(QLatin1String("freq")).toDouble());
    if (s.call.isEmpty() || s.freqHz <= 0)
      continue;
    s.metres = DxpedFeed::metresForHz(s.freqHz);
    s.mode = o.value(QLatin1String("mode")).toString().trimmed().toUpper();
    s.modeGuessed = o.value(QLatin1String("mode_source")).toString() == QLatin1String("BANDPLAN");
    s.comment = o.value(QLatin1String("comment")).toString().simplified();
    s.spotter = o.value(QLatin1String("de_call")).toString().trimmed().toUpper();
    s.source = o.value(QLatin1String("source")).toString();
    const double t = o.value(QLatin1String("time")).toDouble(received);
    s.time = QDateTime::fromMSecsSinceEpoch(qint64(t * 1000.0), QTimeZone(QTimeZone::UTC));
    s.received = received;
    out << s;
  }
  return out;
}

bool SpotWatch::sameStation(const QString& spotted, const QString& watched) {
  const QString a = spotted.trimmed().toUpper();
  const QString b = watched.trimmed().toUpper();
  if (a.isEmpty() || b.isEmpty())
    return false;
  if (a == b)
    return true;
  const QStringList as = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  const QStringList bs = b.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  // A portable watched call ("VP2V/NY9K") must match whole; a plain one may
  // be either side of the spotted call's slash.
  if (bs.size() > 1)
    return false;
  return as.size() > 1 && as.contains(b);
}

QString SpotWatch::stdMode(const QString& spotMode) {
  const QString m = spotMode.trimmed().toUpper();
  if (m == QLatin1String("FT8") || m == QLatin1String("FT4") || m == QLatin1String("FT2") ||
      m == QLatin1String("RTTY") || m == QLatin1String("JS8") || m == QLatin1String("MSK144"))
    return m;
  if (m == QLatin1String("PSK") || m == QLatin1String("PSK31") || m == QLatin1String("BPSK31"))
    return QStringLiteral("PSK31");
  if (m.startsWith(QLatin1String("PSK")) || m.startsWith(QLatin1String("BPSK")))
    return m.startsWith(QLatin1Char('B')) ? m.mid(1) : m;
  if (m == QLatin1String("JT65"))
    return QStringLiteral("JT65A");
  if (m == QLatin1String("Q65"))
    return QStringLiteral("Q65A");
  return QString();
}

void SpotWatch::ingest(const QList<DxSpot>& spots, const QDateTime& nowUtc) {
  const QDateTime oldest = nowUtc.addSecs(-qint64(keepMins_) * 60);
  for (const DxSpot& s : spots) {
    if (s.time.isValid() && s.time < oldest)
      continue;
    for (const QString& call : watch_) {
      if (!sameStation(s.call, call))
        continue;
      auto it = latest_.find(call);
      if (it != latest_.end() && it->time.isValid() && s.time.isValid() && s.time <= it->time)
        continue;
      latest_.insert(call, s);
      emit spotted(call, s);
    }
  }
  prune(nowUtc);
  emit changed();
}

void SpotWatch::prune(const QDateTime& nowUtc) {
  const QDateTime oldest = nowUtc.addSecs(-qint64(keepMins_) * 60);
  bool gone = false;
  for (auto it = latest_.begin(); it != latest_.end();) {
    if (it->time.isValid() && it->time < oldest) {
      it = latest_.erase(it);
      gone = true;
    } else {
      ++it;
    }
  }
  if (gone)
    emit changed();
}

void SpotWatch::poll() {
  if (!enabled_ || watch_.isEmpty() || busy_ || qEnvironmentVariableIsSet("STD_SELFTEST")) {
    prune();
    return;
  }
  if (!net_)
    net_ = new QNetworkAccessManager(this);
  // Spothole matches the start of the call, so ask for each whole watched
  // call (and each side of a portable one); sameStation drops 3B9FR when
  // 3B9KW is watched.
  QStringList prefixes;
  for (const QString& call : watch_)
    for (const QString& part : call.split(QLatin1Char('/'), Qt::SkipEmptyParts))
      if (part.size() >= 3 && !prefixes.contains(part))
        prefixes << part;
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("mode_type"), QStringLiteral("DATA"));
  query.addQueryItem(QStringLiteral("source"), QStringLiteral("Cluster,RBN"));
  query.addQueryItem(QStringLiteral("dx_call_prefix"), prefixes.join(QLatin1Char(',')));
  query.addQueryItem(QStringLiteral("limit"), QStringLiteral("500"));
  if (since_ > 0)
    query.addQueryItem(QStringLiteral("received_since"), QString::number(since_, 'f', 6));
  else
    query.addQueryItem(QStringLiteral("max_age"), QString::number(keepMins_ * 60));
  QUrl url(apiUrl());
  url.setQuery(query);
  QNetworkRequest request(url);
  request.setRawHeader("User-Agent", QByteArrayLiteral("STD/") + STD_VERSION);
  request.setTransferTimeout(15000);
  busy_ = true;
  QNetworkReply* reply = net_->get(request);
  const QStringList asked = watch_;
  connect(reply, &QNetworkReply::finished, this, [this, reply, asked] {
    reply->deleteLater();
    busy_ = false;
    checkedAt_ = QDateTime::currentDateTimeUtc();
    if (reply->error() != QNetworkReply::NoError) {
      error_ = QStringLiteral("Spothole did not answer (%1).").arg(reply->errorString());
      prune();
      emit changed();
      return;
    }
    QString err;
    double newest = since_;
    const QList<DxSpot> list = parseSpots(reply->readAll(), &err, &newest);
    if (!err.isEmpty()) {
      error_ = err;
      emit changed();
      return;
    }
    error_.clear();
    if (!enabled_)
      return;
    if (asked != watch_) {
      poll();  // the list changed while this was out: ask for the new one
      return;
    }
    since_ = newest;
    ingest(list, checkedAt_);
  });
}
