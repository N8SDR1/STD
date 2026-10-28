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

#ifdef __cplusplus
}
#endif
