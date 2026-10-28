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

#ifdef __cplusplus
}
#endif
