// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

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

// The ADIF log, read once and again whenever the file changes.
class QsoMemory {
public:
  void setPath(const QString& path);
  // Nothing found: count is 0.
  QsoHistory lookup(const QString& call);
  // For the self-test: read ADIF text directly.
  void loadText(const QByteArray& adif);

private:
  void refresh();

  QString path_;
  QDateTime stamp_;
  qint64 size_ = -1;
  QHash<QString, QsoHistory> calls_;
};

// The fields of each record in ADIF text, keys upper case.
QList<QHash<QString, QString>> parseAdif(const QByteArray& adif);
