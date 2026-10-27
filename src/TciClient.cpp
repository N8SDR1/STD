// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TciClient.h"

#include <QtEndian>

#include <QAbstractSocket>
#include <QDataStream>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebSocket>
#include <QWidget>

#include <cstring>

namespace {

constexpr int kHeaderBytes = 64;
constexpr int kSampleBytes = 4;  // float32
constexpr quint32 kFloat32 = 3;
constexpr quint32 kRxAudio = 1;
constexpr quint32 kTxAudio = 2;
constexpr quint32 kTxChrono = 3;

QStringList commandArgs(const QString& raw, QString* name) {
  const int split = raw.indexOf(':');
  const QString command = (split < 0 ? raw : raw.left(split)).trimmed();
  *name = command.toLower();
  if (split < 0 || split + 1 >= raw.size())
    return {};
  QStringList args = raw.mid(split + 1).split(',');
  for (QString& arg : args)
    arg = arg.trimmed();
  return args;
}

bool exclusiveSession(const QString& text) {
  const QString lower = text.toLower();
  return lower.contains(QLatin1String("one client")) ||
         lower.contains(QLatin1String("single client")) ||
         lower.contains(QLatin1String("another client")) ||
         lower.contains(QLatin1String("second client")) ||
         lower.contains(QLatin1String("already connected")) ||
         lower.contains(QLatin1String("already in use")) ||
         lower.contains(QLatin1String("only one")) ||
         lower.contains(QLatin1String("client limit")) ||
         (lower.contains(QLatin1String("busy")) && lower.contains(QLatin1String("client")));
}

bool flagIsTrue(const QString& text, bool* ok) {
  const QString value = text.trimmed().toLower();
  if (value == QLatin1String("true") || value == QLatin1String("1")) {
    *ok = true;
    return true;
  }
  if (value == QLatin1String("false") || value == QLatin1String("0")) {
    *ok = true;
    return false;
  }
  *ok = false;
  return false;
}

}  // namespace

TciClient::TciClient(QObject* parent) : QObject(parent) {}

void TciClient::connectToHost(const QString& hostText, quint16 port) {
  ++generation_;
  const int token = generation_;
  settled_ = false;
  userClose_ = false;
  open_ = false;
  transmit_ = false;
  audioStarted_ = false;
  text_.clear();
  sampleRate_ = kAudioRate;

  QString host = hostText.trimmed();
  if (host.startsWith(QLatin1String("ws://"), Qt::CaseInsensitive) ||
      host.startsWith(QLatin1String("wss://"), Qt::CaseInsensitive)) {
    const QUrl pasted(host);
    if (!pasted.host().isEmpty())
      host = pasted.host();
    if (pasted.port() > 0)
      port = static_cast<quint16>(pasted.port());
  }

  if (host.isEmpty() || port == 0) {
    settle(host.isEmpty() ? QStringLiteral("enter a host") : QStringLiteral("enter a port"));
    return;
  }

  host_ = host;
  port_ = port;

  if (socket_) {
    socket_->disconnect(this);
    socket_->abort();
    socket_->deleteLater();
    socket_ = nullptr;
  }

  socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
  connect(socket_, &QWebSocket::connected, this, [this] {
    if (settled_)
      return;
    open_ = true;
    beginSession();
    emit sessionUp(host_, port_);
  });
  connect(socket_, &QWebSocket::disconnected, this, [this] {
    if (settled_ || userClose_)
      return;
    if (!open_)
      return;
    const QString reason = socket_ ? socket_->closeReason() : QString();
    if (exclusiveSession(reason)) {
      yieldSingleClient();
      return;
    }
    settle(reason.isEmpty() ? QStringLiteral("the TCI socket closed") : reason);
  });
  connect(socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    if (settled_ || userClose_)
      return;
    const QString reason = socket_ ? socket_->errorString() : QString();
    if (exclusiveSession(reason)) {
      yieldSingleClient();
      return;
    }
    // A live socket reports the close reason on disconnected. Settling
    // here would hide "this TCI server allows one client".
    if (open_)
      return;
    settle(reason.isEmpty() ? QStringLiteral("the TCI socket failed") : reason);
  });
  connect(socket_, &QWebSocket::textMessageReceived, this, [this](const QString& message) {
    text_ += message;
    int end = -1;
    while ((end = text_.indexOf(';')) >= 0) {
      const QString command = text_.left(end).trimmed();
      text_.remove(0, end + 1);
      if (!command.isEmpty())
        handleCommand(command);
    }
  });
  connect(socket_, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& frame) {
    onBinary(frame);
  });

  QUrl url;
  url.setScheme(QStringLiteral("ws"));
  url.setHost(host_);
  url.setPort(port_);
  socket_->open(url);
  armTimeout(token);
}

void TciClient::disconnectFromHost() {
  if (settled_ && !open_)
    return;
  userClose_ = true;
  ++generation_;
  // AUDIO_STOP is per socket. It drops this subscription only.
  stopAudio();
  open_ = false;
  transmit_ = false;
  if (socket_)
    socket_->close();
  settle(QString());
}

void TciClient::setTransmit(bool) {
  // This build does not send TRX. true would key. false would unkey
  // through Lyra's requestMoxFromTci(false).
  transmit_ = false;
}

void TciClient::setDialHz(qint64 hz) {
  if (hz <= 0)
    return;
  // MSHV Network::set_freq for a TCI model. Receiver 0, VFO A.
  sendCommand(QStringLiteral("vfo:0,0,%1").arg(hz));
}

void TciClient::setSideband(const QString& sideband) {
  const QString token = sideband.trimmed().toLower();
  if (token != QLatin1String("digu") && token != QLatin1String("digl"))
    return;
  // MSHV Network::set_mode writes modulation:<trx>,<mode>. DIGL is
  // the same command with the lower sideband token.
  sendCommand(QStringLiteral("modulation:0,%1").arg(token));
}

void TciClient::sendCommand(const QString& command) {
  if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    return;
  socket_->sendTextMessage(command.endsWith(';') ? command : command + QLatin1Char(';'));
}

void TciClient::beginSession() {
  // Receiver 0, real RX audio, this socket only. Lyra stores that
  // flag per client and does not treat it as startRequested, so a
  // live Protocol 2 session stays up. START would close that session.
  // AUDIO_SAMPLERATE and AUDIO_STREAM_* are global, so they are not
  // sent. A band button may write VFO and MODULATION later. TRX is
  // never sent.
  sendCommand(QStringLiteral("AUDIO_START:0"));
  audioStarted_ = true;
}

void TciClient::stopAudio() {
  if (!audioStarted_)
    return;
  audioStarted_ = false;
  sendCommand(QStringLiteral("AUDIO_STOP:0"));
}

void TciClient::yieldSingleClient() {
  if (settled_)
    return;
  // Drop our socket so the client that was already there can have the
  // radio back. Staying connected is what leaves that session down.
  stopAudio();
  userClose_ = true;
  ++generation_;
  open_ = false;
  transmit_ = false;
  settle(QStringLiteral("this TCI server allows one client"));
  if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    socket_->close();
}

void TciClient::handleCommand(const QString& raw) {
  if (exclusiveSession(raw)) {
    yieldSingleClient();
    return;
  }
  QString name;
  const QStringList args = commandArgs(raw, &name);
  if (name == QLatin1String("audio_samplerate") && !args.isEmpty()) {
    bool ok = false;
    const int rate = args.at(0).toInt(&ok);
    if (ok && rate > 0)
      sampleRate_ = rate;
    return;
  }
  if (name == QLatin1String("vfo") && args.size() >= 3) {
    bool ok = false;
    const qint64 hz = args.at(2).toLongLong(&ok);
    if (ok && args.at(0) == QLatin1String("0") && args.at(1) == QLatin1String("0"))
      emit dialChanged(hz);
    return;
  }
  if (name == QLatin1String("rx_filter_band") && args.size() >= 3) {
    if (args.at(0) != QLatin1String("0"))
      return;
    bool okLo = false;
    bool okHi = false;
    const int low = args.at(1).toInt(&okLo);
    const int high = args.at(2).toInt(&okHi);
    if (okLo && okHi)
      emit receiveBandChanged(low, high);
    return;
  }
  if (name == QLatin1String("modulation") && args.size() >= 2) {
    if (args.at(0) == QLatin1String("0") && !args.at(1).isEmpty())
      emit modeChanged(args.at(1));
    return;
  }
  if (name == QLatin1String("trx") && args.size() >= 2) {
    if (args.at(0) != QLatin1String("0"))
      return;
    bool ok = false;
    const bool on = flagIsTrue(args.at(1), &ok);
    if (ok)
      emit pttChanged(on);
  }
}

void TciClient::onBinary(const QByteArray& frame) {
  if (frame.size() < kHeaderBytes)
    return;

  QDataStream stream(frame);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  quint32 receiver = 0;
  quint32 sampleRate = 0;
  quint32 format = 0;
  quint32 codec = 0;
  quint32 crc = 0;
  quint32 length = 0;
  quint32 type = 0;
  quint32 channels = 0;
  stream >> receiver >> sampleRate >> format >> codec >> crc >> length >> type >> channels;
  if (stream.status() != QDataStream::Ok)
    return;

  Q_UNUSED(receiver);
  Q_UNUSED(codec);
  Q_UNUSED(crc);
  if (type != kRxAudio)
    return;

  // Lyra's type-1 payload is one demodulated channel, duplicated when
  // the header says stereo. Channel 0 is that real audio. No TX reply.
  const int ch = channels < 1 ? 1 : int(channels);
  const int bps = format == kFloat32 ? 4 : format == 0 ? 2 : 0;
  if (bps == 0 || length == 0)
    return;
  const int scalars = int(length);
  const int payload = frame.size() - kHeaderBytes;
  if (payload < scalars * bps)
    return;
  const int frames = scalars / ch;
  if (frames <= 0)
    return;

  QByteArray mono(frames * int(sizeof(float)), Qt::Uninitialized);
  float* dst = reinterpret_cast<float*>(mono.data());
  const char* src = frame.constData() + kHeaderBytes;
  if (format == kFloat32) {
    for (int i = 0; i < frames; ++i) {
      const quint32 bits = qFromLittleEndian<quint32>(src + (i * ch) * 4);
      float value = 0.0f;
      std::memcpy(&value, &bits, sizeof(value));
      dst[i] = value;
    }
  } else {
    for (int i = 0; i < frames; ++i) {
      const qint16 sample = qFromLittleEndian<qint16>(src + (i * ch) * 2);
      dst[i] = float(sample) / 32768.0f;
    }
  }
  if (sampleRate > 0)
    sampleRate_ = int(sampleRate);
  emit rxAudio(sampleRate_, mono);
}

void TciClient::sendTxAudio(const float* samples, int count) {
  if (!socket_ || !open_ || count <= 0 || !samples)
    return;

  QByteArray frame;
  QDataStream stream(&frame, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  stream << quint32(0) << quint32(sampleRate_) << kFloat32 << quint32(0) << quint32(0)
         << quint32(count) << kTxAudio << quint32(1);
  for (int i = 0; i < 8; ++i)
    stream << quint32(0);
  for (int i = 0; i < count; ++i)
    stream << samples[i];
  if (frame.size() != kHeaderBytes + count * kSampleBytes)
    return;
  socket_->sendBinaryMessage(frame);
}

void TciClient::settle(const QString& error) {
  if (settled_)
    return;
  settled_ = true;
  open_ = false;
  transmit_ = false;
  emit sessionDown(error);
}

void TciClient::armTimeout(int token) {
  QTimer::singleShot(8000, this, [this, token] {
    if (token != generation_ || settled_ || open_)
      return;
    settle(QStringLiteral("timed out waiting for the TCI socket"));
    if (socket_)
      socket_->abort();
  });
}

void openTciConnectDialog(QWidget* parent, TciClient* client) {
  QSettings settings;
  QString host = settings.value(QStringLiteral("tci/host"), QStringLiteral("127.0.0.1")).toString();
  int port = settings.value(QStringLiteral("tci/port"), TciClient::kDefaultPort).toInt();
  if (port < 1 || port > 65535)
    port = TciClient::kDefaultPort;

  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("TCI radio"));
  dialog.setModal(true);
  dialog.setMinimumWidth(380);

  auto* hostEdit = new QLineEdit(host, &dialog);
  hostEdit->setObjectName(QStringLiteral("tciHost"));
  hostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
  hostEdit->setClearButtonEnabled(true);

  auto* portEdit = new QSpinBox(&dialog);
  portEdit->setObjectName(QStringLiteral("tciPort"));
  portEdit->setRange(1, 65535);
  portEdit->setValue(port);
  portEdit->setToolTip(QStringLiteral("TCI port. The usual default is 50001."));

  auto* form = new QFormLayout;
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);
  form->addRow(QStringLiteral("Host"), hostEdit);
  form->addRow(QStringLiteral("Port"), portEdit);

  auto* note = new QLabel(
      QStringLiteral(
          "Connect shares the radio and subscribes to receive audio. "
          "It does not send START, and it does not key. "
          "A band button sets the FT8 dial and the sideband."),
      &dialog);
  note->setWordWrap(true);
  note->setObjectName(QStringLiteral("panelNote"));

  auto* connectButton = new QPushButton(client->isOpen() ? QStringLiteral("Disconnect")
                                                          : QStringLiteral("Connect"),
                                         &dialog);
  auto* closeButton = new QPushButton(QStringLiteral("Close"), &dialog);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  buttons->addWidget(connectButton);
  buttons->addWidget(closeButton);

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(12);
  layout->addLayout(form);
  layout->addWidget(note);
  layout->addLayout(buttons);

  QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(connectButton, &QPushButton::clicked, &dialog, [client, hostEdit, portEdit, connectButton] {
    if (client->isOpen()) {
      client->disconnectFromHost();
      return;
    }
    const QString nextHost = hostEdit->text().trimmed();
    const quint16 nextPort = static_cast<quint16>(portEdit->value());
    QSettings store;
    store.setValue(QStringLiteral("tci/host"), nextHost);
    store.setValue(QStringLiteral("tci/port"), nextPort);
    connectButton->setEnabled(false);
    client->connectToHost(nextHost, nextPort);
  });
  QObject::connect(client, &TciClient::sessionUp, &dialog, [connectButton](const QString&, quint16) {
    connectButton->setEnabled(true);
    connectButton->setText(QStringLiteral("Disconnect"));
  });
  QObject::connect(client, &TciClient::sessionDown, &dialog, [connectButton](const QString&) {
    connectButton->setEnabled(true);
    connectButton->setText(QStringLiteral("Connect"));
  });

  hostEdit->setFocus();
  dialog.exec();
}
