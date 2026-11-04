// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeLink.h"

#include <QCoreApplication>
#include <QtGlobal>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

RadeLink::RadeLink() = default;

RadeLink::~RadeLink() {
  close();
#ifdef _WIN32
  if (shimModule_)
    FreeLibrary(static_cast<HMODULE>(shimModule_));
  if (radeModule_)
    FreeLibrary(static_cast<HMODULE>(radeModule_));
#endif
  shimModule_ = nullptr;
  radeModule_ = nullptr;
  open_ = nullptr;
}

bool RadeLink::load() {
  if (open_ && tx_ && rx_ && finish_ && selftest_)
    return true;
#ifdef _WIN32
  const QString dir = QCoreApplication::applicationDirPath();
  const auto wide = [&dir](const char* name) { return (dir + QLatin1Char('/') + QLatin1String(name)).toStdWString(); };
  if (!radeModule_)
    radeModule_ = LoadLibraryW(wide("librade.dll").c_str());
  if (!shimModule_)
    shimModule_ = LoadLibraryW(wide("std_rade.dll").c_str());
  if (!shimModule_) {
    error_ = QStringLiteral("std_rade.dll did not load (%1). TX stays off. START was not sent.")
                 .arg(GetLastError());
    return false;
  }
  auto* mod = static_cast<HMODULE>(shimModule_);
  open_ = reinterpret_cast<OpenFn>(GetProcAddress(mod, "std_rade_open"));
  close_ = reinterpret_cast<CloseFn>(GetProcAddress(mod, "std_rade_close"));
  tx_ = reinterpret_cast<TxFn>(GetProcAddress(mod, "std_rade_tx"));
  finish_ = reinterpret_cast<FinishFn>(GetProcAddress(mod, "std_rade_finish"));
  rx_ = reinterpret_cast<RxFn>(GetProcAddress(mod, "std_rade_rx"));
  sync_ = reinterpret_cast<SyncFn>(GetProcAddress(mod, "std_rade_sync"));
  snr_ = reinterpret_cast<SnrFn>(GetProcAddress(mod, "std_rade_snr"));
  foff_ = reinterpret_cast<SnrFn>(GetProcAddress(mod, "std_rade_freq_offset"));
  selftest_ = reinterpret_cast<TestFn>(GetProcAddress(mod, "std_rade_selftest"));
  errorFn_ = reinterpret_cast<ErrFn>(GetProcAddress(mod, "std_rade_error"));
  call_ = reinterpret_cast<CallFn>(GetProcAddress(mod, "std_rade_set_callsign"));
  heard_ = reinterpret_cast<HeardFn>(GetProcAddress(mod, "std_rade_heard"));
  options_ = reinterpret_cast<OptionsFn>(GetProcAddress(mod, "std_rade_set_options"));
  auto rateFn = reinterpret_cast<SyncFn>(GetProcAddress(mod, "std_rade_speech_rate"));
  // The speaker plays 48 kHz. An older DLL that speaks 16 kHz would play
  // three times too fast, so refuse it.
  if (!rateFn || rateFn() != 48000) {
    error_ = QStringLiteral("std_rade.dll is an older build. TX stays off. START was not sent.");
    open_ = nullptr;
    return false;
  }
  if (!open_ || !close_ || !tx_ || !finish_ || !rx_ || !sync_ || !snr_ || !selftest_) {
    error_ = QStringLiteral("std_rade.dll is missing an export. TX stays off. START was not sent.");
    open_ = nullptr;
    return false;
  }
  return true;
#else
  error_ = QStringLiteral("RADE is built for Windows. START was not sent.");
  return false;
#endif
}

void RadeLink::note() {
  if (errorFn_)
    error_ = QString::fromUtf8(errorFn_());
}

bool RadeLink::open(bool v2, bool noBpf) {
  if (!load())
    return false;
  close();
  const int rc = open_(v2 ? 1 : 0, noBpf ? 1 : 0);
  note();
  opened_ = rc == 0;
  return opened_;
}

void RadeLink::close() {
  if (opened_ && close_)
    close_();
  opened_ = false;
}

int RadeLink::tx(const float* speech, int n, float* modem, int cap) {
  if (!opened_ || !tx_)
    return -1;
  const int rc = tx_(speech, n, modem, cap);
  if (rc < 0)
    note();
  return rc;
}

int RadeLink::finish(float* modem, int cap) {
  if (!opened_ || !finish_)
    return -1;
  const int rc = finish_(modem, cap);
  if (rc < 0)
    note();
  return rc;
}

int RadeLink::rx(const float* modem, int n, float* speech, int cap) {
  if (!opened_ || !rx_)
    return -1;
  const int rc = rx_(modem, n, speech, cap);
  if (rc < 0)
    note();
  return rc;
}

int RadeLink::sync() {
  return opened_ && sync_ ? sync_() : 0;
}

void RadeLink::setCallsign(const QString& call) {
  if (call_)
    call_(call.trimmed().toUpper().toLatin1().constData());
}

void RadeLink::setOptions(bool denoise, bool wide) {
  if (options_)
    options_(denoise ? 1 : 0, wide ? 1 : 0);
}

int RadeLink::heard(QString* call) {
  if (!heard_)
    return 0;
  char buf[32] = {0};
  const int seq = heard_(buf, int(sizeof buf));
  if (call)
    *call = QString::fromLatin1(buf);
  return seq;
}

float RadeLink::freqOffset() {
  return opened_ && foff_ ? foff_() : 0.0f;
}

float RadeLink::snr() {
  return opened_ && snr_ ? snr_() : 0.0f;
}

int RadeLink::selftest() {
  if (!load() || !selftest_)
    return 1;
  const int rc = selftest_();
  note();
  opened_ = false;
  return rc;
}

int radeCodecSelfTest() {
  RadeLink link;
  if (!link.load()) {
    qCritical("%s", qPrintable(link.error()));
    return 1;
  }
  const int rc = link.selftest();
  if (rc != 0)
    qCritical("RADE codec round trip failed: %s", qPrintable(link.error()));
  return rc == 0 ? 0 : 1;
}
