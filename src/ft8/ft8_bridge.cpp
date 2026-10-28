// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads MSHV's DecoderFt8 and forwards EmitDecodetTextFt. The algorithm
// files are not modified. Copyright for those files stays in the MSHV tree:
// Hrisimir Hristov, LZ2HV, and the WSJT Development Group, GPL-3.

#include "ft8_api.h"

#include "decoderms_ft8.h"
#include "HvGenFt4/gen_ft4.h"
#include "HvGenFt8/gen_ft8.h"

#include <QString>
#include <QStringList>

struct Ft8Session {
  DecoderFt8* decoder = nullptr;
  DecoderFt4* ft4 = nullptr;
  DecoderFt2* ft2 = nullptr;
  Ft8LineFn fn = nullptr;
  void* user = nullptr;
  int reported = 0;
  bool multiAnswer = false;
};

namespace {

void takeLine(Ft8Session* session, const QStringList& list) {
  if (!session || !session->fn || list.size() < 5)
    return;
  if (list.at(4).trimmed().isEmpty())
    return;
  // PrintMsg fires inside ft8_decode. Hand the line up now so Receive
  // paints while the rest of the pass is still running.
  const QByteArray time = list.at(0).toUtf8();
  const QByteArray dt = list.at(2).toUtf8();
  const QByteArray message = list.at(4).toUtf8();
  const int db = list.at(1).toInt();
  const int freqHz = list.size() > 7 ? list.at(7).toInt() : 0;
  session->fn(time.constData(), db, dt.constData(), message.constData(), freqHz, session->user);
  session->reported += 1;
}

}  // namespace

extern "C" __declspec(dllexport) Ft8Session* ft8_session_open(void) {
  auto* session = new Ft8Session;
  session->decoder = new DecoderFt8(0);
  session->ft4 = new DecoderFt4(0);
  // Depth 1 is the fast pass. Depth 3 was still running after the
  // next 15-second period had started.
  session->decoder->SetStDecoderDeep(1);
  session->decoder->SetStApDecode(true);
  session->decoder->SetStTxFreq(1200.0);
  session->ft4->SetStDecoderDeep(1);
  session->ft4->SetStApDecode(true);
  session->ft4->SetStTxFreq(1200.0);
  session->ft2 = new DecoderFt2(0);
  session->ft2->SetStDecoderDeep(1);
  session->ft2->SetStApDecode(true);
  session->ft2->SetStTxFreq(1200.0);
  QObject::connect(session->decoder, &DecoderFt8::EmitDecodetTextFt, session->decoder,
                   [session](const QStringList& list) { takeLine(session, list); },
                   Qt::DirectConnection);
  QObject::connect(session->ft4, &DecoderFt4::EmitDecodetTextFt, session->ft4,
                   [session](const QStringList& list) { takeLine(session, list); },
                   Qt::DirectConnection);
  QObject::connect(session->ft2, &DecoderFt2::EmitDecodetTextFt, session->ft2,
                   [session](const QStringList& list) { takeLine(session, list); },
                   Qt::DirectConnection);
  return session;
}

extern "C" __declspec(dllexport) void ft8_session_set_multi_answer(Ft8Session* session, int on) {
  if (!session)
    return;
  session->multiAnswer = on != 0;
  if (session->decoder)
    session->decoder->SetStMultiAnswerMod(session->multiAnswer);
  if (session->ft4)
    session->ft4->SetStMultiAnswerMod(session->multiAnswer);
  if (session->ft2)
    session->ft2->SetStMultiAnswerMod(session->multiAnswer);
}

extern "C" __declspec(dllexport) void ft8_session_close(Ft8Session* session) {
  if (!session)
    return;
  delete session->ft2;
  delete session->ft4;
  delete session->decoder;
  delete session;
}

extern "C" __declspec(dllexport) int ft8_session_decode(Ft8Session* session, double* samples,
                                                        int count, int lowHz, int highHz, int rxHz,
                                                        int txHz, int depth, int apOn, int id3dec,
                                                        const char* hhmmss, Ft8LineFn fn,
                                                        void* user) {
  if (!session || !session->decoder || !samples || count < 1024)
    return 0;
  if (lowHz < 0)
    lowHz = 0;
  if (highHz > 5000)
    highHz = 5000;
  if (highHz < lowHz + 50)
    highHz = lowHz + 50;
  if (rxHz < lowHz || rxHz > highHz)
    rxHz = lowHz + (highHz - lowHz) / 2;
  if (txHz < 0)
    txHz = 0;
  if (txHz > 5000)
    txHz = 5000;

  if (depth < 1)
    depth = 1;
  if (depth > 3)
    depth = 3;
  const bool staged = id3dec >= 1 && id3dec <= 3;
  if (!staged)
    id3dec = 100;

  session->fn = fn;
  session->user = user;
  session->reported = 0;
  const QString stamp = QString::fromLatin1(hhmmss && hhmmss[0] ? hhmmss : "000000");
  session->decoder->SetStDecode(stamp, 0, false);
  session->decoder->SetNewP(true);
  session->decoder->SetStTxFreq(txHz);
  session->decoder->SetStDecoderDeep(depth);
  session->decoder->SetStApDecode(apOn != 0);
  session->decoder->SetStMultiAnswerMod(session->multiAnswer);
  session->decoder->Decode3intFt(staged);

  bool have = false;
  try {
    // id3dec 1/2/3 are MSHV's three-stage passes. 100 is one pass on the
    // waveform, not a zero-padded 15 seconds.
    session->decoder->ft8_decode(samples, count, lowHz, highHz, rxHz, have, id3dec, lowHz, highHz);
  } catch (...) {
    session->fn = nullptr;
    session->user = nullptr;
    return 0;
  }
  const int reported = session->reported;
  session->fn = nullptr;
  session->user = nullptr;
  return reported;
}

extern "C" __declspec(dllexport) int ft4_session_decode(Ft8Session* session, double* samples,
                                                        int count, int lowHz, int highHz, int rxHz,
                                                        int txHz, int depth, int apOn,
                                                        const char* hhmmss, Ft8LineFn fn,
                                                        void* user) {
  if (!session || !session->ft4 || !samples || count < 1024)
    return 0;
  if (lowHz < 0)
    lowHz = 0;
  if (highHz > 5000)
    highHz = 5000;
  if (highHz < lowHz + 50)
    highHz = lowHz + 50;
  if (rxHz < lowHz || rxHz > highHz)
    rxHz = lowHz + (highHz - lowHz) / 2;
  if (txHz < 0)
    txHz = 0;
  if (txHz > 5000)
    txHz = 5000;
  if (depth < 1)
    depth = 1;
  if (depth > 3)
    depth = 3;

  session->fn = fn;
  session->user = user;
  session->reported = 0;
  const QString stamp = QString::fromLatin1(hhmmss && hhmmss[0] ? hhmmss : "000000");
  session->ft4->SetStDecode(stamp, 0, false);
  session->ft4->SetStTxFreq(txHz);
  session->ft4->SetStDecoderDeep(depth);
  session->ft4->SetStApDecode(apOn != 0);
  session->ft4->SetStMultiAnswerMod(session->multiAnswer);

  bool have = false;
  try {
    // One pass. The decoder reads 72576 samples from the start of dd.
    session->ft4->ft4_decode(samples, lowHz, highHz, lowHz, highHz, rxHz, have);
  } catch (...) {
    session->fn = nullptr;
    session->user = nullptr;
    return 0;
  }
  const int reported = session->reported;
  session->fn = nullptr;
  session->user = nullptr;
  return reported;
}

extern "C" __declspec(dllexport) int ft2_session_decode(Ft8Session* session, double* samples,
                                                        int count, int lowHz, int highHz, int rxHz,
                                                        int txHz, int depth, int apOn,
                                                        const char* hhmmss, Ft8LineFn fn,
                                                        void* user) {
  if (!session || !session->ft2 || !samples || count < 1024)
    return 0;
  if (lowHz < 0)
    lowHz = 0;
  if (highHz > 5000)
    highHz = 5000;
  if (highHz < lowHz + 50)
    highHz = lowHz + 50;
  if (rxHz < lowHz || rxHz > highHz)
    rxHz = lowHz + (highHz - lowHz) / 2;
  if (txHz < 0)
    txHz = 0;
  if (txHz > 5000)
    txHz = 5000;
  if (depth < 1)
    depth = 1;
  if (depth > 3)
    depth = 3;

  session->fn = fn;
  session->user = user;
  session->reported = 0;
  const QString stamp = QString::fromLatin1(hhmmss && hhmmss[0] ? hhmmss : "000000");
  session->ft2->SetStDecode(stamp, 0, false);
  session->ft2->SetStTxFreq(txHz);
  session->ft2->SetStDecoderDeep(depth);
  session->ft2->SetStApDecode(apOn != 0);
  session->ft2->SetStMultiAnswerMod(session->multiAnswer);

  bool have = false;
  try {
    // One pass. The decoder reads the first 45000 samples.
    session->ft2->ft2_decode(samples, lowHz, highHz, lowHz, highHz, rxHz, have);
  } catch (...) {
    session->fn = nullptr;
    session->user = nullptr;
    return 0;
  }
  const int reported = session->reported;
  session->fn = nullptr;
  session->user = nullptr;
  return reported;
}

namespace {

int bounded(int count, int capacity) {
  if (count <= 0 || count > capacity)
    return 0;
  return count;
}

}  // namespace

extern "C" __declspec(dllexport) int ft8_generate(const char* message, double toneHz, int* samples,
                                                  int capacity) {
  if (!message || !message[0] || !samples || capacity < 840000)
    return 0;
  static GenFt8 gen(false);
  const int count = gen.genft8(QString::fromUtf8(message), samples, 48000.0, toneHz,
                               QStringLiteral("0#0#0#0#0#0#0"), QString());
  return bounded(count, capacity);
}

extern "C" __declspec(dllexport) int ft4_generate(const char* message, double toneHz, int* samples,
                                                  int capacity) {
  if (!message || !message[0] || !samples || capacity < 420000)
    return 0;
  static GenFt4 gen(false);
  const int count = gen.genft4(QString::fromUtf8(message), samples, 48000.0, toneHz);
  return bounded(count, capacity);
}
