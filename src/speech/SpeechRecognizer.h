// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

struct whisper_context;

// Turns recorded speech (16 kHz mono) into text with the downloaded model.
// The model loads on first use and stays loaded; recognizing runs on its
// own thread so the window and the radio audio keep going.
class SpeechRecognizer : public QObject {
  Q_OBJECT

public:
  explicit SpeechRecognizer(QObject* parent = nullptr);
  ~SpeechRecognizer() override;

  // True when this processor can run the recognizer (AVX2 and FMA).
  static bool cpuSupported();

  bool busy() const { return busy_.load(); }
  // Starts recognizing. hint is words the recognizer should expect (calls,
  // names). False when already busy; done() follows otherwise.
  bool start(const QString& modelPath, std::vector<float> samples, const QString& hint);
  // Frees the loaded model, e.g. after it was removed.
  void unload();

signals:
  // Raw recognizer text, or an error with empty text.
  void done(const QString& text, const QString& error);

private:
  std::thread thread_;
  std::atomic<bool> busy_{false};
  std::atomic<bool> abort_{false};
  std::mutex mutex_;  // guards ctx_ and loadedPath_
  whisper_context* ctx_ = nullptr;
  QString loadedPath_;
};

// Ham clean-up of recognizer text for the transmit box: spoken letters and
// digits run together into one word ("kilo eight alpha bravo" -> "K8AB",
// "five nine nine" -> "599", "slash portable" -> "/P"), "seventy three"
// becomes 73, filler marks go, and the result is upper case.
QString speechToTxText(const QString& heard);

// Hint words for the recognizer: ham terms plus the operator's own call.
QString speechHint(const QString& ownCall, const QString& extra);
