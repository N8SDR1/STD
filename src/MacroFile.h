// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QString>
#include <QVector>

// Fldigi 4.2.06 macro definition file. One file is the active set.
// Twelve buttons are on screen. The bank button (1..4) picks which
// twelve of the 48. This does not key and does not send START.

inline constexpr int kMacroCount = 48;
inline constexpr int kMacrosPerBank = 12;

struct MacroSlot {
  QString name;
  QString text;
};

// One row in Fldigi's Select Tag list. An empty tag is a separator.
struct MacroTagRow {
  QString tag;
  QString note;
};

// Forty-eight empty named slots, F-1 through F-48. This is a new set.
QVector<MacroSlot> blankMacroSet();

// True when the first line is a Fldigi macro definition file.
// Older files without "extended" shift numbers above 9 by +2, as Fldigi does.
bool loadMacroDefinition(const QString& body, QVector<MacroSlot>* rows, QString* error);

QString saveMacroDefinition(const QVector<MacroSlot>& rows);

// One station tag that is not the callsign or the locator.
// Callsign stays dx/reportCall. Locator stays dx/reportGrid.
struct MacroUserField {
  QString tag;
  QString key;
  QString objectName;
  QString label;
  QString note;
};

// Copies a macro into a transmit box. <TX> and <RX> are removed.
// <MYCALL> reads the callsign. <MYLOC> and <MYGRID> read the locator.
// userTags supplies the other station tags, keyed by the tag text.
// The stored macro is unchanged.
QString macroInsertText(const QString& body, const QString& call, const QString& locator,
                        const QHash<QString, QString>& userTags = {});

// Station tags that need a field. Callsign and locator are not in this list.
QVector<MacroUserField> macroUserFields();

// Fldigi 4.2 macro editor tag list (<FREQ>, <MYCALL>, and the rest).
QVector<MacroTagRow> macroTagList();

// What a macro fills in from. Tag keys are "<NAME>" style, upper case.
struct MacroContext {
  QString myCall;
  QString myLoc;
  QHash<QString, QString> tags;  // station fields and the QSO bar
  QString mode;                  // "RTTY", "BPSK31", ...
  QString band;                  // "40m"
  QString freqKhz;               // dial, "7074.000"
  QString version;               // "STD 0.1.4"
  int serial = 1;                // <CNTR>
  QDateTime nowUtc;              // invalid: the current time
};

// A macro worked out for one press. Fldigi tags decide what it does:
// <TX> keys, <RX> returns to receive after the text, <CLRTX> replaces the
// transmit box instead of adding to it. <LOG>, <CLRQSO>, <CLRRX>, <INCR>
// and <DECR> run when a keyed macro finishes sending, or at once when the
// macro does not key. <TIMER:NN> sends it again NN seconds after it ends.
// {CALL} style tags work too. <NAME|OM> sends OM when Name is empty.
struct MacroPlan {
  QString text;             // what goes into the transmit box
  bool keys = false;        // <TX>
  bool toggle = false;      // <TX/RX>
  bool backToRx = false;    // <RX>
  bool clearTx = false;     // <CLRTX>
  bool clearRx = false;     // <CLRRX>
  bool log = false;         // <LOG> or <LNW>
  QString logNote;          // <LOG:msg>
  bool clearQso = false;    // <CLRQSO> or <CLRLOG>
  int serialStep = 0;       // <INCR> +1, <DECR> -1
  int repeatSeconds = 0;    // <TIMER:NN>
  QStringList missing;      // other-station fields that were empty, e.g. "Call"
  QStringList skipped;      // Fldigi tags STD does not run, removed from the text
};

MacroPlan planMacro(const QString& body, const MacroContext& context);

// The button label: Fldigi's @>| style marks become ▶, and tags in the
// name fill in ("Reply <CALL>" shows "Reply K8AB").
QString macroButtonLabel(const QString& name, const MacroContext& context, bool keys);
