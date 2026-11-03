// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "speech/SpeechReader.h"

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

// Microphone capture for speech to text. Windows shared-mode capture
// (WASAPI) asked for 16 kHz mono float, the rate the recognizer wants, so
// no Qt multimedia or FFmpeg files ship with STD. It never keys the radio
// and does not touch the TCI audio.
class SpeechInput : public QObject {
  Q_OBJECT

public:
  static constexpr int kRate = 16000;

  // Recording devices. The first entry is the Windows default.
  static QList<SpeechReader::Choice> inputs();

  explicit SpeechInput(QObject* parent = nullptr);
  ~SpeechInput() override;

  // Opens the device (empty id: Windows default) and starts recording.
  // False when it cannot be opened; error() says why.
  bool start(const QString& deviceId);
  // Stops recording. The samples stay until take() or the next start().
  void stop();
  bool running() const { return running_.load(); }
  // Everything recorded since start(), 16 kHz mono.
  std::vector<float> take();
  QString error() const { return error_; }
  // Set when the device fails after it opened (unplugged, format changed).
  QString lostError();

signals:
  // About 20 times a second while recording: the loudest 50 ms, dBFS.
  void level(double dbfs);

private:
  void run(QString deviceId);

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopFlag_{false};
  std::mutex mutex_;
  std::vector<float> samples_;
  QString error_;
  QString lostError_;
};

// The speech gate, applied after recording: 20 ms frames at or above
// gateDb open it, it stays open 300 ms after the last loud frame, and
// 100 ms before the first one is kept so a soft start is not clipped.
// Frames while it is shut are left out.
std::vector<float> gateSpeech(const std::vector<float>& in, double gateDb);

// dBFS of a block, -120 for silence.
double speechLevelDb(const float* samples, int count);
