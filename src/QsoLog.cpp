// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QsoLog.h"

#include <QDataStream>
#include <QDir>
#include <QTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

namespace {

constexpr quint32 kMagic = 0xadbccbda;
constexpr quint32 kSchema = 3;
constexpr quint32 kHeartbeat = 0;
constexpr quint32 kStatus = 1;
constexpr quint32 kReply = 4;
constexpr quint32 kHaltTx = 8;
constexpr quint32 kFreeText = 9;
constexpr quint32 kQsoLogged = 5;
constexpr quint32 kClose = 6;
constexpr quint32 kLoggedAdif = 12;

QString adif(const QString& name, const QString& value) {
  const QString text = value.trimmed();
  if (text.isEmpty())
    return {};
  const int bytes = text.toUtf8().size();
  return QStringLiteral("<%1:%2>%3").arg(name).arg(bytes).arg(text);
}

QString bandForHz(qint64 hz) {
  if (hz <= 0)
    return {};
  const double mhz = double(hz) / 1.0e6;
  const struct {
    double lo;
    double hi;
    const char* band;
  } rows[] = {
      {1.8, 2.0, "160m"},   {3.5, 4.0, "80m"},    {5.25, 5.45, "60m"}, {7.0, 7.3, "40m"},
      {10.1, 10.15, "30m"}, {14.0, 14.35, "20m"}, {18.068, 18.168, "17m"},
      {21.0, 21.45, "15m"}, {24.89, 24.99, "12m"}, {28.0, 29.7, "10m"}, {50.0, 54.0, "6m"},
  };
  for (const auto& row : rows) {
    if (mhz >= row.lo && mhz <= row.hi)
      return QString::fromLatin1(row.band);
  }
  return {};
}

QString recordText(const QsoLog::Record& qso) {
  QString freq;
  if (qso.freqHz > 0)
    freq = QString::number(double(qso.freqHz) / 1.0e6, 'f', 6);
  const QDateTime on = qso.timeOn.isValid() ? qso.timeOn.toUTC() : qso.timeOff.toUTC();
  const QDateTime off = qso.timeOff.toUTC();
  QStringList fields;
  fields << adif(QStringLiteral("CALL"), qso.call.toUpper());
  fields << adif(QStringLiteral("GRIDSQUARE"), qso.grid.toUpper());
  fields << adif(QStringLiteral("MODE"), qso.mode);
  fields << adif(QStringLiteral("BAND"), bandForHz(qso.freqHz));
  fields << adif(QStringLiteral("FREQ"), freq);
  fields << adif(QStringLiteral("QSO_DATE"), on.toString(QStringLiteral("yyyyMMdd")));
  fields << adif(QStringLiteral("TIME_ON"), on.toString(QStringLiteral("hhmmss")));
  fields << adif(QStringLiteral("QSO_DATE_OFF"), off.toString(QStringLiteral("yyyyMMdd")));
  fields << adif(QStringLiteral("TIME_OFF"), off.toString(QStringLiteral("hhmmss")));
  fields << adif(QStringLiteral("RST_SENT"), qso.rstSent);
  fields << adif(QStringLiteral("RST_RCVD"), qso.rstRcvd);
  fields << adif(QStringLiteral("STATION_CALLSIGN"), qso.myCall.toUpper());
  fields << adif(QStringLiteral("MY_GRIDSQUARE"), qso.myGrid.toUpper());
  fields << adif(QStringLiteral("CONTEST_ID"), qso.contestId);
  fields << adif(QStringLiteral("STX_STRING"), qso.stxString);
  fields << adif(QStringLiteral("SRX_STRING"), qso.srxString);
  fields << adif(QStringLiteral("STX"), qso.stx);
  fields << adif(QStringLiteral("SRX"), qso.srx);
  fields << adif(QStringLiteral("CLASS"), qso.fdClass);
  fields << adif(QStringLiteral("ARRL_SECT"), qso.arrlSection);
  fields << adif(QStringLiteral("NAME"), qso.name);
  fields << adif(QStringLiteral("QTH"), qso.qth);
  fields << adif(QStringLiteral("STATE"), qso.state);
  fields << adif(QStringLiteral("VE_PROV"), qso.veProv);
  fields << adif(QStringLiteral("COMMENT"), qso.comment);
  fields.removeAll(QString());
  return fields.join(QString()) + QStringLiteral("<EOR>");
}

QByteArray wsjt(quint32 type, const QString& version, const QsoLog::Record* qso, const QString& adifRecord) {
  QByteArray message;
  QDataStream out(&message, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_4);
  out.setByteOrder(QDataStream::BigEndian);
  out << kMagic << kSchema << type << QStringLiteral("STD").toUtf8();
  if (type == kHeartbeat) {
    out << kSchema << version.toUtf8() << QString().toUtf8();
  } else if (type == kQsoLogged && qso) {
    const QDateTime off = qso->timeOff.toUTC();
    const QDateTime on = qso->timeOn.isValid() ? qso->timeOn.toUTC() : off;
    const quint64 hz = qso->freqHz > 0 ? quint64(qso->freqHz) : 0;
    const QByteArray blank = QString().toUtf8();
    out << off << qso->call.toUtf8() << qso->grid.toUtf8() << hz << qso->mode.toUtf8()
        << qso->rstSent.toUtf8() << qso->rstRcvd.toUtf8() << blank << qso->comment.toUtf8()
        << qso->name.toUtf8()
        << on << blank << qso->myCall.toUtf8() << qso->myGrid.toUtf8() << qso->stxString.toUtf8()
        << qso->srxString.toUtf8() << blank << blank << blank << blank;
  } else if (type == kLoggedAdif) {
    const QByteArray body =
        QByteArrayLiteral("\n<ADIF_VER:5>3.1.0\n<PROGRAMID:3>STD\n<EOH>\n") + adifRecord.toUtf8();
    out << body;
  }
  return message;
}

QHostAddress resolveHost(const QString& host) {
  const QString text = host.trimmed();
  if (text.isEmpty())
    return {};
  if (text.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
    return QHostAddress::LocalHost;
  const QHostAddress literal(text);
  if (!literal.isNull())
    return literal;
  const QHostInfo info = QHostInfo::fromName(text);
  if (info.addresses().isEmpty())
    return {};
  return info.addresses().first();
}

quint16 portOr(int value, quint16 fallback) {
  if (value < 1 || value > 65535)
    return fallback;
  return quint16(value);
}

}  // namespace

QString qsoBandForHz(qint64 hz) {
  return bandForHz(hz);
}

QsoLog::QsoLog(QObject* parent) : QObject(parent) {
  udp_ = new QUdpSocket(this);
  // Bound now so a logger's Reply comes back to the port STD sends from.
  if (!qEnvironmentVariableIsSet("STD_SELFTEST"))
    udp_->bind(QHostAddress::AnyIPv4, 0);
  connect(udp_, &QUdpSocket::readyRead, this, [this] { readRequests(); });
  pulse_ = new QTimer(this);
  pulse_->setInterval(15000);
  connect(pulse_, &QTimer::timeout, this, [this] { sendHeartbeat(); });
  applySettings();
}

QString QsoLog::defaultAdifPath() {
  const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return QDir(root).filePath(QStringLiteral("log/std.adi"));
}

QString QsoLog::logFolder() {
  return QFileInfo(defaultAdifPath()).absolutePath();
}

QString QsoLog::adifCopyPath(const QString& source) {
  const QString name = QFileInfo(source.trimmed()).fileName();
  if (name.isEmpty())
    return {};
  return QDir(logFolder()).filePath(QStringLiteral("imported/") + name);
}

QString QsoLog::keepAdifCopy(const QString& source) {
  const QString from = source.trimmed();
  if (from.isEmpty())
    return {};
  const QString copy = adifCopyPath(from);
  const QFileInfo src(from);
  if (copy.isEmpty() || QFileInfo(copy).absoluteFilePath() == src.absoluteFilePath())
    return from;  // already the copy
  if (!src.isFile())
    return QFileInfo::exists(copy) ? copy : from;
  const QFileInfo kept(copy);
  if (!kept.exists() || kept.size() != src.size() || kept.lastModified() < src.lastModified()) {
    QDir().mkpath(kept.absolutePath());
    const QString temp = copy + QStringLiteral(".part");
    QFile::remove(temp);
    if (QFile::copy(from, temp)) {
      QFile::remove(copy);
      QFile::rename(temp, copy);
    }
  }
  return from;
}

QByteArray QsoLog::heartbeatDatagram(const QString& version) {
  return wsjt(kHeartbeat, version, nullptr, QString());
}

QByteArray QsoLog::decodeDatagram(const QString& mode, int snr, float deltaTime, quint32 audioHz,
                                  const QString& message, const QTime& time) {
  QByteArray packet;
  QDataStream out(&packet, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_4);
  out.setByteOrder(QDataStream::BigEndian);
  const QTime stamp = time.isValid() ? time : QTime(0, 0);
  out << kMagic << kSchema << quint32(2) << QStringLiteral("STD").toUtf8() << true << stamp
      << qint32(snr) << deltaTime << audioHz << mode.left(16).toUtf8() << message.toUtf8() << false
      << false;
  return packet;
}

QByteArray QsoLog::wsprDecodeDatagram(int snr, float deltaTime, quint64 freqHz, qint32 drift,
                                      const QString& call, const QString& grid, qint32 powerDbm,
                                      const QTime& time) {
  QByteArray packet;
  QDataStream out(&packet, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_4);
  out.setByteOrder(QDataStream::BigEndian);
  const QTime stamp = time.isValid() ? time : QTime(0, 0);
  QString bare = call.trimmed().toUpper();
  if (bare.startsWith(QLatin1Char('<')) && bare.endsWith(QLatin1Char('>')) && bare.size() >= 3)
    bare = bare.mid(1, bare.size() - 2);
  out << kMagic << kSchema << quint32(10) << QStringLiteral("STD").toUtf8() << true << stamp
      << qint32(snr) << deltaTime << freqHz << drift << bare.toUtf8() << grid.trimmed().toUpper().toUtf8()
      << powerDbm << false;
  return packet;
}

void QsoLog::applySettings() {
  QSettings settings;
  path_ = settings.value(QStringLiteral("log/adifPath")).toString().trimmed();
  if (path_.isEmpty())
    path_ = defaultAdifPath();
  udpHost_ = settings.value(QStringLiteral("log/udpHost"), QStringLiteral("127.0.0.1")).toString().trimmed();
  tcpHost_ = settings.value(QStringLiteral("log/tcpHost"), QStringLiteral("127.0.0.1")).toString().trimmed();
  udpPort_ = portOr(settings.value(QStringLiteral("log/udpPort"), 2237).toInt(), 2237);
  tcpPort_ = portOr(settings.value(QStringLiteral("log/tcpPort"), 52001).toInt(), 52001);
  udpQso_ = settings.value(QStringLiteral("log/udpQso"), false).toBool();
  udpAdif_ = settings.value(QStringLiteral("log/udpAdif"), false).toBool();
  udpLink_ = settings.value(QStringLiteral("log/udpLink"), false).toBool();
  udpAccept_ = settings.value(QStringLiteral("log/udpAccept"), true).toBool();
  tcp_ = settings.value(QStringLiteral("log/tcp"), false).toBool();
  lastStatus_.clear();
  const bool wantUdp = udpOn() && !qEnvironmentVariableIsSet("STD_SELFTEST");
  if (wantUdp && !pulse_->isActive()) {
    sendHeartbeat();
    pulse_->start();
  } else if (!wantUdp && pulse_->isActive()) {
    pulse_->stop();
  }
}

void QsoLog::shutdown() {
  if (pulse_)
    pulse_->stop();
  if (!udpLive_ || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  sendUdp(wsjt(kClose, QString(), nullptr, QString()));
  udpLive_ = false;
}

bool QsoLog::sendUdp(const QByteArray& datagram) {
  const QHostAddress address = resolveHost(udpHost_);
  if (address.isNull() || udpPort_ == 0 || !udp_)
    return false;
  const qint64 sent = udp_->writeDatagram(datagram, address, udpPort_);
  if (sent == datagram.size())
    udpLive_ = true;
  return sent == datagram.size();
}

bool QsoLog::sendTcp(const QByteArray& payload) {
  const QHostAddress address = resolveHost(tcpHost_);
  if (address.isNull() || tcpPort_ == 0 || payload.isEmpty())
    return false;
  QTcpSocket socket;
  socket.connectToHost(address, tcpPort_);
  if (!socket.waitForConnected(200))
    return false;
  const qint64 wrote = socket.write(payload);
  const bool flushed = wrote == payload.size() && socket.waitForBytesWritten(200);
  socket.disconnectFromHost();
  return flushed;
}

bool QsoLog::udpOn() const {
  return udpLink_ || udpQso_ || udpAdif_;
}

void QsoLog::sendStatus(const UdpStatus& status) {
  if (qEnvironmentVariableIsSet("STD_SELFTEST") || !udpOn())
    return;
  const QByteArray datagram = statusDatagram(status);
  if (datagram == lastStatus_)
    return;
  lastStatus_ = datagram;
  sendUdp(datagram);
}

QByteArray QsoLog::statusDatagram(const UdpStatus& s) {
  QByteArray packet;
  QDataStream out(&packet, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_4);
  out.setByteOrder(QDataStream::BigEndian);
  // NetworkMessage.hpp Status, schema 3, in order.
  out << kMagic << kSchema << kStatus << QStringLiteral("STD").toUtf8() << s.dialHz << s.mode.toUtf8()
      << s.dxCall.toUpper().toUtf8() << s.report.toUtf8() << s.mode.toUtf8() << s.txEnabled << s.transmitting
      << s.decoding << s.rxDf << s.txDf << s.deCall.toUpper().toUtf8() << s.deGrid.toUtf8() << s.dxGrid.toUtf8()
      << false /* Tx watchdog */ << QString().toUtf8() /* sub-mode */ << false /* fast mode */
      << quint8(0) /* special operation */ << quint32(0xFFFFFFFF) /* frequency tolerance */ << s.periodSeconds
      << QStringLiteral("STD").toUtf8() /* configuration */ << s.txMessage.toUtf8();
  return packet;
}

bool QsoLog::parseRequest(const QByteArray& datagram, UdpRequest* r) {
  QDataStream in(datagram);
  in.setVersion(QDataStream::Qt_5_4);
  in.setByteOrder(QDataStream::BigEndian);
  quint32 magic = 0;
  quint32 schema = 0;
  QByteArray id;
  in >> magic >> schema >> r->type >> id;
  if (in.status() != QDataStream::Ok || magic != kMagic || schema < 2)
    return false;
  r->id = QString::fromUtf8(id);
  // A request for another program sharing the port is not ours.
  if (!r->id.isEmpty() && r->id.compare(QLatin1String("STD"), Qt::CaseInsensitive) != 0)
    return false;
  if (r->type == kReply) {
    QByteArray mode;
    QByteArray message;
    bool lowConfidence = false;
    in >> r->timeMs >> r->snr >> r->deltaTime >> r->deltaFrequency >> mode >> message >> lowConfidence;
    if (in.status() != QDataStream::Ok)
      return false;
    if (!in.atEnd())
      in >> r->modifiers;
    r->mode = QString::fromUtf8(mode);
    r->message = QString::fromUtf8(message).trimmed();
    return !r->message.isEmpty();
  }
  if (r->type == kHaltTx) {
    in >> r->autoTxOnly;
    return in.status() == QDataStream::Ok;
  }
  if (r->type == kFreeText) {
    QByteArray text;
    in >> text >> r->send;
    r->text = QString::fromUtf8(text);
    return in.status() == QDataStream::Ok;
  }
  return false;
}

void QsoLog::readRequests() {
  while (udp_ && udp_->hasPendingDatagrams()) {
    QByteArray datagram(int(udp_->pendingDatagramSize()), Qt::Uninitialized);
    udp_->readDatagram(datagram.data(), datagram.size());
    UdpRequest request;
    if (udpOn() && udpAccept_ && parseRequest(datagram, &request))
      emit requestReceived(request);
  }
}

void QsoLog::sendDecode(const QString& mode, int snr, float deltaTime, quint32 audioHz,
                        const QString& message, const QTime& time) {
  if (qEnvironmentVariableIsSet("STD_SELFTEST") || !udpOn() || message.trimmed().isEmpty())
    return;
  sendUdp(decodeDatagram(mode, snr, deltaTime, audioHz, message, time));
}

void QsoLog::sendWsprDecode(int snr, float deltaTime, quint64 freqHz, qint32 drift, const QString& call,
                            const QString& grid, qint32 powerDbm, const QTime& time) {
  if (qEnvironmentVariableIsSet("STD_SELFTEST") || !udpOn() || call.trimmed().isEmpty())
    return;
  sendUdp(wsprDecodeDatagram(snr, deltaTime, freqHz, drift, call, grid, powerDbm, time));
}

void QsoLog::sendHeartbeat() {
  if (qEnvironmentVariableIsSet("STD_SELFTEST") || !udpOn())
    return;
  sendUdp(heartbeatDatagram(QStringLiteral(STD_VERSION)));
  // The logger forgets nothing, but a restarted one needs Status again.
  if (!lastStatus_.isEmpty())
    sendUdp(lastStatus_);
}

QString QsoLog::adifRecord(const Record& qso) {
  return recordText(qso);
}

QsoLog::Result QsoLog::store(const Record& qso) {
  Result result;
  result.path = path_.isEmpty() ? defaultAdifPath() : path_;
  if (qso.call.trimmed().isEmpty() || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return result;

  const QString line = recordText(qso);
  QDir().mkpath(QFileInfo(result.path).absolutePath());
  QFile file(result.path);
  const bool fresh = !file.exists() || file.size() == 0;
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    result.detail = QStringLiteral("Could not write the ADIF file.");
    return result;
  }
  QString header;
  if (fresh)
    header = QStringLiteral("STD ADIF\n<ADIF_VER:5>3.1.0\n<PROGRAMID:3>STD\n<EOH>\n");
  const QByteArray bytes = (header + line + QLatin1Char('\n')).toUtf8();
  result.wrote = file.write(bytes) == bytes.size();
  file.close();
  if (!result.wrote) {
    result.detail = QStringLiteral("Could not write the ADIF file.");
    return result;
  }

  if (udpQso_)
    result.udpQso = sendUdp(wsjt(kQsoLogged, QString(), &qso, QString()));
  if (udpAdif_)
    result.udpAdif = sendUdp(wsjt(kLoggedAdif, QString(), nullptr, line));
  if (tcp_) {
    const QByteArray payload =
        (QStringLiteral("<command:3>log<parameters:%1>%2").arg(line.toUtf8().size()).arg(line)).toUtf8();
    result.tcp = sendTcp(payload);
  }
  return result;
}
