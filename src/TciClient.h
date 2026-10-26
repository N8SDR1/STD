// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

class QWidget;

// One TCI WebSocket. Text commands share the socket with type-1 RX audio
// and type-2 TX audio. This client does not open a sound device.
class TciClient : public QObject {
  Q_OBJECT

public:
  static constexpr quint16 kDefaultPort = 50001;
  static constexpr int kAudioRate = 12000;

  explicit TciClient(QObject* parent = nullptr);

  void connectToHost(const QString& host, quint16 port);
  void disconnectFromHost();
  bool isOpen() const { return open_; }
  QString host() const { return host_; }
  quint16 port() const { return port_; }

  // TRX:0,true,tci keys the radio. TRX:0,false,tci selects the TCI
  // audio source and stays in receive. The window does not call this
  // with true.
  void setTransmit(bool on);

signals:
  void sessionUp(const QString& host, quint16 port);
  void sessionDown(const QString& error);
  void dialChanged(qint64 hz);
  void modeChanged(const QString& mode);
  void pttChanged(bool on);

private:
  void sendCommand(const QString& command);
  void beginSession();
  void handleCommand(const QString& raw);
  void onBinary(const QByteArray& frame);
  void sendTxAudio(const float* samples, int count);
  void settle(const QString& error);
  void armTimeout(int token);

  class QWebSocket* socket_ = nullptr;
  QString host_;
  quint16 port_ = kDefaultPort;
  QString text_;
  int sampleRate_ = kAudioRate;
  int generation_ = 0;
  bool open_ = false;
  bool settled_ = true;
  bool userClose_ = false;
  bool transmit_ = false;
};

void openTciConnectDialog(QWidget* parent, TciClient* client);
