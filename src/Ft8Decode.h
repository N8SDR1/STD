// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

// Buffers TCI receive audio and decodes one FT8 period at a time.
// It never opens a socket and never transmits.
class Ft8Decode : public QObject {
  Q_OBJECT

public:
  explicit Ft8Decode(QObject* parent = nullptr);
  ~Ft8Decode() override;

  void pushAudio(const float* samples, int count, int sampleRateHz);
  void setArmed(bool armed, int lowHz, int highHz);
  void clear();

signals:
  void decodedLine(const QString& line);

private:
  void poll();
  void ensureWorker();

  struct Private;
  Private* d_ = nullptr;
};
