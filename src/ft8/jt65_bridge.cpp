// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MSHV JT65 and Q65. The algorithms stay in the MSHV tree (GPL-3,
// Hristov / WSJT Development Group). This file only constructs the
// slim DecoderMs and forwards the lines.

#include "ft8_api.h"

#include "decoderms_jt65.h"
#include "HvMsPlayer/libsound/HvGenQ65/gen_q65.h"

#include <QString>

#include <cmath>
#include <cstring>

namespace {

struct Jt65Slot {
  DecoderMs* ms = nullptr;
  Ft8LineFn fn = nullptr;
  void* user = nullptr;
  int reported = 0;
};

struct Q65Slot {
  DecoderQ65* dec = nullptr;
  Ft8LineFn fn = nullptr;
  void* user = nullptr;
  int reported = 0;
};

thread_local Jt65Slot gJt65;
thread_local Q65Slot gQ65;

void takeJt65(const QStringList& list) {
  if (!gJt65.fn || list.size() < 7)
    return;
  const QString text = list.at(6).trimmed();
  if (text.isEmpty())
    return;
  const QByteArray time = list.at(0).toUtf8();
  const QByteArray dt = list.at(3).toUtf8();
  const QByteArray message = text.toUtf8();
  const int db = list.at(2).toInt();
  const int freq = list.size() > 9 ? list.at(9).toInt() : 0;
  gJt65.fn(time.constData(), db, dt.constData(), message.constData(), freq, gJt65.user);
  gJt65.reported += 1;
}

void takeQ65(const QStringList& list) {
  if (!gQ65.fn || list.size() < 5)
    return;
  const QString text = list.at(4).trimmed();
  if (text.isEmpty())
    return;
  const QByteArray time = list.at(0).toUtf8();
  const QByteArray dt = list.at(2).toUtf8();
  const QByteArray message = text.toUtf8();
  const int db = list.at(1).toInt();
  const int freq = list.size() > 7 ? list.at(7).toInt() : 0;
  gQ65.fn(time.constData(), db, dt.constData(), message.constData(), freq, gQ65.user);
  gQ65.reported += 1;
}

DecoderMs* jt65Decoder() {
  if (gJt65.ms)
    return gJt65.ms;
  gJt65.ms = new DecoderMs(QString());
  QObject::connect(gJt65.ms, &DecoderMs::EmitDecodetText, gJt65.ms,
                   [](const QStringList& list, bool, bool) { takeJt65(list); }, Qt::DirectConnection);
  return gJt65.ms;
}

DecoderQ65* q65Decoder() {
  if (gQ65.dec)
    return gQ65.dec;
  gQ65.dec = new DecoderQ65(QString());
  gQ65.dec->SetStDecoderDeep(1);
  gQ65.dec->SetStApDecode(false);
  gQ65.dec->AvgDecodeChanged(false);
  QObject::connect(gQ65.dec, &DecoderQ65::EmitDecodetText, gQ65.dec,
                   [](const QStringList& list) { takeQ65(list); }, Qt::DirectConnection);
  return gQ65.dec;
}

int bounded(int count, int capacity) {
  if (count <= 0 || count > capacity)
    return 0;
  return count;
}

}  // namespace

void DecoderMs::SetBackColor() {
  f_back_color = !f_back_color;
  emit EmitBackColor(f_back_color);
}

void DecoderMs::zero_int_beg_end(int* d, int begin, int end) {
  if (!d)
    return;
  for (int i = begin; i < end; ++i)
    d[i] = 0;
}

void DecoderMs::prepare65(double nfqso, int ntol, QString time) {
  s_nfqso_all = nfqso;
  G_DfTolerance = ntol;
  s_mousebutton = 0;
  s_bVHF_jt65 = false;
  s_decoder_deep = 1;
  s_avg_jt65 = false;
  s_deep_search_jt65 = false;
  s_lapon = false;
  s_time = time.isEmpty() ? QStringLiteral("000000") : time;
}

DecoderMs::DecoderMs(QString) {
  pi = 4.0 * std::atan(1.0);
  twopi = 8.0 * std::atan(1.0);
  DEC_SAMPLE_RATE = 12000.0;
  G_DfTolerance = 400;
  G_MinSigdB = 1;
  s_decoder_deep = 1;
  s_bVHF_jt65 = false;
  s_deep_search_jt65 = false;
  s_avg_jt65 = false;
  s_lapon = false;
  s_nQSOProgress = 0;
  s_max65_cand_for_dec = 8;
  s_nfqso_all = 1270.46;
  s_mousebutton = 0;
  s_time = QStringLiteral("000000");
  s_fopen = false;
  f_back_color = false;
  first_symspec65 = true;
  first_subtract65 = true;
  first_avg65 = true;
  clearave_jt65 = false;
  s_aggres_lev_deeps = 95;
  s_aggres_lev_ftd = 5;
  s1_ofs_jt65 = 256 + 20;
  thresh0_jt65 = 0.0;
  std::memset(ref_jt65, 0, sizeof(ref_jt65));
  std::memset(w_symspec65, 0, sizeof(w_symspec65));
  std::memset(s1_jt65_, 0, sizeof(s1_jt65_));
  std::memset(s3a_jt65_, 0, sizeof(s3a_jt65_));
  std::memset(mrs_jt65, 0, sizeof(mrs_jt65));
  std::memset(mrs2_jt65, 0, sizeof(mrs2_jt65));
  std::memset(param_jt65, 0, sizeof(param_jt65));
  std::memset(correct_jt65, 0, sizeof(correct_jt65));
  std::memset(cw_jt65, 0, sizeof(cw_jt65));
  std::memset(s1save_jt65_, 0, sizeof(s1save_jt65_));
  std::memset(s3save_jt65_, 0, sizeof(s3save_jt65_));
  std::memset(iutc_jt65, 0, sizeof(iutc_jt65));
  std::memset(nfsave_jt65, 0, sizeof(nfsave_jt65));
  std::memset(syncsave_jt65, 0, sizeof(syncsave_jt65));
  std::memset(dtsave_jt65, 0, sizeof(dtsave_jt65));
  std::memset(nflipsave_jt65, 0, sizeof(nflipsave_jt65));
  std::memset(cused_jt65, 0, sizeof(cused_jt65));
  std::memset(iused_jt65, 0, sizeof(iused_jt65));
  std::memset(apsymbols_jt65, 0, sizeof(apsymbols_jt65));
  pomAll.initPomAll();
  TGen65 = new Gen65();
  TGen65->setup65();
  DecQ65 = nullptr;
  DecFt2_0 = nullptr;
}

DecoderMs::~DecoderMs() {
  delete TGen65;
  TGen65 = nullptr;
}

extern "C" __declspec(dllexport) int jt65_generate(const char* message, int submode, int* samples,
                                                   int capacity) {
  if (!message || !message[0] || !samples || capacity < 60 * 48000)
    return 0;
  if (submode != 1 && submode != 2 && submode != 4)
    submode = 1;
  static Gen65 gen;
  if (std::fabs(gen.pr[0]) != 1.0)
    gen.setup65();
  // MSHV gen65 ignores the cursor. Tone 0 is (118 * 11025 / 1024) Hz.
  // packmsg walks a Fortran character*22. Without the trailing blanks it
  // never sees the third field separator and stores the CQ as plain text,
  // then unpacktext writes past an empty QString.
  char text[23];
  std::memset(text, ' ', 22);
  text[22] = '\0';
  const int ncopy = int(std::strlen(message));
  std::memcpy(text, message, ncopy < 22 ? ncopy : 22);
  const int count = gen.gen65(text, submode, 1, 1.0, 48000.0, samples);
  return bounded(count, capacity);
}

extern "C" __declspec(dllexport) int q65_generate(const char* message, double toneHz, int submode,
                                                  int periodSec, int* samples, int capacity) {
  if (!message || !message[0] || !samples)
    return 0;
  if (periodSec != 15 && periodSec != 30 && periodSec != 60 && periodSec != 120)
    periodSec = 30;
  if (capacity < periodSec * 48000)
    return 0;
  if (submode != 1 && submode != 2 && submode != 4 && submode != 8)
    submode = 1;
  if (toneHz < 200.0)
    toneHz = 200.0;
  if (toneHz > 4000.0)
    toneHz = 4000.0;
  static GenQ65 gen(false);
  const int count = gen.genq65(QString::fromUtf8(message), samples, 48000.0, toneHz, submode, periodSec);
  return bounded(count, capacity);
}

extern "C" __declspec(dllexport) int jt65_decode_wave(double* samples, int count, int nfqso, int ntol,
                                                      int submode, const char* hhmmss, Ft8LineFn fn,
                                                      void* user) {
  if (!samples || count < 12000)
    return 0;
  if (submode != 1 && submode != 2 && submode != 4)
    submode = 1;
  if (ntol < 50)
    ntol = 50;
  if (ntol > 2000)
    ntol = 2000;
  if (nfqso < 200)
    nfqso = 1270;
  DecoderMs* ms = nullptr;
  try {
    ms = jt65Decoder();
  } catch (...) {
    return 0;
  }
  if (!ms)
    return 0;
  gJt65.fn = fn;
  gJt65.user = user;
  gJt65.reported = 0;
  ms->prepare65(nfqso, ntol, QString::fromLatin1(hhmmss && hhmmss[0] ? hhmmss : "000000"));
  try {
    ms->jt65_decode(samples, count, submode);
  } catch (...) {
    gJt65.fn = nullptr;
    gJt65.user = nullptr;
    return 0;
  }
  const int reported = gJt65.reported;
  gJt65.fn = nullptr;
  gJt65.user = nullptr;
  return reported;
}

extern "C" __declspec(dllexport) int q65_decode_wave(double* samples, int count, int lowHz, int highHz,
                                                     int rxHz, int modid, int periodSec, const char* hhmmss,
                                                     Ft8LineFn fn, void* user) {
  if (!samples || count < 12000)
    return 0;
  if (modid < 14 || modid > 17)
    modid = 14;
  if (periodSec != 15 && periodSec != 30 && periodSec != 60 && periodSec != 120)
    periodSec = 30;
  if (lowHz < 100)
    lowHz = 100;
  if (highHz > 5000)
    highHz = 5000;
  if (highHz < lowHz + 50)
    highHz = lowHz + 50;
  if (rxHz < lowHz || rxHz > highHz)
    rxHz = lowHz + (highHz - lowHz) / 2;
  DecoderQ65* dec = nullptr;
  try {
    dec = q65Decoder();
  } catch (...) {
    return 0;
  }
  if (!dec)
    return 0;
  gQ65.fn = fn;
  gQ65.user = user;
  gQ65.reported = 0;
  dec->SetPeriod(periodSec);
  dec->SetStDecoderDeep(1);
  dec->SetStApDecode(false);
  dec->SetStDecode(QString::fromLatin1(hhmmss && hhmmss[0] ? hhmmss : "000000"), 0, false);
  dec->SetTxFreq(rxHz);
  bool have = false;
  try {
    dec->q65_decode(samples, lowHz, highHz, rxHz, modid, have);
  } catch (...) {
    gQ65.fn = nullptr;
    gQ65.user = nullptr;
    return 0;
  }
  const int reported = gQ65.reported;
  gQ65.fn = nullptr;
  gQ65.user = nullptr;
  return reported;
}
