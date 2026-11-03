// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTime>

class QTimer;
class QUdpSocket;

// ADIF band name for a dial frequency, "40m". Empty out of band.
QString qsoBandForHz(qint64 hz);

// Local ADIF file plus the logger feeds MSHV uses: WSJT-X UDP
// (QSO Logged and Logged ADIF) and a DXKeeper TCP log command.
// The UDP link also sends Status and decodes, and hears a logger's Reply
// ("work this station"), Halt Tx, and Free Text, as WSJT-X does.
// Nothing here keys a radio or sends the TCI text command START.
class QsoLog : public QObject {
  Q_OBJECT

public:
  // WSJT-X UDP Status (type 1): what STD is doing, for the logger.
  struct UdpStatus {
    quint64 dialHz = 0;
    QString mode;
    QString dxCall;
    QString report;
    bool txEnabled = false;
    bool transmitting = false;
    bool decoding = false;
    quint32 rxDf = 0;
    quint32 txDf = 0;
    QString deCall;
    QString deGrid;
    QString dxGrid;
    quint32 periodSeconds = 0;
    QString txMessage;
  };

  // A request a logger sent: Reply (4), Halt Tx (8), or Free Text (9).
  struct UdpRequest {
    quint32 type = 0;
    QString id;
    quint32 timeMs = 0;
    qint32 snr = 0;
    double deltaTime = 0.0;
    quint32 deltaFrequency = 0;
    QString mode;
    QString message;
    quint8 modifiers = 0;
    bool autoTxOnly = false;
    QString text;
    bool send = false;
  };

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
    QString stx;  // serial sent, digits only
    QString srx;  // serial received, digits only
    QString fdClass;
    QString arrlSection;
    QString name;
    QString qth;
    QString state;
    QString veProv;
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
  // STD's own log folder: the folder of defaultAdifPath().
  static QString logFolder();
  // A second ADIF (Lookup → Your log) is kept as a copy in
  // logFolder()/imported, refreshed whenever the original is newer. Returns
  // the file to read: the original while it exists, else the copy, so a
  // cleaned-out Downloads folder does not lose those stations.
  static QString keepAdifCopy(const QString& source);
  static QString adifCopyPath(const QString& source);
  // The ADIF line store() writes for this record, ending in <EOR>.
  static QString adifRecord(const Record& qso);
  static QByteArray heartbeatDatagram(const QString& version);

  // WSJT-X UDP type 2 (Decode) and type 10 (WSPRDecode), schema 3.
  // GridTracker reads these. They are built even when UDP is off.
  // Sending happens only while a logger UDP option is on, and never
  // during STD_SELFTEST. This does not key and does not send START.
  static QByteArray decodeDatagram(const QString& mode, int snr, float deltaTime, quint32 audioHz,
                                   const QString& message, const QTime& time);
  static QByteArray wsprDecodeDatagram(int snr, float deltaTime, quint64 freqHz, qint32 drift,
                                       const QString& call, const QString& grid, qint32 powerDbm,
                                       const QTime& time);
  static QByteArray statusDatagram(const UdpStatus& status);
  // Reads a logger's datagram. Returns false for anything that is not a
  // Reply, Halt Tx, or Free Text addressed to STD.
  static bool parseRequest(const QByteArray& datagram, UdpRequest* request);

  // The UDP link is on: Options → Log → Link to a logger, or a UDP log option.
  bool udpOn() const;
  // Sent when it differs from the last one, and again with each heartbeat.
  void sendStatus(const UdpStatus& status);

  void applySettings();
  void shutdown();
  Result store(const Record& qso);
  void sendDecode(const QString& mode, int snr, float deltaTime, quint32 audioHz, const QString& message,
                  const QTime& time);
  void sendWsprDecode(int snr, float deltaTime, quint64 freqHz, qint32 drift, const QString& call,
                      const QString& grid, qint32 powerDbm, const QTime& time);

signals:
  // A logger asked: Reply to a decode, Halt Tx, or set the free text.
  // Only while "Accept requests from a logger" is on.
  void requestReceived(const QsoLog::UdpRequest& request);

private:
  void sendHeartbeat();
  void readRequests();
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
  bool udpLink_ = false;
  bool udpAccept_ = true;
  QByteArray lastStatus_;
};
