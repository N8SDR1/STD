// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

struct ISpVoice;

// Reads received text aloud with a Windows voice, on a sound device the
// operator picks. It does not use TCI and never keys the radio.
//
// Text is spoken a phrase at a time: a whole line when it ends, a run of
// finished words on a long line, and whatever is left after a second with
// nothing new. When it has caught up it waits for more text.
class SpeechReader : public QObject {
  Q_OBJECT

public:
  struct Choice {
    QString id;    // SAPI token id, empty for the Windows default
    QString name;  // what the operator sees
  };

  // Installed Windows voices, including the newer OneCore voices.
  static QList<Choice> voices();
  // Sound output devices. The first entry is the Windows default.
  static QList<Choice> outputs();

  explicit SpeechReader(QObject* parent = nullptr);
  ~SpeechReader() override;

  // Empty ids are the Windows defaults. rate is -10..10, volume 0..100.
  void setVoice(const QString& voiceId);
  void setOutput(const QString& outputId);
  void setRate(int rate);
  void setVolume(int volume);
  void setMuted(bool muted);
  // Behind by more than a couple of phrases: drop the backlog and read the
  // newest text, so the voice stays with the band.
  void setKeepUp(bool on);
  void setSkipGarbled(bool on);

  // Received text, as it prints.
  void addText(const QString& text);
  // Stop talking now and drop anything waiting.
  void stop();
  // Speak a line straight away (the Test button). Ignores mute.
  void say(const QString& text);

  bool available() const;

private:
  void flush(bool all);
  void speak(const QString& text);
  bool ensureVoice();

  ISpVoice* voice_ = nullptr;
  bool comReady_ = false;
  bool triedVoice_ = false;
  QString voiceId_;
  QString outputId_;
  int rate_ = 0;
  int volume_ = 80;
  bool muted_ = false;
  bool keepUp_ = true;
  bool skipGarbled_ = true;
  QString pending_;
  QTimer idle_;
};

// Takes the text that is ready to speak off the front of buffer: through
// the last line end, else the finished words once there are eight, else
// everything when all is true. Empty when nothing is ready yet.
QString speechTakeReady(QString* buffer, bool all);

// Readies a phrase for the voice. Callsigns and signal reports are spelled
// out (N8SDR as N 8 S D R, 599 as 5 9 9). With skipGarbled, runs that are
// mostly symbols, as noise decodes are, are left out.
QString speechClean(const QString& text, bool skipGarbled);
