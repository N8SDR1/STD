// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;

// One announced DX operation from the NG3K ADXO list.
struct DxpedEntry {
  QString call;     // as announced, such as "H44MS" or the prefix "3B9"
  QString lookup;   // full call from the info link when the announcement is a prefix
  QString entity;   // "Solomon Is"
  QString dates;    // "Oct 3-Nov 20, 2026", as written
  QDate start;
  QDate end;
  QString qsl;      // "LoTW", "IK2GZU", "Club Log OQRS"
  QString source;   // "OPDX (Sep 22, 2026)"
  QString info;     // the free text: operators, bands, modes, QSL notes
  QString link;     // QRZ page or the NG3K list
  QString bands;    // "80-10m", "HF 60m", read from the info
  QStringList modes;  // "CW", "SSB", "FT8", "Digital", ... read from the info
  bool digital = false;  // any digital mode, "+ digital", or "all modes"
  bool foxHound = false;  // FT8 Fox/Hound: "f/h", "SuperFox", "Fox/Hound"
  int bandKey = 0;  // longest wavelength in metres, for sorting (HF = 80)

  bool activeOn(const QDate& day) const { return start.isValid() && end.isValid() && start <= day && day <= end; }
};

// What a double-click on a DXpedition does: the STD mode to switch to and
// the band in metres (0 keeps the band).
struct DxpedTune {
  QString mode;    // "FT8", "FT4", "RTTY", "PSK31" (or the PSK rate listed), ...; empty: none
  int metres = 0;  // 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6, 2; 0 keeps the band
  bool dial = false;  // the mode has a dial table, so the band chip moves
  bool superHound = false;  // FT8 and the listing says Fox/Hound
};

// The DXpeditions list: NG3K's ADXO RSS feed (https://www.ng3k.com/adxo.xml),
// kept on disk so the panel opens with the last list when offline. Nothing
// is fetched until the panel is shown, then at most once an hour while it
// stays open. Refresh fetches at once. The fetch never blocks the window.
class DxpedFeed : public QObject {
  Q_OBJECT

public:
  explicit DxpedFeed(QObject* parent = nullptr);

  static QString feedUrl();
  static QString listUrl();  // the human page, https://www.ng3k.com/Misc/adxo.html
  static QString cachePath();

  // Read the cached list from disk, if any. Called once at start.
  void loadCache();
  // Fetch when the cached list is older than an hour, or always when forced.
  void refresh(bool force);

  const QList<DxpedEntry>& entries() const { return entries_; }
  QDateTime fetchedAt() const { return fetchedAt_; }  // UTC, invalid if never
  QString lastError() const { return error_; }
  bool busy() const { return busy_; }

  // Parsing, public for the self-test.
  static QList<DxpedEntry> parseRss(const QByteArray& body, QString* error);
  static bool parseDates(const QString& text, QDate* start, QDate* end);
  static QString bandsOf(const QString& info, int* key);
  static QStringList modesOf(const QString& info, bool* digital);
  // Amateur bands in metres named by a bands string ("80-10m", "HF + 6m"),
  // longest first. Empty when it names none.
  static QList<int> bandMetres(const QString& bands);
  // Pick the mode and band for a double-click. DXpeditions run the FT
  // modes, RTTY, and PSK, so the first one listed of FT8 (Super Hound when
  // the listing says Fox/Hound), FT4, RTTY, and PSK (PSK31 unless another
  // rate is named), then FT2, JT65, Q65, MSK144. "+ digital" alone is FT8.
  // The current band when it is listed and the mode has a dial there, else
  // the nearest listed band that has one. hasDial(mode, metres) answers that.
  static DxpedTune planTune(const DxpedEntry& e, int currentMetres,
                            const std::function<bool(const QString&, int)>& hasDial);
  static bool foxHoundOf(const QString& info);
  static const QList<int>& allBands();
  // The amateur band a dial is in, in metres, or 0.
  static int metresForHz(qint64 hz);  // 160 ... 2, the order STD's band chips use

  // Replace the list (the self-test feeds a canned sample through this).
  void setEntries(const QList<DxpedEntry>& list, const QDateTime& when);

signals:
  void updated();

private:
  QNetworkAccessManager* net_ = nullptr;
  QList<DxpedEntry> entries_;
  QDateTime fetchedAt_;
  QString error_;
  bool busy_ = false;
};
