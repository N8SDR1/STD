// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MSHV Multi Answering Auto Seq Protocol, Standard and DXpedition.
// Rewritten from MSHV src/HvTxW/hvmultianswermodw.cpp by Hrisimir
// Hristov, LZ2HV (GPL-3): DetectTextInMsg, DecListTextAll, DecodeMacros,
// gen_msg, RefreshLists, SetTxRxMsg, SetTxMsgEnd, ConfigRestrictW. Only the
// Standard activity type (no contest exchange), which is the only one MSHV
// enables these modes for. Super Fox / Super Hound stay outside.

#include "MaEngine.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

qint64 utcNow() {
  return QDateTime::currentSecsSinceEpoch();
}

QString utcStamp() {
  return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd hh:mm"));
}

bool isLocator(const QString& s) {
  if (s.size() != 4 && s.size() != 6)
    return false;
  const QString u = s.toUpper();
  if (u.at(0) < QLatin1Char('A') || u.at(0) > QLatin1Char('R') || u.at(1) < QLatin1Char('A') ||
      u.at(1) > QLatin1Char('R') || !u.at(2).isDigit() || !u.at(3).isDigit())
    return false;
  if (u == QLatin1String("RR73"))
    return false;
  if (s.size() == 6 && (u.at(4) < QLatin1Char('A') || u.at(4) > QLatin1Char('X') ||
                        u.at(5) < QLatin1Char('A') || u.at(5) > QLatin1Char('X')))
    return false;
  return true;
}

// MSHV HvQthLoc::isValidCallsign, in spirit: letters, digits and '/',
// at least one digit and one letter in the base call.
bool isCallsign(const QString& s) {
  if (s.size() < 3 || s.size() > 13)
    return false;
  bool letter = false;
  bool digit = false;
  for (const QChar c : s) {
    if (c.isLetter())
      letter = true;
    else if (c.isDigit())
      digit = true;
    else if (c != QLatin1Char('/'))
      return false;
  }
  if (!letter || !digit)
    return false;
  if (s.startsWith(QLatin1Char('/')) || s.endsWith(QLatin1Char('/')))
    return false;
  return true;
}

bool gridLatLon(const QString& token, double* lat, double* lon) {
  const QString g = token.trimmed().toUpper();
  if (!isLocator(g))
    return false;
  double lonv = (g.at(0).unicode() - u'A') * 20.0 - 180.0 + (g.at(2).unicode() - u'0') * 2.0;
  double latv = (g.at(1).unicode() - u'A') * 10.0 - 90.0 + (g.at(3).unicode() - u'0') * 1.0;
  if (g.size() == 6) {
    lonv += (g.at(4).unicode() - u'A') * (5.0 / 60.0) + 2.5 / 60.0;
    latv += (g.at(5).unicode() - u'A') * (2.5 / 60.0) + 1.25 / 60.0;
  } else {
    lonv += 1.0;
    latv += 0.5;
  }
  *lat = latv;
  *lon = lonv;
  return true;
}

}  // namespace

bool MaEngine::isStandardCall(const QString& call) {
  static const QRegularExpression re(QStringLiteral(
      "^\\s*([A-Z]{0,2}|[A-Z][0-9]|[0-9][A-Z])([0-9][A-Z]{0,3})(/R|/P)?\\s*$"),
                                     QRegularExpression::CaseInsensitiveOption);
  return re.match(call).hasMatch();
}

QString MaEngine::baseCall(const QString& str) {
  if (!str.contains(QLatin1Char('/')))
    return str;
  const QStringList parts = str.split(QLatin1Char('/'));
  if (parts.size() >= 3)
    return parts.at(1);
  const bool c0 = isCallsign(parts.at(0));
  const bool c1 = isCallsign(parts.at(1));
  if (c0 && c1)
    return parts.at(1);
  if (c0)
    return parts.at(0);
  if (c1)
    return parts.at(1);
  return parts.at(0);
}

// MSHV format_rpt_ma, standard activity: sign and two digits, 0 is +00.
QString MaEngine::formatReport(const QString& report) {
  QString s = report.trimmed();
  if (s.isEmpty())
    return s;
  if (s.at(0) != QLatin1Char('-') && s.at(0) != QLatin1Char('+'))
    s.prepend(QLatin1Char('+'));
  const int v = s.toInt();
  QChar sign = s.at(0);
  if (v == 0)
    sign = QLatin1Char('+');
  return QString(sign) + QStringLiteral("%1").arg(std::abs(v), 2, 10, QLatin1Char('0'));
}

void MaEngine::setMode(Mode mode) {
  if (mode_ == mode)
    return;
  mode_ = mode;
  // MSHV SetMAStd -> RefreshLRestrict: clear both lists.
  now_.clear();
  queue_.clear();
  folded_.clear();
  countNw_ = 0;
  posNw_ = 0;
  stop_ = false;
  hadStations_ = false;
  slots_ = std::clamp(slots_, 1, maxSlots());
  blockStop_ = true;
  genMsg();
  blockStop_ = false;
}

void MaEngine::setStation(const QString& call, const QString& grid) {
  const QString c = call.trimmed().toUpper();
  const QString g = grid.trimmed().toUpper();
  if (c == myCall_ && g == myGrid_)
    return;
  myCall_ = c;
  myGrid_ = g;
  myBase_ = baseCall(c);
  blockStop_ = true;
  genMsg();
  blockStop_ = false;
}

int MaEngine::maxSlots() const {
  const bool q65 = digital_.startsWith(QLatin1String("Q65"));
  if (restricted_ || q65)
    return 1;
  if (mode_ == Mode::Standard)
    return 2;
  if (digital_ == QLatin1String("FT4"))
    return 4;
  if (digital_ == QLatin1String("FT2"))
    return 3;
  return 6;
}

int MaEngine::slotCount() const {
  return std::clamp(slots_, 1, maxSlots());
}

void MaEngine::setSlotCount(int count) {
  slots_ = std::max(1, count);
  refreshLists(0);
}

void MaEngine::setDigitalMode(const QString& mode) {
  digital_ = mode;
}

void MaEngine::setRestricted(bool restricted) {
  restricted_ = restricted;
}

void MaEngine::setQueueLimit(int limit) {
  queueLimit_ = std::max(0, limit);
}

void MaEngine::setMaxTry(int value, bool periods, double periodSec) {
  maxTry_ = std::max(1, value);
  maxPeriods_ = periods;
  periodSec_ = periodSec > 0.0 ? periodSec : 15.0;
}

void MaEngine::setCqOnFreeSlot(bool on) {
  if (cqOnFree_ == on)
    return;
  cqOnFree_ = on;
  if (slotCount() > 1)
    genMsg();
}

void MaEngine::setTxSm(bool on) {
  txSm_ = on;
}

void MaEngine::setCns(bool on) {
  cns_ = on;
}

void MaEngine::setCqLine(const QString& line) {
  const QString l = line.trimmed().toUpper();
  if (l == cqLine_)
    return;
  cqLine_ = l;
  blockStop_ = true;
  genMsg();
  blockStop_ = false;
}

void MaEngine::setSort(int mode) {
  sort_ = mode;
  sortQueue();
}

void MaEngine::setDupeCheck(std::function<bool(const QString&)> dupe) {
  dupe_ = std::move(dupe);
}

bool MaEngine::takeStop() {
  const bool s = stop_;
  stop_ = false;
  return s;
}

int MaEngine::findRow(const QVector<Row>& rows, const QString& call) const {
  if (call.isEmpty())
    return -1;
  const QString base = baseCall(call);
  for (int i = 0; i < rows.size(); ++i) {
    if (rows.at(i).call == call || baseCall(rows.at(i).call) == base)
      return i;
  }
  return -1;
}

QString MaEngine::distanceTo(const QString& grid) const {
  double lat1 = 0.0;
  double lon1 = 0.0;
  double lat2 = 0.0;
  double lon2 = 0.0;
  if (!gridLatLon(myGrid_, &lat1, &lon1) || !gridLatLon(grid, &lat2, &lon2))
    return QStringLiteral("?");
  constexpr double kRad = 3.141592653589793 / 180.0;
  const double d = std::acos(std::clamp(std::sin(lat1 * kRad) * std::sin(lat2 * kRad) +
                                            std::cos(lat1 * kRad) * std::cos(lat2 * kRad) *
                                                std::cos((lon2 - lon1) * kRad),
                                        -1.0, 1.0));
  return QString::number(int(std::lround(d * 6371.0)));
}

void MaEngine::sortQueue() {
  if (sort_ == 1) {
    std::stable_sort(queue_.begin(), queue_.end(),
                     [](const Row& a, const Row& b) { return a.dist.toInt() > b.dist.toInt(); });
  } else if (sort_ == 2) {
    std::stable_sort(queue_.begin(), queue_.end(), [](const Row& a, const Row& b) { return a.db > b.db; });
  }
}

void MaEngine::standardCalls(const QString& c1, const QString& c2, bool* fc1, bool* fc2, int* noQso) const {
  *fc1 = isStandardCall(c1);
  *fc2 = isStandardCall(c2);
  *noQso = 0;
  const QString c1end = c1.right(2);
  const QString c2end = c2.right(2);
  auto pr = [](const QString& e) { return e == QLatin1String("/P") || e == QLatin1String("/R"); };
  if (*fc1 && *fc2) {
    if ((c1end == QLatin1String("/P") && c2end == QLatin1String("/R")) ||
        (c1end == QLatin1String("/R") && c2end == QLatin1String("/P"))) {
      *fc1 = false;
      *fc2 = false;
    }
  } else if (*fc1 && !*fc2) {
    if (pr(c1end)) {
      *noQso = isStandardCall(baseCall(c2)) ? 2 : 1;
      *fc1 = false;
    }
  } else if (!*fc1 && *fc2) {
    if (pr(c2end)) {
      *noQso = isStandardCall(baseCall(c1)) ? 4 : 3;
      *fc2 = false;
    }
  } else {
    const bool b1 = isStandardCall(baseCall(c1));
    const bool b2 = isStandardCall(baseCall(c2));
    if (b1 && b2)
      *noQso = 0;
    else if (!b1 && b2)
      *noQso = 5;
    else if (b1 && !b2)
      *noQso = 6;
    else
      *noQso = 7;
  }
}

QString MaEngine::makeSMsg(const QString& hc0, const QString& hc1, const QString& rpt) const {
  const QString rp0 = rpt.isEmpty() ? QStringLiteral("-10") : rpt;
  return baseCall(hc0) + QStringLiteral(" RR73; ") + baseCall(hc1) + QStringLiteral(" <") + myCall_ +
         QStringLiteral("> ") + rp0;
}

// MSHV DecodeMacros(row, id), Standard activity, id_mshf 0.
QString MaEngine::decodeMacros(int row) {
  const int txId = now_.at(row).id;
  static const char* kStd[5] = {"%T %M %G4", "%T %M %R", "%T %M R%R", "%T %M RR73", "%T %M 73"};
  static const char* kDx[5] = {"%T %M %G4", "%T %M %R", "%T %M R%R", "%T %M RR73", "%T %M RR73"};
  QString out = QString::fromLatin1(mode_ == Mode::Standard ? kStd[std::clamp(txId, 0, 4)]
                                                             : kDx[std::clamp(txId, 0, 4)]);
  QString my = myCall_;
  QString his = now_.at(row).call;
  bool myStd = false;
  bool hisStd = false;
  int noQso = 0;
  standardCalls(my, his, &myStd, &hisStd, &noQso);
  const int nslot = slotCount();

  if (txId == 0) {
    if (!hisStd && !myStd && noQso != 100)
      out = QLatin1Char('<') + his + QStringLiteral("> ") + my;
    else if (!hisStd)
      his = QLatin1Char('<') + his + QLatin1Char('>');
    else if (!myStd)
      out = QLatin1Char('<') + his + QStringLiteral("> ") + my;
  } else if (txId == 1 || txId == 2) {
    if (!hisStd && !myStd) {
      if (noQso == 0 || noQso == 1 || noQso == 2 || noQso == 6) {
        his = QLatin1Char('<') + his + QLatin1Char('>');
        my = myBase_;
      }
      if (noQso == 3 || noQso == 4 || noQso == 5) {
        his = baseCall(his);
        my = QLatin1Char('<') + my + QLatin1Char('>');
      }
      if (noQso == 7) {
        his = QLatin1Char('<') + his + QLatin1Char('>');
        my = QLatin1Char('<') + my + QLatin1Char('>');
      }
    } else if (!hisStd && myStd) {
      his = QLatin1Char('<') + his + QLatin1Char('>');
    } else if (hisStd && !myStd) {
      my = QLatin1Char('<') + my + QLatin1Char('>');
    }
  } else {
    // RR73 (id 3, 4). DXpedition with "TX RR73; next" folds the next
    // queued caller into this slot: "A RR73; B <MY> rpt".
    const bool txSm = mode_ == Mode::Dxpedition && txSm_;
    const QString nextCall = queue_.isEmpty() ? QString() : queue_.first().call;
    const int nextId = queue_.isEmpty() ? -1 : queue_.first().id;
    if (txSm && !txRx_ && !nextCall.isEmpty() && hisStd && isStandardCall(nextCall) && now_.size() >= nslot &&
        (nextId == 1 || nextId == 2)) {
      if (!folded_.contains(his)) {
        folded_.push_back(his);
        ++countNw_;
        refreshLists(countNw_);
      }
      const int nw = now_.size() - countNw_ + posNw_;
      if (nw >= 0 && nw < now_.size())
        out = makeSMsg(his, now_.at(nw).call, now_.at(nw).txRpt);
      ++posNw_;
    } else if (posNw_ - countNw_ < 0 && hisStd) {
      const int nw = now_.size() - countNw_ + posNw_;
      if (nw >= 0 && nw < now_.size())
        out = makeSMsg(his, now_.at(nw).call, now_.at(nw).txRpt);
      ++posNw_;
    }
    if (!hisStd && !myStd) {
      if (noQso == 0 || noQso == 1 || noQso == 2 || noQso == 5 || noQso == 6 || noQso == 7)
        my = QLatin1Char('<') + my + QLatin1Char('>');
      else if (noQso == 3 || noQso == 4)
        his = QLatin1Char('<') + his + QLatin1Char('>');
    } else if (!hisStd && myStd) {
      my = QLatin1Char('<') + my + QLatin1Char('>');
    } else if (hisStd && !myStd) {
      his = QLatin1Char('<') + his + QLatin1Char('>');
    }
    // Decoded during TX and built during TX: MSHV removes it one end later.
    if (now_[row].genTx.isEmpty())
      now_[row].genTx = genInTx_;
  }

  out.replace(QStringLiteral("%T"), his);
  out.replace(QStringLiteral("%M"), my);
  out.replace(QStringLiteral("%R"), now_.at(row).txRpt);
  out.replace(QStringLiteral("%G4"), myGrid_.left(4));
  return out;
}

// MSHV gen_msg.
void MaEngine::genMsg() {
  if (!active()) {
    current_.clear();
    return;
  }
  const int rwc = now_.size() - countNw_;
  current_.clear();
  posNw_ = 0;
  const QString cq = cqLine_.isEmpty()
                         ? (QStringLiteral("CQ ") + myCall_ + QLatin1Char(' ') + myGrid_.left(4)).trimmed()
                         : cqLine_;
  if (rwc <= 0) {
    // Standard stops when it runs out of stations unless CNS is on. Only
    // when the last station leaves, not while calling CQ from the start.
    if (!blockStop_ && mode_ == Mode::Standard && !cns_ && hadStations_)
      stop_ = true;
    hadStations_ = false;
    current_ = cq;
    return;
  }
  hadStations_ = true;
  QStringList parts;
  for (int i = 0; i < rwc && i < now_.size(); ++i)
    parts << decodeMacros(i);
  if (cqOnFree_ && rwc < 2 && rwc < slotCount())
    parts << cq;
  current_ = parts.join(QLatin1Char('#'));
}

// MSHV RefreshLists. Queue rows move into Now up to the slot count.
void MaEngine::refreshLists(int extra) {
  if (!(now_.size() >= slotCount() + extra || queue_.isEmpty())) {
    int moved = 0;
    for (int i = 0; i < queue_.size(); ++i) {
      Row r = queue_.at(i);
      if (r.started.isEmpty())
        r.started = utcStamp();
      r.tryUtc = utcNow();
      now_.push_back(r);
      ++moved;
      if (now_.size() >= slotCount())
        break;
    }
    queue_.remove(0, moved);
  }
  if (extra == 0)
    genMsg();
}

bool MaEngine::addManual(const QString& call) {
  const QString c = call.trimmed().toUpper();
  if (!isCallsign(c) || findRow(queue_, c) >= 0 || findRow(now_, c) >= 0)
    return false;
  Row r;
  r.call = c;
  r.id = 0;
  r.txRpt = QStringLiteral("+00");
  queue_.push_back(r);
  sortQueue();
  refreshLists(0);
  return true;
}

void MaEngine::removeNow(int row) {
  if (row < 0 || row >= now_.size())
    return;
  now_.removeAt(row);
  refreshLists(0);
}

void MaEngine::removeQueue(int row) {
  if (row < 0 || row >= queue_.size())
    return;
  queue_.removeAt(row);
}

void MaEngine::clearNow() {
  now_.clear();
  refreshLists(0);
}

void MaEngine::clearQueue() {
  queue_.clear();
}

// MSHV SetTxRxMsg(true): drop Now rows past Max Time / Periods.
void MaEngine::txStarted() {
  txRx_ = true;
  genInTx_ = QStringLiteral("1");
  const qint64 t = utcNow();
  const qint64 limit = maxPeriods_ ? qint64(periodSec_ * 2.0 * double(maxTry_)) : qint64(maxTry_) * 60;
  bool changed = false;
  for (int i = now_.size() - 1; i >= 0; --i) {
    if (t - now_.at(i).tryUtc >= limit) {
      now_.removeAt(i);
      changed = true;
    }
  }
  if (changed)
    refreshLists(0);
}

// MSHV SetTxRxMsg(false) -> SetTxMsgEnd. RR73 / 73 rows built before this
// transmission went out are done: logged and removed.
QVector<MaEngine::Logged> MaEngine::txEnded() {
  txRx_ = false;
  genInTx_ = QStringLiteral("0");
  txMsgEnd();
  return takeLogged();
}

QVector<MaEngine::Logged> MaEngine::takeLogged() {
  QVector<Logged> out;
  out.swap(pendingLog_);
  return out;
}

void MaEngine::txMsgEnd() {
  countNw_ = 0;
  folded_.clear();
  bool changed = false;
  for (int i = now_.size() - 1; i >= 0; --i) {
    Row& r = now_[i];
    const bool done = r.id == 3 || r.id == 4;
    if (done && r.genTx == QLatin1String("0")) {
      pendingLog_.push_back(Logged{r.call, r.grid, r.txRpt, r.rxRpt, r.started});
      now_.removeAt(i);
      changed = true;
    } else if (done && r.genTx == QLatin1String("1")) {
      r.genTx = QStringLiteral("0");
    }
  }
  if (changed)
    refreshLists(0);
}

// MSHV DetectTextInMsg + DecListTextAll, Standard activity type.
void MaEngine::decoded(const QString& message, int db, int freq, bool doubleClick) {
  if (!active() || myCall_.isEmpty())
    return;
  QString str = message.trimmed().toUpper();
  if (str.startsWith(QLatin1String("TU; ")))
    str.remove(0, 4);
  str.remove(QLatin1Char('<'));
  str.remove(QLatin1Char('>'));
  str.remove(QLatin1Char(';'));
  const QStringList t = str.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (t.isEmpty())
    return;

  QString hisCall;
  QString hisLoc;
  QString myCall;
  QString rpt;
  QString contR;
  QString rr73;
  bool someDigits = false;
  bool foxForMe = false;
  for (int i = t.size() - 1; i >= 0; --i) {
    const QString& w = t.at(i);
    if (i > 0 && w == QLatin1String("..."))
      hisCall = w;
    if (rr73.isEmpty() && w == QLatin1String("RR73")) {
      rr73 = w;
    } else if (i != 0 && i != 1 && hisLoc.isEmpty() && isLocator(w)) {
      hisLoc = w;
    } else if (hisCall.isEmpty() && isCallsign(w)) {
      hisCall = w;
      bool allDigits = true;
      for (const QChar c : w) {
        if (c.isLetter())
          allDigits = false;
      }
      if (allDigits) {
        someDigits = true;
        hisCall.clear();
      }
      if (i == 0 && !hisCall.isEmpty() && baseCall(hisCall) == myBase_) {
        hisCall.clear();
        myCall = w;
      }
    } else if (myCall.isEmpty() && isCallsign(w) && baseCall(w) == myBase_) {
      myCall = w;
      if (i > 0)
        foxForMe = true;
    } else if (contR.isEmpty() && w == QLatin1String("R")) {
      contR = w;
    }
  }
  const QString last = t.last();
  if (hisLoc.isEmpty() && rr73.isEmpty() && last != hisCall && last != myCall && !someDigits) {
    bool section = false;
    if (last != QLatin1String("RRR") && last != QLatin1String("73") && last.size() > 1 && last.size() < 4) {
      section = true;
      for (const QChar c : last) {
        if (c.isDigit())
          section = false;
      }
    }
    if (!section && rpt.isEmpty())
      rpt = last;
  }
  if (foxForMe) {
    rpt = last;
    rr73.clear();
  }
  if (hisCall == QLatin1String("..."))
    hisCall.clear();

  if (!(!myCall.isEmpty() || mode_ == Mode::Standard) || hisCall.isEmpty())
    return;
  bool myStd = false;
  bool hisStd = false;
  int noQso = 0;
  standardCalls(myCall_, hisCall, &myStd, &hisStd, &noQso);

  const int rowQueue = findRow(queue_, hisCall);
  const int rowNow = findRow(now_, hisCall);
  int where = 2;  // 0 Queue, 1 Now, 2 New
  if (rowQueue > -1)
    where = 0;
  if (rowNow > -1)
    where = 1;
  if (where == 2 && dupe_ && dupe_(hisCall) && (mode_ != Mode::Standard || !doubleClick))
    return;

  int id = 0;
  bool blockAll = true;
  if (!myCall.isEmpty()) {
    const bool isNumber = !rpt.isEmpty() && rpt.at(rpt.size() - 1).isDigit();
    if (rpt.isEmpty() && hisLoc.isEmpty() && contR.isEmpty() && rr73.isEmpty()) {
      id = 1;
      blockAll = false;
    } else if (rpt.isEmpty() && !hisLoc.isEmpty()) {
      id = contR.isEmpty() ? 1 : 3;
      blockAll = false;
    } else if (!rpt.isEmpty() && rpt.at(0) != QLatin1Char('R') && isNumber && rpt != QLatin1String("73")) {
      id = contR.isEmpty() ? 2 : 3;
      rpt = formatReport(rpt);
      blockAll = false;
    } else if (!rpt.isEmpty() && rpt.at(0) == QLatin1Char('R') && isNumber) {
      id = 3;
      rpt.remove(QLatin1Char('R'));
      rpt = formatReport(rpt);
      blockAll = false;
    } else if (!rpt.isEmpty() && rpt.at(0) == QLatin1Char('R') && rpt.endsWith(QLatin1Char('R'))) {
      id = 3;
      blockAll = true;
    } else if (rpt == QLatin1String("73") || rr73 == QLatin1String("RR73")) {
      id = 4;
      blockAll = true;
    } else {
      return;
    }
  } else if (mode_ == Mode::Standard && doubleClick) {
    id = 0;
    blockAll = false;
    if (!rpt.isEmpty() && rpt.at(0) == QLatin1Char('R') && rpt.at(rpt.size() - 1).isDigit())
      rpt.remove(QLatin1Char('R'));
  } else {
    return;
  }

  if (where == 2 && blockAll)
    return;

  int queueLimit = queueLimit_;
  if (queueLimit == 0) {
    // MSHV 2.59: no Queue. A double click on a new caller with one slot
    // replaces the station in Now.
    if (doubleClick && where == 2 && queue_.isEmpty() && slotCount() == 1 && now_.size() == 1)
      now_.clear();
    if (now_.size() < slotCount())
      queueLimit = 1;
  }
  if (where == 2 && queue_.size() >= queueLimit)
    return;

  const QString dist = hisLoc.isEmpty() ? QStringLiteral("?") : distanceTo(hisLoc);
  if (rpt == QLatin1String("73") || rpt == QLatin1String("RRR") || rpt == QLatin1String("RR73"))
    rpt.clear();
  QString txRpt;
  if (id == 0 || id == 1 || id == 2)
    txRpt = formatReport(QString::number(db));

  if (where == 1) {
    Row& r = now_[rowNow];
    const int prev = r.id;
    if (!txRpt.isEmpty() && (prev == 1 || prev == 2 || prev == 3 || prev == 4))
      txRpt.clear();
    if (hisCall.contains(QLatin1Char('/')))
      r.call = hisCall;
    if (!txRpt.isEmpty())
      r.txRpt = txRpt;
    if (!rpt.isEmpty())
      r.rxRpt = rpt;
    if (dist != QLatin1String("?"))
      r.dist = dist;
    if (!hisLoc.isEmpty())
      r.grid = hisLoc;
    r.freq = freq;
    r.id = id;
    r.db = db;
    r.tryUtc = utcNow();
    if (txRx_ && slotCount() == 1 && (id == 4 || id == 3)) {
      r.genTx = QStringLiteral("0");
      if (id == 4)
        txMsgEnd();
      else
        genMsg();
    } else if ((prev == 2 || prev == 3) && id == 4) {
      r.genTx = QStringLiteral("0");
      txMsgEnd();
    } else {
      genMsg();
    }
  } else if (where == 0) {
    Row& r = queue_[rowQueue];
    if (hisCall.contains(QLatin1Char('/')))
      r.call = hisCall;
    if (!txRpt.isEmpty())
      r.txRpt = txRpt;
    if (!rpt.isEmpty())
      r.rxRpt = rpt;
    if (dist != QLatin1String("?"))
      r.dist = dist;
    if (!hisLoc.isEmpty())
      r.grid = hisLoc;
    r.freq = freq;
    r.id = id;
    r.db = db;
  } else {
    Row r;
    r.call = hisCall;
    r.txRpt = txRpt;
    r.rxRpt = rpt.isEmpty() ? QString() : formatReport(rpt);
    r.dist = dist;
    r.grid = hisLoc;
    r.freq = freq;
    r.id = id;
    r.db = db;
    queue_.push_back(r);
    sortQueue();
    refreshLists(0);
  }
}
