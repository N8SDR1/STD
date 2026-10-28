// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads MSHV's DecoderFt8 and forwards EmitDecodetTextFt. The algorithm
// files are not modified. Copyright for those files stays in the MSHV tree:
// Hrisimir Hristov, LZ2HV, and the WSJT Development Group, GPL-3.

#include "ft8_api.h"

#include "decoderms_ft8.h"

#include <QString>
#include <QStringList>

struct Ft8Session {
  DecoderFt8* decoder = nullptr;
  Ft8LineFn fn = nullptr;
  void* user = nullptr;
  int reported = 0;
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
  // Depth 1 is the fast pass. Depth 3 was still running after the
  // next 15-second period had started.
  session->decoder->SetStDecoderDeep(1);
  session->decoder->SetStApDecode(true);
  session->decoder->SetStTxFreq(1200.0);
  QObject::connect(session->decoder, &DecoderFt8::EmitDecodetTextFt, session->decoder,
                   [session](const QStringList& list) { takeLine(session, list); },
                   Qt::DirectConnection);
  return session;
}

extern "C" __declspec(dllexport) void ft8_session_close(Ft8Session* session) {
  if (!session)
    return;
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
