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

#include <vector>

struct Ft8Session {
  DecoderFt8* decoder = nullptr;
  std::vector<QStringList> lines;
};

namespace {

void takeLine(Ft8Session* session, const QStringList& list) {
  if (list.size() < 5)
    return;
  if (list.at(4).trimmed().isEmpty())
    return;
  session->lines.push_back(list);
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
                                                        int txHz, const char* hhmmss, Ft8LineFn fn,
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

  session->lines.clear();
  const QString stamp = QString::fromLatin1(hhmmss && hhmmss[0] ? hhmmss : "000000");
  session->decoder->SetStDecode(stamp, 0, false);
  session->decoder->SetNewP(true);
  session->decoder->SetStTxFreq(txHz);

  bool have = false;
  try {
    // id3dec 100 is MSHV's normal timed pass. The buffer is the waveform,
    // not a zero-padded 15 seconds.
    session->decoder->ft8_decode(samples, count, lowHz, highHz, rxHz, have, 100, lowHz, highHz);
  } catch (...) {
    return 0;
  }

  int reported = 0;
  for (const QStringList& list : session->lines) {
    if (!fn)
      continue;
    const QByteArray time = list.at(0).toUtf8();
    const QByteArray dt = list.at(2).toUtf8();
    const QByteArray message = list.at(4).toUtf8();
    const int db = list.at(1).toInt();
    const int freqHz = list.size() > 7 ? list.at(7).toInt() : 0;
    fn(time.constData(), db, dt.constData(), message.constData(), freqHz, user);
    ++reported;
  }
  return reported;
}
