// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVector>

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

// qsy_request payload: dest_sid, frequency, message.
QByteArray freedvReporterQsyPayload(const QString& sid, qint64 hz, const QString& message);

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
  // The free-text line in the Message column, such as "Rick, Ohio, IC-7300".
  void setMessage(const QString& text);
  // An empty call reports a RADE signal in sync with no callsign yet.
  void reportReceive(const QString& call, const QString& mode, int snrDb);
  // Ask one connected station to move to hz. Nothing is sent until the
  // reporter is up. This does not key and does not send START.
  void requestQsy(const QString& sid, qint64 hz, const QString& message);

  // One row on the in-app station list. sid is the server's session id.
  struct Station {
    QString sid;
    QString station;
    QString grid;
    QString version;
    QString mode;
    QString message;
    QString heard;
    QString lastTx;
    qint64 freqHz = 0;
    int snr = 0;
    bool snrKnown = false;
    bool transmitting = false;
    bool rxOnly = false;
  };
  QVector<Station> stations() const { return stations_; }
  // Applies one socket.io event packet ("42[...]") with no socket. The
  // self-test uses this. A live connection uses the same parser.
  void ingestPacket(const QByteArray& packet);

signals:
  void status(const QString& text);
  void stationsChanged();
  // Someone asked this station to move. hz is the dial they named.
  void qsyAsked(const QString& station, qint64 hz, const QString& note);

private:
  void open();
  void drop(const QString& why);
  void onText(const QString& text);
  void sendEvent(const QString& name, const QByteArray& payload = QByteArray());
  void sendState();
  void applyEvent(const QString& name, const QJsonValue& arg, bool* dirty);
  Station* stationFor(const QString& sid);

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
  QString message_;
  QVector<Station> stations_;
  int pingMs_ = 25000 + 20000;
  int retryMs_ = 5000;
};
