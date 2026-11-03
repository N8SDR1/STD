// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// What the other station told us, read from their received text:
// "NAME IS BOB", "OP BOB", "QTH DAYTON OH", "UR 579", "LOC EM79", "DE K8AB".
// Empty when not found. These are only suggestions for the QSO bar.
struct QsoHints {
  QString call;   // "DE K8AB", offered only while Call is empty
  QString name;
  QString qth;
  QString st;     // the US state or province at the end of the QTH
  QString loc;
  QString rstIn;  // the report they gave us
};

// Reads hints from received text. myCall is never offered as their call.
QsoHints scanQsoText(const QString& text, const QString& myCall);

// One station as the log remembers it: the newest value of each field.
struct QsoHistory {
  int count = 0;
  QString lastDate;  // "2026-05-04"
  QString lastBand;
  QString lastMode;
  QString name;
  QString qth;
  QString state;
  QString grid;
};

// The ADIF logs, read once and again whenever a file changes. STD's own
// log, and optionally another such as a logger's export. A QSO in both
// (same call, date, and minute) counts once.
class QsoMemory {
public:
  void setPaths(const QStringList& paths);
  // Nothing found: count is 0.
  QsoHistory lookup(const QString& call);
  // For the self-test: read ADIF text directly, one text per file.
  void loadText(const QList<QByteArray>& logs);

private:
  void refresh();

  QStringList paths_;
  QString seen_;  // sizes and times of the files as last read
  QHash<QString, QsoHistory> calls_;
};

// The fields of each record in ADIF text, keys upper case.
QList<QHash<QString, QString>> parseAdif(const QByteArray& adif);
