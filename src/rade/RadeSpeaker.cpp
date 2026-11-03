// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeSpeaker.h"

#include "speech/SpeechReader.h"

#include <algorithm>
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

#ifdef _WIN32
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

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

QString wasapiIdFor(const QString& sapiId) {
  if (sapiId.isEmpty())
    return {};
  QString want;
  for (const SpeechReader::Choice& choice : SpeechReader::outputs()) {
    if (choice.id == sapiId) {
      want = choice.name;
      break;
    }
  }
  if (want.isEmpty() || want == QLatin1String("Windows default"))
    return {};
  const ComScope com(COINIT_APARTMENTTHREADED);
  IMMDeviceEnumerator* devices = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&devices))) ||
      !devices)
    return {};
  IMMDeviceCollection* list = nullptr;
  QString found;
  if (SUCCEEDED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &list)) && list) {
    UINT count = 0;
    list->GetCount(&count);
    for (UINT i = 0; i < count && found.isEmpty(); ++i) {
      IMMDevice* device = nullptr;
      if (FAILED(list->Item(i, &device)) || !device)
        continue;
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
      LPWSTR id = nullptr;
      if (name.compare(want, Qt::CaseInsensitive) == 0 && SUCCEEDED(device->GetId(&id)) && id)
        found = QString::fromWCharArray(id);
      CoTaskMemFree(id);
      release(device);
    }
    release(list);
  }
  release(devices);
  return found;
}
#endif

}  // namespace

RadeSpeaker::RadeSpeaker(QObject* parent) : QObject(parent) {}

RadeSpeaker::~RadeSpeaker() {
  stop();
}

bool RadeSpeaker::start(const QString& sapiOutputId) {
  if (running_ && deviceId_ == sapiOutputId)
    return true;
  stop();
  error_.clear();
  deviceId_ = sapiOutputId;
  stopFlag_ = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
  }
#ifdef _WIN32
  const QString wasapiId = wasapiIdFor(sapiOutputId);
  std::promise<QString> opened;
  std::future<QString> result = opened.get_future();
  running_ = true;
  thread_ = std::thread([this, wasapiId, promise = std::move(opened)]() mutable {
    QString problem;
    const ComScope com(COINIT_MULTITHREADED);
    IMMDeviceEnumerator* devices = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    UINT32 bufferFrames = 0;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&devices));
    const char* step = "find the audio devices";
    if (SUCCEEDED(hr)) {
      step = wasapiId.isEmpty() ? "find the default speaker" : "find that speaker";
      if (wasapiId.isEmpty())
        hr = devices->GetDefaultAudioEndpoint(eRender, eConsole, &device);
      else
        hr = devices->GetDevice(wasapiId.toStdWString().c_str(), &device);
    }
    if (SUCCEEDED(hr)) {
      step = "open the speaker";
      hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
    }
    if (SUCCEEDED(hr)) {
      step = "set 16 kHz mono";
      WAVEFORMATEX format{};
      format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
      format.nChannels = 1;
      format.nSamplesPerSec = 16000;
      format.wBitsPerSample = 32;
      format.nBlockAlign = 4;
      format.nAvgBytesPerSec = 16000 * 4;
      hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                              AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 10000000,
                              0, &format, nullptr);
    }
    if (SUCCEEDED(hr))
      hr = client->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(hr)) {
      step = "start playback";
      hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render));
    }
    if (SUCCEEDED(hr))
      hr = client->Start();
    if (FAILED(hr))
      problem = QStringLiteral("Speaker did not open (%1, 0x%2). START was not sent.")
                    .arg(QString::fromLatin1(step))
                    .arg(static_cast<unsigned long>(hr), 8, 16, QLatin1Char('0'));
    promise.set_value(problem);
    if (problem.isEmpty()) {
      while (!stopFlag_) {
        Sleep(10);
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding)) || padding >= bufferFrames)
          continue;
        UINT32 room = bufferFrames - padding;
        std::vector<float> block;
        {
          const std::lock_guard<std::mutex> lock(mutex_);
          if (queue_.empty())
            continue;
          const size_t take = std::min(queue_.size(), static_cast<size_t>(room));
          block.assign(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(take));
          queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(take));
        }
        BYTE* data = nullptr;
        if (FAILED(render->GetBuffer(static_cast<UINT32>(block.size()), &data)) || !data)
          continue;
        auto* dst = reinterpret_cast<float*>(data);
        for (size_t i = 0; i < block.size(); ++i) {
          float s = block[i];
          if (s > 1.0f)
            s = 1.0f;
          if (s < -1.0f)
            s = -1.0f;
          dst[i] = s;
        }
        render->ReleaseBuffer(static_cast<UINT32>(block.size()), 0);
      }
      client->Stop();
    }
    release(render);
    release(client);
    release(device);
    release(devices);
    running_ = false;
  });
  error_ = result.get();
  if (!error_.isEmpty()) {
    thread_.join();
    running_ = false;
    return false;
  }
  return true;
#else
  error_ = QStringLiteral("Playback is Windows only. START was not sent.");
  return false;
#endif
}

void RadeSpeaker::stop() {
  stopFlag_ = true;
  if (thread_.joinable())
    thread_.join();
  running_ = false;
  const std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

void RadeSpeaker::clear() {
  const std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

void RadeSpeaker::write(const float* samples, int count) {
  if (!samples || count < 1 || !running_)
    return;
  const std::lock_guard<std::mutex> lock(mutex_);
  queue_.insert(queue_.end(), samples, samples + count);
  constexpr size_t kMax = 16000 * 2;
  if (queue_.size() > kMax)
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(queue_.size() - kMax));
}

QString RadeSpeaker::error() const {
  return error_;
}
