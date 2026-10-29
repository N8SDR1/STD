// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {

QString releasesApiUrl() {
  return QStringLiteral("https://api.github.com/repos/%1/%2/releases")
      .arg(QStringLiteral(STD_REPO_OWNER), QStringLiteral(STD_REPO_NAME));
}

QList<int> parseVersion(const QString& text) {
  static const QRegularExpression re(QStringLiteral("v?(\\d+)\\.(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
  const QRegularExpressionMatch match = re.match(text);
  if (!match.hasMatch())
    return {};
  return {match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt(),
          match.captured(4).isEmpty() ? 0 : match.captured(4).toInt()};
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

bool UpdateChecker::isNewer(const QString& remoteTag, const QString& localVer) {
  const QList<int> remote = parseVersion(remoteTag);
  const QList<int> local = parseVersion(localVer);
  if (remote.size() != 4 || local.size() != 4)
    return false;
  for (int i = 0; i < 4; ++i) {
    if (remote.at(i) != local.at(i))
      return remote.at(i) > local.at(i);
  }
  return false;
}

void UpdateChecker::check() {
  QNetworkRequest request{QUrl(releasesApiUrl())};
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", QByteArrayLiteral("STD/") + STD_VERSION);
  request.setTransferTimeout(8000);

  QNetworkReply* reply = nam_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit checkFailed(reply->errorString());
      return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray()) {
      emit checkFailed(QStringLiteral("Unexpected response from GitHub."));
      return;
    }
    QString bestTag;
    QString bestUrl;
    QString bestBody;
    const QString current = QStringLiteral(STD_VERSION);
    for (const QJsonValue& value : doc.array()) {
      const QJsonObject object = value.toObject();
      if (object.value(QStringLiteral("draft")).toBool())
        continue;
      const QString tag = object.value(QStringLiteral("tag_name")).toString();
      if (tag.isEmpty())
        continue;
      if (isNewer(tag, current) && (bestTag.isEmpty() || isNewer(tag, bestTag))) {
        bestTag = tag;
        bestUrl = object.value(QStringLiteral("html_url")).toString();
        bestBody = object.value(QStringLiteral("body")).toString();
      }
    }
    if (bestTag.isEmpty()) {
      emit noUpdate();
      return;
    }
    if (bestUrl.isEmpty()) {
      bestUrl = QStringLiteral("https://github.com/%1/%2/releases")
                    .arg(QStringLiteral(STD_REPO_OWNER), QStringLiteral(STD_REPO_NAME));
    }
    emit updateAvailable(bestTag, bestUrl, bestBody);
  });
}
