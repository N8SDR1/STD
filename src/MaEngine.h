// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>

#include <functional>

// MSHV Multi Answering Auto Seq Protocol, the Now and Queue engine behind
// both of its modes. Rewritten from MSHV src/HvTxW/hvmultianswermodw.cpp
// (LZ2HV, GPL-3), Standard activity type only, which is the only one MSHV
// allows these modes in.
//
//   Standard    (MSHV Ctrl+Q) callers who answer the CQ wait in the Queue
//               and are worked from it. One TX slot, two off the standard
//               HF sub-bands. CNS keeps calling CQ when Now empties.
//   DXpedition  (MSHV Ctrl+`) several QSOs per transmission, one message
//               per TX slot, 60 Hz apart. Up to 6 slots in FT8, 4 in FT4,
//               3 in FT2, one on the standard sub-bands.
//
// Each Now row holds what that station last sent (id):
//   0 we called them first      -> HIS MY GRID4
//   1 they sent call or grid    -> HIS MY report
//   2 they sent a report        -> HIS MY R+report
//   3 they sent R+report or RRR -> HIS MY RR73
//   4 they sent RR73 or 73      -> HIS MY 73 (Standard), RR73 (DXpedition)
// message() joins one line per Now row with '#', the MSHV generator form.
class MaEngine {
public:
  enum class Mode { Off, Standard, Dxpedition };

  struct Row {
    QString call;
    QString txRpt;   // report we send them
    QString rxRpt;   // report they sent us
    QString dist;
    QString grid;
    int freq = 0;
    QString started; // yyyyMMdd hh:mm UTC, first entered Now
    int id = 1;
    qint64 tryUtc = 0;  // last time they were heard or entered Now
    QString genTx;      // "" not yet, "1" built during TX, "0" built during RX
    int db = 0;         // their signal, for sorting
  };

  struct Logged {
    QString call;
    QString grid;
    QString txRpt;
    QString rxRpt;
    QString started;
  };

  void setMode(Mode mode);
  Mode mode() const { return mode_; }
  bool active() const { return mode_ != Mode::Off; }

  void setStation(const QString& call, const QString& grid);
  // Slots the operator chose. Clamped by maxSlots().
  void setSlotCount(int count);
  int slotCount() const;
  // MSHV ConfigRestrictW. restricted is the dial on a standard FT8 / FT4 /
  // WSPR frequency, where MSHV never uses more than one slot.
  int maxSlots() const;
  void setDigitalMode(const QString& mode);
  void setRestricted(bool restricted);
  void setQueueLimit(int limit);
  // MSHV Max Time / Periods. periods false is minutes.
  void setMaxTry(int value, bool periods, double periodSec);
  void setCqOnFreeSlot(bool on);
  // DXpedition "TX RR73; next" (MSHV cb_tx_sm).
  void setTxSm(bool on);
  void setCns(bool on);
  void setCqLine(const QString& line);
  // 0 off, 1 distance, 2 S/N. Applied when a caller joins the Queue.
  void setSort(int mode);
  // Returns true when this call is a dupe and must not be added.
  void setDupeCheck(std::function<bool(const QString&)> dupe);
  bool dupeCheckSet() const { return bool(dupe_); }

  // A decode directed at us (auto sequence) or a double click.
  // db is their S/N here, which becomes the report we send.
  void decoded(const QString& message, int db, int freq, bool doubleClick);
  // MSHV SetTxRxMsg(true) at TX start, SetTxRxMsg(false) at TX end.
  void txStarted();
  // Returns every QSO finished since the last call, including ones a 73
  // closed during TX (MSHV AddToLog).
  QVector<Logged> txEnded();
  QVector<Logged> takeLogged();
  // Current transmission. Empty only while there is nothing to send.
  QString message() const { return current_; }
  // MSHV EmitStopAuto: Standard ran out of stations with CNS off.
  bool takeStop();

  const QVector<Row>& now() const { return now_; }
  const QVector<Row>& queue() const { return queue_; }
  bool addManual(const QString& call);
  void removeNow(int row);
  void removeQueue(int row);
  void clearNow();
  void clearQueue();

  static bool isStandardCall(const QString& call);
  static QString baseCall(const QString& call);
  static QString formatReport(const QString& report);

private:
  QString decodeMacros(int row);
  void genMsg();
  void refreshLists(int extra);
  void txMsgEnd();
  void sortQueue();
  void standardCalls(const QString& mine, const QString& his, bool* myStd, bool* hisStd, int* noQso) const;
  QString makeSMsg(const QString& hc0, const QString& hc1, const QString& rpt) const;
  int findRow(const QVector<Row>& rows, const QString& call) const;
  QString distanceTo(const QString& grid) const;

  Mode mode_ = Mode::Off;
  QString digital_ = QStringLiteral("FT8");
  QString myCall_;
  QString myGrid_;
  QString myBase_;
  int slots_ = 1;
  bool restricted_ = false;
  int queueLimit_ = 5;
  int maxTry_ = 2;
  bool maxPeriods_ = true;
  double periodSec_ = 15.0;
  bool cqOnFree_ = false;
  bool txSm_ = false;
  bool cns_ = false;
  QString cqLine_;
  int sort_ = 0;
  std::function<bool(const QString&)> dupe_;

  QVector<Row> now_;
  QVector<Row> queue_;
  QString current_;
  QString genInTx_ = QStringLiteral("0");
  bool txRx_ = false;
  bool stop_ = false;
  bool hadStations_ = false;
  bool blockStop_ = false;
  // MSHV s_count_nw_i3b / s_pos_nw_i3b / t_list_i3b: calls folded into a
  // "RR73; next" message this transmission.
  int countNw_ = 0;
  int posNw_ = 0;
  QVector<QString> folded_;
  QVector<Logged> pendingLog_;
};
