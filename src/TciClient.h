// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>

class QWidget;

// One TCI WebSocket. Connect shares a radio that is already on the
// air. The only command it sends then is AUDIO_START for receiver 0,
// which subscribes this socket to real RX audio. It does not send
// START, TRX, or the global audio-format setters. A later band or
// mode choice may send one VFO write and one MODULATION.
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

  // Local multiplier for a future TX audio frame. Does not send a command.
  void setTxGain(float linear);

  // FT8 dial on receiver 0, VFO A. No-op unless the socket is open.
  // Does not send START and does not key.
  void setDialHz(qint64 hz);

  // DIGU or DIGL on receiver 0. Any other token is ignored.
  void setSideband(const QString& sideband);

signals:
  void sessionUp(const QString& host, quint16 port);
  void sessionDown(const QString& error);
  void dialChanged(qint64 hz);
  void modeChanged(const QString& mode);
  void pttChanged(bool on);
  // Receiver 0 passband, as reported. low and high keep the radio's sign.
  void receiveBandChanged(int lowHz, int highHz);
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
  float txGain_ = 1.0f;
};

void openTciConnectDialog(QWidget* parent, TciClient* client);
