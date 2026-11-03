// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// Contest mode for the keyboard modes (RTTY, PSK, Hell, and the rest).
// A contest picks the exchange, the eight Run and eight S&P messages for
// F1 to F8, and how a received exchange is read. Messages are macros:
// <TX> keys, <LOG> logs once sent, <XOUT> (or <EXCH>) is your exchange,
// <XIN> theirs, <CALL> their call.
struct ContestTemplate {
  QString id;
  QString name;
  QString adifId;      // CONTEST_ID in the log
  QString cq;          // the CQ word: TEST, RU, FD
  bool rst = true;     // a 599 goes before the exchange
  QString myExchHint;  // what you put in Options → Contest Mode
  QString theirHint;   // what the Exch box holds
  int exchWords = 1;   // words in their exchange, after the 599
  QStringList run;     // F1..F8 while running
  QStringList sp;      // F1..F8 while searching and pouncing
  QStringList labels;  // button names, F1..F8 while running
  QStringList spLabels;  // button names while searching and pouncing
};

const QVector<ContestTemplate>& contestTemplates();
const ContestTemplate& contestTemplate(const QString& id);

// Your exchange as sent: "599 OH", "599 007", "2A OH". A # in myExch is
// the serial, three digits.
QString contestExchange(const ContestTemplate& t, const QString& myExch, int serial);

// Their exchange, read from received text: the words after their 599
// (or the class and section on Field Day). Empty when not found.
QString contestExchangeHeard(const ContestTemplate& t, const QString& text, const QString& theirCall,
                             const QString& myCall);

// A dupe is the same call on the same band in this contest.
QString contestDupeKey(const QString& call, const QString& band);

// F1..F8 text and names, with the operator's edits from Options → Contest Mode.
QStringList contestMessages(const ContestTemplate& t, bool run);
QStringList contestLabels(const ContestTemplate& t, bool run);

// Enter Sends Message: which F key Enter sends (0 = F1), or -1 for none.
// Run: F1 CQ with no call, F2 his call and exchange, F3 TU and log once
// their exchange is in. S&P: F4 my call, then F2 TU, exchange, and log.
int contestEnterMessage(bool run, bool callEmpty, bool exchEmpty);

// Call|band keys already logged in this contest since the session began.
QSet<QString> contestWorked(const QString& adifPath, const QString& contestId, const QDateTime& since);
