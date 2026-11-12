// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DxpedFeed.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

constexpr qint64 kStaleSecs = 60 * 60;  // the list changes a few times a day

int monthOf(const QString& name) {
  static const char* months[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                 "jul", "aug", "sep", "oct", "nov", "dec"};
  const QString m = name.left(3).toLower();
  for (int i = 0; i < 12; ++i) {
    if (m == QLatin1String(months[i]))
      return i + 1;
  }
  return 0;
}

QDate dayOf(int year, int month, int day) {
  if (month < 1 || year < 1900)
    return {};
  return QDate(year, month, std::min(day, QDate(year, month, 1).daysInMonth()));
}

}  // namespace

DxpedFeed::DxpedFeed(QObject* parent) : QObject(parent) {}

QString DxpedFeed::feedUrl() {
  return QStringLiteral("https://www.ng3k.com/adxo.xml");
}

QString DxpedFeed::listUrl() {
  return QStringLiteral("https://www.ng3k.com/Misc/adxo.html");
}

QString DxpedFeed::cachePath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/dxpeditions.xml");
}

bool DxpedFeed::parseDates(const QString& text, QDate* start, QDate* end) {
  static const QRegularExpression range(QStringLiteral(
      "^([A-Za-z]{3})[a-z]*\\.?\\s+(\\d{1,2})(?:,\\s*(\\d{4}))?\\s*[-\\x{2013}]\\s*"
      "(?:([A-Za-z]{3})[a-z]*\\.?\\s+)?(\\d{1,2}),\\s*(\\d{4})$"));
  static const QRegularExpression single(
      QStringLiteral("^([A-Za-z]{3})[a-z]*\\.?\\s+(\\d{1,2}),\\s*(\\d{4})$"));
  const QString t = text.simplified();
  QRegularExpressionMatch m = range.match(t);
  if (m.hasMatch()) {
    const int sm = monthOf(m.captured(1));
    const int em = m.captured(4).isEmpty() ? sm : monthOf(m.captured(4));
    const int ey = m.captured(6).toInt();
    int sy = m.captured(3).isEmpty() ? ey : m.captured(3).toInt();
    if (m.captured(3).isEmpty() && (sm > em || (sm == em && m.captured(2).toInt() > m.captured(5).toInt())))
      sy = ey - 1;  // "Dec 28-Jan 5, 2027"
    *start = dayOf(sy, sm, m.captured(2).toInt());
    *end = dayOf(ey, em, m.captured(5).toInt());
    return start->isValid() && end->isValid();
  }
  m = single.match(t);
  if (m.hasMatch()) {
    *start = dayOf(m.captured(3).toInt(), monthOf(m.captured(1)), m.captured(2).toInt());
    *end = *start;
    return start->isValid();
  }
  *start = QDate();
  *end = QDate();
  return false;
}

QString DxpedFeed::bandsOf(const QString& info, int* key) {
  // "80-10m", "160-6m", "40 20 17 15 12 10m", "60m", "HF", "QO100".
  static const QRegularExpression band(QStringLiteral(
      "\\b(\\d{1,3}(?:\\s*[-,]?\\s*\\d{1,3})*)\\s?m\\b|\\b(HF|VHF|UHF|EME)\\b|\\b(QO-?100)\\b"));
  QStringList found;
  int longest = 0;
  auto it = band.globalMatch(info);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    QString token = m.captured(0).simplified();
    if (!m.captured(1).isEmpty()) {
      static const QRegularExpression num(QStringLiteral("\\d+"));
      auto nums = num.globalMatch(m.captured(1));
      bool sane = true;
      int top = 0;
      while (nums.hasNext()) {
        const int n = nums.next().captured(0).toInt();
        // Amateur bands in metres; anything else is a power or a count.
        static const int bands[] = {2200, 630, 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6, 4, 2};
        if (std::find(std::begin(bands), std::end(bands), n) == std::end(bands))
          sane = false;
        top = std::max(top, n);
      }
      if (!sane)
        continue;
      token.remove(QLatin1Char(' '));
      if (token.contains(QLatin1Char(',')) || m.captured(1).contains(QLatin1Char(' ')))
        token = m.captured(1).simplified() + QLatin1Char('m');
      longest = std::max(longest, top);
    } else if (m.captured(2) == QLatin1String("HF")) {
      longest = std::max(longest, 80);
    } else if (!m.captured(2).isEmpty()) {
      longest = std::max(longest, 2);
    }
    if (!found.contains(token))
      found << token;
  }
  if (key)
    *key = longest;
  return found.join(QLatin1Char(' '));
}

QStringList DxpedFeed::modesOf(const QString& info, bool* digital) {
  struct Mode {
    const char* pattern;
    const char* label;
    bool digital;
    bool caseless;
  };
  // "fm" in lower case means "from" in these notes, so the mode words are
  // matched in capitals. FT8 Fox/Hound is written "f/h".
  static const Mode modes[] = {
      {"\\bCW\\b", "CW", false, false},
      {"\\bSSB\\b", "SSB", false, false},
      {"\\bAM\\b", "AM", false, false},
      {"\\bFM\\b", "FM", false, false},
      {"\\bFT8\\b|\\bf/h\\b|\\bF/H\\b|SuperFox", "FT8", true, false},
      {"\\bFT4\\b", "FT4", true, false},
      {"\\bFT2\\b", "FT2", true, false},
      {"\\bRTTY\\b", "RTTY", true, false},
      {"\\bJT65\\b", "JT65", true, false},
      {"\\bJT9\\b", "JT9", true, false},
      {"\\bQ65\\b", "Q65", true, false},
      {"\\bMSK144\\b", "MSK144", true, false},
      {"\\bJS8\\b", "JS8", true, false},
      {"\\bOlivia\\b", "Olivia", true, true},
      {"\\bMFSK\\d*\\b", "MFSK", true, false},
      {"\\bHell\\b", "Hell", true, false},
      {"\\bFST4W?\\b|\\bWSPR\\b", "WSPR", true, false},
      {"\\bSSTV\\b", "SSTV", false, false},
  };
  static const QRegularExpression generic(
      QStringLiteral("\\bdigital\\b|\\bdigi\\b|\\bdata modes?\\b|\\bFT modes\\b"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression allModes(QStringLiteral("\\ball modes\\b"),
                                           QRegularExpression::CaseInsensitiveOption);
  QStringList out;
  bool dig = false;
  // PSK keeps the rate it names: "PSK31", "BPSK63", or just "PSK".
  static const QRegularExpression psk(QStringLiteral("\\bB?PSK(\\d*)\\b"));
  QString pskLabel;
  if (const QRegularExpressionMatch m = psk.match(info); m.hasMatch())
    pskLabel = QStringLiteral("PSK") + m.captured(1);
  for (const Mode& mode : modes) {
    const QRegularExpression re(QString::fromLatin1(mode.pattern),
                                mode.caseless ? QRegularExpression::CaseInsensitiveOption
                                              : QRegularExpression::NoPatternOption);
    if (re.match(info).hasMatch()) {
      out << QString::fromLatin1(mode.label);
      dig = dig || mode.digital;
    }
    if (!pskLabel.isEmpty() && QLatin1String(mode.label) == QLatin1String("RTTY")) {
      out << pskLabel;  // listed after RTTY, as the table reads
      dig = true;
    }
  }
  if (allModes.match(info).hasMatch()) {
    out << QStringLiteral("All");
    dig = true;
  } else if (generic.match(info).hasMatch()) {
    if (!dig)
      out << QStringLiteral("Digital");
    dig = true;
  }
  if (digital)
    *digital = dig;
  return out;
}

const QList<int>& DxpedFeed::allBands() {
  static const QList<int> bands = {160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6, 2};
  return bands;
}

int DxpedFeed::metresForHz(qint64 hz) {
  static const struct {
    int metres;
    qint64 lo, hi;
  } edges[] = {{160, 1800000, 2000000},   {80, 3500000, 4000000},   {60, 5250000, 5450000},
               {40, 7000000, 7300000},    {30, 10100000, 10150000}, {20, 14000000, 14350000},
               {17, 18068000, 18168000},  {15, 21000000, 21450000}, {12, 24890000, 24990000},
               {10, 28000000, 29700000},  {6, 50000000, 54000000},  {2, 144000000, 148000000}};
  for (const auto& e : edges)
    if (hz >= e.lo && hz <= e.hi)
      return e.metres;
  return 0;
}

QList<int> DxpedFeed::bandMetres(const QString& bands) {
  const QList<int>& order = allBands();
  QList<int> out;
  auto add = [&](int m) {
    if (order.contains(m) && !out.contains(m))
      out << m;
  };
  QString rest = bands;
  if (rest.contains(QRegularExpression(QStringLiteral("\\bHF\\b")))) {
    for (int m : order)
      if (m >= 10)
        add(m);
  }
  if (rest.contains(QRegularExpression(QStringLiteral("\\bVHF\\b")))) {
    add(6);
    add(2);
  }
  // "80-10m": every band from 80 down to 10.
  static const QRegularExpression range(QStringLiteral("\\b(\\d{1,3})-(\\d{1,3})m\\b"));
  auto r = range.globalMatch(rest);
  while (r.hasNext()) {
    const QRegularExpressionMatch m = r.next();
    const int hi = std::max(m.captured(1).toInt(), m.captured(2).toInt());
    const int lo = std::min(m.captured(1).toInt(), m.captured(2).toInt());
    for (int b : order)
      if (b <= hi && b >= lo)
        add(b);
  }
  rest.remove(range);
  // "60m" and "40 20 17 15 12 10m".
  static const QRegularExpression list(QStringLiteral("\\b(\\d{1,3}(?:[ ,]+\\d{1,3})*)m\\b"));
  auto l = list.globalMatch(rest);
  while (l.hasNext()) {
    const QString numbers = l.next().captured(1);
    for (const QString& n : numbers.split(QRegularExpression(QStringLiteral("[ ,]+")), Qt::SkipEmptyParts))
      add(n.toInt());
  }
  std::sort(out.begin(), out.end(), [&](int a, int b) { return order.indexOf(a) < order.indexOf(b); });
  return out;
}

DxpedTune DxpedFeed::planTune(const DxpedEntry& e, int currentMetres,
                              const std::function<bool(const QString&, int)>& hasDial) {
  DxpedTune tune;
  // DXpeditions run the FT modes, RTTY, and PSK; that is the order.
  static const struct {
    const char* listed;
    const char* stdMode;
    bool dial;
  } picks[] = {{"FT8", "FT8", true},   {"FT4", "FT4", true},   {"RTTY", "RTTY", false},
               {"PSK", "PSK31", false}, {"FT2", "FT2", true},   {"JT65", "JT65A", true},
               {"Q65", "Q65A", true},   {"MSK144", "MSK144", true}};
  static const QStringList pskRates = {QStringLiteral("PSK31"),  QStringLiteral("PSK63"),
                                       QStringLiteral("PSK125"), QStringLiteral("PSK250"),
                                       QStringLiteral("PSK500"), QStringLiteral("PSK1000")};
  for (const auto& p : picks) {
    if (QLatin1String(p.listed) == QLatin1String("PSK")) {
      for (const QString& m : e.modes) {
        if (m.startsWith(QLatin1String("PSK"))) {
          tune.mode = pskRates.contains(m) ? m : QStringLiteral("PSK31");
          break;
        }
      }
    } else if (e.modes.contains(QLatin1String(p.listed))) {
      tune.mode = QString::fromLatin1(p.stdMode);
    }
    if (!tune.mode.isEmpty()) {
      tune.dial = p.dial;
      break;
    }
  }
  // "+ digital" or "all modes" without a named digital mode: FT8.
  if (tune.mode.isEmpty() && e.digital) {
    tune.mode = QStringLiteral("FT8");
    tune.dial = true;
  }
  if (tune.mode.isEmpty())
    return tune;
  tune.superHound = tune.mode == QLatin1String("FT8") && e.foxHound;
  if (!tune.dial)
    return tune;
  QList<int> listed = bandMetres(e.bands);
  if (listed.isEmpty())
    listed = {currentMetres};  // no band named: stay on this one
  QList<int> usable;
  for (int m : listed)
    if (m > 0 && hasDial(tune.mode, m))
      usable << m;
  if (usable.isEmpty())
    return tune;  // metres 0: the band chip stays
  if (usable.contains(currentMetres)) {
    tune.metres = currentMetres;
    return tune;
  }
  const QList<int>& order = allBands();
  const int here = order.indexOf(currentMetres);
  int best = usable.first();
  if (here >= 0) {
    int bestGap = 99;
    for (int m : usable) {
      const int gap = std::abs(int(order.indexOf(m)) - here);
      if (gap < bestGap) {
        bestGap = gap;
        best = m;
      }
    }
  }
  tune.metres = best;
  return tune;
}

bool DxpedFeed::foxHoundOf(const QString& info) {
  static const QRegularExpression fh(QStringLiteral("\\bf/h\\b|\\bsuper ?fox\\b|\\bfox ?/ ?hound\\b|\\bhound\\b"),
                                     QRegularExpression::CaseInsensitiveOption);
  return fh.match(info).hasMatch();
}

QList<DxpedEntry> DxpedFeed::parseRss(const QByteArray& body, QString* error) {
  QList<DxpedEntry> list;
  QXmlStreamReader xml(body);
  bool sawChannel = false;
  QString title, description, link;
  bool inItem = false;
  static const QRegularExpression titleRe(
      QStringLiteral("^(.*?):\\s*(.+?)\\s*--\\s*(\\S+)\\s*--\\s*(?:QSL via:\\s*)?(.*)$"));
  static const QRegularExpression qrzRe(QStringLiteral("qrz\\.com/db/([A-Za-z0-9/]+)"));

  auto finishItem = [&] {
    DxpedEntry e;
    QStringList parts;
    for (const QString& p : description.split(QStringLiteral("--")))
      parts << p.simplified();
    if (parts.size() >= 3) {
      e.dates = parts.value(0);
      e.entity = parts.value(1);
      e.call = parts.value(2).toUpper();
      int next = 3;
      if (parts.value(next).startsWith(QLatin1String("QSL:"), Qt::CaseInsensitive))
        e.qsl = parts.value(next++).mid(4).trimmed();
      if (parts.value(next).startsWith(QLatin1String("Source:"), Qt::CaseInsensitive))
        e.source = parts.value(next++).mid(7).trimmed();
      e.info = parts.mid(next).join(QStringLiteral(" -- ")).simplified();
    } else {
      const QRegularExpressionMatch m = titleRe.match(title.simplified());
      if (!m.hasMatch())
        return;
      e.entity = m.captured(1).trimmed();
      e.dates = m.captured(2).trimmed();
      e.call = m.captured(3).toUpper();
      e.qsl = m.captured(4).trimmed();
      e.info = description.simplified();
    }
    if (e.call.isEmpty())
      return;
    e.link = link.trimmed();
    e.lookup = e.call;
    const QRegularExpressionMatch q = qrzRe.match(e.link);
    if (q.hasMatch()) {
      const QString full = q.captured(1).toUpper();
      if (full.size() > e.call.size() && full.startsWith(e.call))
        e.lookup = full;  // "3B9" announced, worked as 3B9KW
    }
    parseDates(e.dates, &e.start, &e.end);
    e.bands = bandsOf(e.info, &e.bandKey);
    e.modes = modesOf(e.info, &e.digital);
    e.foxHound = foxHoundOf(e.info);
    list << e;
  };

  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      const QStringView name = xml.name();
      if (name == QLatin1String("channel")) {
        sawChannel = true;
      } else if (name == QLatin1String("item")) {
        inItem = true;
        title.clear();
        description.clear();
        link.clear();
      } else if (inItem && name == QLatin1String("title")) {
        title = xml.readElementText();
      } else if (inItem && name == QLatin1String("description")) {
        description = xml.readElementText();
      } else if (inItem && name == QLatin1String("link")) {
        link = xml.readElementText();
      }
    } else if (xml.isEndElement() && xml.name() == QLatin1String("item")) {
      inItem = false;
      finishItem();
    }
  }
  if (xml.hasError() && list.isEmpty()) {
    if (error)
      *error = QStringLiteral("The DXpedition list could not be read: %1").arg(xml.errorString());
    return {};
  }
  if (!sawChannel) {
    if (error)
      *error = QStringLiteral("The reply was not the NG3K DXpedition feed.");
    return {};
  }
  std::stable_sort(list.begin(), list.end(), [](const DxpedEntry& a, const DxpedEntry& b) {
    return a.start < b.start;
  });
  if (error)
    error->clear();
  return list;
}

void DxpedFeed::setEntries(const QList<DxpedEntry>& list, const QDateTime& when) {
  entries_ = list;
  fetchedAt_ = when;
  emit updated();
}

void DxpedFeed::loadCache() {
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  QFile file(cachePath());
  if (!file.open(QIODevice::ReadOnly))
    return;
  QString err;
  const QList<DxpedEntry> list = parseRss(file.readAll(), &err);
  if (!list.isEmpty())
    setEntries(list, QFileInfo(file).lastModified().toUTC());
}

void DxpedFeed::refresh(bool force) {
  if (busy_ || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (!force && fetchedAt_.isValid() &&
      fetchedAt_.secsTo(QDateTime::currentDateTimeUtc()) < kStaleSecs)
    return;
  if (!net_)
    net_ = new QNetworkAccessManager(this);
  QNetworkRequest request{QUrl(feedUrl())};
  request.setRawHeader("User-Agent", QByteArrayLiteral("STD/") + STD_VERSION);
  request.setTransferTimeout(15000);
  busy_ = true;
  emit updated();
  QNetworkReply* reply = net_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    busy_ = false;
    if (reply->error() != QNetworkReply::NoError) {
      error_ = QStringLiteral("NG3K did not answer (%1).").arg(reply->errorString());
      emit updated();
      return;
    }
    const QByteArray body = reply->readAll();
    QString err;
    const QList<DxpedEntry> list = parseRss(body, &err);
    if (list.isEmpty()) {
      error_ = err.isEmpty() ? QStringLiteral("NG3K sent an empty list.") : err;
      emit updated();
      return;
    }
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile out(cachePath());
    if (out.open(QIODevice::WriteOnly)) {
      out.write(body);
      out.commit();
    }
    error_.clear();
    setEntries(list, QDateTime::currentDateTimeUtc());
  });
}
