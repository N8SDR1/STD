// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "spot/FreeDvReporter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <algorithm>
#include <cmath>

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

QByteArray freedvReporterQsyPayload(const QString& sid, qint64 hz, const QString& message) {
  return QJsonDocument(QJsonObject{{QStringLiteral("dest_sid"), sid},
                                   {QStringLiteral("frequency"), QJsonValue(hz)},
                                   {QStringLiteral("message"), message}})
      .toJson(QJsonDocument::Compact);
}

static QString jsonText(const QJsonObject& object, const char* key) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isString() ? value.toString() : QString();
}

static bool jsonNumber(const QJsonObject& object, const char* key, qint64* out) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (!value.isDouble() || !out)
    return false;
  *out = qint64(value.toDouble());
  return true;
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
    if (!stations_.isEmpty()) {
      stations_.clear();
      emit stationsChanged();
    }
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
  ingestPacket(text.toUtf8());
}

void FreeDvReporter::ingestPacket(const QByteArray& packet) {
  if (!packet.startsWith("42"))
    return;
  const QJsonDocument doc = QJsonDocument::fromJson(packet.mid(2));
  if (!doc.isArray() || doc.array().isEmpty() || !doc.array().at(0).isString())
    return;
  const QJsonArray frame = doc.array();
  const QJsonValue arg = frame.size() > 1 ? frame.at(1) : QJsonValue();
  bool dirty = false;
  applyEvent(frame.at(0).toString(), arg, &dirty);
  if (dirty)
    emit stationsChanged();
}

FreeDvReporter::Station* FreeDvReporter::stationFor(const QString& sid) {
  if (sid.isEmpty())
    return nullptr;
  for (Station& row : stations_)
    if (row.sid == sid)
      return &row;
  return nullptr;
}

void FreeDvReporter::applyEvent(const QString& name, const QJsonValue& arg, bool* dirty) {
  if (name == QLatin1String("connection_successful")) {
    if (!ready_) {
      ready_ = true;
      retryMs_ = 5000;
      stations_.clear();
      if (dirty)
        *dirty = true;
      sendState();
      emit status(QStringLiteral("FreeDV Reporter connected as %1. This does not key. START was not sent.")
                      .arg(call_));
    }
    return;
  }
  if (name == QLatin1String("bulk_update")) {
    if (!arg.isArray())
      return;
    for (const QJsonValue& item : arg.toArray()) {
      if (!item.isArray())
        continue;
      const QJsonArray pair = item.toArray();
      if (pair.size() < 1 || !pair.at(0).isString())
        continue;
      const QJsonValue inner = pair.size() > 1 ? pair.at(1) : QJsonValue();
      applyEvent(pair.at(0).toString(), inner, dirty);
    }
    return;
  }
  if (name == QLatin1String("qsy_request")) {
    if (!arg.isObject())
      return;
    const QJsonObject object = arg.toObject();
    const QString station = jsonText(object, "callsign");
    qint64 hz = 0;
    if (station.isEmpty() || !jsonNumber(object, "frequency", &hz) || hz <= 0)
      return;
    emit qsyAsked(station, hz, jsonText(object, "message"));
    return;
  }
  if (!arg.isObject())
    return;
  const QJsonObject object = arg.toObject();
  const QString sid = jsonText(object, "sid");
  if (sid.isEmpty())
    return;
  if (name == QLatin1String("remove_connection")) {
    const auto at = std::remove_if(stations_.begin(), stations_.end(),
                                   [&sid](const Station& row) { return row.sid == sid; });
    if (at == stations_.end())
      return;
    stations_.erase(at, stations_.end());
    if (dirty)
      *dirty = true;
    return;
  }
  Station* row = stationFor(sid);
  if (!row) {
    if (name != QLatin1String("new_connection") && name != QLatin1String("freq_change") &&
        name != QLatin1String("tx_report") && name != QLatin1String("rx_report") &&
        name != QLatin1String("message_update"))
      return;
    Station fresh;
    fresh.sid = sid;
    stations_.append(fresh);
    row = &stations_.last();
  }
  auto takeText = [&](const char* key, QString* field) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString() || !field)
      return;
    *field = value.toString();
  };
  if (name == QLatin1String("new_connection")) {
    takeText("callsign", &row->station);
    takeText("grid_square", &row->grid);
    takeText("version", &row->version);
    const QJsonValue rxOnly = object.value(QStringLiteral("rx_only"));
    if (rxOnly.isBool())
      row->rxOnly = rxOnly.toBool();
  } else if (name == QLatin1String("freq_change")) {
    takeText("callsign", &row->station);
    takeText("grid_square", &row->grid);
    qint64 hz = 0;
    if (jsonNumber(object, "freq", &hz) && hz > 0)
      row->freqHz = hz;
  } else if (name == QLatin1String("tx_report")) {
    takeText("callsign", &row->station);
    takeText("grid_square", &row->grid);
    takeText("mode", &row->mode);
    takeText("last_tx", &row->lastTx);
    const QJsonValue tx = object.value(QStringLiteral("transmitting"));
    if (tx.isBool())
      row->transmitting = tx.toBool();
  } else if (name == QLatin1String("rx_report")) {
    takeText("callsign", &row->heard);
    takeText("mode", &row->mode);
    const QJsonValue snr = object.value(QStringLiteral("snr"));
    if (snr.isDouble()) {
      row->snr = int(std::lround(snr.toDouble()));
      row->snrKnown = true;
    }
  } else if (name == QLatin1String("message_update")) {
    takeText("message", &row->message);
  } else {
    return;
  }
  if (dirty)
    *dirty = true;
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

void FreeDvReporter::requestQsy(const QString& sid, qint64 hz, const QString& message) {
  if (!ready_ || sid.isEmpty() || hz <= 0)
    return;
  sendEvent(QStringLiteral("qsy_request"), freedvReporterQsyPayload(sid, hz, message));
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
