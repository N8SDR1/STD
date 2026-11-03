// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// The speech-to-text models STD can fetch. None ship with STD; one is
// downloaded only when the operator picks it, so the install stays small.
struct SpeechModelInfo {
  QString id;        // settings value, e.g. "base.en"
  QString label;     // shown in the picker
  QString file;      // file name on disk and on the server
  QString url;       // direct download
  qint64 bytes = 0;  // exact size
  QByteArray sha256; // lower-case hex
};

QList<SpeechModelInfo> speechModels();
// The model with this id, or an empty info (id empty) when unknown.
SpeechModelInfo speechModel(const QString& id);
// Where models are kept: <AppData>/speech-models.
QString speechModelDir();
QString speechModelPath(const SpeechModelInfo& model);
// Present at the full size. The checksum is checked when it downloads.
bool speechModelInstalled(const SpeechModelInfo& model);
// Deletes the model and any half-finished download. True when nothing is left.
bool removeSpeechModel(const SpeechModelInfo& model);
// "77.7 MB"
QString speechModelSize(qint64 bytes);

// Downloads one model to a .part file, checks size and SHA-256, then
// renames it into place. One download at a time.
class SpeechModelDownload : public QObject {
  Q_OBJECT

public:
  explicit SpeechModelDownload(QObject* parent = nullptr);
  ~SpeechModelDownload() override;

  bool busy() const { return reply_ != nullptr; }
  QString modelId() const { return model_.id; }
  // False when another download is running or the folder cannot be written.
  bool start(const SpeechModelInfo& model);
  void cancel();

signals:
  void progress(qint64 received, qint64 total);
  // ok false with a reason when it failed or was cancelled.
  void finished(const QString& modelId, bool ok, const QString& message);

private:
  void onData();
  void onDone();
  void fail(const QString& message);

  QNetworkAccessManager* net_ = nullptr;
  QPointer<QNetworkReply> reply_;
  SpeechModelInfo model_;
  QFile part_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
  qint64 received_ = 0;
  bool cancelled_ = false;
};
