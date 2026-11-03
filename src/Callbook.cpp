// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Callbook.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace {

QString titleCase(const QString& text) {
  QStringList words = text.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (QString& w : words)
    w[0] = w.at(0).toUpper();
  return words.join(QLatin1Char(' '));
}

QString firstName(const QString& text) {
  return titleCase(text.simplified().section(QLatin1Char(' '), 0, 0));
}

QString gridCase(const QString& grid) {
  const QString g = grid.trimmed();
  return g.left(4).toUpper() + g.mid(4).toLower();
}

QString agent() {
  return QStringLiteral("STD-%1").arg(QStringLiteral(STD_VERSION));
}

}  // namespace

Callbook::Callbook(QObject* parent) : QObject(parent) {
  net_ = new QNetworkAccessManager(this);
  reload();
}

bool Callbook::enabled() const {
  return source_ != QLatin1String("off") && !user_.isEmpty() && !password_.isEmpty() &&
         !qEnvironmentVariableIsSet("STD_SELFTEST");
}

QString Callbook::serviceName() const {
  return source_ == QLatin1String("qrz") ? QStringLiteral("QRZ") : QStringLiteral("HamQTH");
}

void Callbook::reload() {
  const QSettings s;
  source_ = s.value(QStringLiteral("callbook/source"), QStringLiteral("off")).toString();
  user_ = s.value(QStringLiteral("callbook/user")).toString().trimmed();
  password_ = s.value(QStringLiteral("callbook/password")).toString();
  session_.clear();
  cache_.clear();
}

bool Callbook::portable(const QString& call) {
  return call.contains(QLatin1Char('/'));
}

QString Callbook::homeCall(const QString& call) {
  QString longest;
  for (const QString& part : call.trimmed().toUpper().split(QLatin1Char('/'), Qt::SkipEmptyParts))
    if (part.size() > longest.size())
      longest = part;
  return longest;
}

void Callbook::lookup(const QString& call) {
  const QString asked = call.trimmed().toUpper();
  if (asked.isEmpty() || !enabled())
    return;
  if (cache_.contains(asked)) {
    emit answered(asked, cache_.value(asked), QString());
    return;
  }
  if (session_.isEmpty())
    login(asked);
  else
    query(asked, false);
}

void Callbook::login(const QString& call) {
  QNetworkRequest request;
  request.setTransferTimeout(15000);
  QNetworkReply* reply = nullptr;
  if (source_ == QLatin1String("qrz")) {
    // The password goes in the body, not the address.
    request.setUrl(QUrl(QStringLiteral("https://xmldata.qrz.com/xml/current/")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("username"), user_);
    form.addQueryItem(QStringLiteral("password"), password_);
    form.addQueryItem(QStringLiteral("agent"), agent());
    reply = net_->post(request, form.query(QUrl::FullyEncoded).toUtf8());
  } else {
    request.setUrl(QUrl(QStringLiteral("https://www.hamqth.com/api/v2/login")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    body.insert(QStringLiteral("callsign"), user_);
    body.insert(QStringLiteral("password"), password_);
    reply = net_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, call] {
    reply->deleteLater();
    const QByteArray body = reply->readAll();
    QString error;
    if (source_ == QLatin1String("qrz")) {
      QString key;
      parseQrz(body, &key, &error);
      session_ = key;
    } else {
      session_ = parseHamqthToken(body, &error);
    }
    if (session_.isEmpty()) {
      if (error.isEmpty())
        error = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                          : QStringLiteral("no session came back");
      finish(call, CallbookRecord{}, QStringLiteral("%1 login failed: %2").arg(serviceName(), error));
      return;
    }
    query(call, false);
  });
}

void Callbook::query(const QString& call, bool retried) {
  QNetworkRequest request;
  request.setTransferTimeout(15000);
  if (source_ == QLatin1String("qrz")) {
    QUrl url(QStringLiteral("https://xmldata.qrz.com/xml/current/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("s"), session_);
    q.addQueryItem(QStringLiteral("callsign"), call);
    url.setQuery(q);
    request.setUrl(url);
  } else {
    request.setUrl(QUrl(QStringLiteral("https://www.hamqth.com/api/v2/callbook/%1")
                            .arg(QString::fromLatin1(QUrl::toPercentEncoding(call)))));
    request.setRawHeader("Authorization", "Bearer " + session_.toUtf8());
    request.setRawHeader("Accept", "application/json");
  }
  QNetworkReply* reply = net_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, call, retried] {
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    QString error;
    CallbookRecord r;
    bool sessionGone = false;
    if (source_ == QLatin1String("qrz")) {
      QString key;
      r = parseQrz(body, &key, &error);
      // No key in the answer means the session has ended.
      sessionGone = key.isEmpty() && reply->error() == QNetworkReply::NoError;
    } else if (status == 401) {
      sessionGone = true;
    } else if (status == 404) {
      error = QStringLiteral("Not found");
    } else if (reply->error() != QNetworkReply::NoError) {
      error = reply->errorString();
    } else {
      r = parseHamqth(body, &error);
    }
    if (sessionGone) {
      session_.clear();
      if (!retried) {
        login(call);
        return;
      }
      error = QStringLiteral("the session was refused");
    }
    finish(call, r, error);
  });
}

void Callbook::finish(const QString& call, const CallbookRecord& r, const QString& error) {
  if (r.found || error.startsWith(QLatin1String("Not found")))
    cache_.insert(call, r);
  emit answered(call, r, error);
}

CallbookRecord Callbook::parseQrz(const QByteArray& xml, QString* key, QString* error) {
  CallbookRecord r;
  QXmlStreamReader in(xml);
  QString section;
  QHash<QString, QString> fields;
  while (!in.atEnd()) {
    in.readNext();
    if (in.isStartElement()) {
      const QString name = in.name().toString();
      if (name == QLatin1String("QRZDatabase"))
        continue;
      if (name == QLatin1String("Session") || name == QLatin1String("Callsign")) {
        section = name;
        continue;
      }
      const QString text = in.readElementText().trimmed();
      if (section == QLatin1String("Session")) {
        if (name == QLatin1String("Key"))
          *key = text;
        else if (name == QLatin1String("Error"))
          *error = text;
      } else if (section == QLatin1String("Callsign")) {
        fields.insert(name.toLower(), text);
      }
    }
  }
  if (in.hasError() && error->isEmpty())
    *error = QStringLiteral("QRZ sent something STD could not read");
  if (fields.value(QStringLiteral("call")).isEmpty())
    return r;
  r.found = true;
  r.call = fields.value(QStringLiteral("call")).toUpper();
  const QString nick = fields.value(QStringLiteral("nickname"));
  r.name = !nick.isEmpty() ? firstName(nick) : firstName(fields.value(QStringLiteral("fname")));
  r.qth = titleCase(fields.value(QStringLiteral("addr2")));
  r.state = fields.value(QStringLiteral("state")).toUpper();
  r.grid = gridCase(fields.value(QStringLiteral("grid")));
  return r;
}

QString Callbook::parseHamqthToken(const QByteArray& json, QString* error) {
  const QJsonObject o = QJsonDocument::fromJson(json).object();
  const QString token = o.value(QStringLiteral("token")).toString();
  if (token.isEmpty()) {
    const QString detail = o.value(QStringLiteral("detail")).toString();
    *error = !detail.isEmpty() ? detail : o.value(QStringLiteral("title")).toString();
  }
  return token;
}

CallbookRecord Callbook::parseHamqth(const QByteArray& json, QString* error) {
  CallbookRecord r;
  const QJsonObject o = QJsonDocument::fromJson(json).object();
  const QString call = o.value(QStringLiteral("callsign")).toString();
  if (call.isEmpty()) {
    *error = QStringLiteral("HamQTH sent no record");
    return r;
  }
  auto text = [](const QJsonObject& from, const char* key) {
    return from.value(QLatin1String(key)).toString().trimmed();
  };
  const QJsonObject address = o.value(QStringLiteral("address")).toObject();
  const QJsonObject region = o.value(QStringLiteral("region")).toObject();
  const QJsonObject location = o.value(QStringLiteral("location")).toObject();
  r.found = true;
  r.call = call.toUpper();
  const QString nick = text(o, "nick");
  r.name = firstName(!nick.isEmpty() ? nick : text(o, "name"));
  QString qth = text(address, "city");
  if (qth.isEmpty())
    qth = text(o, "qth");
  r.qth = titleCase(qth);
  r.state = (!text(region, "usState").isEmpty() ? text(region, "usState") : text(o, "us_state")).toUpper();
  r.grid = gridCase(!text(location, "grid").isEmpty() ? text(location, "grid") : text(o, "grid"));
  return r;
}
