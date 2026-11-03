// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ContestMode.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTimeZone>

namespace {

// The eight Run and S&P messages, with %CQ% for the contest's CQ word.
// Kept short: in RTTY every extra word is a chance to garble.
const QStringList kRun = {
    QStringLiteral("<TX>CQ %CQ% <MYCALL> <MYCALL> CQ<RX>"),
    QStringLiteral("<TX><CALL> <XOUT> <XOUT><RX>"),
    QStringLiteral("<TX>TU <MYCALL> %CQ%<LOG><CLRQSO><RX>"),
    QStringLiteral("<TX><MYCALL><RX>"),
    QStringLiteral("<TX><CALL><RX>"),
    QStringLiteral("<TX><CALL> QSO B4 TU <MYCALL> %CQ%<CLRQSO><RX>"),
    QStringLiteral("<TX>AGN AGN<RX>"),
    QStringLiteral("<TX><XOUT> <XOUT><RX>"),
};
const QStringList kSp = {
    QStringLiteral("<TX>CQ %CQ% <MYCALL> <MYCALL> CQ<RX>"),
    QStringLiteral("<TX><CALL> TU <XOUT> <XOUT><LOG><CLRQSO><RX>"),
    QStringLiteral("<TX>TU<RX>"),
    QStringLiteral("<TX><MYCALL> <MYCALL><RX>"),
    QStringLiteral("<TX><CALL><RX>"),
    QStringLiteral("<TX><CALL> <MYCALL> <MYCALL><RX>"),
    QStringLiteral("<TX>AGN AGN<RX>"),
    QStringLiteral("<TX><XOUT> <XOUT><RX>"),
};
const QStringList kSpLabels = {QStringLiteral("CQ"),      QStringLiteral("TU exch"), QStringLiteral("TU"),
                               QStringLiteral("My call"), QStringLiteral("His call"), QStringLiteral("Call+me"),
                               QStringLiteral("AGN?"),    QStringLiteral("Rpt exch")};
const QStringList kRunLabels = {QStringLiteral("CQ"),      QStringLiteral("Exch"),   QStringLiteral("TU"),
                                QStringLiteral("My call"), QStringLiteral("His call"), QStringLiteral("QSO B4"),
                                QStringLiteral("AGN?"),    QStringLiteral("Rpt exch")};

ContestTemplate make(const char* id, const char* name, const char* adif, const char* cq, bool rst,
                     const char* myHint, const char* theirHint, int words) {
  ContestTemplate t;
  t.id = QString::fromLatin1(id);
  t.name = QString::fromLatin1(name);
  t.adifId = QString::fromLatin1(adif);
  t.cq = QString::fromLatin1(cq);
  t.rst = rst;
  t.myExchHint = QString::fromLatin1(myHint);
  t.theirHint = QString::fromLatin1(theirHint);
  t.exchWords = words;
  for (const QString& m : kRun)
    t.run << QString(m).replace(QStringLiteral("%CQ%"), t.cq);
  for (const QString& m : kSp)
    t.sp << QString(m).replace(QStringLiteral("%CQ%"), t.cq);
  t.labels = kRunLabels;
  t.spLabels = kSpLabels;
  return t;
}

bool isRst(const QString& w) {
  static const QRegularExpression rst(QStringLiteral("^5[1-9N][9N]$"));
  return rst.match(w).hasMatch();
}

bool isCallLike(const QString& w) {
  static const QRegularExpression call(QStringLiteral("^(?:[A-Z0-9]{1,3}/)?[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]"
                                                      "(?:/[A-Z0-9]{1,4})?$"));
  return w.size() >= 3 && call.match(w).hasMatch();
}

}  // namespace

const QVector<ContestTemplate>& contestTemplates() {
  static const QVector<ContestTemplate> list = [] {
    QVector<ContestTemplate> v;
    v << make("rtty-ru", "ARRL RTTY Roundup", "ARRL-RTTY", "RU", true,
              "Your state or province, such as OH; DX sends # for the serial", "Their state, province, or serial", 1);
    v << make("cqww-rtty", "CQ WW RTTY", "CQ-WW-RTTY", "TEST", true,
              "Your CQ zone, and in the US and Canada your state, such as 05 OH", "Their zone, and state", 2);
    v << make("na-sprint", "NA RTTY Sprint", "NA-SPRINT-RTTY", "NA", false,
              "Serial, name, and state, such as # RICK OH", "Their serial, name, and state", 3);
    v << make("field-day", "ARRL Field Day", "ARRL-FD", "FD", false,
              "Your class and section, such as 2A OH", "Their class and section", 2);
    v << make("feld-hell", "Feld Hell sprint", "FH-SPRINT", "FH", true,
              "Your exchange for the sprint, such as OH 5065", "Their exchange", 2);
    v << make("serial", "Serial number", "", "TEST", true, "#, the serial, three digits", "Their serial", 1);
    v << make("custom", "Custom", "", "TEST", true, "Whatever this contest asks for", "Their exchange", 2);
    return v;
  }();
  return list;
}

const ContestTemplate& contestTemplate(const QString& id) {
  for (const ContestTemplate& t : contestTemplates())
    if (t.id == id)
      return t;
  return contestTemplates().first();
}

QString contestExchange(const ContestTemplate& t, const QString& myExch, int serial) {
  QString mine = myExch.simplified().toUpper();
  mine.replace(QLatin1Char('#'), QStringLiteral("%1").arg(std::max(1, serial), 3, 10, QLatin1Char('0')));
  if (!t.rst)
    return mine;
  return mine.isEmpty() ? QStringLiteral("599") : QStringLiteral("599 ") + mine;
}

QString contestExchangeHeard(const ContestTemplate& t, const QString& text, const QString& theirCall,
                             const QString& myCall) {
  QString flat = text.toUpper();
  flat.replace(QRegularExpression(QStringLiteral("[^A-Z0-9/]")), QStringLiteral(" "));
  const QStringList w = flat.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const QString his = theirCall.trimmed().toUpper();
  const QString mine = myCall.trimmed().toUpper();
  static const QSet<QString> filler = {QStringLiteral("TU"), QStringLiteral("DE"), QStringLiteral("K"),
                                       QStringLiteral("BK"), QStringLiteral("KN"), QStringLiteral("QSL"),
                                       QStringLiteral("R"),  QStringLiteral("RR"), QStringLiteral("NR"),
                                       QStringLiteral("CQ"), QStringLiteral("TEST"), QStringLiteral("AGN")};
  // Where their exchange starts: after their last 599, or after their call
  // for the contests without one.
  int start = -1;
  for (int i = int(w.size()) - 1; i >= 0; --i) {
    if (t.rst ? isRst(w.at(i)) : (w.at(i) == his || (!his.isEmpty() && w.at(i) == mine))) {
      start = i + 1;
      break;
    }
  }
  if (start < 0)
    return {};
  QStringList got;
  for (int i = start; i < w.size() && got.size() < t.exchWords; ++i) {
    const QString& x = w.at(i);
    if (filler.contains(x) || isRst(x) || x == his || x == mine)
      continue;
    if (isCallLike(x) && t.id != QLatin1String("na-sprint"))
      break;  // the next station, not this exchange
    if (!got.isEmpty() && got.last() == x)
      continue;  // sent twice
    got << x;
  }
  return got.join(QLatin1Char(' '));
}

QString contestDupeKey(const QString& call, const QString& band) {
  return call.trimmed().toUpper() + QLatin1Char('|') + band.trimmed().toLower();
}

QStringList contestMessages(const ContestTemplate& t, bool run) {
  const QSettings store;
  QStringList out;
  const QStringList& base = run ? t.run : t.sp;
  for (int i = 0; i < base.size(); ++i)
    out << store.value(QStringLiteral("contest/msg/%1/%2%3").arg(t.id, run ? QStringLiteral("run") : QStringLiteral("sp")).arg(i),
                       base.at(i)).toString();
  return out;
}

QStringList contestLabels(const ContestTemplate& t, bool run) {
  const QSettings store;
  QStringList out;
  const QStringList& base = run ? t.labels : t.spLabels;
  for (int i = 0; i < base.size(); ++i)
    out << store.value(QStringLiteral("contest/msg/%1/%2label%3").arg(t.id, run ? QStringLiteral("run") : QStringLiteral("sp")).arg(i),
                       base.at(i)).toString();
  return out;
}

int contestEnterMessage(bool run, bool callEmpty, bool exchEmpty) {
  if (run) {
    if (callEmpty)
      return 0;                // F1 CQ
    return exchEmpty ? 1 : 2;  // F2 his call and exchange, then F3 TU and log
  }
  if (callEmpty)
    return -1;                 // S&P with no call: nothing to send
  return exchEmpty ? 3 : 1;    // F4 my call, then F2 TU, exchange, and log
}

QSet<QString> contestWorked(const QString& adifPath, const QString& contestId, const QDateTime& since) {
  QSet<QString> out;
  QFile file(adifPath);
  if (contestId.isEmpty() || !file.open(QIODevice::ReadOnly))
    return out;
  const QString text = QString::fromUtf8(file.readAll());
  static const QRegularExpression field(QStringLiteral("<([A-Za-z_]+):([0-9]+)(?::[^>]*)?>"));
  static const QRegularExpression eor(QStringLiteral("<eor>"), QRegularExpression::CaseInsensitiveOption);
  int head = text.indexOf(QStringLiteral("<eoh>"), 0, Qt::CaseInsensitive);
  head = head < 0 ? 0 : head + 5;
  const QStringList records = text.mid(head).split(eor);
  for (const QString& rec : records) {
    QHash<QString, QString> f;
    auto it = field.globalMatch(rec);
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      f.insert(m.captured(1).toUpper(), rec.mid(m.capturedEnd(), m.captured(2).toInt()));
    }
    if (f.value(QStringLiteral("CONTEST_ID")).compare(contestId, Qt::CaseInsensitive) != 0)
      continue;
    const QString date = f.value(QStringLiteral("QSO_DATE"));
    QString time = f.value(QStringLiteral("TIME_ON"));
    if (time.size() == 4)
      time += QStringLiteral("00");
    const QDateTime on = QDateTime::fromString(date + time, QStringLiteral("yyyyMMddHHmmss"));
    QDateTime onUtc = on;
    onUtc.setTimeZone(QTimeZone::utc());
    if (since.isValid() && onUtc.isValid() && onUtc < since)
      continue;
    out.insert(contestDupeKey(f.value(QStringLiteral("CALL")), f.value(QStringLiteral("BAND"))));
  }
  return out;
}
