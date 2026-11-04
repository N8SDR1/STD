/* Copyright (C) 2026 Rick Langford (N8SDR)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cdecl wrapper around freedv/rade_c so MSVC can LoadLibrary it.
 * The only source is https://github.com/freedv/rade_c at
 * c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9
 *
 * Speech in is 16 kHz float and speech out is 48 kHz float, about [-1, 1].
 * TX runs RNNoise on the mic and RX runs the Opus bandwidth expander, as
 * the FreeDV app does; std_rade_set_options turns either off.
 * Modem in and out is 8 kHz real, the rade_tx real part (nominal 1).
 * STD applies the TCI scales. This file does not.
 * V1 end-of-over carries the callsign the way FreeDV does: a 6-bit
 * character set, CRC-8, LDPC(112,56), interleaved QPSK.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "fargan.h"
#include "osce.h"
#include "osce_structs.h"
#include "rnnoise.h"
#include "std_rade_hra.h"
#include "lpcnet.h"

typedef struct {
  float real;
  float imag;
} RADE_COMP;

struct rade;

#define RADE_API __declspec(dllimport) __stdcall

void RADE_API rade_initialize(void);
void RADE_API rade_finalize(void);
struct rade* RADE_API rade_open(char model_file[], int flags);
void RADE_API rade_close(struct rade* r);
int RADE_API rade_n_tx_out(struct rade* r);
int RADE_API rade_n_tx_eoo_out(struct rade* r);
int RADE_API rade_nin_max(struct rade* r);
int RADE_API rade_n_features_in_out(struct rade* r);
int RADE_API rade_n_eoo_bits(struct rade* r);
int RADE_API rade_nin(struct rade* r);
int RADE_API rade_tx(struct rade* r, RADE_COMP tx_out[], float features_in[]);
int RADE_API rade_tx_eoo(struct rade* r, RADE_COMP tx_eoo_out[]);
void RADE_API rade_tx_set_eoo_bits(struct rade* r, float eoo_bits[]);
int RADE_API rade_rx(struct rade* r, float features_out[], int* has_eoo_out, float eoo_out[],
                     RADE_COMP rx_in[]);
int RADE_API rade_sync(struct rade* r);
float RADE_API rade_snrdB_3k_est(struct rade* r);

#define RADE_VERBOSE_0 0x8
#define RADE_MODE_V2 0x10
#define RADE_NO_TX_BPF 0x80

int opus_select_arch(void);

static CRITICAL_SECTION g_lock;

__attribute__((constructor)) static void std_rade_init_lock(void) {
  InitializeCriticalSection(&g_lock);
}

static char g_err[256];
static char g_call[16];
static struct rade* g_r;
static LPCNetEncState* g_lpc;
static FARGANState* g_fargan;
static int g_arch;
static int g_v2;
static int g_frames_per;
static int g_feat_idx;
static int g_fargan_ready;
static int g_cont_frames;
static int g_n_tx;
static int g_n_eoo;
static int g_n_feat;
static int g_nin_max;
static int g_n_eoo_bits;

static float* g_feat;
static float* g_feat_out;
static float* g_eoo_bits;
static float* g_speech;
static int g_speech_n;
static int g_speech_cap;
static float* g_s16;
static int g_s16_cap;
static RADE_COMP* g_tx;
static RADE_COMP* g_eoo;
static RADE_COMP* g_rx_scratch;
static RADE_COMP* g_rx_pending;
static int g_rx_n;
static int g_rx_cap;
static float g_cont[5 * NB_TOTAL_FEATURES];
static int g_sync;
static float g_snr;

/* ---- Microphone leveller ----------------------------------------------
 * FreeDV levels the microphone before the encoder: momentary loudness
 * (400 ms, K-weighted) is pulled toward -23 LUFS with at most +12 dB and
 * -20 dB of gain, 0.5 s down and 6 s up, frozen below -33 LUFS, and a
 * limiter keeps the peaks under -1 dBFS. This is the same behaviour,
 * written here from those numbers. */
enum { kAgcBlock = 160, kAgcWindow = 40 };
typedef struct {
  double b0, b1, b2, a1, a2;
  double z1, z2;
} Biquad;
static Biquad g_kw[2];
static double g_agc_ms[kAgcWindow];
static int g_agc_ms_n;
static int g_agc_ms_at;
static float g_agc_block[kAgcBlock];
static int g_agc_fill;
static int g_agc_blocks;
static int g_agc_valid;
static double g_agc_target;
static double g_agc_gain;
static double g_lim_gain = 1.0;

static void biquad_set(Biquad* q, double b0, double b1, double b2, double a0, double a1, double a2) {
  q->b0 = b0 / a0;
  q->b1 = b1 / a0;
  q->b2 = b2 / a0;
  q->a1 = a1 / a0;
  q->a2 = a2 / a0;
  q->z1 = q->z2 = 0.0;
}

static double biquad_run(Biquad* q, double x) {
  const double y = q->b0 * x + q->z1;
  q->z1 = q->b1 * x - q->a1 * y + q->z2;
  q->z2 = q->b2 * x - q->a2 * y;
  return y;
}

static void agc_reset(void) {
  const double fs = 16000.0;
  const double pi = 3.14159265358979323846;
  /* ITU-R BS.1770 K-weighting: a high shelf, then a high pass. */
  {
    const double gain = 3.99984385397, q = 0.7071752369554193, fc = 1681.9744509555319;
    const double a = pow(10.0, gain / 40.0), w = 2.0 * pi * fc / fs;
    const double c = cos(w), alpha = sin(w) / (2.0 * q), r = 2.0 * sqrt(a) * alpha;
    biquad_set(&g_kw[0], a * ((a + 1) + (a - 1) * c + r), -2 * a * ((a - 1) + (a + 1) * c),
               a * ((a + 1) + (a - 1) * c - r), (a + 1) - (a - 1) * c + r, 2 * ((a - 1) - (a + 1) * c),
               (a + 1) - (a - 1) * c - r);
  }
  {
    const double q = 0.5003270373253953, fc = 38.13547087613982;
    const double w = 2.0 * pi * fc / fs, c = cos(w), alpha = sin(w) / (2.0 * q);
    biquad_set(&g_kw[1], (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
  }
  memset(g_agc_ms, 0, sizeof g_agc_ms);
  g_agc_ms_n = g_agc_ms_at = g_agc_fill = g_agc_blocks = g_agc_valid = 0;
  g_agc_target = g_agc_gain = 0.0;
  g_lim_gain = 1.0;
}

static void agc_block(float* x) {
  double ms = 0.0;
  int i;
  for (i = 0; i < kAgcBlock; i++) {
    const double k = biquad_run(&g_kw[1], biquad_run(&g_kw[0], x[i]));
    ms += k * k;
  }
  g_agc_ms[g_agc_ms_at] = ms / kAgcBlock;
  g_agc_ms_at = (g_agc_ms_at + 1) % kAgcWindow;
  if (g_agc_ms_n < kAgcWindow)
    g_agc_ms_n++;
  if (++g_agc_blocks >= 5) {
    double sum = 0.0;
    g_agc_blocks = 0;
    for (i = 0; i < g_agc_ms_n; i++)
      sum += g_agc_ms[i];
    sum /= kAgcWindow; /* momentary loudness looks at the whole 400 ms */
    g_agc_valid = 0;
    if (sum > 0.0) {
      const double lufs = -0.691 + 10.0 * log10(sum);
      if (lufs > -33.0) {
        g_agc_valid = 1;
        g_agc_target = -23.0 - lufs;
        if (g_agc_target > 12.0)
          g_agc_target = 12.0;
        if (g_agc_target < -20.0)
          g_agc_target = -20.0;
      }
    }
  }
  if (g_agc_valid) {
    const double tc = g_agc_target < g_agc_gain ? 0.5 : 6.0;
    g_agc_gain += (g_agc_target - g_agc_gain) / tc * ((double)kAgcBlock / 16000.0);
  }
  {
    const double scale = pow(10.0, g_agc_gain / 20.0);
    const double ceiling = 0.8912509; /* -1 dBFS */
    const double release = 1.0 - exp(-1.0 / (0.05 * 16000.0));
    for (i = 0; i < kAgcBlock; i++) {
      const double v = x[i] * scale;
      const double peak = fabs(v) * g_lim_gain;
      if (peak > ceiling)
        g_lim_gain = ceiling / fabs(v);
      else
        g_lim_gain += (1.0 - g_lim_gain) * release;
      x[i] = (float)(v * g_lim_gain);
    }
  }
}

/* Levels n samples in place, in 10 ms blocks; the last partial block waits. */
static int agc_feed(const float* in, int n, float* out) {
  int produced = 0;
  int i;
  for (i = 0; i < n; i++) {
    g_agc_block[g_agc_fill++] = in[i];
    if (g_agc_fill == kAgcBlock) {
      agc_block(g_agc_block);
      memcpy(out + produced, g_agc_block, sizeof g_agc_block);
      produced += kAgcBlock;
      g_agc_fill = 0;
    }
  }
  return produced;
}

/* ---- 16 kHz <-> 48 kHz, RNNoise, and bandwidth expansion ---------------
 * RNNoise works on 48 kHz 10 ms frames. The bandwidth expander turns 16 kHz
 * speech into 48 kHz with the band above 8 kHz filled in. Both are what the
 * FreeDV app runs ("TX Noise Suppression" and "BW Expander", both on by
 * default there). The 3:1 converters are a 144-tap Blackman-windowed sinc,
 * cut off at 7.4 kHz, run as three 48-tap phases. */
enum { kPhaseTaps = 48, kRnFrame = 480, kBweFrame = 320 };
static float g_r3[3][kPhaseTaps];
static int g_r3_built;

static void build_r3(void) {
  const double pi = 3.14159265358979323846;
  const int n = 3 * kPhaseTaps;
  const double fc = 7400.0 / 48000.0;
  double sum = 0.0;
  double h[3 * kPhaseTaps];
  int i;
  for (i = 0; i < n; i++) {
    double t = (double)i - (double)(n - 1) / 2.0;
    double arg = 2.0 * fc * t;
    double sinc = fabs(arg) < 1e-12 ? 1.0 : sin(pi * arg) / (pi * arg);
    double w = 0.42 - 0.5 * cos(2.0 * pi * i / (n - 1)) + 0.08 * cos(4.0 * pi * i / (n - 1));
    h[i] = sinc * w;
    sum += h[i];
  }
  for (i = 0; i < n; i++)
    g_r3[i % 3][i / 3] = (float)(h[i] / sum);
  g_r3_built = 1;
}

/* Up by 3: each 16 kHz input sample gives three 48 kHz outputs. */
typedef struct {
  float hist[kPhaseTaps];
} Up3;

static void up3_reset(Up3* u) {
  memset(u, 0, sizeof *u);
}

static void up3_run(Up3* u, const float* in, int n, float* out) {
  int i, p, k;
  for (i = 0; i < n; i++) {
    memmove(u->hist + 1, u->hist, (kPhaseTaps - 1) * sizeof(float));
    u->hist[0] = in[i];
    for (p = 0; p < 3; p++) {
      float acc = 0.0f;
      for (k = 0; k < kPhaseTaps; k++)
        acc += g_r3[p][k] * u->hist[k];
      out[3 * i + p] = 3.0f * acc;
    }
  }
}

/* Down by 3: three 48 kHz inputs give one 16 kHz output. */
typedef struct {
  float hist[3 * kPhaseTaps];
  int phase;
} Down3;

static void down3_reset(Down3* d) {
  memset(d, 0, sizeof *d);
}

static int down3_run(Down3* d, const float* in, int n, float* out) {
  int i, k, produced = 0;
  for (i = 0; i < n; i++) {
    memmove(d->hist + 1, d->hist, (3 * kPhaseTaps - 1) * sizeof(float));
    d->hist[0] = in[i];
    if (++d->phase == 3) {
      float acc = 0.0f;
      d->phase = 0;
      for (k = 0; k < 3 * kPhaseTaps; k++)
        acc += g_r3[k % 3][k / 3] * d->hist[k];
      out[produced++] = acc;
    }
  }
  return produced;
}

static int g_denoise = 1;
static int g_bwe = 1;
static DenoiseState* g_rn;
static int g_rn_first;
static float g_rn_in[kRnFrame];
static int g_rn_fill;
static Up3 g_tx_up;
static Down3 g_tx_down;

static OSCEModel* g_osce;
static silk_OSCE_BWE_struct* g_bwe_state;
static int g_osce_ok;
static opus_int16 g_bwe_in[kBweFrame];
static int g_bwe_fill;
static Up3 g_rx_up;

static void denoise_reset(void) {
  if (g_rn)
    rnnoise_init(g_rn, NULL);
  g_rn_first = 1;
  g_rn_fill = 0;
  up3_reset(&g_tx_up);
  down3_reset(&g_tx_down);
}

static void bwe_reset(void) {
  if (g_bwe_state)
    osce_bwe_reset(g_bwe_state);
  g_bwe_fill = 0;
  up3_reset(&g_rx_up);
}

/* 16 kHz mic in, 16 kHz noise-suppressed mic out. The first RNNoise frame is
 * dropped, as the FreeDV app does, so the model starts from a known state. */
static int denoise_feed(const float* in, int n, float* out) {
  float up[3 * 64];
  float clean[kRnFrame];
  int produced = 0;
  int at = 0;
  while (at < n) {
    int chunk = n - at;
    int i;
    if (chunk > 64)
      chunk = 64;
    up3_run(&g_tx_up, in + at, chunk, up);
    at += chunk;
    for (i = 0; i < 3 * chunk; i++) {
      /* RNNoise wants the 16-bit sample range. */
      g_rn_in[g_rn_fill++] = up[i] * 32768.0f;
      if (g_rn_fill == kRnFrame) {
        int k;
        g_rn_fill = 0;
        rnnoise_process_frame(g_rn, clean, g_rn_in);
        if (g_rn_first) {
          g_rn_first = 0;
          continue;
        }
        for (k = 0; k < kRnFrame; k++)
          clean[k] *= 1.0f / 32768.0f;
        produced += down3_run(&g_tx_down, clean, kRnFrame, out + produced);
      }
    }
  }
  return produced;
}

/* 16 kHz decoded speech in, 48 kHz speech out. With the expander off the
 * speech is only resampled, so the band above 8 kHz stays empty. */
static int widen_speech(const float* in, int n, float* out) {
  int produced = 0;
  int i;
  if (!g_bwe || !g_osce_ok) {
    up3_run(&g_rx_up, in, n, out);
    return 3 * n;
  }
  for (i = 0; i < n; i++) {
    float v = in[i] * 32768.0f;
    if (v > 32767.0f)
      v = 32767.0f;
    if (v < -32768.0f)
      v = -32768.0f;
    g_bwe_in[g_bwe_fill++] = (opus_int16)lrintf(v);
    if (g_bwe_fill == kBweFrame) {
      opus_int16 wide[3 * kBweFrame];
      int k;
      g_bwe_fill = 0;
      osce_bwe(g_osce, g_bwe_state, wide, g_bwe_in, kBweFrame, g_arch);
      for (k = 0; k < 3 * kBweFrame; k++)
        out[produced + k] = (float)wide[k] * (1.0f / 32768.0f);
      produced += 3 * kBweFrame;
    }
  }
  return produced;
}

static void set_err(const char* text) {
  snprintf(g_err, sizeof g_err, "%s", text ? text : "");
}

static void reset_stream(void) {
  g_feat_idx = 0;
  g_speech_n = 0;
  g_rx_n = 0;
  g_fargan_ready = 0;
  g_cont_frames = 0;
  g_sync = 0;
  g_snr = 0.0f;
  agc_reset();
  denoise_reset();
  bwe_reset();
}

static void free_buffers(void) {
  free(g_feat);
  free(g_feat_out);
  free(g_eoo_bits);
  free(g_speech);
  free(g_tx);
  free(g_eoo);
  free(g_rx_scratch);
  free(g_rx_pending);
  g_feat = g_feat_out = g_eoo_bits = g_speech = NULL;
  g_tx = g_eoo = g_rx_scratch = g_rx_pending = NULL;
  g_speech_n = g_speech_cap = g_rx_n = g_rx_cap = 0;
}

static void destroy_codec(void) {
  if (g_lpc) {
    lpcnet_encoder_destroy(g_lpc);
    g_lpc = NULL;
  }
  free(g_fargan);
  g_fargan = NULL;
  if (g_rn) {
    rnnoise_destroy(g_rn);
    g_rn = NULL;
  }
  free(g_osce);
  g_osce = NULL;
  free(g_bwe_state);
  g_bwe_state = NULL;
  g_osce_ok = 0;
  free(g_s16);
  g_s16 = NULL;
  g_s16_cap = 0;
  if (g_r) {
    rade_close(g_r);
    g_r = NULL;
  }
  free_buffers();
  reset_stream();
}

static int ensure_speech(int need) {
  if (need <= g_speech_cap)
    return 1;
  int cap = g_speech_cap > 0 ? g_speech_cap : 4096;
  while (cap < need)
    cap *= 2;
  float* next = (float*)realloc(g_speech, (size_t)cap * sizeof(float));
  if (!next)
    return 0;
  g_speech = next;
  g_speech_cap = cap;
  return 1;
}

static int ensure_rx(int need) {
  if (need <= g_rx_cap)
    return 1;
  int cap = g_rx_cap > 0 ? g_rx_cap : 4096;
  while (cap < need)
    cap *= 2;
  RADE_COMP* next = (RADE_COMP*)realloc(g_rx_pending, (size_t)cap * sizeof(RADE_COMP));
  if (!next)
    return 0;
  g_rx_pending = next;
  g_rx_cap = cap;
  return 1;
}

static void pcm_from_float(const float* src, opus_int16* dst) {
  int i;
  for (i = 0; i < LPCNET_FRAME_SIZE; i++) {
    float v = src[i] * 32768.0f;
    if (v > 32767.0f)
      v = 32767.0f;
    if (v < -32767.0f)
      v = -32767.0f;
    dst[i] = (opus_int16)floor(0.5 + (double)v);
  }
}

static int emit_tx(float* modem, int cap, int written) {
  int n_out;
  int i;
  if (written + g_n_tx > cap)
    return -2;
  n_out = rade_tx(g_r, g_tx, g_feat);
  if (n_out < 0)
    n_out = 0;
  if (n_out > g_n_tx)
    n_out = g_n_tx;
  for (i = 0; i < n_out; i++)
    modem[written + i] = g_tx[i].real;
  g_feat_idx = 0;
  return written + n_out;
}

static int pull_features(float* modem, int cap, int written) {
  while (g_speech_n >= LPCNET_FRAME_SIZE) {
    opus_int16 pcm[LPCNET_FRAME_SIZE];
    if (g_feat_idx + 1 >= g_frames_per && written + g_n_tx > cap)
      break;
    pcm_from_float(g_speech, pcm);
    memmove(g_speech, g_speech + LPCNET_FRAME_SIZE,
            (size_t)(g_speech_n - LPCNET_FRAME_SIZE) * sizeof(float));
    g_speech_n -= LPCNET_FRAME_SIZE;
    lpcnet_compute_single_frame_features(g_lpc, pcm, &g_feat[g_feat_idx * NB_TOTAL_FEATURES], g_arch);
    g_feat_idx++;
    if (g_feat_idx >= g_frames_per) {
      int next = emit_tx(modem, cap, written);
      if (next == -2)
        return written;
      if (next < 0)
        return -1;
      written = next;
    }
  }
  return written;
}

static void synthesize_features(const float* feat, int n_out, float* speech, int cap, int* written) {
  int n_frames = n_out / NB_TOTAL_FEATURES;
  int fi;
  for (fi = 0; fi < n_frames; fi++) {
    const float* frame = feat + fi * NB_TOTAL_FEATURES;
    if (!g_fargan_ready) {
      memcpy(&g_cont[g_cont_frames * NB_TOTAL_FEATURES], frame, NB_TOTAL_FEATURES * sizeof(float));
      if (++g_cont_frames >= 5) {
        float packed[5 * NB_FEATURES];
        float zeros[FARGAN_CONT_SAMPLES];
        int i;
        for (i = 0; i < 5; i++)
          memcpy(&packed[i * NB_FEATURES], &g_cont[i * NB_TOTAL_FEATURES], NB_FEATURES * sizeof(float));
        memset(zeros, 0, sizeof zeros);
        fargan_cont(g_fargan, zeros, packed);
        g_fargan_ready = 1;
      }
      continue;
    }
    if (*written + LPCNET_FRAME_SIZE > cap)
      break;
    fargan_synthesize(g_fargan, speech + *written, frame);
    *written += LPCNET_FRAME_SIZE;
  }
}

/* FreeDV's over-the-air character set: 1-9 are ASCII 38-46, 10-19 the
 * digits, 20-45 the letters, 46 the slash. Anything else is skipped. */
static int ota_char(int c) {
  if (c >= 'a' && c <= 'z')
    c -= 'a' - 'A';
  if (c >= 'A' && c <= 'Z')
    return c - 'A' + 20;
  if (c >= '0' && c <= '9')
    return c - '0' + 10;
  if (c == '/')
    return 46;
  if (c >= 38 && c <= 46)
    return c - 37;
  return 0;
}

static unsigned char crc8_1d(const unsigned char* data, int n) {
  unsigned char crc = 0;
  int i;
  int b;
  for (i = 0; i < n && data[i]; i++) {
    crc ^= data[i];
    for (b = 0; b < 8; b++)
      crc = (unsigned char)((crc & 0x80) ? ((crc << 1) ^ 0x1D) : (crc << 1));
  }
  return crc;
}

/* Fills the rade_n_eoo_bits() floats for one callsign. Returns 0 on success. */
static int callsign_eoo_bits(const char* call, float* out, int n_bits) {
  enum { kChars = 8, kInfo = 56, kCode = 112 };
  unsigned char chars[kChars];
  unsigned char code[kCode];
  unsigned char woven[kCode];
  unsigned long long info = 0;
  int n = 0;
  int i;
  if (n_bits < kCode)
    return -1;
  memset(chars, 0, sizeof chars);
  for (i = 0; call && call[i] && i < kChars; i++) {
    const int c = ota_char((unsigned char)call[i]);
    if (c)
      chars[n++] = (unsigned char)c;
  }
  /* Bits 0-7 are the CRC, then eight 6-bit characters, all LSB first. */
  {
    const unsigned char crc = crc8_1d(chars, n);
    for (i = 0; i < 8; i++)
      code[i] = (unsigned char)((crc >> i) & 1);
    for (i = 8; i < kInfo; i++)
      code[i] = (unsigned char)((chars[(i - 8) / 6] >> ((i - 8) % 6)) & 1);
  }
  for (i = 0; i < kInfo; i++)
    if (code[i])
      info |= 1ULL << i;
  /* Parity: the check matrix's parity half is lower-bidiagonal, so each
   * parity bit is its row's information parity plus the one before it. */
  for (i = 0; i < kInfo; i++) {
    unsigned long long v = info & std_rade_hra_rows[i];
    int p = 0;
    while (v) {
      p ^= 1;
      v &= v - 1;
    }
    if (i > 0)
      p ^= code[kInfo + i - 1];
    code[kInfo + i] = (unsigned char)p;
  }
  /* Bit pairs are spread across the 56 symbols, symbol k to 37k mod 56. */
  for (i = 0; i < kCode / 2; i++) {
    const int to = (37 * i) % (kCode / 2);
    woven[2 * to] = code[2 * i];
    woven[2 * to + 1] = code[2 * i + 1];
  }
  for (i = 0; i < kCode / 2; i++) {
    const int pair = woven[2 * i] * 2 + woven[2 * i + 1];
    static const float re[4] = {1.0f, 0.0f, 0.0f, -1.0f};
    static const float im[4] = {0.0f, 1.0f, -1.0f, 0.0f};
    out[2 * i] = re[pair];
    out[2 * i + 1] = im[pair];
  }
  /* The rest of the frame is a known (1, 0) pattern the receiver uses for noise. */
  for (i = kCode; i < n_bits; i++)
    out[i] = (i % 2) ? 0.0f : 1.0f;
  return 0;
}

static char g_heard[16];
static int g_heard_seq;

static int call_char(int v) {
  if (v >= 1 && v <= 9)
    return v + 37;
  if (v >= 10 && v <= 19)
    return v - 10 + '0';
  if (v >= 20 && v <= 45)
    return v - 20 + 'A';
  if (v == 46)
    return '/';
  return 0;
}

/* Reads a callsign out of an end-of-over: undo the symbol spreading, turn
 * each QPSK symbol into two bit likelihoods, run normalised min-sum over the
 * LDPC(112,56) check matrix, then check the CRC. Returns 1 on a good call. */
static int callsign_from_eoo(const float* eoo, int n_bits, char* out, int cap) {
  enum { kInfo = 56, kCode = 112, kRows = 56, kMaxDeg = 64, kIter = 100 };
  static int cols[kRows][kMaxDeg];
  static int deg[kRows];
  static int built;
  float llr[kCode];
  float total[kCode];
  float msg[kRows][kMaxDeg];
  unsigned char bit[kCode];
  int i, r, k, it;
  if (!out || cap < 2 || n_bits < kCode)
    return 0;
  out[0] = 0;
  if (!built) {
    for (r = 0; r < kRows; r++) {
      int d = 0;
      for (k = 0; k < kInfo; k++)
        if (std_rade_hra_rows[r] >> k & 1ULL)
          cols[r][d++] = k;
      if (r > 0)
        cols[r][d++] = kInfo + r - 1;
      cols[r][d++] = kInfo + r;
      deg[r] = d;
    }
    built = 1;
  }
  /* Code pair i went out on symbol 37i mod 56. Positive means bit 0. */
  for (i = 0; i < kCode / 2; i++) {
    const int at = (37 * i) % (kCode / 2);
    const float re = eoo[2 * at], im = eoo[2 * at + 1];
    llr[2 * i] = re + im;
    llr[2 * i + 1] = re - im;
  }
  memset(msg, 0, sizeof msg);
  for (it = 0; it < kIter; it++) {
    int ok = 1;
    for (i = 0; i < kCode; i++)
      total[i] = llr[i];
    for (r = 0; r < kRows; r++)
      for (k = 0; k < deg[r]; k++)
        total[cols[r][k]] += msg[r][k];
    for (i = 0; i < kCode; i++)
      bit[i] = total[i] < 0.0f;
    for (r = 0; r < kRows && ok; r++) {
      int p = 0;
      for (k = 0; k < deg[r]; k++)
        p ^= bit[cols[r][k]];
      ok = !p;
    }
    if (ok)
      break;
    for (r = 0; r < kRows; r++) {
      float min1 = 1e30f, min2 = 1e30f;
      int minAt = -1, sign = 0;
      float in[kMaxDeg];
      for (k = 0; k < deg[r]; k++) {
        const float v = total[cols[r][k]] - msg[r][k];
        const float a = fabsf(v);
        in[k] = v;
        sign ^= v < 0.0f;
        if (a < min1) {
          min2 = min1;
          min1 = a;
          minAt = k;
        } else if (a < min2) {
          min2 = a;
        }
      }
      for (k = 0; k < deg[r]; k++) {
        const float mag = 0.75f * (k == minAt ? min2 : min1);
        const int s = sign ^ (in[k] < 0.0f);
        msg[r][k] = s ? -mag : mag;
      }
    }
  }
  if (it >= kIter)
    return 0;
  {
    unsigned char chars[8];
    unsigned char crc = 0;
    int n = 0;
    for (i = 0; i < 8; i++)
      crc |= (unsigned char)(bit[i] << i);
    memset(chars, 0, sizeof chars);
    for (i = 8; i < kInfo; i++)
      chars[(i - 8) / 6] |= (unsigned char)(bit[i] << ((i - 8) % 6));
    if (crc8_1d(chars, 8) != crc)
      return 0;
    for (i = 0; i < 8 && chars[i]; i++) {
      const int c = call_char(chars[i]);
      if (c && n + 1 < cap)
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n > 0;
  }
}

/* The last callsign heard in an end-of-over. Returns a count that goes up
 * by one each time a new one is decoded, 0 before the first. */
__declspec(dllexport) int std_rade_heard(char* out, int cap) {
  int seq;
  EnterCriticalSection(&g_lock);
  if (out && cap > 0)
    snprintf(out, (size_t)cap, "%s", g_heard);
  seq = g_heard_seq;
  LeaveCriticalSection(&g_lock);
  return seq;
}

/* Test hook: decode end-of-over floats without an open codec. */
__declspec(dllexport) int std_rade_decode_callsign(const float* eoo, int n_bits, char* out, int cap) {
  return callsign_from_eoo(eoo, n_bits, out, cap);
}

static void apply_callsign(void) {
  if (!g_r || g_v2 || g_n_eoo_bits < 1 || !g_eoo_bits || !g_call[0])
    return;
  if (callsign_eoo_bits(g_call, g_eoo_bits, g_n_eoo_bits) == 0)
    rade_tx_set_eoo_bits(g_r, g_eoo_bits);
}

/* Sets the callsign sent in the V1 end-of-over. V2 has no text channel. */
__declspec(dllexport) void std_rade_set_callsign(const char* call) {
  EnterCriticalSection(&g_lock);
  snprintf(g_call, sizeof g_call, "%s", call ? call : "");
  apply_callsign();
  LeaveCriticalSection(&g_lock);
}

/* Test hook: the end-of-over floats for a callsign, without an open codec. */
__declspec(dllexport) int std_rade_callsign_bits(const char* call, float* out, int n_bits) {
  return callsign_eoo_bits(call, out, n_bits);
}

/* Noise suppression on the mic and bandwidth expansion of decoded speech.
 * Both are on by default, as in the FreeDV app. */
__declspec(dllexport) void std_rade_set_options(int denoise, int bwe) {
  EnterCriticalSection(&g_lock);
  if (g_denoise != (denoise ? 1 : 0)) {
    g_denoise = denoise ? 1 : 0;
    denoise_reset();
  }
  if (g_bwe != (bwe ? 1 : 0)) {
    g_bwe = bwe ? 1 : 0;
    bwe_reset();
  }
  LeaveCriticalSection(&g_lock);
}

/* Speech out of std_rade_rx is at this rate. */
__declspec(dllexport) int std_rade_speech_rate(void) {
  return 48000;
}

__declspec(dllexport) const char* std_rade_error(void) {
  return g_err;
}

__declspec(dllexport) int std_rade_open(int v2, int no_bpf) {
  char model[4];
  int flags;
  EnterCriticalSection(&g_lock);
  destroy_codec();
  rade_initialize();
  flags = RADE_VERBOSE_0;
  if (v2)
    flags |= RADE_MODE_V2;
  /* V1 has no BPF flag in this rade_c. The checkbox is stored by STD. */
  if (v2 && no_bpf)
    flags |= RADE_NO_TX_BPF;
  model[0] = 0;
  g_r = rade_open(model, flags);
  if (!g_r) {
    set_err("rade_open failed");
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  g_v2 = v2 ? 1 : 0;
  g_n_tx = rade_n_tx_out(g_r);
  g_n_eoo = rade_n_tx_eoo_out(g_r);
  g_n_feat = rade_n_features_in_out(g_r);
  g_nin_max = rade_nin_max(g_r);
  g_n_eoo_bits = rade_n_eoo_bits(g_r);
  g_frames_per = (NB_TOTAL_FEATURES > 0) ? g_n_feat / NB_TOTAL_FEATURES : 0;
  if (g_n_tx < 1 || g_n_eoo < 1 || g_frames_per < 1 || g_nin_max < 1 ||
      g_frames_per * NB_TOTAL_FEATURES != g_n_feat) {
    destroy_codec();
    set_err("rade frame size is unusable");
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  g_arch = opus_select_arch();
  g_lpc = lpcnet_encoder_create();
  g_fargan = (FARGANState*)calloc(1, sizeof(FARGANState));
  g_feat = (float*)calloc((size_t)g_n_feat, sizeof(float));
  g_feat_out = (float*)calloc((size_t)g_n_feat, sizeof(float));
  g_tx = (RADE_COMP*)calloc((size_t)g_n_tx, sizeof(RADE_COMP));
  g_eoo = (RADE_COMP*)calloc((size_t)g_n_eoo, sizeof(RADE_COMP));
  g_rx_scratch = (RADE_COMP*)calloc((size_t)g_nin_max, sizeof(RADE_COMP));
  if (g_n_eoo_bits > 0)
    g_eoo_bits = (float*)calloc((size_t)g_n_eoo_bits, sizeof(float));
  if (!g_lpc || !g_fargan || !g_feat || !g_feat_out || !g_tx || !g_eoo || !g_rx_scratch ||
      (g_n_eoo_bits > 0 && !g_eoo_bits)) {
    destroy_codec();
    set_err("codec allocation failed");
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  fargan_init(g_fargan);
  if (!g_r3_built)
    build_r3();
  g_rn = rnnoise_create(NULL);
  g_osce = (OSCEModel*)calloc(1, sizeof(OSCEModel));
  g_bwe_state = (silk_OSCE_BWE_struct*)calloc(1, sizeof(silk_OSCE_BWE_struct));
  g_osce_ok = g_osce && g_bwe_state && osce_load_models(g_osce, NULL, 0) == 0;
  reset_stream();
  {
    float zeros[FARGAN_CONT_SAMPLES];
    float none[5 * NB_FEATURES];
    memset(zeros, 0, sizeof zeros);
    memset(none, 0, sizeof none);
    fargan_cont(g_fargan, zeros, none);
    g_fargan_ready = 1;
  }
  apply_callsign();
  snprintf(g_err, sizeof g_err, "open v%d features %d tx %d eoo %d nin %d rnnoise %s bwe %s",
           g_v2 ? 2 : 1, g_n_feat, g_n_tx, g_n_eoo, g_nin_max, g_rn ? "ok" : "missing",
           g_osce_ok ? "ok" : "missing");
  LeaveCriticalSection(&g_lock);
  return 0;
}

__declspec(dllexport) void std_rade_close(void) {
  EnterCriticalSection(&g_lock);
  destroy_codec();
  rade_finalize();
  LeaveCriticalSection(&g_lock);
}

__declspec(dllexport) int std_rade_tx(const float* speech, int n, float* modem, int cap) {
  int written;
  EnterCriticalSection(&g_lock);
  if (!g_r || !g_lpc || !modem || cap < 1) {
    set_err("tx is not open");
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  if (n > 0 && speech) {
    if (!ensure_speech(g_speech_n + 2 * n + 2 * kRnFrame + kAgcBlock)) {
      set_err("speech buffer failed");
      LeaveCriticalSection(&g_lock);
      return -1;
    }
    if (g_denoise && g_rn) {
      /* Denoise into the free end of the buffer, then level in place. */
      float* tail = g_speech + g_speech_n + n + kRnFrame;
      const int clean = denoise_feed(speech, n, tail);
      g_speech_n += agc_feed(tail, clean, g_speech + g_speech_n);
    } else {
      g_speech_n += agc_feed(speech, n, g_speech + g_speech_n);
    }
  }
  written = pull_features(modem, cap, 0);
  if (written < 0)
    set_err("rade_tx failed");
  LeaveCriticalSection(&g_lock);
  return written;
}

__declspec(dllexport) int std_rade_finish(float* modem, int cap) {
  int written = 0;
  int n_out;
  int i;
  EnterCriticalSection(&g_lock);
  if (!g_r || !modem || cap < 1) {
    set_err("finish is not open");
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  /* Speech not yet a whole modem frame is dropped, as the FreeDV app does,
   * and the end-of-over follows the last full frame. */
  g_speech_n = 0;
  g_feat_idx = 0;
  agc_reset();
  denoise_reset();
  if (written + g_n_eoo > cap) {
    set_err("end-of-over frame did not fit");
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  n_out = rade_tx_eoo(g_r, g_eoo);
  if (n_out < 0)
    n_out = 0;
  if (n_out > g_n_eoo)
    n_out = g_n_eoo;
  for (i = 0; i < n_out; i++)
    modem[written + i] = g_eoo[i].real;
  written += n_out;
  g_feat_idx = 0;
  LeaveCriticalSection(&g_lock);
  return written;
}

__declspec(dllexport) int std_rade_rx(const float* modem, int n, float* speech, int cap) {
  int written = 0;
  int cap16;
  int i;
  EnterCriticalSection(&g_lock);
  if (!g_r || !g_fargan || !speech || cap < 1) {
    set_err("rx is not open");
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  if (n > 0 && modem) {
    if (g_rx_n > 1000000) {
      set_err("rx backlog");
      LeaveCriticalSection(&g_lock);
      return -1;
    }
    if (!ensure_rx(g_rx_n + n)) {
      set_err("rx buffer failed");
      LeaveCriticalSection(&g_lock);
      return -1;
    }
    for (i = 0; i < n; i++) {
      g_rx_pending[g_rx_n + i].real = modem[i];
      g_rx_pending[g_rx_n + i].imag = 0.0f;
    }
    g_rx_n += n;
  }
  /* FARGAN speaks 16 kHz; that becomes 48 kHz on the way out. Leave room
   * for a part frame held by the expander. */
  cap16 = cap / 3 - kBweFrame;
  if (cap16 < g_frames_per * LPCNET_FRAME_SIZE) {
    LeaveCriticalSection(&g_lock);
    return 0;
  }
  if (cap16 > g_s16_cap) {
    float* next = (float*)realloc(g_s16, (size_t)cap16 * sizeof(float));
    if (!next) {
      set_err("speech buffer failed");
      LeaveCriticalSection(&g_lock);
      return -1;
    }
    g_s16 = next;
    g_s16_cap = cap16;
  }
  for (;;) {
    int nin = rade_nin(g_r);
    int has_eoo = 0;
    int n_out;
    if (nin < 1)
      break;
    if (nin > g_nin_max) {
      set_err("rade_nin is past the buffer");
      LeaveCriticalSection(&g_lock);
      return -1;
    }
    if (g_rx_n < nin)
      break;
    if (written + g_frames_per * LPCNET_FRAME_SIZE > cap16)
      break;
    memcpy(g_rx_scratch, g_rx_pending, (size_t)nin * sizeof(RADE_COMP));
    memmove(g_rx_pending, g_rx_pending + nin, (size_t)(g_rx_n - nin) * sizeof(RADE_COMP));
    g_rx_n -= nin;
    n_out = rade_rx(g_r, g_feat_out, &has_eoo, g_eoo_bits, g_rx_scratch);
    g_sync = rade_sync(g_r);
    if (g_sync)
      g_snr = rade_snrdB_3k_est(g_r);
    if (has_eoo) {
      /* The end-of-over holds the callsign, not speech. */
      char call[16];
      if (!g_v2 && g_eoo_bits && callsign_from_eoo(g_eoo_bits, g_n_eoo_bits, call, sizeof call)) {
        snprintf(g_heard, sizeof g_heard, "%s", call);
        g_heard_seq++;
      }
    } else if (n_out > 0) {
      synthesize_features(g_feat_out, n_out, g_s16, cap16, &written);
    }
  }
  written = written > 0 ? widen_speech(g_s16, written, speech) : 0;
  LeaveCriticalSection(&g_lock);
  return written;
}

__declspec(dllexport) int std_rade_sync(void) {
  int sync;
  EnterCriticalSection(&g_lock);
  sync = g_r ? g_sync : 0;
  LeaveCriticalSection(&g_lock);
  return sync;
}

__declspec(dllexport) float std_rade_snr(void) {
  float snr;
  EnterCriticalSection(&g_lock);
  snr = g_r && g_sync ? g_snr : 0.0f;
  LeaveCriticalSection(&g_lock);
  return snr;
}

static double rms(const float* x, int n) {
  double e = 0.0;
  int i;
  for (i = 0; i < n; i++)
    e += (double)x[i] * (double)x[i];
  return n > 0 ? sqrt(e / n) : 0.0;
}

/* Power of x at f Hz, by Goertzel. */
static double tone_power(const float* x, int n, double f, double fs) {
  const double w = 2.0 * 3.14159265358979323846 * f / fs;
  const double c = 2.0 * cos(w);
  double s1 = 0.0, s2 = 0.0;
  int i;
  for (i = 0; i < n; i++) {
    const double s0 = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n);
}

/* The 3:1 converters keep a tone, RNNoise takes noise down, and the
 * expander puts energy above 8 kHz that plain resampling does not. */
static int check_dsp(void) {
  enum { kN = 16000 };
  static float in[kN], mid[3 * kN], out[3 * kN + 3 * kBweFrame];
  const int saved_bwe = g_bwe;
  double a, b, hi_on = 0.0, hi_off = 0.0;
  unsigned seed = 12345u;
  int i, n, m;
  if (!g_rn || !g_osce_ok) {
    snprintf(g_err, sizeof g_err, "rnnoise %s bwe %s", g_rn ? "ok" : "missing",
             g_osce_ok ? "ok" : "missing");
    return -1;
  }
  for (i = 0; i < kN; i++)
    in[i] = 0.3f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / 16000.0f);
  up3_reset(&g_tx_up);
  down3_reset(&g_tx_down);
  up3_run(&g_tx_up, in, kN, mid);
  n = down3_run(&g_tx_down, mid, 3 * kN, out);
  a = rms(in + kN / 2, kN / 2);
  b = rms(out + n / 2, n - n / 2);
  if (n != kN || b < 0.95 * a || b > 1.05 * a) {
    snprintf(g_err, sizeof g_err, "3:1 converters lost the tone (%d samples, %.3f vs %.3f)", n, b, a);
    return -1;
  }
  for (i = 0; i < kN; i++) {
    seed = seed * 1664525u + 1013904223u;
    in[i] = 0.05f * ((float)(seed >> 8) / 8388608.0f - 1.0f);
  }
  denoise_reset();
  n = denoise_feed(in, kN, out);
  a = rms(in, kN);
  b = rms(out + n / 2, n - n / 2);
  denoise_reset();
  if (n < kN / 2 || b > 0.3 * a) {
    snprintf(g_err, sizeof g_err, "rnnoise did not take noise down (%d samples, %.4f vs %.4f)", n, b, a);
    return -1;
  }
  /* A 150 Hz buzz with all its harmonics, like a voiced vowel. */
  for (i = 0; i < kN; i++) {
    float phase = (float)i * 150.0f / 16000.0f;
    float frac = phase - floorf(phase);
    in[i] = 0.4f * (frac - 0.5f);
  }
  for (m = 0; m < 2; m++) {
    double hi = 0.0;
    g_bwe = m == 0;
    bwe_reset();
    n = widen_speech(in, kN, out);
    for (i = 0; i < 4; i++)
      hi += tone_power(out + n / 2, n / 2, 10050.0 + 1500.0 * i, 48000.0);
    if (m == 0)
      hi_on = hi;
    else
      hi_off = hi;
  }
  g_bwe = saved_bwe;
  bwe_reset();
  if (n < 3 * kN - 3 * kBweFrame || hi_on < 100.0 * hi_off || hi_on <= 0.0) {
    snprintf(g_err, sizeof g_err, "bandwidth expander added nothing above 8 kHz (%.3g vs %.3g)", hi_on,
             hi_off);
    return -1;
  }
  return 0;
}

__declspec(dllexport) int std_rade_selftest(void) {
  enum { kSpeech = 12800, kCap = 65536 };
  float* speech = NULL;
  float* modem = NULL;
  float* back = NULL;
  int i;
  int produced = 0;
  int flushed = 0;
  int total;
  int got = 0;
  int off;
  double energy = 0.0;
  int rc = 1;
  char saved_call[sizeof g_call];
  int heard_before = g_heard_seq;
  memcpy(saved_call, g_call, sizeof g_call);
  speech = (float*)malloc(sizeof(float) * kSpeech);
  modem = (float*)malloc(sizeof(float) * kCap);
  back = (float*)malloc(sizeof(float) * kCap);
  if (!speech || !modem || !back) {
    set_err("selftest allocation failed");
    goto done;
  }
  for (i = 0; i < kSpeech; i++) {
    float t = (float)i / 16000.0f;
    float env = 0.5f + 0.5f * sinf(2.0f * 3.14159265f * 3.0f * t);
    float phase = t * 120.0f;
    float frac = phase - floorf(phase);
    float pulse = frac < 0.04f ? (1.0f - frac / 0.04f) : 0.0f;
    speech[i] = 0.25f * env * pulse;
  }
  /* The end-of-over must carry a callsign and read back. */
  snprintf(g_call, sizeof g_call, "%s", "N8SDR/VE");
  if (std_rade_open(0, 0) != 0)
    goto done;
  if (check_dsp() != 0)
    goto done;
  produced = std_rade_tx(speech, kSpeech, modem, kCap);
  if (produced < 0)
    goto done;
  flushed = std_rade_finish(modem + produced, kCap - produced);
  if (flushed < 0)
    goto done;
  total = produced + flushed;
  for (i = 0; i < total; i++)
    energy += (double)modem[i] * (double)modem[i];
  if (total < 800 || energy < 1.0) {
    snprintf(g_err, sizeof g_err, "tx energy low samples %d energy %.3f", total, energy);
    goto done;
  }
  /* 200 ms of silence after the end-of-over, as goes out on the air. */
  if (total + 1600 <= kCap) {
    memset(modem + total, 0, 1600 * sizeof(float));
    total += 1600;
  }
  off = 0;
  while (off < total) {
    int chunk = total - off;
    int n;
    if (chunk > 800)
      chunk = 800;
    n = std_rade_rx(modem + off, chunk, back, kCap);
    if (n < 0)
      goto done;
    got += n;
    off += chunk;
  }
  if (g_heard_seq == heard_before || strcmp(g_heard, "N8SDR/VE") != 0) {
    snprintf(g_err, sizeof g_err, "end-of-over callsign did not read back (%s)", g_heard);
    goto done;
  }
  snprintf(g_err, sizeof g_err, "modem %d energy %.1f speech %d heard %s", total, energy, got, g_heard);
  rc = 0;
done:
  std_rade_close();
  snprintf(g_call, sizeof g_call, "%s", saved_call);
  free(speech);
  free(modem);
  free(back);
  return rc;
}
