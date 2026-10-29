// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "spot/PskReporter.h"

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

namespace {

void appendBe16(QByteArray* out, quint16 value) {
  out->append(char((value >> 8) & 0xff));
  out->append(char(value & 0xff));
}

void appendBe32(QByteArray* out, quint32 value) {
  out->append(char((value >> 24) & 0xff));
  out->append(char((value >> 16) & 0xff));
  out->append(char((value >> 8) & 0xff));
  out->append(char(value & 0xff));
}

void appendText(QByteArray* out, const QString& text) {
  const QByteArray bytes = text.toUtf8();
  const int count = qMin(255, bytes.size());
  out->append(char(count));
  out->append(bytes.constData(), count);
}

void writeLength(QByteArray* record) {
  const int length = record->size();
  (*record)[2] = char((length >> 8) & 0xff);
  (*record)[3] = char(length & 0xff);
}

bool looksLikeCall(const QString& token) {
  static const QRegularExpression re(QStringLiteral(
      "^(?:[A-Z0-9]{1,4}/)?(?:[A-Z]{1,2}|[0-9][A-Z])[0-9][A-Z]{1,4}(?:/[A-Z0-9]{1,4})?$"));
  return re.match(token).hasMatch();
}

bool looksLikeGrid(const QString& token) {
  static const QRegularExpression re(QStringLiteral("^[A-R]{2}[0-9]{2}(?:[A-X]{2})?$"));
  return re.match(token).hasMatch();
}

QString cleanToken(QString token) {
  token = token.toUpper();
  while (!token.isEmpty() && !token.front().isLetterOrNumber() && token.front() != QLatin1Char('/'))
    token.remove(0, 1);
  while (!token.isEmpty() && !token.back().isLetterOrNumber() && token.back() != QLatin1Char('/'))
    token.chop(1);
  return token;
}

const QByteArray kRxDescriptor = QByteArray::fromHex(
    "0003002C50E200040000"
    "8002FFFF0000768F"
    "8004FFFF0000768F"
    "8008FFFF0000768F"
    "8009FFFF0000768F"
    "0000");

const QByteArray kTxDescriptor = QByteArray::fromHex(
    "0002003C50E30007"
    "8001FFFF0000768F"
    "800500040000768F"
    "800600010000768F"
    "800AFFFF0000768F"
    "8003FFFF0000768F"
    "800B00010000768F"
    "00960004");

}  // namespace

QByteArray pskReporterDatagram(const QString& rxCall, const QString& rxGrid, const QString& program,
                               const QString& antenna, const QVector<PskSpot>& spots, quint32 unixTime,
                               quint32 sequence, quint32 randomId) {
  const QString call = rxCall.trimmed().toUpper();
  if (call.isEmpty() || spots.isEmpty())
    return {};

  QByteArray rx;
  rx.append(QByteArray::fromHex("50E2"));
  rx.append(char(0));
  rx.append(char(0));
  appendText(&rx, call);
  appendText(&rx, rxGrid.trimmed().toUpper().left(6));
  appendText(&rx, program.left(64));
  appendText(&rx, antenna.left(64));
  rx.append(char(0));
  rx.append(char(0));
  writeLength(&rx);

  QByteArray tx;
  tx.append(QByteArray::fromHex("50E3"));
  tx.append(char(0));
  tx.append(char(0));
  for (const PskSpot& spot : spots) {
    const QString remote = spot.call.trimmed().toUpper();
    if (remote.isEmpty())
      continue;
    appendText(&tx, remote);
    const quint32 freq = quint32(qBound(qint64(0), spot.freqHz, qint64(0xffffffff)));
    appendBe32(&tx, freq);
    tx.append(char(quint8(qint8(qBound(-128, spot.snr, 127)))));
    appendText(&tx, spot.mode.left(16));
    appendText(&tx, spot.grid.trimmed().toUpper().left(6));
    tx.append(char(1));
    appendBe32(&tx, spot.unixTime);
  }
  if (tx.size() <= 4)
    return {};
  tx.append(char(0));
  tx.append(char(0));
  writeLength(&tx);

  const int total = 16 + kRxDescriptor.size() + kTxDescriptor.size() + rx.size() + tx.size();
  if (total > 1400 || total > 65535)
    return {};
  QByteArray packet;
  packet.reserve(total);
  appendBe16(&packet, 0x000A);
  appendBe16(&packet, quint16(total));
  appendBe32(&packet, unixTime);
  appendBe32(&packet, sequence);
  appendBe32(&packet, randomId);
  packet.append(kRxDescriptor);
  packet.append(kTxDescriptor);
  packet.append(rx);
  packet.append(tx);
  return packet;
}

QString pskReporterMapUrl(const QString& call) {
  const QString trimmed = call.trimmed().toUpper();
  if (trimmed.isEmpty())
    return QStringLiteral("https://pskreporter.info/pskmap.html");
  return QStringLiteral("https://pskreporter.info/pskmap.html?preset&callsign=%1&what=all")
      .arg(QString::fromUtf8(QUrl::toPercentEncoding(trimmed)));
}

bool pskSpotFromMessage(const QString& message, QString* call, QString* grid) {
  if (!call || !grid)
    return false;
  call->clear();
  grid->clear();
  static const QRegularExpression split(QStringLiteral("\\s+"));
  const QStringList words = message.toUpper().split(split, Qt::SkipEmptyParts);
  static const QStringList skip{QStringLiteral("CQ"),   QStringLiteral("DE"),   QStringLiteral("QRZ"),
                                QStringLiteral("DX"),   QStringLiteral("POTA"), QStringLiteral("SOTA"),
                                QStringLiteral("IOTA"), QStringLiteral("WWFF"), QStringLiteral("BOTA"),
                                QStringLiteral("QRP"),  QStringLiteral("UP"),   QStringLiteral("TEST"),
                                QStringLiteral("RRR"),  QStringLiteral("RR73"), QStringLiteral("73")};
  QString foundCall;
  QString foundGrid;
  for (const QString& raw : words) {
    const QString word = cleanToken(raw);
    if (word.isEmpty() || skip.contains(word))
      continue;
    if (foundCall.isEmpty() && looksLikeCall(word)) {
      foundCall = word;
      continue;
    }
    if (!foundCall.isEmpty() && foundGrid.isEmpty() && looksLikeGrid(word))
      foundGrid = word;
  }
  if (foundCall.isEmpty())
    return false;
  *call = foundCall;
  *grid = foundGrid;
  return true;
}

QString pskReporterModeName(const QString& mode) {
  if (mode.startsWith(QLatin1String("JT65")))
    return QStringLiteral("JT65");
  if (mode.startsWith(QLatin1String("Q65")))
    return QStringLiteral("Q65");
  if (mode.startsWith(QLatin1String("JS8")))
    return QStringLiteral("JS8");
  return mode;
}

PskUploader::PskUploader(QObject* parent)
    : QObject(parent), socket_(new QUdpSocket(this)), timer_(new QTimer(this)) {
  randomId_ = QRandomGenerator::global()->generate();
  timer_->setSingleShot(true);
  connect(timer_, &QTimer::timeout, this, [this] { flush(); });
}

void PskUploader::note(const PskSpot& spot, const QString& rxCall, const QString& rxGrid) {
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  QSettings settings;
  if (!settings.value(QStringLiteral("dx/reportEnabled"), false).toBool())
    return;
  const QString mine = rxCall.trimmed().toUpper();
  const QString remote = spot.call.trimmed().toUpper();
  if (mine.isEmpty() || remote.isEmpty() || remote == mine)
    return;
  const QString key = remote + QLatin1Char('|') + spot.mode + QLatin1Char('|') +
                      QString::number(spot.freqHz / 100);
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (seen_.value(key) > now - 600)
    return;
  seen_.insert(key, now);
  queue_.push_back(spot);
  rxCall_ = mine;
  rxGrid_ = rxGrid.trimmed().toUpper();
  if (!timer_->isActive())
    timer_->start(2000);
}

void PskUploader::flush() {
  if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
    queue_.clear();
    return;
  }
  QSettings settings;
  if (!settings.value(QStringLiteral("dx/reportEnabled"), false).toBool()) {
    queue_.clear();
    return;
  }
  if (queue_.isEmpty())
    return;
  const QString host =
      settings.value(QStringLiteral("dx/reportHost"), QStringLiteral("report.pskreporter.info"))
          .toString()
          .trimmed();
  port_ = quint16(settings.value(QStringLiteral("dx/reportPort"), 4739).toUInt());
  if (host.isEmpty() || port_ == 0) {
    queue_.clear();
    return;
  }
  pendingHost_ = host;
  batch_ = queue_;
  queue_.clear();
  QHostAddress literal;
  if (literal.setAddress(host)) {
    sendTo(literal);
    return;
  }
  if (host == lookedUpName_ && !resolved_.isNull()) {
    sendTo(resolved_);
    return;
  }
  QHostInfo::lookupHost(host, this, [this](const QHostInfo& info) {
    if (qEnvironmentVariableIsSet("STD_SELFTEST") || info.addresses().isEmpty()) {
      batch_.clear();
      return;
    }
    lookedUpName_ = pendingHost_;
    resolved_ = info.addresses().first();
    sendTo(resolved_);
  });
}

void PskUploader::sendTo(const QHostAddress& address) {
  if (batch_.isEmpty() || address.isNull())
    return;
  const QByteArray packet = pskReporterDatagram(
      rxCall_, rxGrid_, QStringLiteral("STD ") + QLatin1String(STD_VERSION), QString(), batch_,
      quint32(QDateTime::currentSecsSinceEpoch()), ++sequence_, randomId_);
  batch_.clear();
  if (packet.isEmpty() || !socket_)
    return;
  socket_->writeDatagram(packet, address, port_);
}
