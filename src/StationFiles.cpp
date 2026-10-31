// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "StationFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

bool stationSettingsKey(const QString& key) {
  return key.startsWith(QLatin1String("layout/")) || key == QLatin1String("dx/reportCall") ||
         key == QLatin1String("dx/reportGrid");
}

int copyStationSettings(const QSettings& from, QSettings& to, bool replaceLayouts) {
  QStringList incoming;
  bool hasLayout = false;
  const QStringList sourceKeys = from.allKeys();
  for (const QString& key : sourceKeys) {
    if (!stationSettingsKey(key))
      continue;
    incoming.append(key);
    if (key.startsWith(QLatin1String("layout/")))
      hasLayout = true;
  }
  if (incoming.isEmpty())
    return 0;
  if (replaceLayouts && hasLayout) {
    const QStringList oldKeys = to.allKeys();
    for (const QString& key : oldKeys) {
      if (key.startsWith(QLatin1String("layout/")))
        to.remove(key);
    }
  }
  for (const QString& key : incoming)
    to.setValue(key, from.value(key));
  to.sync();
  return incoming.size();
}

bool backupLogbookFile(const QString& sourcePath, const QString& destPath, QString* error) {
  const QString source = sourcePath.trimmed();
  const QString dest = destPath.trimmed();
  if (source.isEmpty() || !QFileInfo::exists(source)) {
    if (error)
      *error = QStringLiteral("There is no logbook file yet.");
    return false;
  }
  if (dest.isEmpty()) {
    if (error)
      *error = QStringLiteral("Pick a file for the logbook copy.");
    return false;
  }
  if (QFileInfo(source).absoluteFilePath().compare(QFileInfo(dest).absoluteFilePath(),
                                                   Qt::CaseInsensitive) == 0) {
    if (error)
      *error = QStringLiteral("Pick a different file. The logbook stays where it is.");
    return false;
  }
  QDir().mkpath(QFileInfo(dest).absolutePath());
  if (QFileInfo::exists(dest) && !QFile::remove(dest)) {
    if (error)
      *error = QStringLiteral("Could not replace the backup file.");
    return false;
  }
  if (!QFile::copy(source, dest)) {
    if (error)
      *error = QStringLiteral("Could not copy the logbook.");
    return false;
  }
  return true;
}

bool exportAdifForLogger(const QString& sourcePath, const QString& destPath, int* records,
                         QString* error) {
  if (records)
    *records = 0;
  const QString dest = destPath.trimmed();
  if (dest.isEmpty()) {
    if (error)
      *error = QStringLiteral("Pick a file for the ADIF export.");
    return false;
  }
  QByteArray body;
  const QString source = sourcePath.trimmed();
  if (!source.isEmpty() && QFileInfo::exists(source)) {
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
      if (error)
        *error = QStringLiteral("Could not read the logbook.");
      return false;
    }
    body = in.readAll();
  }
  const int eoh = body.toUpper().indexOf("<EOH>");
  QByteArray logged = eoh < 0 ? body : body.mid(eoh + 5);
  int count = 0;
  const QByteArray upper = logged.toUpper();
  for (int pos = 0; (pos = upper.indexOf("<EOR>", pos)) >= 0; pos += 5)
    ++count;
  QByteArray out = QByteArrayLiteral("STD ADIF export\n<ADIF_VER:5>3.1.0\n<PROGRAMID:3>STD\n<EOH>\n");
  out += logged;
  if (!logged.isEmpty() && !logged.endsWith('\n'))
    out += '\n';
  QDir().mkpath(QFileInfo(dest).absolutePath());
  QFile file(dest);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error)
      *error = QStringLiteral("Could not write the ADIF export.");
    return false;
  }
  if (file.write(out) != out.size()) {
    if (error)
      *error = QStringLiteral("Could not write the ADIF export.");
    return false;
  }
  if (records)
    *records = count;
  return true;
}
