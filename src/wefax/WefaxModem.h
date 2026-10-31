// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <vector>

// HF WEFAX receive, rewritten from the Fldigi wefax parameters (Dave Freese
// W1HKJ and Remi Chateauneu, GPL-3). IOC 576, 120 lines per minute. Black is
// 400 Hz below the center and white is 400 Hz above it. The center follows
// the receive cursor. This path does not transmit and does not send START.

struct WefaxSpec {
  int lpm = 120;
  int ioc = 576;
  int centerHz = 1900;
};

struct WefaxImage {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> gray;
};

int wefaxPixelsPerLine(int ioc);
int wefaxSamplesPerLine(int sampleRateHz, int lpm);

// Test picture only. buildTxWave does not call this, and TX does not key.
// lineScale stretches each line. 1.01 is a one-percent fast clock.
std::vector<float> wefaxModulateTest(const WefaxSpec& spec, int sampleRateHz,
                                    const std::vector<unsigned char>& gray, int width,
                                    int height, double lineScale = 1.0);

// slantPercent stretches the line the way the Slant control does. Zero is 120 lpm.
WefaxImage wefaxDecode(const WefaxSpec& spec, const float* samples, int count, int sampleRateHz,
                       double slantPercent = 0.0);

QString wefaxSaveDirectory();
void wefaxSetSaveDirectory(const QString& directory);

class WefaxEngine : public QObject {
  Q_OBJECT

public:
  explicit WefaxEngine(QObject* parent = nullptr);
  ~WefaxEngine() override;

  void setArmed(bool armed);
  void setCenterHz(int hz);
  // Percent clock stretch. Zero keeps 120 lines per minute. Range is -20 to 20.
  void setSlant(int percent);
  int slant() const;
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);
  const QImage& image() const;
  int rowCount() const;
  QString saveNow();

signals:
  void imageChanged();
  void imageSaved(const QString& path);

private:
  struct Private;
  Private* d_;
};
