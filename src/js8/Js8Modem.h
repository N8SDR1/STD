// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <string>
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

// One Normal frame is 500 ms of silence plus 79 symbols (13.14 s).
// JS8Call only copies a frame that starts on a 15 s UTC boundary.
int js8FrameSamples();
// maxFrames 0 plans every frame back to back. Pass 1 to take a single
// 15 s part. consumed, when set, receives how many characters of text
// went into the plan. messageStart is false for a later part of the
// same message so the frame type stays a continuation.
// myCall (Options → User) turns on JS8Call message structure for the first
// frame: "HB" / "@HB HEARTBEAT", "CQ ..." and "W4KUS SNR?" style directed
// messages go out as their own frame types. Without it every frame is text.
Js8Plan planJs8(const QString& text, int toneHz, int maxFrames = 0, int* consumed = nullptr,
                bool messageStart = true, const QString& myCall = QString(),
                const QString& myGrid = QString());

// The first frame of a message, when the text has JS8Call structure.
// bits is empty for plain text. consumed is how many characters of text the
// frame carries; display is how JS8Call prints it ("N8SDR: W4KUS SNR?").
struct Js8Header {
  std::string bits;
  int consumed = 0;
  QString display;
  QString toCall;
};
Js8Header js8Header(const QString& text, const QString& myCall, const QString& myGrid);
// snrDb, when set, receives the frame SNR in a 2500 Hz bandwidth, the
// same reference WSJT-X and JS8Call print. A clean local frame is positive.
QString js8Decode(const float* samples, int count, int sampleRateHz, int toneHz, int* snrDb = nullptr);

// One frame from its 12-character JS8Call payload and 3 transmission bits
// (1 first, 2 last, 4 data). The self-test uses it to send heartbeat,
// directed, compound and compressed frames that STD does not build itself.
std::vector<float> js8FrameAudio(const QString& frame12, int type, int toneHz);

struct Js8Heard {
  int freqHz = 0;
  QString text;
  int snrDb = 0;
  bool last = false;  // the frame ends its message
};
// One receive period, the way Js8Engine decodes it. samples are 48 kHz and
// start leadMs before the 15 s UTC boundary. The cursor frame is tried
// first, then every JS8 signal in 400-2650 Hz, each at its own time offset.
std::vector<Js8Heard> js8DecodePeriod(const float* samples, int count, int sampleRateHz, int toneHz,
                                      int leadMs);

class Js8Engine : public QObject {
  Q_OBJECT

public:
  explicit Js8Engine(QObject* parent = nullptr);

  void setArmed(bool armed);
  void setToneHz(int hz);
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

signals:
  // freqHz is that station's audio frequency, not the cursor.
  void textReady(const QString& text, int snrDb, int freqHz);
  // The same frame with its end-of-message flag, for the JS8 activity lists.
  void frameHeard(const QString& text, int snrDb, int freqHz, bool last);

private:
  void publish(qint64 periodStart, int generation, const std::vector<Js8Heard>& hits);

  int generation_ = 0;
  bool armed_ = false;
  int toneHz_ = 1500;
  int rate_ = 48000;
  std::vector<float> ring_;
  qint64 total_ = 0;
  qint64 newestMs_ = 0;
  qint64 decodedPeriod_ = -1;
  QStringList heard_;
};
