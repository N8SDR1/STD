// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// One watched callsign. heard is when STD last decoded it (UTC, empty if
// not since STD started); where is "20 m FT8, -12 dB".
struct HotEntry {
  QString call;
  bool shown = true;  // ticked: shown on the top bar when Show is Ticked
  QDateTime heard;
  QString where;
  // Where it was heard, for a double-click on its chip: the band in
  // metres, STD's mode, and the radio dial (0 when unknown).
  int heardMetres = 0;
  QString heardMode;
  qint64 heardDialHz = 0;
  // The last report came from a cluster spot (Spothole), not from STD's own
  // decode; where then reads "spotted ...".
  bool spotted = false;
  // Heard in the last 15 minutes (red on the top bar).
  bool recent(const QDateTime& nowUtc = QDateTime::currentDateTimeUtc()) const;
};

// The hot list: callsigns to watch for, the way SDRLogger+ keeps one.
// Calls come from the DXpeditions panel (tick the call) or are typed in
// View → Hot list. When a decode, a WSPR spot, JS8, or received keyboard
// text names a watched call, the top bar marks it, and with Visual + Voice
// a Windows voice says "Hot list, <call>" once per call every 15 minutes.
// Alerts are Off until chosen. Nothing here keys or sends START.
class HotList : public QObject {
  Q_OBJECT

public:
  enum Alerts { Off = 0, Visual = 1, Voice = 2 };
  enum Show { ShowAll = 0, ShowTicked = 1, ShowHeard = 2 };

  // persist: read and write QSettings (hotlist/...). The self-test makes
  // one that keeps nothing.
  explicit HotList(bool persist = true, QObject* parent = nullptr);

  const QList<HotEntry>& entries() const { return entries_; }
  bool contains(const QString& call) const;
  // The entry for a call, or an empty one (call empty) when it is not listed.
  HotEntry entry(const QString& call) const;
  // Upper case, trimmed; commas and spaces separate several calls.
  void add(const QString& calls);
  void remove(const QString& call);
  // On the list after the call: true.
  bool toggle(const QString& call);
  void clear();
  void clearHeard();
  void setShown(const QString& call, bool shown);

  Alerts alerts() const { return alerts_; }
  void setAlerts(Alerts a);
  // Off → Visual → Visual + Voice → Off, as SDRLogger+'s pill cycles.
  void cycleAlerts();
  static QString alertsName(Alerts a);
  bool onBar() const { return onBar_; }
  void setOnBar(bool on);
  Show show() const { return show_; }
  void setShow(Show s);
  // The entries the top bar shows, by Show, heard ones first.
  QList<HotEntry> displayed() const;

  // A whole decode line or spot. Returns the watched calls it names.
  // Alerts Off: nothing is marked and nothing is returned.
  QStringList noteHeard(const QString& message, const QString& where,
                        const QDateTime& nowUtc = QDateTime::currentDateTimeUtc(), int metres = 0,
                        const QString& mode = QString(), qint64 dialHz = 0);
  // A cluster spot of a listed call (the DXpeditions panel's Spothole
  // watch). Marks it as heard, with spotted set. Alerts Off: nothing.
  bool noteSpot(const QString& call, const QString& where,
                const QDateTime& nowUtc = QDateTime::currentDateTimeUtc(), int metres = 0,
                const QString& mode = QString(), qint64 dialHz = 0);
  // Received keyboard text as it prints; a call counts once its word ends.
  QStringList noteStream(const QString& chunk, const QString& where,
                         const QDateTime& nowUtc = QDateTime::currentDateTimeUtc(), int metres = 0,
                         const QString& mode = QString(), qint64 dialHz = 0);
  // The watched calls a line names: whole words, <...> FT8 brackets
  // removed, and either side of a slash ("FO/WE9G" matches FO and WE9G).
  QStringList matches(const QString& text) const;

  static constexpr int kVoiceCooldownSecs = 15 * 60;

signals:
  void changed();
  // A watched call was heard. speak: Visual + Voice and the call was not
  // spoken in the last 15 minutes.
  void heard(const QString& call, const QString& where, bool speak);

private:
  void save() const;
  bool speakNow(const QString& call, const QDateTime& nowUtc);
  int indexOf(const QString& call) const;

  bool persist_ = true;
  QList<HotEntry> entries_;
  Alerts alerts_ = Off;
  bool onBar_ = true;
  Show show_ = ShowAll;
  QHash<QString, QDateTime> spoken_;
  QString stream_;
};
