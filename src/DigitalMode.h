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

// fldigi feld::restart column rate. Transmit pixels per second are
// 14 times this. Receive columns stay 20 pixels high.
inline double hellColumnRate(const QString& mode) {
  if (mode == QLatin1String("Slow Hell"))
    return 2.1875;
  if (mode == QLatin1String("Feld Hell X5"))
    return 87.5;
  if (mode == QLatin1String("Feld Hell X9"))
    return 157.5;
  if (mode == QLatin1String("Hell 80"))
    return 35.0;
  return 17.5;
}

// Fldigi hell_bandwidth for the frequency-shift rows. 0 is amplitude Hell.
inline double hellFskShiftHz(const QString& mode) {
  if (mode == QLatin1String("FSK Hell"))
    return 122.5;
  if (mode == QLatin1String("FSK Hell-105"))
    return 55.0;
  if (mode == QLatin1String("Hell 80"))
    return 300.0;
  return 0.0;
}

// Fldigi keyboard text: one transmit box, a receive echo, CLR RX, and CLR TX.
// FT8, FT4, FT2, JT65, and Q65 stay on the MSHV sequencer. CW stays in Lyra.
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

// Fldigi QPSK. K=5 convolutional code, one dibit per varicode bit.
// 8PSK is a different constellation and is not this.
inline bool qpskMode(const QString& mode) {
  return mode.startsWith(QLatin1String("QPSK"), Qt::CaseInsensitive);
}

// 0 when the name is not a QPSK speed STD modulates. There is no QPSK1000.
inline double qpskBaud(const QString& mode) {
  if (!qpskMode(mode))
    return 0.0;
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

inline bool keyedPsk(const QString& mode) {
  return (pskMode(mode) && pskBaud(mode) > 0.0) || qpskBaud(mode) > 0.0;
}

// HF WEFAX receive. The name is the Fldigi menu label. It does not key.
inline bool wefaxMode(const QString& mode) {
  return mode.startsWith(QLatin1String("WEFAX"), Qt::CaseInsensitive);
}

// MSHV JT65. The generator tone stays near 1270 Hz. A is HF, B and C
// are the wider spacings. 60 second period.
inline bool jt65Mode(const QString& mode) {
  return mode == QLatin1String("JT65A") || mode == QLatin1String("JT65B") ||
         mode == QLatin1String("JT65C");
}

// MSHV mode65: A=1, B=2, C=4.
inline int jt65Mode65(const QString& mode) {
  if (mode.endsWith(QLatin1Char('C')))
    return 4;
  if (mode.endsWith(QLatin1Char('B')))
    return 2;
  return 1;
}

// Tone spacing times about 66 tones. A is ~178 Hz.
inline int jt65ChannelHz(const QString& mode) {
  if (mode == QLatin1String("JT65C"))
    return 720;
  if (mode == QLatin1String("JT65B"))
    return 360;
  if (mode == QLatin1String("JT65A"))
    return 180;
  return 0;
}

// MSHV Q65. Default period is 30 s. The tone follows the cursor.
inline bool q65Mode(const QString& mode) {
  return mode == QLatin1String("Q65A") || mode == QLatin1String("Q65B") ||
         mode == QLatin1String("Q65C") || mode == QLatin1String("Q65D");
}

// MSHV mq65: A=1, B=2, C=4, D=8.
inline int q65Mq65(const QString& mode) {
  if (mode.endsWith(QLatin1Char('D')))
    return 8;
  if (mode.endsWith(QLatin1Char('C')))
    return 4;
  if (mode.endsWith(QLatin1Char('B')))
    return 2;
  return 1;
}

// DecoderQ65 modid: A=14, B=15, C=16, D=17.
inline int q65ModId(const QString& mode) {
  if (mode.endsWith(QLatin1Char('D')))
    return 17;
  if (mode.endsWith(QLatin1Char('C')))
    return 16;
  if (mode.endsWith(QLatin1Char('B')))
    return 15;
  return 14;
}

inline int q65ChannelHz(const QString& mode) {
  if (mode == QLatin1String("Q65D"))
    return 1740;
  if (mode == QLatin1String("Q65C"))
    return 870;
  if (mode == QLatin1String("Q65B"))
    return 440;
  if (mode == QLatin1String("Q65A"))
    return 220;
  return 0;
}

inline bool sequencedFamily(const QString& mode) {
  return mode == QLatin1String("FT8") || mode == QLatin1String("FT4") ||
         mode == QLatin1String("FT2") || jt65Mode(mode) || q65Mode(mode);
}

inline bool keyboardTextMode(const QString& mode) {
  if (hellFamily(mode))
    return true;
  static const char* prefixes[] = {"RTTY",    "PSK",   "BPSK", "QPSK", "8PSK", "Olivia",
                                   "Contestia", "MT63", "MFSK", "Domino", "THOR", "IFKP",
                                   "Throb",     "JS8"};
  for (const char* prefix : prefixes) {
    if (mode.startsWith(QLatin1String(prefix), Qt::CaseInsensitive))
      return true;
  }
  return false;
}
