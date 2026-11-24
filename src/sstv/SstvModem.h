// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <vector>

// SSTV receive, written for STD from the published mode timings (the VIS
// code, the 1200 Hz sync, black at 1500 Hz and white at 2300 Hz). The mode is
// read from the VIS header in front of each picture. Line sync is tracked
// pulse by pulse, so a sound card that runs slightly fast or slow does not
// slant the picture. This path does not transmit and does not send START.

enum class SstvMode {
  None,
  Robot36,
  Robot72,
  MartinM1,
  MartinM2,
  ScottieS1,
  ScottieS2,
  ScottieDX,
  PD50,
  PD90,
  PD120,
  PD160,
  PD180,
  PD240,
  PD290,
};

// Every mode STD decodes, in menu order.
const std::vector<SstvMode>& sstvModes();
QString sstvModeName(SstvMode mode);
SstvMode sstvModeFromName(const QString& name);
int sstvVisCode(SstvMode mode);
SstvMode sstvModeFromVis(int code);
// Picture size in pixels.
int sstvWidth(SstvMode mode);
int sstvHeight(SstvMode mode);
// Transmit time in seconds, VIS header included.
double sstvSeconds(SstvMode mode);
// Picture rows fully sent after `seconds` on the air (VIS header included).
int sstvRowsSentAt(SstvMode mode, double seconds);

// The tones for a picture, VIS header first. The picture is scaled to the
// mode's size. Used by the self-test and, later, by transmit; this function
// only builds samples and never keys anything.
// toneShiftHz moves every tone, as a mistuned receiver would; tests only.
std::vector<float> sstvModulate(SstvMode mode, const QImage& picture, int sampleRateHz,
                                double amplitude = 0.5, double toneShiftHz = 0.0);

// A CW identification sent after the picture as audio: `gapMs` of silence,
// then `text` in Morse at `wpm` on a `toneHz` tone with 5 ms soft edges.
// Letters, digits and / only; anything else is skipped.
std::vector<float> sstvCwId(const QString& text, int wpm, int toneHz, int sampleRateHz, double amplitude = 0.5,
                            int gapMs = 500);

struct SstvResult {
  SstvMode mode = SstvMode::None;
  QImage image;
  int rows = 0;
  // Tuning error measured on the VIS leader, in Hz.
  double offsetHz = 0.0;
  // How many pictures started while decoding (a false header makes two).
  int starts = 0;
  QString endReason;
};

// Decode the first picture in a recording. For tests and files.
SstvResult sstvDecode(const float* samples, int count, int sampleRateHz);

// Pages where stations post the SSTV pictures they receive on 14.230 MHz,
// kept to ones that were showing recent pictures when last checked.
struct SstvWebGallery {
  const char* name;
  const char* place;
  const char* url;
};
const std::vector<SstvWebGallery>& sstvWebGalleries();

QString sstvRxDirectory();
void sstvSetRxDirectory(const QString& directory);

class SstvEngine : public QObject {
  Q_OBJECT

public:
  explicit SstvEngine(QObject* parent = nullptr);
  ~SstvEngine() override;

  // While disarmed, audio is ignored.
  void setArmed(bool armed);
  bool armed() const;
  // Save each finished picture to the RX folder. On by default.
  void setAutoSave(bool on);
  // Start a picture now in the given mode, without waiting for a VIS header.
  // For tuning in mid-picture; the lines then lock to the next sync pulses.
  void startNow(SstvMode mode);
  // Stop the current picture and wait for the next VIS header.
  void stop();
  void clear();
  void pushAudio(const float* samples, int count, int sampleRateHz);

  bool receiving() const;
  SstvMode mode() const;
  const QImage& image() const;
  int rowCount() const;
  double offsetHz() const;
  // Why the last picture ended: complete, no sync, a new picture, or Stop.
  QString endReason() const;
  // Saves the picture so far. Returns the file path, or empty on failure.
  QString saveNow();

signals:
  void pictureStarted(SstvMode mode);
  void imageChanged();
  void pictureFinished();
  void imageSaved(const QString& path);

private:
  struct Private;
  Private* d_;
};
