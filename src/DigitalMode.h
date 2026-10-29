// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Fldigi 4.2.06 Op Mode → Hell. "Hell" is the shared layout name.
inline bool hellFamily(const QString& mode) {
  return mode == QLatin1String("Hell") || mode == QLatin1String("Feld Hell") ||
         mode == QLatin1String("Slow Hell") || mode == QLatin1String("Feld Hell X5") ||
         mode == QLatin1String("Feld Hell X9") || mode == QLatin1String("FSK Hell") ||
         mode == QLatin1String("FSK Hell-105") || mode == QLatin1String("Hell 80");
}

// Fldigi keyboard text: one transmit box, a receive echo, CLR RX, and CLR TX.
// FT8, FT4, and FT2 stay on the MSHV sequencer. CW stays in Lyra.
// A later menu item is covered when its name starts with one of these
// (Olivia is not on the menu yet). Matching is case-insensitive.
inline bool rttyMode(const QString& mode) {
  return mode.startsWith(QLatin1String("RTTY"), Qt::CaseInsensitive);
}

// BPSK speeds. QPSK and 8PSK are a different constellation and are not these.
inline bool pskMode(const QString& mode) {
  if (mode.startsWith(QLatin1String("QPSK"), Qt::CaseInsensitive) ||
      mode.startsWith(QLatin1String("8PSK"), Qt::CaseInsensitive))
    return false;
  return mode.startsWith(QLatin1String("PSK"), Qt::CaseInsensitive) ||
         mode.startsWith(QLatin1String("BPSK"), Qt::CaseInsensitive);
}

// 0 when the name is not a BPSK speed STD modulates.
inline double pskBaud(const QString& mode) {
  if (!pskMode(mode))
    return 0.0;
  if (mode.contains(QLatin1String("1000")))
    return 1000.0;
  if (mode.contains(QLatin1String("500")))
    return 500.0;
  if (mode.contains(QLatin1String("250")))
    return 250.0;
  if (mode.contains(QLatin1String("125")))
    return 125.0;
  if (mode.contains(QLatin1String("63")))
    return 62.5;
  if (mode.contains(QLatin1String("31")))
    return 31.25;
  return 0.0;
}

// HF WEFAX receive. The name is the Fldigi menu label. It does not key.
inline bool wefaxMode(const QString& mode) {
  return mode.startsWith(QLatin1String("WEFAX"), Qt::CaseInsensitive);
}

inline bool keyboardTextMode(const QString& mode) {
  if (hellFamily(mode))
    return true;
  static const char* prefixes[] = {"RTTY",    "PSK",   "BPSK", "QPSK", "8PSK", "Olivia",
                                   "Contestia", "MT63", "MFSK", "Domino", "THOR", "IFKP",
                                   "Throb"};
  for (const char* prefix : prefixes) {
    if (mode.startsWith(QLatin1String(prefix), Qt::CaseInsensitive))
      return true;
  }
  return false;
}
