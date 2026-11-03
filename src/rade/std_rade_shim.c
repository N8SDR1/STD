/* Copyright (C) 2026 Rick Langford (N8SDR)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cdecl wrapper around freedv/rade_c so MSVC can LoadLibrary it.
 * The only source is https://github.com/freedv/rade_c at
 * c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9
 *
 * Speech in and out is 16 kHz float, about [-1, 1].
 * Modem in and out is 8 kHz real, the rade_tx real part (nominal 1).
 * STD applies the TCI scales. This file does not.
 * End-of-over bits are not set.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "fargan.h"
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
static RADE_COMP* g_tx;
static RADE_COMP* g_eoo;
static RADE_COMP* g_rx_scratch;
static RADE_COMP* g_rx_pending;
static int g_rx_n;
static int g_rx_cap;
static float g_cont[5 * NB_TOTAL_FEATURES];
static int g_sync;
static float g_snr;

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
  reset_stream();
  snprintf(g_err, sizeof g_err, "open v%d features %d tx %d eoo %d nin %d", g_v2 ? 2 : 1, g_n_feat,
           g_n_tx, g_n_eoo, g_nin_max);
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
    if (!ensure_speech(g_speech_n + n)) {
      set_err("speech buffer failed");
      LeaveCriticalSection(&g_lock);
      return -1;
    }
    memcpy(g_speech + g_speech_n, speech, (size_t)n * sizeof(float));
    g_speech_n += n;
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
  /* A short tail under one LPC frame is dropped, matching rade_tx_wav. */
  g_speech_n = 0;
  if (g_feat_idx > 0) {
    memset(&g_feat[g_feat_idx * NB_TOTAL_FEATURES], 0,
           (size_t)(g_frames_per - g_feat_idx) * NB_TOTAL_FEATURES * sizeof(float));
    written = emit_tx(modem, cap, 0);
    if (written < 0) {
      set_err("finish frame did not fit");
      LeaveCriticalSection(&g_lock);
      return -1;
    }
  }
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
    if (written + g_frames_per * LPCNET_FRAME_SIZE > cap)
      break;
    memcpy(g_rx_scratch, g_rx_pending, (size_t)nin * sizeof(RADE_COMP));
    memmove(g_rx_pending, g_rx_pending + nin, (size_t)(g_rx_n - nin) * sizeof(RADE_COMP));
    g_rx_n -= nin;
    n_out = rade_rx(g_r, g_feat_out, &has_eoo, g_eoo_bits, g_rx_scratch);
    g_sync = rade_sync(g_r);
    if (g_sync)
      g_snr = rade_snrdB_3k_est(g_r);
    if (n_out > 0)
      synthesize_features(g_feat_out, n_out, speech, cap, &written);
    (void)has_eoo;
  }
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
  if (std_rade_open(0, 0) != 0)
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
  snprintf(g_err, sizeof g_err, "modem %d energy %.1f speech %d sync %d", total, energy, got,
           std_rade_sync());
  rc = 0;
done:
  std_rade_close();
  free(speech);
  free(modem);
  free(back);
  return rc;
}
