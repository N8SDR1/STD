// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTimer;
class QWebSocket;

// FreeDV Reporter (qso.freedv.org), the live map the FreeDV app reports to.
// It speaks socket.io v4 over a websocket, protocol version 2, the same
// messages as freedv-gui's FreeDVReporter: your call, locator, dial
// frequency and TX state, and each station heard with its SNR.
// The receiver callsign and locator are Options -> User. Nothing is sent
// while STD_SELFTEST is set or while Reporter is off. This does not key
// and does not send START.

// The socket.io connect packet ("40" + auth object).
QByteArray freedvReporterConnectPacket(const QString& call, const QString& grid,
                                       const QString& version, bool rxOnly);

// A socket.io event ("42" + ["name", payload]). An empty payload is sent as
// the event name alone.
QByteArray freedvReporterEvent(const QString& name, const QByteArray& jsonPayload = QByteArray());

// "RADEV1" or "RADEV2", the mode names FreeDV Reporter shows.
QString freedvReporterModeName(bool v2);

class FreeDvReporter : public QObject {
  Q_OBJECT

public:
  explicit FreeDvReporter(QObject* parent = nullptr);
  ~FreeDvReporter() override;

  // Connects while on is true and call and grid are set; disconnects
  // otherwise. Calling it again with the same values does nothing.
  void setActive(bool on, const QString& call, const QString& grid);
  bool active() const { return wantOn_; }
  bool connected() const { return ready_; }

  void setFrequency(qint64 hz);
  void setTransmit(const QString& mode, bool tx);
  // An empty call reports a RADE signal in sync with no callsign yet.
  void reportReceive(const QString& call, const QString& mode, int snrDb);

signals:
  void status(const QString& text);

private:
  void open();
  void drop(const QString& why);
  void onText(const QString& text);
  void sendEvent(const QString& name, const QByteArray& payload = QByteArray());
  void sendState();

  QWebSocket* socket_ = nullptr;
  QTimer* retry_ = nullptr;
  QTimer* watchdog_ = nullptr;
  bool wantOn_ = false;
  bool ready_ = false;
  QString call_;
  QString grid_;
  qint64 freqHz_ = 0;
  QString mode_ = QStringLiteral("RADEV1");
  bool tx_ = false;
  int pingMs_ = 25000 + 20000;
  int retryMs_ = 5000;
};
