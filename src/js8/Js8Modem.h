// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <vector>

// JS8Call-improved Normal. Same 15 s / 50 Hz family as FT8, with the
// JS8 Costas array, the 87-bit frame, and the Huffman data payload.
// Fast, Turbo, Slow, and Ultra use a different symbol clock. 8PSK is
// not this mode. Callsign and locator stay in Options → User.

struct Js8Plan {
  std::vector<float> samples;
  std::vector<int> echoAt;
};

bool js8Mode(const QString& mode);

// JS8Call-improved user guide, "Frequencies (Calling Frequencies and Band Hopping)":
// https://js8call.com/JS8Call-improved/d6/d14/md_docs_2user__guide_2JS8Call__User__Guide.html
// USB dials, offset from FT8 so the two modes do not share a passband.
// 2 m (144.178 MHz) is in that guide. STD's band table stops at 6 m, so it is not here.
int js8SuggestedBandCount();
qint64 js8SuggestedDialHz(int bandIndex);
QString js8SuggestedBandName(int bandIndex);
QString js8SuggestedPlace(int bandIndex);

Js8Plan planJs8(const QString& text, int toneHz);
QString js8Decode(const float* samples, int count, int sampleRateHz, int toneHz);

class Js8Engine : public QObject {
  Q_OBJECT

public:
  explicit Js8Engine(QObject* parent = nullptr);

  void setArmed(bool armed);
  void setToneHz(int hz);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  void textReady(const QString& text);

private:
  bool armed_ = false;
  int toneHz_ = 1500;
  int rate_ = 48000;
  std::vector<float> pending_;
  QString last_;
};
