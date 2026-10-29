// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Ft8Decode.h"

#include "ft8/ft8_api.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

constexpr int kAudioRate = 12000;
// decoderft8.cpp sync8 reads dd[] through index 179999, then the pass
// writes dd[zero_dd .. 180009]. MSHV's buffer is 16*12000 and the tail
// is padded; c_dd stays the real length. A shorter block is a heap smash
// after the first line is printed. dd1[] is 182600, so c_dd must stay
// below that.
// 79 FT8 symbols at 160 ms. The single pass starts once that waveform is in.
constexpr int kDecodeSamples = 12640 * kAudioRate / 1000;
constexpr int kMinKeep = 6 * kAudioRate;
// JT65 keeps 60 s and Q65 keeps 30 s. 75 s leaves room for the clock.
constexpr int kRing = 75 * kAudioRate;
constexpr int kDecoderCapacity = 16 * kAudioRate;
constexpr double kDecoderPad = 1e-8;
constexpr qint64 kPeriodMs = 15000;
constexpr qint64 kDecodeAtMs = 12640;
// MSHV scales int16 audio by 1/2560, which is 12.8 at full scale.
constexpr double kMshvScale = 32768.0 * 0.000390625;

struct Hit {
  QString time;
  int db = 0;
  QString dt;
  QString message;
  int freqHz = 0;
};

struct Job {
  std::vector<double> samples;
  int lowHz = 100;
  int highHz = 3500;
  int rxHz = 1200;
  int txHz = 1500;
  int depth = 1;
  int ap = 1;
  int id3dec = 100;
  int kind = 0;
  int multi = 0;
  int submode = 1;
  int modid = 14;
  int periodSec = 15;
  QString stamp;
};

class Worker : public QThread {
public:
  explicit Worker(QObject* parent = nullptr) : QThread(parent) {
    setStackSize(64 * 1024 * 1024);
  }

  bool submit(Job job) {
    QMutexLocker lock(&mutex_);
    if (queue_.size() >= 1)
      return false;
    queue_.push_back(std::move(job));
    cond_.wakeOne();
    return true;
  }

  void stop() {
    {
      QMutexLocker lock(&mutex_);
      stop_ = true;
      cond_.wakeOne();
    }
    wait(8000);
    if (isRunning()) {
      terminate();
      wait(1000);
    }
  }

  void run() override {
#ifdef Q_OS_WIN
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/ft8dec.dll");
    const HMODULE module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                              LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
      emitHit(Hit{QString(), 0, QString(),
                  QStringLiteral("FT8 decoder did not load (%1).").arg(GetLastError()), 0});
      return;
    }
    auto open = reinterpret_cast<decltype(&ft8_session_open)>(GetProcAddress(module, "ft8_session_open"));
    auto close =
        reinterpret_cast<decltype(&ft8_session_close)>(GetProcAddress(module, "ft8_session_close"));
    auto decode =
        reinterpret_cast<decltype(&ft8_session_decode)>(GetProcAddress(module, "ft8_session_decode"));
    auto decode4 =
        reinterpret_cast<decltype(&ft4_session_decode)>(GetProcAddress(module, "ft4_session_decode"));
    auto decode2 =
        reinterpret_cast<decltype(&ft2_session_decode)>(GetProcAddress(module, "ft2_session_decode"));
    auto decode65 =
        reinterpret_cast<decltype(&jt65_decode_wave)>(GetProcAddress(module, "jt65_decode_wave"));
    auto decode65q =
        reinterpret_cast<decltype(&q65_decode_wave)>(GetProcAddress(module, "q65_decode_wave"));
    auto setMulti = reinterpret_cast<decltype(&ft8_session_set_multi_answer)>(
        GetProcAddress(module, "ft8_session_set_multi_answer"));
    if (!open || !close || !decode) {
      emitHit(Hit{QString(), 0, QString(), QStringLiteral("FT8 decoder did not load."), 0});
      FreeLibrary(module);
      return;
    }
    Ft8Session* session = open();
    if (!session) {
      emitHit(Hit{QString(), 0, QString(), QStringLiteral("FT8 decoder did not load."), 0});
      FreeLibrary(module);
      return;
    }
    for (;;) {
      Job job;
      {
        QMutexLocker lock(&mutex_);
        while (queue_.empty() && !stop_)
          cond_.wait(&mutex_);
        if (stop_ && queue_.empty())
          break;
        job = std::move(queue_.front());
        queue_.pop_front();
      }
      int minimum = kMinKeep;
      if (job.kind == 2)
        minimum = 30000;
      else if (job.kind == 3)
        minimum = 60 * kAudioRate;
      else if (job.kind == 4)
        minimum = 30 * kAudioRate;
      if (job.samples.size() < static_cast<size_t>(minimum))
        continue;
      double sum = 0.0;
      for (double& sample : job.samples) {
        sample *= kMshvScale;
        sum += sample;
      }
      const double average = sum / static_cast<double>(job.samples.size());
      for (double& sample : job.samples)
        sample -= average;
      const int realCount = static_cast<int>(job.samples.size());
      if (job.kind < 3 && job.samples.size() < static_cast<size_t>(kDecoderCapacity))
        job.samples.resize(static_cast<size_t>(kDecoderCapacity), kDecoderPad);
      const QByteArray stamp = job.stamp.toLatin1();
      const auto take = [](const char* time, int db, const char* dt, const char* message, int freqHz,
                           void* user) {
        auto* worker = static_cast<Worker*>(user);
        Hit hit;
        hit.time = QString::fromUtf8(time);
        hit.db = db;
        hit.dt = QString::fromUtf8(dt);
        hit.message = QString::fromUtf8(message);
        hit.freqHz = freqHz;
        worker->emitHit(hit);
      };
      if (setMulti)
        setMulti(session, job.multi);
      if (job.kind == 2) {
        if (!decode2) {
          emitHit(Hit{QString(), 0, QString(), QStringLiteral("FT2 decoder did not load."), 0});
          continue;
        }
        decode2(session, job.samples.data(), realCount, job.lowHz, job.highHz, job.rxHz, job.txHz,
                job.depth, job.ap, stamp.constData(), take, this);
        continue;
      }
      if (job.kind == 3) {
        if (!decode65) {
          emitHit(Hit{QString(), 0, QString(), QStringLiteral("JT65 decoder did not load."), 0});
          continue;
        }
        decode65(job.samples.data(), realCount, job.rxHz, 2000, job.submode, stamp.constData(), take,
                 this);
        continue;
      }
      if (job.kind == 4) {
        if (!decode65q) {
          emitHit(Hit{QString(), 0, QString(), QStringLiteral("Q65 decoder did not load."), 0});
          continue;
        }
        decode65q(job.samples.data(), realCount, job.lowHz, job.highHz, job.rxHz, job.modid,
                  job.periodSec, stamp.constData(), take, this);
        continue;
      }
      if (job.kind == 1) {
        if (!decode4) {
          emitHit(Hit{QString(), 0, QString(), QStringLiteral("FT4 decoder did not load."), 0});
          continue;
        }
        decode4(session, job.samples.data(), realCount, job.lowHz, job.highHz, job.rxHz, job.txHz,
                job.depth, job.ap, stamp.constData(),
                take, this);
        continue;
      }
      decode(session, job.samples.data(), realCount, job.lowHz, job.highHz, job.rxHz, job.txHz,
             job.depth, job.ap, job.id3dec, stamp.constData(), take, this);
    }
    close(session);
    FreeLibrary(module);
#else
    emitHit(Hit{QString(), 0, QString(), QStringLiteral("FT8 decoder did not load."), 0});
#endif
  }

  void emitHit(const Hit& hit) {
    if (onHit)
      onHit(hit);
  }

  std::function<void(const Hit&)> onHit;

private:

  QMutex mutex_;
  QWaitCondition cond_;
  std::deque<Job> queue_;
  bool stop_ = false;
};

}  // namespace

struct Ft8Decode::Private {
  Worker worker;
  QTimer timer;
  QMutex mutex;
  std::vector<double> ring = std::vector<double>(kRing, 0.0);
  qint64 total = 0;
  qint64 newestMs = 0;
  qint64 stagePeriod = -1;
  int stageDone = 0;
  int decim = 0;
  double residual = 0;
  int rate = 0;
  bool armed = false;
  Period mode = Period::Ft8;
  bool threeStage = true;
  int depth = 1;
  bool ap = true;
  bool multiAnswer = false;
  int clockOffsetMs = 0;
  int lowHz = 100;
  int highHz = 3500;
  int rxHz = 1200;
  int txHz = 1500;
  bool workerStarted = false;
};

Ft8Decode::Ft8Decode(QObject* parent) : QObject(parent), d_(new Private) {
  d_->worker.onHit = [this](const Hit& hit) {
    emit decoded(hit.time, hit.db, hit.dt, hit.message, hit.freqHz);
  };
  connect(&d_->timer, &QTimer::timeout, this, [this] { poll(); });
  d_->timer.start(50);
}

Ft8Decode::~Ft8Decode() {
  d_->timer.stop();
  d_->worker.stop();
  delete d_;
}

void Ft8Decode::setArmed(bool armed, int lowHz, int highHz) {
  QMutexLocker lock(&d_->mutex);
  d_->armed = armed;
  if (lowHz < 0)
    lowHz = 0;
  if (highHz > 5000)
    highHz = 5000;
  if (highHz < lowHz + 50)
    highHz = lowHz + 50;
  d_->lowHz = lowHz;
  d_->highHz = highHz;
}

void Ft8Decode::setSpot(int rxHz, int txHz) {
  QMutexLocker lock(&d_->mutex);
  d_->rxHz = std::clamp(rxHz, 0, 5000);
  d_->txHz = std::clamp(txHz, 0, 5000);
}

void Ft8Decode::setMode(Period mode) {
  QMutexLocker lock(&d_->mutex);
  if (d_->mode == mode)
    return;
  d_->mode = mode;
  d_->stagePeriod = -1;
  d_->stageDone = 0;
}

void Ft8Decode::setMultiAnswer(bool on) {
  QMutexLocker lock(&d_->mutex);
  d_->multiAnswer = on;
}

void Ft8Decode::setOptions(bool threeStage, int depth, bool ap, int clockOffsetMs) {
  QMutexLocker lock(&d_->mutex);
  d_->threeStage = threeStage;
  d_->depth = std::clamp(depth, 1, 3);
  d_->ap = ap;
  d_->clockOffsetMs = std::clamp(clockOffsetMs, -5000, 5000);
}

void Ft8Decode::clear() {
  QMutexLocker lock(&d_->mutex);
  d_->total = 0;
  d_->newestMs = 0;
  d_->stagePeriod = -1;
  d_->stageDone = 0;
  d_->decim = 0;
  d_->residual = 0;
  d_->rate = 0;
}

void Ft8Decode::ensureWorker() {
  if (d_->workerStarted)
    return;
  d_->workerStarted = true;
  d_->worker.start();
}

void Ft8Decode::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!samples || count <= 0 || sampleRateHz < 8000)
    return;
  QMutexLocker lock(&d_->mutex);
  if (!d_->armed)
    return;
  if (d_->rate != sampleRateHz) {
    d_->rate = sampleRateHz;
    d_->decim = 0;
    d_->residual = 0;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  auto store = [this](float sample) {
    d_->ring[static_cast<size_t>(d_->total % kRing)] = sample;
    ++d_->total;
  };
  if (sampleRateHz % kAudioRate == 0) {
    const int step = sampleRateHz / kAudioRate;
    for (int i = 0; i < count; ++i) {
      if (d_->decim == 0)
        store(samples[i]);
      d_->decim = (d_->decim + 1) % step;
    }
  } else {
    const double advance = static_cast<double>(sampleRateHz) / static_cast<double>(kAudioRate);
    double cursor = d_->residual;
    while (cursor < count) {
      const int index = static_cast<int>(cursor);
      const double fraction = cursor - index;
      const float left = samples[index];
      const float right = samples[index + 1 < count ? index + 1 : index];
      store(static_cast<float>(left + (right - left) * fraction));
      cursor += advance;
    }
    d_->residual = cursor - count;
  }
  d_->newestMs = now;
}

void Ft8Decode::poll() {
  Job job;
  bool ready = false;
  {
    QMutexLocker lock(&d_->mutex);
    if (!d_->armed || d_->newestMs <= 0)
      return;
    const qint64 clock = d_->newestMs + d_->clockOffsetMs;
    const Period mode = d_->mode;
    // FT4 is 7.5 s. MSHV starts one decode once the period clock passes
    // 6100 ms. FT2 is 3.75 s and starts once the clock passes 3400 ms.
    // The three-stage timer is FT8 only.
    qint64 periodMs = kPeriodMs;
    int kind = 0;
    if (mode == Period::Ft4) {
      periodMs = 7500;
      kind = 1;
    } else if (mode == Period::Ft2) {
      periodMs = 3750;
      kind = 2;
    } else if (mode == Period::Jt65A || mode == Period::Jt65B || mode == Period::Jt65C) {
      periodMs = 60000;
      kind = 3;
    } else if (mode == Period::Q65A || mode == Period::Q65B || mode == Period::Q65C ||
               mode == Period::Q65D) {
      periodMs = 30000;
      kind = 4;
    }
    const qint64 periodStart = (clock / periodMs) * periodMs;
    if (periodStart != d_->stagePeriod) {
      d_->stagePeriod = periodStart;
      d_->stageDone = 0;
    }
    int id3dec = 100;
    int samples = kDecodeSamples;
    qint64 atMs = kDecodeAtMs;
    int submode = 1;
    int modid = 14;
    int periodSec = 15;
    if (kind == 3 || kind == 4) {
      if (d_->stageDone >= 1)
        return;
      id3dec = 0;
      if (kind == 3) {
        atMs = 52000;
        samples = 60 * kAudioRate;
        periodSec = 60;
        if (mode == Period::Jt65C)
          submode = 4;
        else if (mode == Period::Jt65B)
          submode = 2;
      } else {
        atMs = 27000;
        samples = 30 * kAudioRate;
        periodSec = 30;
        if (mode == Period::Q65D)
          modid = 17;
        else if (mode == Period::Q65C)
          modid = 16;
        else if (mode == Period::Q65B)
          modid = 15;
      }
    } else if (mode == Period::Ft4 || mode == Period::Ft2) {
      if (d_->stageDone >= 1)
        return;
      id3dec = 0;
      atMs = mode == Period::Ft2 ? 3400 : 6100;
      samples = static_cast<int>(atMs) * kAudioRate / 1000;
    } else if (d_->threeStage) {
      struct Step {
        int id;
        qint64 at;
        int count;
      };
      // MSHV lists the period that is still on the air. main_ms.cpp starts
      // the first pass once time_pos_1period > 11820 ms (141696 samples,
      // before the 12.64 s tone run ends). display_ms.cpp then waits 1740 ms
      // (162432 samples) and 1000 ms (172800 samples). Lines are emitted
      // from inside the pass, not after a 15 s boundary.
      const Step steps[] = {{1, 11820, 141696}, {2, 13560, 162432}, {3, 14560, 172800}};
      if (d_->stageDone >= 3)
        return;
      const Step& step = steps[d_->stageDone];
      id3dec = step.id;
      atMs = step.at;
      samples = step.count;
    } else if (d_->stageDone >= 1) {
      return;
    }
    if (clock < periodStart + atMs)
      return;
    const double samplesBack = static_cast<double>(clock - periodStart) * 12.0;
    const qint64 startIdx = (d_->total - 1) - static_cast<qint64>(std::llround(samplesBack));
    const qint64 oldest = d_->total > kRing ? d_->total - kRing : 0;
    job.samples.assign(static_cast<size_t>(samples), 0.0);
    int real = 0;
    for (int n = 0; n < samples; ++n) {
      const qint64 index = startIdx + n;
      if (index < oldest || index >= d_->total)
        continue;
      job.samples[static_cast<size_t>(n)] = d_->ring[static_cast<size_t>(index % kRing)];
      ++real;
    }
    const int slack = kind >= 3 ? samples / 5 : 1200;
    if (real < samples - slack)
      return;
    job.lowHz = d_->lowHz;
    job.highHz = d_->highHz;
    job.rxHz = d_->rxHz;
    job.txHz = d_->txHz;
    job.depth = d_->depth;
    job.ap = d_->ap ? 1 : 0;
    job.multi = d_->multiAnswer ? 1 : 0;
    job.id3dec = id3dec;
    job.kind = kind;
    job.submode = submode;
    job.modid = modid;
    job.periodSec = periodSec;
    job.stamp = QDateTime::fromMSecsSinceEpoch(periodStart, QTimeZone::utc()).toString(QStringLiteral("hhmmss"));
    d_->stageDone += 1;
    ready = true;
  }
  if (!ready)
    return;
  ensureWorker();
  if (!d_->worker.submit(std::move(job))) {
    QMutexLocker lock(&d_->mutex);
    if (d_->stageDone > 0)
      d_->stageDone -= 1;
  }
}
