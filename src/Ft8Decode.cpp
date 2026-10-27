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
constexpr int kPeriodSamples = 15 * kAudioRate;
constexpr int kMinSamples = 14 * kAudioRate;
constexpr int kRing = 20 * kAudioRate;
constexpr qint64 kPeriodMs = 15000;
constexpr qint64 kGraceMs = 400;
// MSHV scales int16 audio by 1/2560, which is 12.8 at full scale.
constexpr double kMshvScale = 32768.0 * 0.000390625;

struct Job {
  std::vector<double> samples;
  int lowHz = 100;
  int highHz = 3500;
  QString stamp;
};

class Worker : public QThread {
public:
  explicit Worker(QObject* parent = nullptr) : QThread(parent) {
    setStackSize(32 * 1024 * 1024);
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
      emitLine(QStringLiteral("FT8 decoder did not load (%1).").arg(GetLastError()));
      return;
    }
    auto open = reinterpret_cast<decltype(&ft8_session_open)>(GetProcAddress(module, "ft8_session_open"));
    auto close =
        reinterpret_cast<decltype(&ft8_session_close)>(GetProcAddress(module, "ft8_session_close"));
    auto decode =
        reinterpret_cast<decltype(&ft8_session_decode)>(GetProcAddress(module, "ft8_session_decode"));
    if (!open || !close || !decode) {
      emitLine(QStringLiteral("FT8 decoder did not load."));
      FreeLibrary(module);
      return;
    }
    Ft8Session* session = open();
    if (!session) {
      emitLine(QStringLiteral("FT8 decoder did not load."));
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
      if (job.samples.size() < static_cast<size_t>(kMinSamples))
        continue;
      double sum = 0.0;
      for (double& sample : job.samples) {
        sample *= kMshvScale;
        sum += sample;
      }
      const double average = sum / static_cast<double>(job.samples.size());
      for (double& sample : job.samples)
        sample -= average;
      const QByteArray stamp = job.stamp.toLatin1();
      std::vector<QString> lines;
      decode(session, job.samples.data(), static_cast<int>(job.samples.size()), job.lowHz, job.highHz,
             stamp.constData(),
             [](const char* time, int db, const char* dt, const char* message, int freqHz, void* user) {
               auto* out = static_cast<std::vector<QString>*>(user);
               out->push_back(QStringLiteral("%1   %2 dB   %3   %4   %5 Hz")
                                  .arg(QString::fromUtf8(time))
                                  .arg(db)
                                  .arg(QString::fromUtf8(dt))
                                  .arg(QString::fromUtf8(message))
                                  .arg(freqHz));
             },
             &lines);
      for (const QString& line : lines)
        emitLine(line);
    }
    close(session);
    FreeLibrary(module);
#else
    emitLine(QStringLiteral("FT8 decoder did not load."));
#endif
  }

  void emitLine(const QString& line) {
    if (onLine)
      onLine(line);
  }

  std::function<void(const QString&)> onLine;

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
  qint64 lastSubmitted = -1;
  int decim = 0;
  double residual = 0;
  int rate = 0;
  bool armed = false;
  int lowHz = 100;
  int highHz = 3500;
  bool workerStarted = false;
};

Ft8Decode::Ft8Decode(QObject* parent) : QObject(parent), d_(new Private) {
  d_->worker.onLine = [this](const QString& line) { emit decodedLine(line); };
  connect(&d_->timer, &QTimer::timeout, this, [this] { poll(); });
  d_->timer.start(200);
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

void Ft8Decode::clear() {
  QMutexLocker lock(&d_->mutex);
  d_->total = 0;
  d_->newestMs = 0;
  d_->lastSubmitted = -1;
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
    if (!d_->armed || d_->total < kMinSamples || d_->newestMs <= 0)
      return;
    const qint64 closed = QDateTime::currentMSecsSinceEpoch() - kGraceMs;
    const qint64 periodEnd = (closed / kPeriodMs) * kPeriodMs;
    const qint64 periodStart = periodEnd - kPeriodMs;
    if (periodStart == d_->lastSubmitted)
      return;
    const double samplesBack = static_cast<double>(d_->newestMs - periodStart) * 12.0;
    const qint64 startIdx = (d_->total - 1) - static_cast<qint64>(std::llround(samplesBack));
    const qint64 oldest = d_->total > kRing ? d_->total - kRing : 0;
    job.samples.assign(kPeriodSamples, 0.0);
    int real = 0;
    for (int n = 0; n < kPeriodSamples; ++n) {
      const qint64 index = startIdx + n;
      if (index < oldest || index >= d_->total)
        continue;
      job.samples[static_cast<size_t>(n)] = d_->ring[static_cast<size_t>(index % kRing)];
      ++real;
    }
    if (real < kMinSamples)
      return;
    job.lowHz = d_->lowHz;
    job.highHz = d_->highHz;
    job.stamp = QDateTime::fromMSecsSinceEpoch(periodStart, QTimeZone::utc()).toString(QStringLiteral("hhmmss"));
    d_->lastSubmitted = periodStart;
    ready = true;
  }
  if (!ready)
    return;
  ensureWorker();
  if (!d_->worker.submit(std::move(job))) {
    QMutexLocker lock(&d_->mutex);
    d_->lastSubmitted = -1;
  }
}
