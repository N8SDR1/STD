// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WxFeed.h"

#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr int kPollMs = 10 * 60 * 1000;

// A JSON number, or a number written as a string (Ecowitt sends strings).
double number(const QJsonValue& v, bool* ok) {
  if (v.isDouble()) {
    *ok = true;
    return v.toDouble();
  }
  if (v.isString()) {
    bool good = false;
    const double d = v.toString().toDouble(&good);
    *ok = good;
    return d;
  }
  *ok = false;
  return 0.0;
}

double fToC(double f) {
  return (f - 32.0) * 5.0 / 9.0;
}

QString compass(int degrees) {
  static const char* points[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const int i = int(std::lround(((degrees % 360) + 360) % 360 / 45.0)) % 8;
  return QString::fromLatin1(points[i]);
}

}  // namespace

WxFeed::WxFeed(QObject* parent) : QObject(parent) {
  net_ = new QNetworkAccessManager(this);
  poll_ = new QTimer(this);
  poll_->setInterval(kPollMs);
  connect(poll_, &QTimer::timeout, this, [this] { fetchNow(); });
}

bool WxFeed::enabled() const {
  const QString source = QSettings().value(QStringLiteral("wx/source"), QStringLiteral("off")).toString();
  return source != QLatin1String("off") && !qEnvironmentVariableIsSet("STD_SELFTEST");
}

void WxFeed::reload() {
  poll_->stop();
  if (!enabled()) {
    reading_ = WxReading{};
    error_.clear();
    emit updated();
    return;
  }
  poll_->start();
  fetchNow();
}

void WxFeed::fetchNow() {
  if (!enabled())
    return;
  const QSettings s;
  const QString source = s.value(QStringLiteral("wx/source")).toString();
  QUrl url;
  WxReading (*parse)(const QByteArray&, QString*) = nullptr;
  QNetworkRequest request;
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("STD/%1 (%2)")
                        .arg(QStringLiteral(STD_VERSION),
                             s.value(QStringLiteral("dx/reportCall")).toString().trimmed().toUpper()));
  if (source == QLatin1String("openmeteo")) {
    double lat = 0.0;
    double lon = 0.0;
    if (!gridToLatLon(s.value(QStringLiteral("dx/reportGrid")).toString(), &lat, &lon)) {
      finish(reading_, QStringLiteral("Open-Meteo needs your locator in Options → User."));
      return;
    }
    url = QUrl(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"), QString::number(lat, 'f', 3));
    q.addQueryItem(QStringLiteral("longitude"), QString::number(lon, 'f', 3));
    q.addQueryItem(QStringLiteral("current"),
                   QStringLiteral("temperature_2m,relative_humidity_2m,weather_code,"
                                  "wind_speed_10m,wind_direction_10m"));
    url.setQuery(q);
    parse = &WxFeed::parseOpenMeteo;
  } else if (source == QLatin1String("nws")) {
    const QString station = s.value(QStringLiteral("wx/nwsStation")).toString().trimmed().toUpper();
    if (station.size() < 3) {
      finish(reading_, QStringLiteral("Enter the NWS station, such as KHAO, in Options → Weather."));
      return;
    }
    url = QUrl(QStringLiteral("https://api.weather.gov/stations/%1/observations/latest").arg(station));
    request.setRawHeader("Accept", "application/geo+json");
    parse = &WxFeed::parseNws;
  } else if (source == QLatin1String("ambient")) {
    const QString api = s.value(QStringLiteral("wx/ambientApiKey")).toString().trimmed();
    const QString app = s.value(QStringLiteral("wx/ambientAppKey")).toString().trimmed();
    if (api.isEmpty() || app.isEmpty()) {
      finish(reading_, QStringLiteral("Enter the Ambient Weather API key and application key."));
      return;
    }
    url = QUrl(QStringLiteral("https://rt.ambientweather.net/v1/devices"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("apiKey"), api);
    q.addQueryItem(QStringLiteral("applicationKey"), app);
    url.setQuery(q);
    parse = &WxFeed::parseAmbient;
  } else if (source == QLatin1String("ecowitt")) {
    const QString app = s.value(QStringLiteral("wx/ecowittAppKey")).toString().trimmed();
    const QString api = s.value(QStringLiteral("wx/ecowittApiKey")).toString().trimmed();
    const QString mac = s.value(QStringLiteral("wx/ecowittMac")).toString().trimmed().toUpper();
    if (app.isEmpty() || api.isEmpty() || mac.isEmpty()) {
      finish(reading_, QStringLiteral("Enter the Ecowitt application key, API key, and station MAC."));
      return;
    }
    url = QUrl(QStringLiteral("https://api.ecowitt.net/api/v3/device/real_time"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("application_key"), app);
    q.addQueryItem(QStringLiteral("api_key"), api);
    q.addQueryItem(QStringLiteral("mac"), mac);
    q.addQueryItem(QStringLiteral("call_back"), QStringLiteral("outdoor,wind"));
    q.addQueryItem(QStringLiteral("temp_unitid"), QStringLiteral("1"));        // C
    q.addQueryItem(QStringLiteral("wind_speed_unitid"), QStringLiteral("7"));  // km/h
    url.setQuery(q);
    parse = &WxFeed::parseEcowitt;
  } else {
    return;
  }
  request.setUrl(url);
  request.setTransferTimeout(20000);
  QNetworkReply* reply = net_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, parse] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      finish(reading_, QStringLiteral("Weather was not fetched: %1").arg(reply->errorString()));
      return;
    }
    QString error;
    const WxReading r = parse(reply->readAll(), &error);
    finish(r.valid ? r : reading_, error);
  });
}

void WxFeed::finish(const WxReading& r, const QString& error) {
  reading_ = r;
  error_ = error;
  emit updated();
}

QHash<QString, QString> WxFeed::macroTags() const {
  const bool metric = QSettings().value(QStringLiteral("wx/metric"), false).toBool();
  QHash<QString, QString> tags;
  const bool have = reading_.valid;
  tags.insert(QStringLiteral("<WX>"), have ? describe(reading_, metric) : QString());
  tags.insert(QStringLiteral("<WX_TEMP>"), have ? temperature(reading_, metric) : QString());
  tags.insert(QStringLiteral("<WX_COND>"), have ? reading_.condition : QString());
  tags.insert(QStringLiteral("<WX_WIND>"), have ? wind(reading_, metric) : QString());
  tags.insert(QStringLiteral("<WX_HUM>"),
              have && reading_.humidity >= 0 ? QStringLiteral("%1%").arg(std::lround(reading_.humidity))
                                             : QString());
  return tags;
}

QString WxFeed::temperature(const WxReading& r, bool metric) {
  if (!r.valid)
    return {};
  return metric ? QStringLiteral("%1C").arg(std::lround(r.tempC))
                : QStringLiteral("%1F").arg(std::lround(r.tempC * 9.0 / 5.0 + 32.0));
}

QString WxFeed::wind(const WxReading& r, bool metric) {
  if (!r.valid || r.windKmh < 0)
    return {};
  const long speed = metric ? std::lround(r.windKmh) : std::lround(r.windKmh / 1.609344);
  if (speed < 1)
    return QStringLiteral("calm");
  const QString from = r.windDir >= 0 ? compass(r.windDir) + QLatin1Char(' ') : QString();
  return QStringLiteral("%1%2 %3").arg(from).arg(speed).arg(metric ? QStringLiteral("km/h") : QStringLiteral("mph"));
}

QString WxFeed::describe(const WxReading& r, bool metric) {
  if (!r.valid)
    return {};
  QStringList parts;
  parts << temperature(r, metric);
  if (!r.condition.isEmpty())
    parts << r.condition;
  const QString w = wind(r, metric);
  if (!w.isEmpty())
    parts << (w == QLatin1String("calm") ? w : QStringLiteral("wind ") + w);
  if (r.humidity >= 0)
    parts << QStringLiteral("humidity %1%").arg(std::lround(r.humidity));
  return parts.join(QStringLiteral(", "));
}

bool WxFeed::gridToLatLon(const QString& grid, double* lat, double* lon) {
  const QString g = grid.trimmed().toUpper();
  if (g.size() < 4 || g.at(0) < QLatin1Char('A') || g.at(0) > QLatin1Char('R') || g.at(1) < QLatin1Char('A') ||
      g.at(1) > QLatin1Char('R') || !g.at(2).isDigit() || !g.at(3).isDigit())
    return false;
  double lo = (g.at(0).unicode() - 'A') * 20.0 - 180.0 + (g.at(2).unicode() - '0') * 2.0;
  double la = (g.at(1).unicode() - 'A') * 10.0 - 90.0 + (g.at(3).unicode() - '0') * 1.0;
  if (g.size() >= 6 && g.at(4) >= QLatin1Char('A') && g.at(4) <= QLatin1Char('X') && g.at(5) >= QLatin1Char('A') &&
      g.at(5) <= QLatin1Char('X')) {
    lo += (g.at(4).unicode() - 'A') * (2.0 / 24.0) + (1.0 / 24.0);
    la += (g.at(5).unicode() - 'A') * (1.0 / 24.0) + (0.5 / 24.0);
  } else {
    lo += 1.0;
    la += 0.5;
  }
  *lat = la;
  *lon = lo;
  return true;
}

QString WxFeed::wmoCondition(int code) {
  switch (code) {
    case 0: return QStringLiteral("clear");
    case 1: return QStringLiteral("mostly clear");
    case 2: return QStringLiteral("partly cloudy");
    case 3: return QStringLiteral("overcast");
    case 45: case 48: return QStringLiteral("fog");
    case 51: case 53: case 55: return QStringLiteral("drizzle");
    case 56: case 57: return QStringLiteral("freezing drizzle");
    case 61: return QStringLiteral("light rain");
    case 63: return QStringLiteral("rain");
    case 65: return QStringLiteral("heavy rain");
    case 66: case 67: return QStringLiteral("freezing rain");
    case 71: return QStringLiteral("light snow");
    case 73: return QStringLiteral("snow");
    case 75: return QStringLiteral("heavy snow");
    case 77: return QStringLiteral("snow grains");
    case 80: case 81: return QStringLiteral("rain showers");
    case 82: return QStringLiteral("heavy showers");
    case 85: case 86: return QStringLiteral("snow showers");
    case 95: return QStringLiteral("thunderstorm");
    case 96: case 99: return QStringLiteral("thunderstorm with hail");
    default: return {};
  }
}

WxReading WxFeed::parseOpenMeteo(const QByteArray& body, QString* error) {
  WxReading r;
  const QJsonObject cur = QJsonDocument::fromJson(body).object().value(QStringLiteral("current")).toObject();
  bool ok = false;
  r.tempC = number(cur.value(QStringLiteral("temperature_2m")), &ok);
  if (!ok) {
    *error = QStringLiteral("Open-Meteo sent no temperature.");
    return {};
  }
  bool okH = false;
  const double h = number(cur.value(QStringLiteral("relative_humidity_2m")), &okH);
  r.humidity = okH ? h : -1.0;
  bool okW = false;
  const double w = number(cur.value(QStringLiteral("wind_speed_10m")), &okW);
  r.windKmh = okW ? w : -1.0;
  bool okD = false;
  const double d = number(cur.value(QStringLiteral("wind_direction_10m")), &okD);
  r.windDir = okD ? int(std::lround(d)) : -1;
  bool okC = false;
  const double c = number(cur.value(QStringLiteral("weather_code")), &okC);
  r.condition = okC ? wmoCondition(int(c)) : QString();
  r.when = QDateTime::currentDateTimeUtc();
  r.valid = true;
  return r;
}

WxReading WxFeed::parseNws(const QByteArray& body, QString* error) {
  WxReading r;
  const QJsonObject p = QJsonDocument::fromJson(body).object().value(QStringLiteral("properties")).toObject();
  auto value = [&p](const char* key, bool* ok) {
    return number(p.value(QLatin1String(key)).toObject().value(QStringLiteral("value")), ok);
  };
  bool ok = false;
  r.tempC = value("temperature", &ok);
  if (!ok) {
    *error = QStringLiteral("That NWS station sent no temperature. Try a nearby station.");
    return {};
  }
  bool okH = false;
  const double h = value("relativeHumidity", &okH);
  r.humidity = okH ? h : -1.0;
  bool okW = false;
  const double w = value("windSpeed", &okW);  // km/h
  r.windKmh = okW ? w : -1.0;
  bool okD = false;
  const double d = value("windDirection", &okD);
  r.windDir = okD ? int(std::lround(d)) : -1;
  r.condition = p.value(QStringLiteral("textDescription")).toString().trimmed().toLower();
  r.when = QDateTime::currentDateTimeUtc();
  r.valid = true;
  return r;
}

WxReading WxFeed::parseAmbient(const QByteArray& body, QString* error) {
  WxReading r;
  const QJsonArray devices = QJsonDocument::fromJson(body).array();
  if (devices.isEmpty()) {
    *error = QStringLiteral("Ambient Weather sent no station. Check the two keys.");
    return {};
  }
  const QJsonObject last = devices.at(0).toObject().value(QStringLiteral("lastData")).toObject();
  bool ok = false;
  const double f = number(last.value(QStringLiteral("tempf")), &ok);
  if (!ok) {
    *error = QStringLiteral("Ambient Weather sent no outdoor temperature.");
    return {};
  }
  r.tempC = fToC(f);
  bool okH = false;
  const double h = number(last.value(QStringLiteral("humidity")), &okH);
  r.humidity = okH ? h : -1.0;
  bool okW = false;
  const double w = number(last.value(QStringLiteral("windspeedmph")), &okW);
  r.windKmh = okW ? w * 1.609344 : -1.0;
  bool okD = false;
  const double d = number(last.value(QStringLiteral("winddir")), &okD);
  r.windDir = okD ? int(std::lround(d)) : -1;
  r.when = QDateTime::currentDateTimeUtc();
  r.valid = true;
  return r;
}

WxReading WxFeed::parseEcowitt(const QByteArray& body, QString* error) {
  WxReading r;
  const QJsonObject root = QJsonDocument::fromJson(body).object();
  if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
    *error = QStringLiteral("Ecowitt said: %1").arg(root.value(QStringLiteral("msg")).toString());
    return {};
  }
  const QJsonObject data = root.value(QStringLiteral("data")).toObject();
  auto value = [&data](const char* group, const char* key, bool* ok) {
    return number(data.value(QLatin1String(group)).toObject().value(QLatin1String(key)).toObject().value(
                      QStringLiteral("value")),
                  ok);
  };
  bool ok = false;
  r.tempC = value("outdoor", "temperature", &ok);
  if (!ok) {
    *error = QStringLiteral("Ecowitt sent no outdoor temperature.");
    return {};
  }
  bool okH = false;
  const double h = value("outdoor", "humidity", &okH);
  r.humidity = okH ? h : -1.0;
  bool okW = false;
  const double w = value("wind", "wind_speed", &okW);
  r.windKmh = okW ? w : -1.0;
  bool okD = false;
  const double d = value("wind", "wind_direction", &okD);
  r.windDir = okD ? int(std::lround(d)) : -1;
  r.when = QDateTime::currentDateTimeUtc();
  r.valid = true;
  return r;
}
