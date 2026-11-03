// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

// Plays 16 kHz mono float on the Speech speaker. The saved id is a
// Windows voice-output token; the matching playback device is opened
// by friendly name. This does not key and does not send START.
class RadeSpeaker : public QObject {
public:
  explicit RadeSpeaker(QObject* parent = nullptr);
  ~RadeSpeaker() override;

  bool start(const QString& sapiOutputId);
  void stop();
  void clear();
  bool running() const { return running_.load(); }
  QString deviceId() const { return deviceId_; }
  void setVolume(int percent);
  void write(const float* samples, int count);
  QString error() const;

private:
  void run(QString wasapiId);

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopFlag_{false};
  std::mutex mutex_;
  std::vector<float> queue_;
  std::atomic<float> gain_{0.8f};
  QString deviceId_;
  QString error_;
};
