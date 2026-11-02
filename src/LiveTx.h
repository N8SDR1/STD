// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QChar>
#include <QString>

#include <memory>
#include <vector>

struct RttyOptions;

// Live keyboard transmit, as Fldigi does it: the lead-in goes out, then
// each character as it is typed, idle while the box is empty, and the tail
// when the operator ends the transmission. Each call appends 48 kHz float
// samples to out. The modulator keeps its phase, shift, and encoder state
// from one call to the next.
class LiveTextTx {
public:
  virtual ~LiveTextTx() = default;
  // Lead-in. Call once, first.
  virtual void begin(std::vector<float>* out) = 0;
  // One character. False when the mode has no code for it; nothing is added.
  virtual bool put(QChar ch, std::vector<float>* out) = 0;
  // One idle unit for an empty box: a phase reversal in PSK, LTRS in RTTY.
  virtual void idle(std::vector<float>* out) = 0;
  // Tail. Call once, last.
  virtual void end(std::vector<float>* out) = 0;
};

// The idle safety: true once the box has had nothing new for seconds.
// lastTextMs is when the last character left the box (or TX keyed).
inline bool liveIdleExpired(long long lastTextMs, long long nowMs, bool on, int seconds) {
  return on && seconds > 0 && lastTextMs > 0 && nowMs - lastTextMs >= (long long)seconds * 1000;
}

// The Back to RX after choices, seconds. 25 is the default.
inline constexpr int kLiveIdleChoices[] = {5, 10, 15, 25};

// A saved value snapped to the nearest choice; 25 when it is not set.
inline int liveIdleChoice(int seconds) {
  int best = 25;
  int gap = 1 << 30;
  for (const int choice : kLiveIdleChoices) {
    const int d = seconds > choice ? seconds - choice : choice - seconds;
    if (d < gap) {
      gap = d;
      best = choice;
    }
  }
  return best;
}

// PSK, QPSK, and RTTY send live. The other keyboard modes still send the
// box as it stands when TX is pressed.
bool liveTextMode(const QString& mode);

std::unique_ptr<LiveTextTx> makeLivePsk(const QString& mode, int centerHz);
std::unique_ptr<LiveTextTx> makeLiveRtty(int centerHz, const RttyOptions& options);
// Null when the mode does not send live. rtty may be null for the defaults.
std::unique_ptr<LiveTextTx> makeLiveTextTx(const QString& mode, int centerHz, const RttyOptions* rtty);
