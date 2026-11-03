// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "speech/SpeechReader.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <sapi.h>
#endif

bool looksLikeCallsign(const QString& word);

namespace {

#ifdef _WIN32
// The OneCore voices (the newer Windows 10 and 11 ones) are listed here,
// not under the classic SAPI category. SAPI can still speak with them.
const wchar_t kOneCoreVoices[] = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices";

// COM for a static lookup. Balanced only when this call started it.
class ComScope {
public:
  ComScope() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~ComScope() {
    if (SUCCEEDED(hr_))
      CoUninitialize();
  }

private:
  HRESULT hr_;
};

QString fromWide(const wchar_t* text) {
  return text ? QString::fromWCharArray(text) : QString();
}

void listCategory(const wchar_t* category, QList<SpeechReader::Choice>* out, QSet<QString>* seen) {
  ISpObjectTokenCategory* cat = nullptr;
  if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory,
                              reinterpret_cast<void**>(&cat))) ||
      !cat)
    return;
  IEnumSpObjectTokens* tokens = nullptr;
  if (SUCCEEDED(cat->SetId(category, FALSE)) && SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &tokens)) && tokens) {
    ULONG count = 0;
    tokens->GetCount(&count);
    for (ULONG i = 0; i < count; ++i) {
      ISpObjectToken* token = nullptr;
      if (FAILED(tokens->Item(i, &token)) || !token)
        continue;
      wchar_t* id = nullptr;
      wchar_t* name = nullptr;
      token->GetId(&id);
      token->GetStringValue(nullptr, &name);
      const QString shown = fromWide(name).trimmed();
      const QString key = shown.toLower();
      if (id && !shown.isEmpty() && !seen->contains(key)) {
        seen->insert(key);
        out->append({fromWide(id), shown});
      }
      CoTaskMemFree(id);
      CoTaskMemFree(name);
      token->Release();
    }
    tokens->Release();
  }
  cat->Release();
}

ISpObjectToken* tokenFor(const QString& id) {
  if (id.isEmpty())
    return nullptr;
  ISpObjectToken* token = nullptr;
  if (FAILED(CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL, IID_ISpObjectToken,
                              reinterpret_cast<void**>(&token))) ||
      !token)
    return nullptr;
  const std::wstring wide = id.toStdWString();
  if (FAILED(token->SetId(nullptr, wide.c_str(), FALSE))) {
    token->Release();
    return nullptr;
  }
  return token;
}
#endif

}  // namespace

QList<SpeechReader::Choice> SpeechReader::voices() {
  QList<Choice> out;
#ifdef _WIN32
  const ComScope com;
  QSet<QString> seen;
  listCategory(SPCAT_VOICES, &out, &seen);
  listCategory(kOneCoreVoices, &out, &seen);
#endif
  return out;
}

QList<SpeechReader::Choice> SpeechReader::outputs() {
  QList<Choice> out;
  out.append({QString(), QStringLiteral("Windows default")});
#ifdef _WIN32
  const ComScope com;
  QSet<QString> seen;
  listCategory(SPCAT_AUDIOOUT, &out, &seen);
#endif
  return out;
}

SpeechReader::SpeechReader(QObject* parent) : QObject(parent) {
#ifdef _WIN32
  comReady_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
#endif
  idle_.setSingleShot(true);
  idle_.setInterval(1000);
  connect(&idle_, &QTimer::timeout, this, [this] { flush(true); });
}

SpeechReader::~SpeechReader() {
#ifdef _WIN32
  if (voice_) {
    voice_->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
    voice_->Release();
  }
  if (comReady_)
    CoUninitialize();
#endif
}

bool SpeechReader::available() const {
  return voice_ != nullptr;
}

bool SpeechReader::ensureVoice() {
#ifdef _WIN32
  if (voice_)
    return true;
  if (triedVoice_)
    return false;
  triedVoice_ = true;
  if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                              reinterpret_cast<void**>(&voice_))) ||
      !voice_) {
    voice_ = nullptr;
    return false;
  }
  setOutput(outputId_);
  setVoice(voiceId_);
  voice_->SetRate(rate_);
  voice_->SetVolume(USHORT(volume_));
  return true;
#else
  return false;
#endif
}

void SpeechReader::setVoice(const QString& voiceId) {
  voiceId_ = voiceId;
#ifdef _WIN32
  if (!voice_)
    return;
  ISpObjectToken* token = tokenFor(voiceId);
  voice_->SetVoice(token);  // null is the Windows default voice
  if (token)
    token->Release();
#endif
}

void SpeechReader::setOutput(const QString& outputId) {
  outputId_ = outputId;
#ifdef _WIN32
  if (!voice_)
    return;
  ISpObjectToken* token = tokenFor(outputId);
  voice_->SetOutput(token, TRUE);  // null is the Windows default device
  if (token)
    token->Release();
#endif
}

void SpeechReader::setRate(int rate) {
  rate_ = std::clamp(rate, -10, 10);
#ifdef _WIN32
  if (voice_)
    voice_->SetRate(rate_);
#endif
}

void SpeechReader::setVolume(int volume) {
  volume_ = std::clamp(volume, 0, 100);
#ifdef _WIN32
  if (voice_)
    voice_->SetVolume(USHORT(volume_));
#endif
}

void SpeechReader::setMuted(bool muted) {
  muted_ = muted;
  if (muted)
    stop();
}

void SpeechReader::setKeepUp(bool on) {
  keepUp_ = on;
}

void SpeechReader::setSkipGarbled(bool on) {
  skipGarbled_ = on;
}

void SpeechReader::addText(const QString& text) {
  if (muted_ || text.isEmpty())
    return;
  pending_ += text;
  flush(false);
  idle_.start();
}

void SpeechReader::stop() {
  pending_.clear();
  idle_.stop();
#ifdef _WIN32
  if (voice_)
    voice_->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
#endif
}

void SpeechReader::say(const QString& text) {
  const QString clean = speechClean(text, false);
  if (clean.isEmpty() || !ensureVoice())
    return;
#ifdef _WIN32
  const std::wstring wide = clean.toStdWString();
  voice_->Speak(wide.c_str(), SPF_ASYNC | SPF_IS_NOT_XML | SPF_PURGEBEFORESPEAK, nullptr);
#endif
}

void SpeechReader::flush(bool all) {
  const QString ready = speechTakeReady(&pending_, all);
  if (!ready.isEmpty())
    speak(speechClean(ready, skipGarbled_));
}

void SpeechReader::speak(const QString& text) {
  if (muted_ || text.isEmpty() || !ensureVoice())
    return;
#ifdef _WIN32
  DWORD flags = SPF_ASYNC | SPF_IS_NOT_XML;
  if (keepUp_) {
    SPVOICESTATUS status{};
    if (SUCCEEDED(voice_->GetStatus(&status, nullptr)) &&
        status.ulLastStreamQueued > status.ulCurrentStream + 2)
      flags |= SPF_PURGEBEFORESPEAK;
  }
  const std::wstring wide = text.toStdWString();
  voice_->Speak(wide.c_str(), flags, nullptr);
#endif
}

QString speechTakeReady(QString* buffer, bool all) {
  if (!buffer || buffer->isEmpty())
    return {};
  int cut = -1;
  for (int i = int(buffer->size()) - 1; i >= 0; --i) {
    if (buffer->at(i) == QLatin1Char('\n') || buffer->at(i) == QLatin1Char('\r')) {
      cut = i + 1;
      break;
    }
  }
  if (cut < 0 && all)
    cut = int(buffer->size());
  if (cut < 0) {
    const int space = int(buffer->lastIndexOf(QLatin1Char(' ')));
    if (space > 0 &&
        buffer->left(space).split(QLatin1Char(' '), Qt::SkipEmptyParts).size() >= 8)
      cut = space + 1;
  }
  if (cut <= 0)
    return {};
  const QString ready = buffer->left(cut);
  buffer->remove(0, cut);
  return ready;
}

QString speechClean(const QString& text, bool skipGarbled) {
  static const QRegularExpression report(QStringLiteral("^[1-5][1-9N][1-9N]$"));
  QStringList words;
  const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  for (const QString& token : tokens) {
    int alnum = 0;
    for (const QChar ch : token) {
      if (ch.isLetterOrNumber())
        ++alnum;
    }
    if (skipGarbled && (alnum == 0 || alnum * 10 < int(token.size()) * 6))
      continue;
    int from = 0;
    int to = int(token.size());
    while (from < to && !token.at(from).isLetterOrNumber())
      ++from;
    while (to > from && !token.at(to - 1).isLetterOrNumber())
      --to;
    const QString core = token.mid(from, to - from).toUpper();
    if (!core.isEmpty() && (looksLikeCallsign(core) || report.match(core).hasMatch())) {
      QStringList letters;
      for (const QChar ch : core)
        letters.append(QString(ch));
      words.append(letters.join(QLatin1Char(' ')));
      continue;
    }
    words.append(token);
  }
  return words.join(QLatin1Char(' '));
}
