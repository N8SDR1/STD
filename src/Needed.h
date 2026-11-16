// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Needed stations for the decode list: a new DXCC entity, an entity new on
// this band, a new grid, or a call already worked. Entities come from
// AD1C's cty.dat (country-files.com); what has been worked comes from the
// ADIF log. Worked only: confirmations are not counted. Nothing here keys.

#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>

// AD1C cty.dat: entity by exact call (=CALL) or the longest prefix.
class CtyTable {
public:
  // Parses cty.dat. Returns false when nothing could be read.
  bool load(const QByteArray& data);
  bool isEmpty() const { return prefix_.isEmpty(); }
  int entities() const { return entities_; }
  // The entity name for a call ("Mauritius"), or empty when unknown.
  QString entity(const QString& call) const;

private:
  QHash<QString, QString> exact_;
  QHash<QString, QString> prefix_;
  int longest_ = 0;
  int entities_ = 0;
};

// What the ADIF log holds, for the needed marks.
struct WorkedIndex {
  QSet<QString> entities;     // entity names
  QSet<QString> entityBands;  // "entity|20m"
  QSet<QString> grids;        // first four characters, upper case
  QSet<QString> gridBands;    // "EM79|20m"
  QSet<QString> calls;        // home calls (LZ2HV for LZ2HV/P)
  QSet<QString> callBands;    // "LZ2HV|20m"
  void clear();
  void add(const CtyTable& cty, const QString& call, const QString& band, const QString& grid);
  // Reads every QSO of an ADIF file (CALL, BAND or FREQ, GRIDSQUARE).
  void addAdif(const CtyTable& cty, const QByteArray& adif);
};

enum class Need { None, Worked, GridBand, Grid, Band, Entity };

// The ADIF band ("20m", "70cm") for a frequency in MHz, or empty.
QString adifBandForMhz(double mhz);

// The station an FT-style message is from, and its grid if it gives one:
// "CQ DX K1ABC FN42" is K1ABC FN42; "N8SDR KC2NDA R-05" is KC2NDA.
bool messageStation(const QString& message, QString* call, QString* grid);

// The call without portable parts: LZ2HV/P and VE3/LZ2HV are LZ2HV.
QString homeCallOf(const QString& call);

// How much a station is needed on band, the ADIF band STD is on now.
// Entity, then entity on this band, then grid, then grid on this band;
// Worked when the call is already in the log on this band.
Need classifyNeed(const CtyTable& cty, const WorkedIndex& log, const QString& call, const QString& grid,
                  const QString& band);

// Short tag for the decode list: NEW, BAND, GRID, GRID+BAND, B4, or empty.
QString needTag(Need need);
