// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HotList.h"

#include <algorithm>

#include <QRegularExpression>
#include <QSettings>

namespace {

QString norm(const QString& call) {
  return call.trimmed().toUpper();
}

}  // namespace

bool HotEntry::recent(const QDateTime& nowUtc) const {
  return heard.isValid() && heard.secsTo(nowUtc) < HotList::kVoiceCooldownSecs;
}

HotList::HotList(bool persist, QObject* parent) : QObject(parent), persist_(persist) {
  setObjectName(QStringLiteral("hotList"));
  if (!persist_)
    return;
  const QSettings s;
  const QStringList hidden = s.value(QStringLiteral("hotlist/hidden")).toStringList();
  for (const QString& raw : s.value(QStringLiteral("hotlist/calls")).toStringList()) {
    const QString call = norm(raw);
    if (call.isEmpty() || indexOf(call) >= 0)
      continue;
    entries_ << HotEntry{call, !hidden.contains(call), {}, {}};
  }
  alerts_ = Alerts(std::clamp(s.value(QStringLiteral("hotlist/alerts"), int(Off)).toInt(), 0, 2));
  onBar_ = s.value(QStringLiteral("hotlist/onBar"), true).toBool();
  show_ = Show(std::clamp(s.value(QStringLiteral("hotlist/show"), int(ShowAll)).toInt(), 0, 2));
}

void HotList::save() const {
  if (!persist_)
    return;
  QSettings s;
  QStringList calls, hidden;
  for (const HotEntry& e : entries_) {
    calls << e.call;
    if (!e.shown)
      hidden << e.call;
  }
  s.setValue(QStringLiteral("hotlist/calls"), calls);
  s.setValue(QStringLiteral("hotlist/hidden"), hidden);
  s.setValue(QStringLiteral("hotlist/alerts"), int(alerts_));
  s.setValue(QStringLiteral("hotlist/onBar"), onBar_);
  s.setValue(QStringLiteral("hotlist/show"), int(show_));
}

int HotList::indexOf(const QString& call) const {
  const QString c = norm(call);
  for (int i = 0; i < entries_.size(); ++i)
    if (entries_.at(i).call == c)
      return i;
  return -1;
}

bool HotList::contains(const QString& call) const {
  return indexOf(call) >= 0;
}

HotEntry HotList::entry(const QString& call) const {
  const int i = indexOf(call);
  return i >= 0 ? entries_.at(i) : HotEntry{};
}

void HotList::add(const QString& calls) {
  bool any = false;
  static const QRegularExpression split(QStringLiteral("[\\s,;]+"));
  for (const QString& raw : calls.split(split, Qt::SkipEmptyParts)) {
    const QString call = norm(raw);
    if (call.isEmpty() || indexOf(call) >= 0)
      continue;
    entries_ << HotEntry{call, true, {}, {}};
    any = true;
  }
  if (any) {
    save();
    emit changed();
  }
}

void HotList::remove(const QString& call) {
  const int i = indexOf(call);
  if (i < 0)
    return;
  entries_.removeAt(i);
  spoken_.remove(norm(call));
  save();
  emit changed();
}

bool HotList::toggle(const QString& call) {
  if (contains(call)) {
    remove(call);
    return false;
  }
  add(call);
  return contains(call);
}

void HotList::clear() {
  if (entries_.isEmpty())
    return;
  entries_.clear();
  spoken_.clear();
  save();
  emit changed();
}

void HotList::clearHeard() {
  for (HotEntry& e : entries_) {
    e.heard = QDateTime();
    e.where.clear();
    e.heardMetres = 0;
    e.heardMode.clear();
    e.heardDialHz = 0;
  }
  emit changed();
}

void HotList::setShown(const QString& call, bool shown) {
  const int i = indexOf(call);
  if (i < 0 || entries_.at(i).shown == shown)
    return;
  entries_[i].shown = shown;
  save();
  emit changed();
}

void HotList::setAlerts(Alerts a) {
  if (a == alerts_)
    return;
  alerts_ = a;
  if (a == Off)
    stream_.clear();
  save();
  emit changed();
}

void HotList::cycleAlerts() {
  setAlerts(Alerts((int(alerts_) + 1) % 3));
}

QString HotList::alertsName(Alerts a) {
  switch (a) {
    case Visual:
      return QStringLiteral("Visual");
    case Voice:
      return QStringLiteral("Visual + Voice");
    default:
      return QStringLiteral("Off");
  }
}

void HotList::setOnBar(bool on) {
  if (on == onBar_)
    return;
  onBar_ = on;
  save();
  emit changed();
}

void HotList::setShow(Show s) {
  if (s == show_)
    return;
  show_ = s;
  save();
  emit changed();
}

QList<HotEntry> HotList::displayed() const {
  QList<HotEntry> out;
  for (const HotEntry& e : entries_) {
    if (show_ == ShowTicked && !e.shown)
      continue;
    if (show_ == ShowHeard && !e.heard.isValid())
      continue;
    out << e;
  }
  // Heard most recently first, then the rest in list order.
  std::stable_sort(out.begin(), out.end(), [](const HotEntry& a, const HotEntry& b) {
    if (a.heard.isValid() != b.heard.isValid())
      return a.heard.isValid();
    return a.heard.isValid() && a.heard > b.heard;
  });
  return out;
}

QStringList HotList::matches(const QString& text) const {
  QStringList found;
  if (entries_.isEmpty())
    return found;
  static const QRegularExpression word(QStringLiteral("[A-Za-z0-9/]+"));
  auto it = word.globalMatch(text);
  while (it.hasNext()) {
    const QString token = it.next().captured(0).toUpper();
    QStringList parts{token};
    if (token.contains(QLatin1Char('/')))
      parts << token.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
      if (part.size() < 2)
        continue;
      if (indexOf(part) >= 0 && !found.contains(part))
        found << part;
    }
  }
  return found;
}

QStringList HotList::noteHeard(const QString& message, const QString& where, const QDateTime& nowUtc,
                               int metres, const QString& mode, qint64 dialHz) {
  if (alerts_ == Off)
    return {};
  const QStringList calls = matches(message);
  for (const QString& call : calls) {
    const int i = indexOf(call);
    entries_[i].heard = nowUtc;
    entries_[i].where = where;
    entries_[i].heardMetres = metres;
    entries_[i].heardMode = mode;
    entries_[i].heardDialHz = dialHz;
    bool speak = false;
    if (alerts_ == Voice) {
      const QDateTime last = spoken_.value(call);
      if (!last.isValid() || last.secsTo(nowUtc) >= kVoiceCooldownSecs) {
        spoken_.insert(call, nowUtc);
        speak = true;
      }
    }
    emit heard(call, where, speak);
  }
  if (!calls.isEmpty())
    emit changed();
  return calls;
}

QStringList HotList::noteStream(const QString& chunk, const QString& where, const QDateTime& nowUtc,
                                int metres, const QString& mode, qint64 dialHz) {
  if (alerts_ == Off || entries_.isEmpty())
    return {};
  stream_ += chunk;
  // Only whole words: keep the part after the last separator for later.
  static const QRegularExpression sep(QStringLiteral("[^A-Za-z0-9/<>]"));
  const int cut = int(stream_.lastIndexOf(sep));
  if (cut < 0) {
    if (stream_.size() > 64)
      stream_ = stream_.right(32);
    return {};
  }
  const QString done = stream_.left(cut + 1);
  stream_ = stream_.mid(cut + 1).right(32);
  return noteHeard(done, where, nowUtc, metres, mode, dialHz);
}
