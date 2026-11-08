// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

#include <vector>

// The meteor-scatter display MSK144 and MSKMS use, laid out like MSHV's:
// time runs left to right across one T/R period, frequency runs up, and a
// ping is a bright burst at the second it landed. The top strip is the
// period on the air now, the strip under it is the one before, each
// stamped with its UTC start. Every decode leaves a marker at its time.
// It only listens to receive audio. It never keys.
class MskPingDisplay : public QWidget {
  Q_OBJECT

public:
  explicit MskPingDisplay(QWidget* parent = nullptr);

  void setPeriodMs(int periodMs);
  int periodMs() const { return periodMs_; }
  void pushAudio(const float* samples, int count, int sampleRateHz);
  // stamp is the hhmmss of the period, t0 the seconds into it.
  void addPing(const QString& stamp, double t0);
  void clear();

  // Self-test: the column a time lands in, and the strips' state.
  int columnsPerPeriod() const;
  int pingCount(bool current) const;
  QString stamp(bool current) const;
  // Brightness 0..255 at a time into the current period and an audio
  // frequency, or -1 if that column has not been painted.
  int levelAt(double seconds, int hz) const;
  // Feeds samples as if they arrived at wall time startMs. Self-test only.
  void pushAudioAt(const float* samples, int count, int sampleRateHz, qint64 startMs);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  struct Strip {
    QImage image;
    QString stamp;
    qint64 startMs = -1;
    QVector<double> pings;
    QVector<bool> painted;
    int cursor = -1;
  };

  void feed(float sample, qint64 sampleMs);
  void column(qint64 endMs);
  void reset(Strip& strip, qint64 startMs);
  void paintStrip(class QPainter& p, const QRect& area, const Strip& strip) const;

  int periodMs_ = 30000;
  Strip current_;
  Strip previous_;
  std::vector<float> block_;
  int fill_ = 0;
  int decim_ = 0;
  double residual_ = 0.0;
  int rate_ = 0;
  std::vector<float> floor_;
  qint64 lastRepaintMs_ = 0;
};
