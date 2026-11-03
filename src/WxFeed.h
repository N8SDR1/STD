// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

// Local weather for the <WX> macro tags. One source at a time, set in
// Options → Weather: Open-Meteo from the locator (free, no account), an
// NWS observation station (US), or an Ambient Weather or Ecowitt station.
// It fetches every 10 minutes while a source is on. Nothing is fetched
// while the source is Off, which is the default.
struct WxReading {
  bool valid = false;
  double tempC = 0.0;
  double humidity = -1.0;  // percent, <0 unknown
  double windKmh = -1.0;   // <0 unknown
  int windDir = -1;        // degrees the wind comes from, <0 unknown
  QString condition;       // "partly cloudy", empty when the source has none
  QDateTime when;          // UTC
};

class WxFeed : public QObject {
  Q_OBJECT

public:
  explicit WxFeed(QObject* parent = nullptr);

  // Read Options → Weather again and fetch now when a source is on.
  void reload();
  void fetchNow();

  const WxReading& reading() const { return reading_; }
  QString lastError() const { return error_; }
  bool enabled() const;

  // "<WX>", "<WX_TEMP>", "<WX_COND>", "<WX_WIND>", "<WX_HUM>" for macros.
  // Empty values while nothing has been fetched.
  QHash<QString, QString> macroTags() const;

  // "54F, partly cloudy, wind SW 8 mph, humidity 72%".
  static QString describe(const WxReading& r, bool metric);
  static QString temperature(const WxReading& r, bool metric);
  static QString wind(const WxReading& r, bool metric);
  static bool gridToLatLon(const QString& grid, double* lat, double* lon);
  static QString wmoCondition(int code);

  // Each source's reply, read into a reading. Public for the self-test.
  static WxReading parseOpenMeteo(const QByteArray& body, QString* error);
  static WxReading parseNws(const QByteArray& body, QString* error);
  static WxReading parseAmbient(const QByteArray& body, QString* error);
  static WxReading parseEcowitt(const QByteArray& body, QString* error);

signals:
  void updated();

private:
  void finish(const WxReading& r, const QString& error);

  QNetworkAccessManager* net_ = nullptr;
  QTimer* poll_ = nullptr;
  WxReading reading_;
  QString error_;
};
