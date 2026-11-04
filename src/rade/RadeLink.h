// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Loads std_rade.dll (cdecl) from the executable directory. The DLL is
// built from freedv/rade_c c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9.
// Speech is 16 kHz float. Modem samples are 8 kHz real, nominal 1.
class RadeLink {
public:
  RadeLink();
  ~RadeLink();

  RadeLink(const RadeLink&) = delete;
  RadeLink& operator=(const RadeLink&) = delete;

  // Resolves the exports. False when the DLL is not beside the executable.
  bool load();
  bool loaded() const { return open_ != nullptr; }
  bool opened() const { return opened_; }

  // v2 selects RADE V2. noBpf sets RADE_NO_TX_BPF, which this rade_c
  // honors for V2 only.
  bool open(bool v2, bool noBpf);
  void close();

  int tx(const float* speech, int n, float* modem, int cap);
  int finish(float* modem, int cap);
  int rx(const float* modem, int n, float* speech, int cap);
  int sync();
  // Callsign for the V1 end-of-over, as FreeDV sends it. Ignored on V2.
  void setCallsign(const QString& call);
  // Last callsign read from an end-of-over; seq goes up with each new one.
  int heard(QString* call);
  float snr();
  int selftest();
  QString error() const { return error_; }

private:
  void note();

  void* radeModule_ = nullptr;
  void* shimModule_ = nullptr;
  bool opened_ = false;
  QString error_;

  using OpenFn = int (*)(int, int);
  using CloseFn = void (*)();
  using TxFn = int (*)(const float*, int, float*, int);
  using FinishFn = int (*)(float*, int);
  using RxFn = int (*)(const float*, int, float*, int);
  using SyncFn = int (*)();
  using SnrFn = float (*)();
  using TestFn = int (*)();
  using ErrFn = const char* (*)();
  using CallFn = void (*)(const char*);
  using HeardFn = int (*)(char*, int);
  OpenFn open_ = nullptr;
  CloseFn close_ = nullptr;
  TxFn tx_ = nullptr;
  FinishFn finish_ = nullptr;
  RxFn rx_ = nullptr;
  SyncFn sync_ = nullptr;
  SnrFn snr_ = nullptr;
  TestFn selftest_ = nullptr;
  ErrFn errorFn_ = nullptr;
  CallFn call_ = nullptr;
  HeardFn heard_ = nullptr;
};

// Offscreen self-test. 0 means rade_tx produced energy and rade_rx accepted it.
int radeCodecSelfTest();
