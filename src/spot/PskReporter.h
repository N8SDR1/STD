// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;
class QUdpSocket;

// PSK Reporter datagram, rewritten from the JTDX / WSJT-X reporter.
// Edson Pereira PY2SDR, psk_reporter.cpp (GPL-3) in JTDX-improved, which
// carries the WSJT-X reporter. STD builds the same UDP payload. That
// file is not compiled in. This does not key and does not send START.

struct PskSpot {
  QString call;
  QString grid;
  qint64 freqHz = 0;
  QString mode;
  int snr = 0;
  quint32 unixTime = 0;
};

// MSHV's UDP limit: 1000-byte IP packet less the IPv6 and UDP headers.
constexpr int kPskMaxDatagram = 952;

// Empty when the receiver call or the spot list is empty, or when the
// spots do not fit in kPskMaxDatagram bytes.
QByteArray pskReporterDatagram(const QString& rxCall, const QString& rxGrid,
                               const QString& program, const QString& antenna,
                               const QVector<PskSpot>& spots, quint32 unixTime,
                               quint32 sequence, quint32 randomId);

// Every spot, split across as many datagrams as needed. sequence is the
// last sequence number used; it is advanced once per datagram.
QVector<QByteArray> pskReporterDatagrams(const QString& rxCall, const QString& rxGrid,
                                         const QString& program, const QString& antenna,
                                         const QVector<PskSpot>& spots, quint32 unixTime,
                                         quint32* sequence, quint32 randomId);

// Map for the callsign already saved under Options → User.
// No callsign: the map with no station filter.
QString pskReporterMapUrl(const QString& call);

// Sender call and optional locator from a decode line.
// "CQ N8SDR EM79", "W1AW N8SDR FN31", "N8SDR EM79 37".
bool pskSpotFromMessage(const QString& message, QString* call, QString* grid);

// JS8 text as JS8Call prints it. The sender is the call before the colon:
// "KN4CRD: N8SDR SNR?" is KN4CRD. A heartbeat or CQ ends with the grid.
// A continuation frame with no "CALL:" names no sender and is not a spot.
bool pskSpotFromJs8(const QString& text, QString* call, QString* grid);

// JT65A and Q65A are reported as JT65 and Q65.
QString pskReporterModeName(const QString& mode);

// Queues a spot and sends one UDP datagram a few seconds later.
// Does nothing while STD_SELFTEST is set, or while upload is off.
// status reports a send or a failure. It does not key and does not send START.
class PskUploader : public QObject {
  Q_OBJECT

public:
  explicit PskUploader(QObject* parent = nullptr);
  void note(const PskSpot& spot, const QString& rxCall, const QString& rxGrid);

signals:
  void status(const QString& text);

private:
  void flush();
  void sendTo(const QHostAddress& address);

  QUdpSocket* socket_ = nullptr;
  QTimer* timer_ = nullptr;
  QVector<PskSpot> queue_;
  QVector<PskSpot> batch_;
  QString rxCall_;
  QString rxGrid_;
  QString pendingHost_;
  QString lookedUpName_;
  QHostAddress resolved_;
  quint16 port_ = 4739;
  quint32 sequence_ = 0;
  quint32 randomId_ = 0;
  QHash<QString, qint64> seen_;
};
