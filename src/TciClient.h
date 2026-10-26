// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>

class QWidget;

// One TCI WebSocket. Connect shares a radio that is already on the
// air. The only command it sends is AUDIO_START for receiver 0, which
// subscribes this socket to real RX audio. It does not send START,
// MODULATION, a VFO write, TRX, or the global audio-format setters.
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

  // Kept off. TRX true would key, and TRX false would unkey.
  void setTransmit(bool on);

signals:
  void sessionUp(const QString& host, quint16 port);
  void sessionDown(const QString& error);
  void dialChanged(qint64 hz);
  void modeChanged(const QString& mode);
  void pttChanged(bool on);
  // Mono float32, one real channel taken from a type-1 RX frame.
  void rxAudio(int sampleRateHz, const QByteArray& mono);

private:
  void sendCommand(const QString& command);
  void beginSession();
  void handleCommand(const QString& raw);
  void yieldSingleClient();
  void stopAudio();
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
  bool audioStarted_ = false;
};

void openTciConnectDialog(QWidget* parent, TciClient* client);
