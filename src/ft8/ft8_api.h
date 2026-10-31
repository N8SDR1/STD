// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// C boundary around MSHV's DecoderFt8. The decoder itself stays in the
// MSHV sources (GPL-3, copyright Hristov / WSJT Development Group).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Ft8LineFn)(const char* time, int db, const char* dt, const char* message,
                          int freqHz, void* user);

typedef struct Ft8Session Ft8Session;

/* Heap-allocates one DecoderFt8. Null if the decoder cannot be constructed. */
Ft8Session* ft8_session_open(void);
void ft8_session_close(Ft8Session* session);

/* samples are mutable: the decoder subtracts tones in place.
   The pointer must address at least 192000 doubles. The decoder reads
   and writes through index 180009 even when count is the shorter
   in-progress window. count itself stays that window length.
   lowHz/highHz are the audio window (Start and Start+BW).
   rxHz is the RX marker (nfqso). txHz is the TX marker (SetStTxFreq).
   depth is 1 fast, 2 normal, 3 deep. apOn enables a-priori decode.
   id3dec is 1, 2, or 3 for three-stage FT8, or 100 for one pass.
   hhmmss is the UTC start of the 15-second period.
   fn is called as each line is found, before this function returns.
   Returns the number of lines handed to fn. */
int ft8_session_decode(Ft8Session* session, double* samples, int count, int lowHz, int highHz,
                       int rxHz, int txHz, int depth, int apOn, int id3dec, const char* hhmmss,
                       Ft8LineFn fn, void* user);

/* One FT4 pass. MSHV does not use the FT8 three-stage timer for FT4.
   The pointer must address at least 72576 doubles (the decoder reads
   that many). count is unused by the decoder and may be the window
   length. fn is called as each line is found. */
int ft4_session_decode(Ft8Session* session, double* samples, int count, int lowHz, int highHz,
                       int rxHz, int txHz, int depth, int apOn, const char* hhmmss, Ft8LineFn fn,
                       void* user);

/* One FT2 pass. MSHV starts it at 3.4 s of the 3.75 s period.
   The pointer must address at least 45000 doubles. The decoder reads
   that many and ignores count. fn is called as each line is found. */
int ft2_session_decode(Ft8Session* session, double* samples, int count, int lowHz, int highHz,
                       int rxHz, int txHz, int depth, int apOn, const char* hhmmss, Ft8LineFn fn,
                       void* user);

/* MSHV multi-answer a-priori state. on is 0 or 1. This does not transmit. */
void ft8_session_set_multi_answer(Ft8Session* session, int on);

/* One FT8, FT4, or FT2 waveform at 48 kHz, full-scale int samples.
   samples must hold at least 840000 (FT8), 420000 (FT4), or 220000 (FT2) ints.
   Returns the sample count, or 0 if the message cannot be encoded.
   Does not key a radio. */
int ft8_generate(const char* message, double toneHz, int* samples, int capacity);
int ft4_generate(const char* message, double toneHz, int* samples, int capacity);
int ft2_generate(const char* message, double toneHz, int* samples, int capacity);

/* JT65 at 48 kHz. submode is 1 A, 2 B, 4 C. The tone is fixed near
   1270 Hz. samples must hold 60*48000 ints. Returns the sample count. */
int jt65_generate(const char* message, int submode, int* samples, int capacity);

/* Q65 at 48 kHz. submode is 1 A, 2 B, 4 C, 8 D. periodSec is 15, 30, 60,
   or 120. samples must hold periodSec*48000 ints. Returns the sample count. */
int q65_generate(const char* message, double toneHz, int submode, int periodSec, int* samples,
                 int capacity);

/* MSHV's Decode menu for JT65 and Q65. Call from the decoding thread just
   before jt65_decode_wave or q65_decode_wave; the next decode uses it.
   depth 1 fast, 2 normal, 3 deep (Q65). ap enables a-priori decoding and
   uses myCall, hisCall and hisGrid. averaging keeps a running average of
   failed periods. autoClearAverage clears it after a decode (Q65).
   singleSignal stops after the first Q65 decode. driftCorrection is MSHV's
   +/- 100 Hz Q65 drift search. vhfUhf turns on JT65 VHF/UHF features.
   maxCandidates is the JT65 cap: 1, 4, 8, 16, or 32. clearAverage drops
   both averages once. emeDelay is MSHV's Q65 Decode After EME Delay.
   deepSearch turns on JT65 deep search against deepSearchDb, lines of
   "CALL,GRID4" separated by '\n' (MSHV's msloc_db format, at most 15000).
   Strings may be null. */
typedef struct SlowDecodeOptions {
  int depth;
  int ap;
  int averaging;
  int autoClearAverage;
  int singleSignal;
  int driftCorrection;
  int vhfUhf;
  int maxCandidates;
  int clearAverage;
  int emeDelay;
  int deepSearch;
  const char* deepSearchDb;
  const char* myCall;
  const char* hisCall;
  const char* hisGrid;
} SlowDecodeOptions;
void slow_set_options(const SlowDecodeOptions* options);

/* 12 kHz doubles, already scaled the way MSHV scales int16 audio.
   JT65 count should be 60*12000. nfqso is the audio center (1270).
   submode is 1, 2, or 4. fn is called as each line is found. */
int jt65_decode_wave(double* samples, int count, int nfqso, int ntol, int submode, const char* hhmmss,
                     Ft8LineFn fn, void* user);

/* 12 kHz doubles. count should be periodSec*12000. modid is 14 A, 15 B,
   16 C, 17 D. fn is called as each line is found. */
int q65_decode_wave(double* samples, int count, int lowHz, int highHz, int rxHz, int modid, int periodSec,
                    const char* hhmmss, Ft8LineFn fn, void* user);

#ifdef __cplusplus
}
#endif
