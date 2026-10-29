// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Same notice as Lyra-SDR-cpp (src/updatechecker.cpp): ask GitHub for
// every release, including pre-releases, and keep the highest version
// that is newer than this build. The window owns the dialog and the
// toolbar chip. This does not key and does not send START.

inline QString stdVersionName() {
  return QStringLiteral("testing phase \u00B7 highly transmissible");
}

class UpdateChecker : public QObject {
  Q_OBJECT

public:
  explicit UpdateChecker(QObject* parent = nullptr);

  void check();

  // remoteTag "v0.1.0", localVer "0.1.0". A leading v is ignored.
  // Unparseable input is not newer.
  static bool isNewer(const QString& remoteTag, const QString& localVer);

signals:
  void updateAvailable(const QString& tag, const QString& url, const QString& body);
  void noUpdate();
  void checkFailed(const QString& reason);

private:
  QNetworkAccessManager* nam_ = nullptr;
};
