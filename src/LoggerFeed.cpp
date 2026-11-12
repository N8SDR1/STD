// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LoggerFeed.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {
// Every five minutes while on; a logged QSO asks sooner.
constexpr int kEveryMs = 5 * 60 * 1000;
}  // namespace

LoggerFeed::LoggerFeed(QObject* parent) : QObject(parent) {
  net_ = new QNetworkAccessManager(this);
  timer_ = new QTimer(this);
  timer_->setInterval(kEveryMs);
  connect(timer_, &QTimer::timeout, this, &LoggerFeed::refresh);
}

QString LoggerFeed::url(int port) {
  return QStringLiteral("http://127.0.0.1:%1/api/adif/export").arg(port);
}

QString LoggerFeed::cachePath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/log/sdrloggerplus.adi");
}

void LoggerFeed::setEnabled(bool on) {
  if (on == enabled_)
    return;
  enabled_ = on;
  if (on) {
    timer_->start();
    refresh();
  } else {
    timer_->stop();
  }
}

void LoggerFeed::setPort(int port) {
  port = std::clamp(port, 1, 65535);
  if (port == port_)
    return;
  port_ = port;
  if (enabled_)
    refresh();
}

void LoggerFeed::refresh() {
  if (!enabled_ || busy_ || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  busy_ = true;
  QNetworkRequest request{QUrl(url(port_))};
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("STD"));
  request.setTransferTimeout(60000);
  QNetworkReply* reply = net_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    busy_ = false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || status != 200) {
      lastError_ = QStringLiteral("SDRLogger+ did not answer on port %1 (%2). Using the last copy.")
                       .arg(port_)
                       .arg(reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                     : QStringLiteral("HTTP %1").arg(status));
      emit failed(lastError_);
      return;
    }
    if (!body.toUpper().contains("<EOH>") && !body.toUpper().contains("<EOR>")) {
      lastError_ = QStringLiteral("SDRLogger+ on port %1 sent something that is not ADIF. Using the last copy.")
                       .arg(port_);
      emit failed(lastError_);
      return;
    }
    // Unchanged since the last fetch: nothing to read again.
    {
      QFile last(cachePath());
      if (last.size() == body.size() && last.open(QIODevice::ReadOnly) && last.readAll() == body) {
        lastError_.clear();
        fetchedAt_ = QDateTime::currentDateTimeUtc();
        return;
      }
    }
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size() || !file.commit()) {
      lastError_ = QStringLiteral("Could not save the SDRLogger+ log to %1.").arg(cachePath());
      emit failed(lastError_);
      return;
    }
    lastError_.clear();
    fetchedAt_ = QDateTime::currentDateTimeUtc();
    records_ = int(body.toUpper().count("<EOR>"));
    emit updated();
  });
}
