// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Needed.h"

#include <QList>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace {

// A cty.dat prefix entry without its overrides: (CQ) [ITU] <lat/lon> {cont} ~tz~.
QString stripOverrides(QString entry) {
  static const QRegularExpression over(QStringLiteral(R"(\(\d+\)|\[\d+\]|<[^>]*>|\{[^}]*\}|~[^~]*~)"));
  entry.remove(over);
  return entry.trimmed().toUpper();
}

bool portable(const QString& part) {
  return part == QLatin1String("P") || part == QLatin1String("M") || part == QLatin1String("MM") ||
         part == QLatin1String("AM") || part == QLatin1String("QRP") || part == QLatin1String("A") ||
         part == QLatin1String("R") || part == QLatin1String("LH") || part == QLatin1String("B") ||
         (part.size() == 1 && part.at(0).isDigit());
}

bool looksLikeCall(const QString& token) {
  if (token.size() < 3 || token.size() > 13)
    return false;
  bool digit = false;
  bool letter = false;
  for (const QChar ch : token) {
    if (ch == QLatin1Char('/'))
      continue;
    if (!ch.isLetterOrNumber())
      return false;
    digit = digit || ch.isDigit();
    letter = letter || ch.isLetter();
  }
  return digit && letter;
}

bool isGrid(const QString& token) {
  static const QRegularExpression grid(QStringLiteral("^[A-R]{2}[0-9]{2}$"));
  return token != QLatin1String("RR73") && grid.match(token).hasMatch();
}

// The part of a call cty.dat is matched on. K1ABC/VE3 and VE3/K1ABC are
// VE3; K1ABC/P and K1ABC/7 stay K1ABC.
QString lookupPart(const QString& call) {
  const QStringList parts = call.toUpper().split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString best;
  for (const QString& part : parts) {
    if (portable(part))
      continue;
    if (best.isEmpty() || part.size() < best.size())
      best = part;
  }
  // One part left: it is the call itself.
  if (parts.size() == 1 || best.isEmpty())
    return parts.isEmpty() ? QString() : parts.first();
  return best;
}

}  // namespace

bool CtyTable::load(const QByteArray& data) {
  exact_.clear();
  prefix_.clear();
  longest_ = 0;
  entities_ = 0;
  const QString text = QString::fromLatin1(data);
  int pos = 0;
  while (pos < text.size()) {
    const int end = text.indexOf(QLatin1Char(';'), pos);
    if (end < 0)
      break;
    const QString record = text.mid(pos, end - pos);
    pos = end + 1;
    // Name: CQ: ITU: Cont: Lat: Lon: TZ: Prefix: then the prefix list.
    QStringList head = record.split(QLatin1Char(':'));
    if (head.size() < 9)
      continue;
    const QString name = head.at(0).trimmed();
    QString primary = head.at(7).trimmed().toUpper();
    if (primary.startsWith(QLatin1Char('*')))
      primary.remove(0, 1);
    const QString list = head.mid(8).join(QLatin1Char(':'));
    if (name.isEmpty())
      continue;
    ++entities_;
    if (!primary.isEmpty()) {
      prefix_.insert(primary, name);
      longest_ = std::max(longest_, int(primary.size()));
    }
    for (const QString& raw : list.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      QString entry = stripOverrides(raw);
      if (entry.isEmpty())
        continue;
      if (entry.startsWith(QLatin1Char('='))) {
        exact_.insert(entry.mid(1), name);
      } else {
        prefix_.insert(entry, name);
        longest_ = std::max(longest_, int(entry.size()));
      }
    }
  }
  return !prefix_.isEmpty();
}

QString CtyTable::entity(const QString& call) const {
  const QString up = call.trimmed().toUpper();
  if (up.isEmpty())
    return QString();
  auto exact = exact_.constFind(up);
  if (exact != exact_.constEnd())
    return exact.value();
  const QString part = lookupPart(up);
  exact = exact_.constFind(part);
  if (exact != exact_.constEnd())
    return exact.value();
  for (int n = std::min(int(part.size()), longest_); n > 0; --n) {
    auto it = prefix_.constFind(part.left(n));
    if (it != prefix_.constEnd())
      return it.value();
  }
  return QString();
}

void WorkedIndex::clear() {
  entities.clear();
  entityBands.clear();
  grids.clear();
  gridBands.clear();
  calls.clear();
  callBands.clear();
}

QString homeCallOf(const QString& call) {
  const QStringList parts = call.trimmed().toUpper().split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString home;
  for (const QString& part : parts) {
    if (portable(part) || !looksLikeCall(part))
      continue;
    if (part.size() > home.size())
      home = part;
  }
  return home;
}

void WorkedIndex::add(const CtyTable& cty, const QString& call, const QString& band, const QString& grid) {
  const QString entity = cty.entity(call);
  if (!entity.isEmpty()) {
    entities.insert(entity);
    if (!band.isEmpty())
      entityBands.insert(entity + QLatin1Char('|') + band.toLower());
  }
  const QString b = band.toLower();
  const QString g = grid.trimmed().toUpper().left(4);
  if (isGrid(g)) {
    grids.insert(g);
    if (!b.isEmpty())
      gridBands.insert(g + QLatin1Char('|') + b);
  }
  const QString home = homeCallOf(call);
  if (!home.isEmpty()) {
    calls.insert(home);
    if (!b.isEmpty())
      callBands.insert(home + QLatin1Char('|') + b);
  }
}

void WorkedIndex::addAdif(const CtyTable& cty, const QByteArray& adif) {
  const QByteArray upper = adif.toUpper();
  int eoh = upper.indexOf("<EOH>");
  int pos = eoh < 0 ? 0 : eoh + 5;
  QString call;
  QString band;
  QString grid;
  double freq = 0.0;
  while (pos < upper.size()) {
    const int open = upper.indexOf('<', pos);
    if (open < 0)
      break;
    const int close = upper.indexOf('>', open);
    if (close < 0)
      break;
    const QByteArray tag = upper.mid(open + 1, close - open - 1);
    pos = close + 1;
    if (tag == "EOR") {
      if (band.isEmpty() && freq > 0.0)
        band = adifBandForMhz(freq);
      if (!call.isEmpty())
        add(cty, call, band, grid);
      call.clear();
      band.clear();
      grid.clear();
      freq = 0.0;
      continue;
    }
    const QList<QByteArray> parts = tag.split(':');
    if (parts.size() < 2)
      continue;
    bool ok = false;
    const int length = parts.at(1).toInt(&ok);
    if (!ok || length < 0 || pos + length > adif.size())
      continue;
    const QString value = QString::fromLatin1(adif.constData() + pos, length).trimmed();
    pos += length;
    const QByteArray& name = parts.at(0);
    if (name == "CALL")
      call = value.toUpper();
    else if (name == "BAND")
      band = value.toLower();
    else if (name == "GRIDSQUARE")
      grid = value;
    else if (name == "FREQ")
      freq = value.toDouble();
  }
}

QString adifBandForMhz(double mhz) {
  struct Edge {
    double low, high;
    const char* name;
  };
  static const Edge kBands[] = {
      {0.1357, 0.1378, "2190m"}, {0.472, 0.479, "630m"}, {1.8, 2.0, "160m"},  {3.5, 4.0, "80m"},
      {5.06, 5.45, "60m"},       {7.0, 7.3, "40m"},      {10.1, 10.15, "30m"}, {14.0, 14.35, "20m"},
      {18.068, 18.168, "17m"},   {21.0, 21.45, "15m"},   {24.89, 24.99, "12m"}, {28.0, 29.7, "10m"},
      {40.0, 45.0, "8m"},        {50.0, 54.0, "6m"},     {70.0, 71.0, "4m"},   {144.0, 148.0, "2m"},
      {222.0, 225.0, "1.25m"},   {420.0, 450.0, "70cm"},
  };
  for (const Edge& e : kBands) {
    if (mhz >= e.low && mhz <= e.high)
      return QLatin1String(e.name);
  }
  return QString();
}

bool messageStation(const QString& message, QString* call, QString* grid) {
  QStringList tokens = message.toUpper().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (QString& t : tokens) {
    if (t.startsWith(QLatin1Char('<')) && t.endsWith(QLatin1Char('>')))
      t = t.mid(1, t.size() - 2);
  }
  int at = -1;
  if (!tokens.isEmpty() && tokens.first() == QLatin1String("CQ")) {
    at = 1;
    // CQ DX, CQ NA, CQ POTA, CQ 040: a word that is not a call.
    if (at < tokens.size() && !looksLikeCall(tokens.at(at)))
      ++at;
  } else if (tokens.size() >= 2) {
    at = 1;
  }
  if (at < 0 || at >= tokens.size() || !looksLikeCall(tokens.at(at)) || tokens.at(at) == QLatin1String("..."))
    return false;
  if (call)
    *call = tokens.at(at);
  if (grid)
    *grid = at + 1 < tokens.size() && isGrid(tokens.at(at + 1)) ? tokens.at(at + 1) : QString();
  return true;
}

Need classifyNeed(const CtyTable& cty, const WorkedIndex& log, const QString& call, const QString& grid,
                  const QString& band) {
  const QString b = band.toLower();
  const QString entity = cty.entity(call);
  if (!entity.isEmpty()) {
    if (!log.entities.contains(entity))
      return Need::Entity;
    if (!band.isEmpty() && !log.entityBands.contains(entity + QLatin1Char('|') + band.toLower()))
      return Need::Band;
  }
  const QString g = grid.trimmed().toUpper().left(4);
  if (isGrid(g)) {
    if (!log.grids.contains(g))
      return Need::Grid;
    if (!b.isEmpty() && !log.gridBands.contains(g + QLatin1Char('|') + b))
      return Need::GridBand;
  }
  // Worked before: that call on this band, or anywhere when the band is unknown.
  const QString home = homeCallOf(call);
  if (!home.isEmpty() && (b.isEmpty() ? log.calls.contains(home) : log.callBands.contains(home + QLatin1Char('|') + b)))
    return Need::Worked;
  return Need::None;
}

QString needTag(Need need) {
  switch (need) {
  case Need::Entity:
    return QStringLiteral("NEW");
  case Need::Band:
    return QStringLiteral("BAND");
  case Need::Grid:
    return QStringLiteral("GRID");
  case Need::GridBand:
    return QStringLiteral("GRID+BAND");
  case Need::Worked:
    return QStringLiteral("B4");
  case Need::None:
    break;
  }
  return QString();
}
