// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "spot/FreeDvReporter.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <algorithm>

namespace {

constexpr int kProtocolVersion = 2;
constexpr int kMaxRetryMs = 120000;

QString reporterHost() {
  const QString host =
      QSettings().value(QStringLiteral("dx/freedvReporterHost"), QStringLiteral("qso.freedv.org"))
          .toString()
          .trimmed();
  return host.isEmpty() ? QStringLiteral("qso.freedv.org") : host;
}

}  // namespace

QByteArray freedvReporterConnectPacket(const QString& call, const QString& grid,
                                       const QString& version, bool rxOnly) {
  QJsonObject auth;
  const QString c = call.trimmed().toUpper();
  const QString g = grid.trimmed();
  if (c.isEmpty() || g.isEmpty()) {
    auth.insert(QStringLiteral("role"), QStringLiteral("view"));
  } else {
    auth.insert(QStringLiteral("role"), QStringLiteral("report"));
    auth.insert(QStringLiteral("callsign"), c);
    auth.insert(QStringLiteral("grid_square"), g);
    auth.insert(QStringLiteral("version"), version);
    auth.insert(QStringLiteral("rx_only"), rxOnly);
    auth.insert(QStringLiteral("os"), QStringLiteral("Windows"));
  }
  auth.insert(QStringLiteral("protocol_version"), kProtocolVersion);
  return QByteArrayLiteral("40") + QJsonDocument(auth).toJson(QJsonDocument::Compact);
}

QByteArray freedvReporterEvent(const QString& name, const QByteArray& jsonPayload) {
  QByteArray out = QByteArrayLiteral("42[");
  out += QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact).mid(1);
  out.chop(1);  // the closing bracket of ["name"]
  if (!jsonPayload.isEmpty()) {
    out += ',';
    out += jsonPayload;
  }
  out += ']';
  return out;
}

QString freedvReporterModeName(bool v2) {
  return v2 ? QStringLiteral("RADEV2") : QStringLiteral("RADEV1");
}

FreeDvReporter::FreeDvReporter(QObject* parent)
    : QObject(parent), retry_(new QTimer(this)), watchdog_(new QTimer(this)) {
  retry_->setSingleShot(true);
  connect(retry_, &QTimer::timeout, this, [this] {
    if (wantOn_)
      open();
  });
  watchdog_->setSingleShot(true);
  connect(watchdog_, &QTimer::timeout, this, [this] {
    drop(QStringLiteral("FreeDV Reporter stopped answering"));
  });
}

FreeDvReporter::~FreeDvReporter() {
  wantOn_ = false;
  if (socket_) {
    socket_->disconnect(this);
    socket_->abort();
  }
}

void FreeDvReporter::setActive(bool on, const QString& call, const QString& grid) {
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    on = false;
  const QString c = call.trimmed().toUpper();
  const QString g = grid.trimmed();
  if (c.isEmpty() || g.isEmpty())
    on = false;
  if (on == wantOn_ && c == call_ && g == grid_)
    return;
  const bool identityChanged = c != call_ || g != grid_;
  call_ = c;
  grid_ = g;
  wantOn_ = on;
  if (!on) {
    retry_->stop();
    if (socket_) {
      if (ready_)
        sendEvent(QStringLiteral("hide_self"));
      socket_->close();
    }
    ready_ = false;
    emit status(QStringLiteral("FreeDV Reporter is off. This does not key. START was not sent."));
    return;
  }
  if (identityChanged && socket_)
    drop(QString());
  retryMs_ = 5000;
  open();
}

void FreeDvReporter::open() {
  if (!wantOn_)
    return;
  if (!socket_) {
    socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(socket_, &QWebSocket::connected, this, [this] {
      watchdog_->start(pingMs_);
    });
    connect(socket_, &QWebSocket::textMessageReceived, this, &FreeDvReporter::onText);
    connect(socket_, &QWebSocket::disconnected, this, [this] {
      const bool was = ready_;
      ready_ = false;
      watchdog_->stop();
      if (!wantOn_)
        return;
      if (was)
        emit status(QStringLiteral("FreeDV Reporter disconnected; trying again. START was not sent."));
      retry_->start(retryMs_);
      retryMs_ = std::min(retryMs_ * 2, kMaxRetryMs);
    });
  }
  if (socket_->state() != QAbstractSocket::UnconnectedState)
    return;
  const QUrl url(QStringLiteral("wss://%1/socket.io/?EIO=4&transport=websocket").arg(reporterHost()));
  socket_->open(url);
}

void FreeDvReporter::drop(const QString& why) {
  ready_ = false;
  watchdog_->stop();
  if (!why.isEmpty())
    emit status(why + QStringLiteral(". This does not key. START was not sent."));
  if (socket_)
    socket_->abort();
}

void FreeDvReporter::onText(const QString& text) {
  if (text.isEmpty())
    return;
  const QChar kind = text.at(0);
  if (kind == QLatin1Char('0')) {
    // engine.io open: the server's ping timing, then the socket.io connect
    // with who we are.
    const QJsonObject open = QJsonDocument::fromJson(text.mid(1).toUtf8()).object();
    const int interval = open.value(QStringLiteral("pingInterval")).toInt(25000);
    const int timeout = open.value(QStringLiteral("pingTimeout")).toInt(20000);
    pingMs_ = std::max(10000, interval + timeout);
    watchdog_->start(pingMs_);
    socket_->sendTextMessage(QString::fromUtf8(freedvReporterConnectPacket(
        call_, grid_, QStringLiteral("STD ") + QStringLiteral(STD_VERSION), false)));
    return;
  }
  if (kind == QLatin1Char('2')) {
    // engine.io ping
    watchdog_->start(pingMs_);
    socket_->sendTextMessage(QStringLiteral("3"));
    return;
  }
  if (kind != QLatin1Char('4') || text.size() < 2)
    return;
  const QChar packet = text.at(1);
  if (packet == QLatin1Char('4') || packet == QLatin1Char('1')) {
    // namespace refused or closed
    drop(QStringLiteral("FreeDV Reporter refused the connection"));
    return;
  }
  if (packet != QLatin1Char('2'))
    return;
  // The server streams every station's reports to every client. STD only
  // needs to know it was accepted, so the rest is not parsed. The FreeDV app
  // also takes the event inside a bulk_update.
  // A quote inside chat text arrives escaped, so ["connection_successful"
  // only matches the event itself.
  if (!text.contains(QLatin1String("[\"connection_successful\"")))
    return;
  if (!ready_) {
    ready_ = true;
    retryMs_ = 5000;
    sendState();
    emit status(QStringLiteral("FreeDV Reporter connected as %1. This does not key. START was not sent.")
                    .arg(call_));
  }
}

void FreeDvReporter::sendEvent(const QString& name, const QByteArray& payload) {
  if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    return;
  socket_->sendTextMessage(QString::fromUtf8(freedvReporterEvent(name, payload)));
}

void FreeDvReporter::sendState() {
  if (!ready_)
    return;
  if (freqHz_ > 0)
    sendEvent(QStringLiteral("freq_change"),
              QJsonDocument(QJsonObject{{QStringLiteral("freq"), QJsonValue(freqHz_)}})
                  .toJson(QJsonDocument::Compact));
  sendEvent(QStringLiteral("tx_report"),
            QJsonDocument(QJsonObject{{QStringLiteral("mode"), mode_}, {QStringLiteral("transmitting"), tx_}})
                .toJson(QJsonDocument::Compact));
  // The FreeDV app sends its message on every connect, empty or not.
  sendEvent(QStringLiteral("message_update"),
            QJsonDocument(QJsonObject{{QStringLiteral("message"), message_}}).toJson(QJsonDocument::Compact));
}

void FreeDvReporter::setFrequency(qint64 hz) {
  if (hz <= 0 || hz == freqHz_)
    return;
  freqHz_ = hz;
  if (ready_)
    sendEvent(QStringLiteral("freq_change"),
              QJsonDocument(QJsonObject{{QStringLiteral("freq"), QJsonValue(freqHz_)}})
                  .toJson(QJsonDocument::Compact));
}

void FreeDvReporter::setTransmit(const QString& mode, bool tx) {
  if (mode == mode_ && tx == tx_)
    return;
  mode_ = mode;
  tx_ = tx;
  if (ready_)
    sendEvent(QStringLiteral("tx_report"),
              QJsonDocument(QJsonObject{{QStringLiteral("mode"), mode_}, {QStringLiteral("transmitting"), tx_}})
                  .toJson(QJsonDocument::Compact));
}

void FreeDvReporter::setMessage(const QString& text) {
  const QString t = text.trimmed();
  if (t == message_)
    return;
  message_ = t;
  if (ready_)
    sendEvent(QStringLiteral("message_update"),
              QJsonDocument(QJsonObject{{QStringLiteral("message"), message_}}).toJson(QJsonDocument::Compact));
}

void FreeDvReporter::reportReceive(const QString& call, const QString& mode, int snrDb) {
  if (!ready_)
    return;
  sendEvent(QStringLiteral("rx_report"),
            QJsonDocument(QJsonObject{{QStringLiteral("callsign"), call.trimmed().toUpper()},
                                      {QStringLiteral("mode"), mode},
                                      {QStringLiteral("snr"), snrDb}})
                .toJson(QJsonDocument::Compact));
}
