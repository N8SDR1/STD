// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>

class QTimer;
class QUdpSocket;

// Local ADIF file plus the logger feeds MSHV uses: WSJT-X UDP
// (QSO Logged and Logged ADIF) and a DXKeeper TCP log command.
// Nothing here keys a radio or sends the TCI text command START.
class QsoLog : public QObject {
public:
  struct Record {
    QString call;
    QString grid;
    QString mode;
    QString rstSent;
    QString rstRcvd;
    QString myCall;
    QString myGrid;
    QString comment;
    QString contestId;
    QString stxString;
    QString srxString;
    QString fdClass;
    QString arrlSection;
    qint64 freqHz = 0;
    QDateTime timeOn;
    QDateTime timeOff;
  };

  struct Result {
    bool wrote = false;
    bool udpQso = false;
    bool udpAdif = false;
    bool tcp = false;
    QString path;
    QString detail;
  };

  explicit QsoLog(QObject* parent = nullptr);

  static QString defaultAdifPath();
  static QByteArray heartbeatDatagram(const QString& version);

  void applySettings();
  void shutdown();
  Result store(const Record& qso);

private:
  void sendHeartbeat();
  bool sendUdp(const QByteArray& datagram);
  bool sendTcp(const QByteArray& payload);

  QUdpSocket* udp_ = nullptr;
  QTimer* pulse_ = nullptr;
  QString path_;
  QString udpHost_;
  QString tcpHost_;
  quint16 udpPort_ = 2237;
  quint16 tcpPort_ = 52001;
  bool udpQso_ = false;
  bool udpAdif_ = false;
  bool tcp_ = false;
  bool udpLive_ = false;
};
