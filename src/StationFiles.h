// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QSettings;

// Layouts and the callsign and locator already stored under Options → User.
// TCI host and port are not part of this copy. This does not add a field,
// does not key, and does not send AUDIO_START or the text command START.

bool stationSettingsKey(const QString& key);

// Copies those keys from `from` into `to`. When replaceLayouts is set,
// layout keys already in `to` are removed first, and only if `from` has
// at least one layout key. Returns how many keys were written. Zero means
// `from` had nothing to copy and `to` was left alone.
int copyStationSettings(const QSettings& from, QSettings& to, bool replaceLayouts);

// Exact copy of the logbook file. The original stays in place.
bool backupLogbookFile(const QString& sourcePath, const QString& destPath, QString* error);

// A complete ADIF file another logger can import. Records come from the
// logbook. The original stays in place. `records` is the number of QSOs.
bool exportAdifForLogger(const QString& sourcePath, const QString& destPath, int* records,
                         QString* error);
