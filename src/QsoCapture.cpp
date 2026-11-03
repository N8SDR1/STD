// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QsoCapture.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

const QString kStop = QStringLiteral("|");

bool isCallsign(const QString& w) {
  static const QRegularExpression call(QStringLiteral("^(?:[A-Z0-9]{1,3}/)?[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]"
                                                      "(?:/[A-Z0-9]{1,4})?$"));
  return w.size() >= 3 && w.size() <= 12 && call.match(w).hasMatch();
}

QString baseCall(const QString& call) {
  QStringList parts = call.split(QLatin1Char('/'));
  QString longest;
  for (const QString& p : parts)
    if (p.size() > longest.size())
      longest = p;
  return longest;
}

QString titleCase(const QStringList& words) {
  QStringList out;
  for (const QString& w : words)
    out << (w.left(1).toUpper() + w.mid(1).toLower());
  return out.join(QLatin1Char(' '));
}

const QSet<QString>& states() {
  static const QSet<QString> set = [] {
    QSet<QString> s;
    for (const char* st : {"AL", "AK", "AZ", "AR", "CA", "CO", "CT", "DE", "FL", "GA", "HI", "ID", "IL", "IN",
                           "IA", "KS", "KY", "LA", "ME", "MD", "MA", "MI", "MN", "MS", "MO", "MT", "NE", "NV",
                           "NH", "NJ", "NM", "NY", "NC", "ND", "OH", "OK", "OR", "PA", "RI", "SC", "SD", "TN",
                           "TX", "UT", "VT", "VA", "WA", "WV", "WI", "WY", "DC", "AB", "BC", "MB", "NB", "NL",
                           "NS", "NT", "NU", "ON", "PE", "QC", "SK", "YT"})
      s.insert(QString::fromLatin1(st));
    return s;
  }();
  return set;
}

// Words that end a name or a QTH: the next item of the exchange.
const QSet<QString>& boundaries() {
  static const QSet<QString> set = {
      QStringLiteral("NAME"), QStringLiteral("NM"),   QStringLiteral("OP"),   QStringLiteral("OPR"),
      QStringLiteral("RST"),  QStringLiteral("UR"),   QStringLiteral("RIG"),  QStringLiteral("ANT"),
      QStringLiteral("WX"),   QStringLiteral("PWR"),  QStringLiteral("LOC"),  QStringLiteral("GRID"),
      QStringLiteral("QRA"),  QStringLiteral("BTU"),  QStringLiteral("HW"),   QStringLiteral("HWCPY"),
      QStringLiteral("SO"),   QStringLiteral("DE"),   QStringLiteral("K"),    QStringLiteral("KN"),
      QStringLiteral("AR"),   QStringLiteral("SK"),   QStringLiteral("73"),   QStringLiteral("TNX"),
      QStringLiteral("TKS"),  QStringLiteral("ES"),   QStringLiteral("AND"),  QStringLiteral("QTH"),
      QStringLiteral("BK"),   QStringLiteral("PSE"),  QStringLiteral("IS"),   QStringLiteral("HR"),
      QStringLiteral("HERE"), QStringLiteral("MY"),   QStringLiteral("THE"),  QStringLiteral("IN"),
      QStringLiteral("NR"),   QStringLiteral("NEAR"), QStringLiteral("JUST"), kStop};
  return set;
}

}  // namespace

QsoHints scanQsoText(const QString& text, const QString& myCall) {
  QsoHints hints;
  // Words, with a sentence end or line break as a stop mark.
  QString flat = text.toUpper();
  flat.replace(QRegularExpression(QStringLiteral("[.!?;\\n\\r]")), QStringLiteral(" | "));
  flat.replace(QRegularExpression(QStringLiteral("[^A-Z0-9/| ]")), QStringLiteral(" "));
  const QStringList w = flat.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const QString mine = baseCall(myCall.trimmed().toUpper());
  auto skip = [&w](int i, std::initializer_list<const char*> filler) {
    while (i < w.size()) {
      bool hit = false;
      for (const char* f : filler)
        hit = hit || w.at(i) == QLatin1String(f);
      if (!hit)
        break;
      ++i;
    }
    return i;
  };
  static const QRegularExpression rst(QStringLiteral("^[1-5][1-9][1-9]$"));
  static const QRegularExpression grid(QStringLiteral("^[A-R]{2}[0-9]{2}(?:[A-X]{2})?$"));
  for (int i = 0; i < w.size(); ++i) {
    const QString& t = w.at(i);
    if (t == QLatin1String("NAME") || t == QLatin1String("NM") || t == QLatin1String("OP") ||
        t == QLatin1String("OPR") || t == QLatin1String("OPERATOR")) {
      const int j = skip(i + 1, {"IS", "HR", "HERE", "MY"});
      if (j < w.size()) {
        const QString& n = w.at(j);
        static const QRegularExpression word(QStringLiteral("^[A-Z]{2,12}$"));
        if (word.match(n).hasMatch() && !boundaries().contains(n))
          hints.name = titleCase({n});
      }
    } else if (t == QLatin1String("QTH")) {
      int j = skip(i + 1, {"IS", "HR", "HERE", "NR", "NEAR", "JUST", "IN"});
      QStringList place;
      while (j < w.size() && place.size() < 4 && !boundaries().contains(w.at(j)) && !rst.match(w.at(j)).hasMatch())
        place << w.at(j++);
      if (!place.isEmpty()) {
        if (place.size() >= 2 && states().contains(place.last())) {
          hints.st = place.takeLast();
        } else {
          hints.st.clear();
        }
        hints.qth = titleCase(place);
      }
    } else if (t == QLatin1String("UR") || t == QLatin1String("YOUR") || t == QLatin1String("RST") ||
               t == QLatin1String("RPT")) {
      const int j = skip(i + 1, {"RST", "SIG", "SIGNAL", "RPT", "IS"});
      if (j < w.size() && rst.match(w.at(j)).hasMatch())
        hints.rstIn = w.at(j);
    } else if (t == QLatin1String("LOC") || t == QLatin1String("GRID") || t == QLatin1String("QRA") ||
               t == QLatin1String("LOCATOR")) {
      const int j = skip(i + 1, {"IS", "HR"});
      if (j < w.size() && grid.match(w.at(j)).hasMatch())
        hints.loc = w.at(j).left(4) + w.at(j).mid(4).toLower();
    } else if (t == QLatin1String("DE") && i + 1 < w.size() && isCallsign(w.at(i + 1)) &&
               baseCall(w.at(i + 1)) != mine) {
      hints.call = w.at(i + 1);
    }
  }
  return hints;
}

QList<QHash<QString, QString>> parseAdif(const QByteArray& adif) {
  QList<QHash<QString, QString>> records;
  QHash<QString, QString> current;
  int i = 0;
  const int n = int(adif.size());
  while (i < n) {
    const int open = adif.indexOf('<', i);
    if (open < 0)
      break;
    const int close = adif.indexOf('>', open);
    if (close < 0)
      break;
    const QByteArray spec = adif.mid(open + 1, close - open - 1);
    const QList<QByteArray> parts = spec.split(':');
    const QString name = QString::fromLatin1(parts.value(0)).trimmed().toUpper();
    i = close + 1;
    if (parts.size() < 2) {
      if (name == QLatin1String("EOR")) {
        if (!current.isEmpty())
          records << current;
        current.clear();
      } else if (name == QLatin1String("EOH")) {
        current.clear();
      }
      continue;
    }
    bool ok = false;
    const int len = parts.at(1).trimmed().toInt(&ok);
    if (!ok || len < 0 || i + len > n)
      continue;
    current.insert(name, QString::fromUtf8(adif.mid(i, len)).trimmed());
    i += len;
  }
  return records;
}

void QsoMemory::setPaths(const QStringList& paths) {
  if (paths != paths_) {
    paths_ = paths;
    seen_.clear();
  }
}

void QsoMemory::loadText(const QList<QByteArray>& logs) {
  calls_.clear();
  QHash<QString, QString> lastStamp;
  QSet<QString> counted;
  QList<QHash<QString, QString>> records;
  for (const QByteArray& adif : logs)
    records += parseAdif(adif);
  for (const auto& r : records) {
    const QString call = r.value(QStringLiteral("CALL")).toUpper();
    if (call.isEmpty())
      continue;
    // The same QSO in two logs: same call, date, and minute.
    const QString once = call + QLatin1Char('|') + r.value(QStringLiteral("QSO_DATE")) +
                         r.value(QStringLiteral("TIME_ON")).left(4);
    if (counted.contains(once))
      continue;
    counted.insert(once);
    QsoHistory& h = calls_[call];
    ++h.count;
    const QString date = r.value(QStringLiteral("QSO_DATE"));
    const QString stamp = date + r.value(QStringLiteral("TIME_ON"));
    // The newest value of each field wins; a later record without one keeps the older.
    const bool newer = stamp >= lastStamp.value(call);
    auto take = [&](QString& field, const char* key) {
      const QString v = r.value(QLatin1String(key));
      if (!v.isEmpty() && (newer || field.isEmpty()))
        field = v;
    };
    take(h.name, "NAME");
    take(h.qth, "QTH");
    take(h.state, "STATE");
    if (h.state.isEmpty() || newer)
      take(h.state, "VE_PROV");
    take(h.grid, "GRIDSQUARE");
    if (newer) {
      lastStamp.insert(call, stamp);
      if (date.size() == 8)
        h.lastDate = date.left(4) + QLatin1Char('-') + date.mid(4, 2) + QLatin1Char('-') + date.mid(6, 2);
      h.lastBand = r.value(QStringLiteral("BAND")).toLower();
      h.lastMode = r.value(QStringLiteral("MODE"));
    }
  }
}

void QsoMemory::refresh() {
  QString now;
  for (const QString& path : paths_) {
    const QFileInfo info(path);
    if (info.exists())
      now += path + QLatin1Char('|') + QString::number(info.size()) + QLatin1Char('|') +
             QString::number(info.lastModified().toMSecsSinceEpoch()) + QLatin1Char('\n');
  }
  if (now == seen_)
    return;
  QList<QByteArray> logs;
  for (const QString& path : paths_) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
      logs << file.readAll();
  }
  loadText(logs);
  seen_ = now;
}

QsoHistory QsoMemory::lookup(const QString& call) {
  refresh();
  return calls_.value(call.trimmed().toUpper());
}
