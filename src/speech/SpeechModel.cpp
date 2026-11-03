// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "speech/SpeechModel.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace {

const QString kBase = QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/");

QString partPath(const SpeechModelInfo& model) {
  return speechModelPath(model) + QStringLiteral(".part");
}

}  // namespace

QList<SpeechModelInfo> speechModels() {
  return {
      {QStringLiteral("tiny.en"), QStringLiteral("Tiny, fastest"), QStringLiteral("ggml-tiny.en.bin"),
       kBase + QStringLiteral("ggml-tiny.en.bin"), 77704715,
       QByteArrayLiteral("921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f")},
      {QStringLiteral("base.en"), QStringLiteral("Base, recommended"), QStringLiteral("ggml-base.en.bin"),
       kBase + QStringLiteral("ggml-base.en.bin"), 147964211,
       QByteArrayLiteral("a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002")},
      {QStringLiteral("small.en"), QStringLiteral("Small, most accurate"), QStringLiteral("ggml-small.en.bin"),
       kBase + QStringLiteral("ggml-small.en.bin"), 487614201,
       QByteArrayLiteral("c6138d6d58ecc8322097e0f987c32f1be8bb0a18532a3f88f734d1bbf9c41e5d")},
  };
}

SpeechModelInfo speechModel(const QString& id) {
  for (const SpeechModelInfo& model : speechModels()) {
    if (model.id == id)
      return model;
  }
  return {};
}

QString speechModelDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/speech-models");
}

QString speechModelPath(const SpeechModelInfo& model) {
  return speechModelDir() + QLatin1Char('/') + model.file;
}

bool speechModelInstalled(const SpeechModelInfo& model) {
  if (model.id.isEmpty())
    return false;
  const QFileInfo info(speechModelPath(model));
  return info.isFile() && info.size() == model.bytes;
}

bool removeSpeechModel(const SpeechModelInfo& model) {
  if (model.id.isEmpty())
    return true;
  QFile::remove(partPath(model));
  QFile::remove(speechModelPath(model));
  return !QFileInfo::exists(speechModelPath(model)) && !QFileInfo::exists(partPath(model));
}

QString speechModelSize(qint64 bytes) {
  return QStringLiteral("%1 MB").arg(double(bytes) / 1e6, 0, 'f', 1);
}

SpeechModelDownload::SpeechModelDownload(QObject* parent)
    : QObject(parent), net_(new QNetworkAccessManager(this)) {}

SpeechModelDownload::~SpeechModelDownload() {
  cancel();
}

bool SpeechModelDownload::start(const SpeechModelInfo& model) {
  if (busy() || model.id.isEmpty())
    return false;
  if (!QDir().mkpath(speechModelDir()))
    return false;
  model_ = model;
  received_ = 0;
  cancelled_ = false;
  hash_.reset();
  part_.setFileName(partPath(model));
  if (!part_.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  QNetworkRequest request{QUrl(model.url)};
  request.setRawHeader("User-Agent", QByteArrayLiteral("STD/") + STD_VERSION);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  reply_ = net_->get(request);
  connect(reply_, &QNetworkReply::readyRead, this, &SpeechModelDownload::onData);
  connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
    emit progress(got, total > 0 ? total : model_.bytes);
  });
  connect(reply_, &QNetworkReply::finished, this, &SpeechModelDownload::onDone);
  return true;
}

void SpeechModelDownload::cancel() {
  if (!reply_)
    return;
  cancelled_ = true;
  reply_->abort();  // finished() follows and cleans up
}

void SpeechModelDownload::onData() {
  if (!reply_)
    return;
  const QByteArray chunk = reply_->readAll();
  received_ += chunk.size();
  hash_.addData(chunk);
  if (part_.write(chunk) != chunk.size()) {
    reply_->abort();
  }
}

void SpeechModelDownload::fail(const QString& message) {
  part_.close();
  QFile::remove(part_.fileName());
  emit finished(model_.id, false, message);
}

void SpeechModelDownload::onDone() {
  if (!reply_)
    return;
  if (reply_->error() == QNetworkReply::NoError && reply_->bytesAvailable() > 0)
    onData();
  QNetworkReply* reply = reply_;
  reply_ = nullptr;
  reply->deleteLater();
  if (cancelled_) {
    fail(QStringLiteral("Download cancelled."));
    return;
  }
  if (reply->error() != QNetworkReply::NoError) {
    fail(part_.error() != QFileDevice::NoError
             ? QStringLiteral("Could not write the model file: %1").arg(part_.errorString())
             : QStringLiteral("Download failed: %1").arg(reply->errorString()));
    return;
  }
  part_.close();
  if (received_ != model_.bytes) {
    fail(QStringLiteral("Download was the wrong size (%1 of %2 bytes). Try again.")
             .arg(received_)
             .arg(model_.bytes));
    return;
  }
  if (hash_.result().toHex() != model_.sha256) {
    fail(QStringLiteral("Download did not match its checksum, so it was thrown away. Try again."));
    return;
  }
  const QString final = speechModelPath(model_);
  QFile::remove(final);
  if (!QFile::rename(part_.fileName(), final)) {
    fail(QStringLiteral("Could not move the model into place."));
    return;
  }
  emit finished(model_.id, true, QStringLiteral("Speech model %1 installed.").arg(model_.id));
}
