// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MSHV MSK144, MSK40 short messages, and MSKMS. The algorithms stay in
// the MSHV tree (GPL-3, Hristov / WSJT Development Group). This file
// constructs the slim DecoderMsk, forwards its lines, and wraps GenMsk.

#include "ft8_api.h"

#include "decoderms_msk.h"

#include <QString>

#include <cmath>
#include <cstring>

namespace {

struct MskSlot {
  DecoderMsk* dec = nullptr;
  MskLineFn fn = nullptr;
  void* user = nullptr;
  int reported = 0;
};

thread_local MskSlot gMsk;

struct MskOptions {
  int ntol = 100;
  int depth = 2;
  bool shortMsgs = false;
  bool swl = false;
  QString myCall;
  QString hisCall;
};
MskOptions gMskOptions;

// MSHV's list: time, t0, width, snr, rpt, df, message, navg, ncorrected,
// eye, "ident freq".
void takeMsk(const QStringList& list) {
  if (!gMsk.fn || list.size() < 7)
    return;
  const QString text = list.at(6).trimmed();
  if (text.isEmpty())
    return;
  int freq = 0;
  if (list.size() > 10) {
    const QStringList tail = list.at(10).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!tail.isEmpty())
      freq = tail.last().toInt();
  }
  const QByteArray time = list.at(0).toUtf8();
  const QByteArray t0 = list.at(1).toUtf8();
  const QByteArray message = text.toUtf8();
  const QByteArray rpt = list.at(4).trimmed().toUtf8();
  const int widthMs = list.at(2).toInt();
  const int df = list.size() > 5 ? list.at(5).toInt() : 0;
  const int navg = list.size() > 7 ? list.at(7).toInt() : 0;
  const int bitErrors = list.size() > 8 ? list.at(8).toInt() : 0;
  const QByteArray eye = list.size() > 9 ? list.at(9).toUtf8() : QByteArray();
  gMsk.fn(time.constData(), list.at(3).toInt(), t0.constData(), message.constData(), freq, widthMs,
          rpt.constData(), df, navg, bitErrors, eye.constData(), gMsk.user);
  gMsk.reported += 1;
}

DecoderMsk* mskDecoder() {
  if (gMsk.dec)
    return gMsk.dec;
  gMsk.dec = new DecoderMsk(QString());
  QObject::connect(gMsk.dec, &DecoderMsk::EmitDecodetText, gMsk.dec,
                   [](const QStringList& list, bool, bool) { takeMsk(list); }, Qt::DirectConnection);
  return gMsk.dec;
}

QString baseCall(const QString& call) {
  if (!call.contains(QLatin1Char('/')))
    return call;
  QString best;
  for (const QString& part : call.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
    if (part.size() > best.size())
      best = part;
  }
  return best;
}

}  // namespace

DecoderMsk::DecoderMsk(QString) {
  twopi = 8.0 * std::atan(1.0);
  pi = 4.0 * std::atan(1.0);
  DEC_SAMPLE_RATE = 12000.0;
  s_mode = 0;
  G_DfTolerance = 100;
  G_ShOpt = false;
  G_SwlOpt = false;
  s_fopen = false;
  s_decoder_deep = 2;
  s_mousebutton = 0;
  s_time = QStringLiteral("000000");
  s_time_last = QStringLiteral("000001");
  rtd_dupe_cou = 0;
  rtd_dupe_pos = 0.0;
  s_MyCall = QStringLiteral("NOT__EXIST");
  s_MyBaseCall = QStringLiteral("NOT__EXIST");
  s_HisCall = QStringLiteral("NOCALL");
  s_HisBaseCall = QStringLiteral("NOT__EXIST");
  s_R1HisCall = QStringLiteral("NOCALL");
  s_R2HisCall = QStringLiteral("NOCALL");
  f_back_color = false;

  s_list_rpt_msk.clear();
  prev_pt_msk = 0.0;
  ping_width_msk = 0.0;
  prev_ping_t_msk = 0.0;
  last_rpt_snr_msk = 0.0;
  one_end_ping_msk = false;
  is_new_rpt_msk = false;
  ss_msk144ms = false;
  last_ntol_msk144 = -1;
  last_df_msk144 = -1;
  last_ntol_msk40 = -1;
  last_df_msk40 = -1;
  TGenMsk = new GenMsk(true);
  first_dec_msk = true;
  first_sq = true;
  f_first_msk144 = false;
  f_first_msk40 = false;
  for (int i = 0; i < HASH_CALLS_COUNT; ++i) {
    hash_msk40_calls[i].hash = -101;
    hash_msk40_calls[i].calls = QStringLiteral("FALSE");
  }
  s_msk144rxequal_s = false;
  s_msk144rxequal_d = false;
  nfft0_msk144_2 = 0;
  pomAll.zero_double_beg_end(dpclast_msk144_2, 0, 3);
  dpclast_msk144_2[0] = 1.0;
  pomAll.zero_double_comp_beg_end(s_corrd, 0, 524300);
  analytic_msk144_2_init_s_corrs_full();
  s_f_rtd = false;
  s_end_rtd = true;
  first_rtd_msk = true;
  pomAll.zero_double_beg_end(s_pcoeffs_msk144, 0, 3);
  s_trained_msk144 = false;
}

DecoderMsk::~DecoderMsk() {
  delete TGenMsk;
  TGenMsk = nullptr;
}

void DecoderMsk::SetBackColor() {
  f_back_color = !f_back_color;
  emit EmitBackColor(f_back_color);
}

QString DecoderMsk::RemBegEndWSpaces(QString s) {
  return s.trimmed();
}

QString DecoderMsk::RemWSpacesInside(QString s) {
  while (s.contains(QLatin1String("  ")))
    s.replace(QLatin1String("  "), QLatin1String(" "));
  return s;
}

double DecoderMsk::dot_product_da_da(double* a, double* b, int count, int beg) {
  double sum = 0.0;
  for (int i = 0; i < count; ++i)
    sum += a[i] * b[i + beg];
  return sum;
}

// Mix by f0 Hz, one complex rotation per sample.
void DecoderMsk::tweak1(double complex* ca, int jz, double f0, double complex* cb) {
  double complex w = 1.0 + 1.0 * I;
  const double dphi = twopi * f0 / DEC_SAMPLE_RATE;
  const double complex step = cos(dphi) + sin(dphi) * I;
  for (int i = 0; i < jz; ++i) {
    w = w * step;
    cb[i] = w * ca[i];
  }
}

void DecoderMsk::zero_int_beg_end(int* d, int begin, int end) {
  if (!d)
    return;
  for (int i = begin; i < end; ++i)
    d[i] = 0;
}

void DecoderMsk::prepareMsk(int ntol, int depth, bool shortMsgs, bool swl, const QString& time,
                            const QString& myCall, const QString& hisCall) {
  G_DfTolerance = ntol;
  s_decoder_deep = depth;
  G_ShOpt = shortMsgs;
  G_SwlOpt = swl;
  s_time = time.isEmpty() ? QStringLiteral("000000") : time;
  const QString my = myCall.isEmpty() ? QStringLiteral("NOT__EXIST") : myCall;
  if (my != s_MyCall) {
    s_MyCall = my;
    s_MyBaseCall = myCall.isEmpty() ? my : baseCall(myCall);
    if (!myCall.isEmpty())
      TGenMsk->save_hash_call_my_his_r1_r2(s_MyCall, 0);
  }
  const QString his = hisCall.isEmpty() ? QStringLiteral("NOCALL") : hisCall;
  // MSHV's EmitListHashCalls order: my, his, r1, r2, grid, his base.
  SetCalsHash(QStringList{my, his, QStringLiteral("NOCALL"), QStringLiteral("NOCALL"), QString(),
                          hisCall.isEmpty() ? QStringLiteral("NOT__EXIST") : baseCall(hisCall)});
}

void DecoderMsk::decodeFull(double* dat, int npts, bool mskms) {
  s_f_rtd = false;
  s_end_rtd = true;
  s_mousebutton = 0;
  s_mode = mskms ? 12 : 0;
  msk_144_40_decode(dat, npts, 0.0, mskms);
}

void DecoderMsk::decodeRtd(double* dat, int npts, double istart, bool mskms) {
  Q_UNUSED(npts);
  s_f_rtd = true;
  s_end_rtd = false;
  s_mousebutton = 2;
  s_mode = mskms ? 12 : 0;
  msk_144_40_rtd(dat, npts, istart, mskms);
}

void DecoderMsk::endRtd() {
  if (ss_msk144ms)
    EndRtdPeriod();
}

extern "C" __declspec(dllexport) void msk_set_options(int ntol, int depth, int shortMsgs, int swl,
                                                      const char* myCall, const char* hisCall) {
  gMskOptions.ntol = ntol < 10 ? 10 : (ntol > 400 ? 400 : ntol);
  gMskOptions.swl = swl != 0;
  gMskOptions.depth = depth < 1 ? 1 : (depth > 3 ? 3 : depth);
  gMskOptions.shortMsgs = shortMsgs != 0;
  gMskOptions.myCall = QString::fromLatin1(myCall ? myCall : "").trimmed().toUpper();
  gMskOptions.hisCall = QString::fromLatin1(hisCall ? hisCall : "").trimmed().toUpper();
}

namespace {

DecoderMsk* readyDecoder(const char* hhmmss, MskLineFn fn, void* user) {
  DecoderMsk* dec = nullptr;
  try {
    dec = mskDecoder();
  } catch (...) {
    return nullptr;
  }
  if (!dec)
    return nullptr;
  gMsk.fn = fn;
  gMsk.user = user;
  gMsk.reported = 0;
  dec->prepareMsk(gMskOptions.ntol, gMskOptions.depth, gMskOptions.shortMsgs, gMskOptions.swl,
                  QString::fromLatin1(hhmmss && hhmmss[0] ? hhmmss : "000000"), gMskOptions.myCall,
                  gMskOptions.hisCall);
  return dec;
}

int finish() {
  const int reported = gMsk.reported;
  gMsk.fn = nullptr;
  gMsk.user = nullptr;
  return reported;
}

}  // namespace

extern "C" __declspec(dllexport) int msk_decode_wave(double* samples, int count, int mskms, const char* hhmmss,
                                                     MskLineFn fn, void* user) {
  if (!samples || count < 7168)
    return 0;
  DecoderMsk* dec = readyDecoder(hhmmss, fn, user);
  if (!dec)
    return 0;
  try {
    dec->decodeFull(samples, count, mskms != 0);
  } catch (...) {
  }
  return finish();
}

extern "C" __declspec(dllexport) int msk_decode_block(double* samples, int count, int istart, int mskms,
                                                      int endOfPeriod, const char* hhmmss, MskLineFn fn,
                                                      void* user) {
  if (!samples || count < MSK_BLOCK_SAMPLES + 1024)
    return 0;
  DecoderMsk* dec = readyDecoder(hhmmss, fn, user);
  if (!dec)
    return 0;
  try {
    dec->decodeRtd(samples, count, double(istart), mskms != 0);
    if (endOfPeriod)
      dec->endRtd();
  } catch (...) {
  }
  return finish();
}

extern "C" __declspec(dllexport) int msk_generate(const char* message, int mskms, int* samples, int capacity) {
  if (!message || !message[0] || !samples || capacity < 234 * 24)
    return 0;
  // genmsk strips leading blanks over a 50-character buffer.
  char text[64];
  std::memset(text, ' ', sizeof(text));
  text[63] = '\0';
  const int ncopy = int(std::strlen(message));
  std::memcpy(text, message, ncopy < 50 ? ncopy : 50);
  text[50] = '\0';
  static GenMsk gen(false);
  int tones[234];
  // 48 kHz: 24 samples per 2000 baud symbol, tones at 1000 and 2000 Hz.
  const int n = gen.genmsk(text, 1.0, tones, true, samples, 48000.0, 4.0, 0, mskms ? 12 : 0, mskms != 0);
  if (n <= 0 || n > capacity)
    return 0;
  return n;
}
