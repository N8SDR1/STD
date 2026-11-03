// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "speech/SpeechInput.h"

#include <algorithm>
#include <cmath>
#include <future>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#endif

namespace {

// A minute of speech is the most one Talk press keeps.
constexpr size_t kMaxSamples = size_t(SpeechInput::kRate) * 60;

#ifdef _WIN32
// PKEY_Device_FriendlyName, written out so no GUID-defining header is needed.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// Plain words for the Windows audio errors a microphone usually hits.
QString micProblem(const char* step, HRESULT hr) {
  QString why;
  switch (static_cast<unsigned long>(hr)) {
    case 0x80070005UL:
      why = QStringLiteral("Windows privacy settings block desktop apps from the microphone.");
      break;
    case 0x88890004UL:  // AUDCLNT_E_DEVICE_INVALIDATED
      why = QStringLiteral("the device was unplugged, disabled, or its format changed in Sound settings.");
      break;
    case 0x8889000AUL:  // AUDCLNT_E_DEVICE_IN_USE
      why = QStringLiteral("another program holds it in exclusive mode.");
      break;
    case 0x88890008UL:  // AUDCLNT_E_UNSUPPORTED_FORMAT
      why = QStringLiteral("Windows would not convert it to 16 kHz mono.");
      break;
    case 0x80070490UL:  // ELEMENT_NOT_FOUND
      why = QStringLiteral("that device is no longer there. Pick it again in Options, Speech.");
      break;
    case 0x8889000FUL:  // AUDCLNT_E_ENDPOINT_CREATE_FAILED
    case 0x88890026UL:  // AUDCLNT_E_RESOURCES_INVALIDATED
      why = QStringLiteral("the Windows audio service lost the device.");
      break;
    default:
      why = QStringLiteral("Windows said no.");
      break;
  }
  return QStringLiteral("Could not %1: %2 (0x%3)")
      .arg(QString::fromLatin1(step), why)
      .arg(static_cast<unsigned long>(hr), 8, 16, QLatin1Char('0'));
}

template <typename T>
void release(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

class ComScope {
public:
  explicit ComScope(DWORD model) : hr_(CoInitializeEx(nullptr, model)) {}
  ~ComScope() {
    if (SUCCEEDED(hr_))
      CoUninitialize();
  }

private:
  HRESULT hr_;
};
#endif

}  // namespace

QList<SpeechReader::Choice> SpeechInput::inputs() {
  QList<SpeechReader::Choice> out;
  out.append({QString(), QStringLiteral("Windows default")});
#ifdef _WIN32
  const ComScope com(COINIT_APARTMENTTHREADED);
  IMMDeviceEnumerator* devices = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&devices))) ||
      !devices)
    return out;
  IMMDeviceCollection* list = nullptr;
  if (SUCCEEDED(devices->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &list)) && list) {
    UINT count = 0;
    list->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      IMMDevice* device = nullptr;
      if (FAILED(list->Item(i, &device)) || !device)
        continue;
      LPWSTR id = nullptr;
      IPropertyStore* props = nullptr;
      QString name;
      if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(props->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR)
          name = QString::fromWCharArray(value.pwszVal);
        PropVariantClear(&value);
        release(props);
      }
      if (SUCCEEDED(device->GetId(&id)) && id && !name.isEmpty())
        out.append({QString::fromWCharArray(id), name});
      CoTaskMemFree(id);
      release(device);
    }
    release(list);
  }
  release(devices);
#endif
  return out;
}

SpeechInput::SpeechInput(QObject* parent) : QObject(parent) {}

SpeechInput::~SpeechInput() {
  stop();
}

bool SpeechInput::start(const QString& deviceId) {
  stop();
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
  }
  error_.clear();
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    lostError_.clear();
  }
  stopFlag_ = false;
  running_ = true;
  std::promise<QString> opened;
  std::future<QString> result = opened.get_future();
  thread_ = std::thread([this, deviceId, promise = std::move(opened)]() mutable {
    QString problem;
#ifdef _WIN32
    const ComScope com(COINIT_MULTITHREADED);
    const char* step = "find the audio devices";
    IMMDeviceEnumerator* devices = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&devices));
    if (SUCCEEDED(hr)) {
      step = deviceId.isEmpty() ? "find the default microphone" : "find that microphone";
      if (deviceId.isEmpty()) {
        hr = devices->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
      } else {
        const std::wstring wide = deviceId.toStdWString();
        hr = devices->GetDevice(wide.c_str(), &device);
      }
    }
    if (SUCCEEDED(hr)) {
      step = "open the microphone";
      hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
    }
    if (SUCCEEDED(hr)) {
      step = "set 16 kHz mono";
      // Ask Windows for 16 kHz mono float; it converts from the device rate.
      WAVEFORMATEX format{};
      format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
      format.nChannels = 1;
      format.nSamplesPerSec = kRate;
      format.wBitsPerSample = 32;
      format.nBlockAlign = 4;
      format.nAvgBytesPerSec = kRate * 4;
      hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                              AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                              10000000, 0, &format, nullptr);
    }
    if (SUCCEEDED(hr)) {
      step = "start capture";
      hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
    }
    if (SUCCEEDED(hr))
      hr = client->Start();
    if (FAILED(hr))
      problem = micProblem(step, hr);
    promise.set_value(problem);
    if (problem.isEmpty()) {
      double sumSq = 0.0;
      int counted = 0;
      while (!stopFlag_) {
        Sleep(10);
        UINT32 packet = 0;
        HRESULT got = S_OK;
        while (SUCCEEDED(got = capture->GetNextPacketSize(&packet)) && packet > 0) {
          BYTE* data = nullptr;
          UINT32 frames = 0;
          DWORD flags = 0;
          if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
            break;
          const auto* in = reinterpret_cast<const float*>(data);
          const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
          {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (UINT32 i = 0; i < frames && samples_.size() < kMaxSamples; ++i)
              samples_.push_back(silent ? 0.0f : in[i]);
          }
          for (UINT32 i = 0; i < frames; ++i) {
            const double v = silent ? 0.0 : double(in[i]);
            sumSq += v * v;
            if (++counted == kRate / 20) {
              const double rms = std::sqrt(sumSq / counted);
              emit level(rms > 1e-6 ? 20.0 * std::log10(rms) : -120.0);
              sumSq = 0.0;
              counted = 0;
            }
          }
          capture->ReleaseBuffer(frames);
        }
        if (FAILED(got)) {
          // The device went away or changed mid-recording.
          const std::lock_guard<std::mutex> lock(mutex_);
          lostError_ = micProblem("keep recording", got);
          break;
        }
      }
      client->Stop();
    }
    release(capture);
    release(client);
    release(device);
    release(devices);
#else
    problem = QStringLiteral("Microphone capture is Windows only.");
    promise.set_value(problem);
#endif
    running_ = false;
  });
  error_ = result.get();
  if (!error_.isEmpty()) {
    thread_.join();
    return false;
  }
  return true;
}

void SpeechInput::stop() {
  stopFlag_ = true;
  if (thread_.joinable())
    thread_.join();
  running_ = false;
}

QString SpeechInput::lostError() {
  const std::lock_guard<std::mutex> lock(mutex_);
  return lostError_;
}

std::vector<float> SpeechInput::take() {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::vector<float> out;
  out.swap(samples_);
  return out;
}

double speechLevelDb(const float* samples, int count) {
  if (!samples || count <= 0)
    return -120.0;
  double sumSq = 0.0;
  for (int i = 0; i < count; ++i)
    sumSq += double(samples[i]) * double(samples[i]);
  const double rms = std::sqrt(sumSq / count);
  return rms > 1e-6 ? 20.0 * std::log10(rms) : -120.0;
}

std::vector<float> gateSpeech(const std::vector<float>& in, double gateDb) {
  constexpr int kFrame = SpeechInput::kRate / 50;  // 20 ms
  constexpr int kHold = 15;                        // 300 ms
  constexpr int kPreRoll = 5;                      // 100 ms
  const int frames = int(in.size()) / kFrame;
  std::vector<bool> keep(static_cast<size_t>(frames), false);
  int hold = 0;
  for (int f = 0; f < frames; ++f) {
    if (speechLevelDb(in.data() + size_t(f) * kFrame, kFrame) >= gateDb) {
      hold = kHold;
      for (int back = std::max(0, f - kPreRoll); back <= f; ++back)
        keep[static_cast<size_t>(back)] = true;
    } else if (hold > 0) {
      --hold;
      keep[static_cast<size_t>(f)] = true;
    }
  }
  std::vector<float> out;
  for (int f = 0; f < frames; ++f) {
    if (keep[static_cast<size_t>(f)])
      out.insert(out.end(), in.begin() + f * kFrame, in.begin() + (f + 1) * kFrame);
  }
  return out;
}
