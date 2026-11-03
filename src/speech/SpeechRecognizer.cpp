// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Speech to text with whisper.cpp (MIT, by Georgi Gerganov and contributors).

#include "speech/SpeechRecognizer.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

#include "whisper.h"

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif

namespace {

void quietLog(enum ggml_log_level, const char*, void*) {}

}  // namespace

bool SpeechRecognizer::cpuSupported() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  int info[4] = {0, 0, 0, 0};
  __cpuid(info, 0);
  if (info[0] < 7)
    return false;
  __cpuid(info, 1);
  const bool fma = (info[2] & (1 << 12)) != 0;
  const bool osxsave = (info[2] & (1 << 27)) != 0;
  const bool avx = (info[2] & (1 << 28)) != 0;
  const bool f16c = (info[2] & (1 << 29)) != 0;
  if (!(fma && osxsave && avx && f16c))
    return false;
  // The operating system must save the wide registers.
  if ((_xgetbv(0) & 0x6) != 0x6)
    return false;
  __cpuidex(info, 7, 0);
  return (info[1] & (1 << 5)) != 0;  // AVX2
#else
  return true;
#endif
}

SpeechRecognizer::SpeechRecognizer(QObject* parent) : QObject(parent) {
  whisper_log_set(quietLog, nullptr);
}

SpeechRecognizer::~SpeechRecognizer() {
  abort_ = true;
  if (thread_.joinable())
    thread_.join();
  unload();
}

void SpeechRecognizer::unload() {
  if (busy_)
    return;
  if (thread_.joinable())
    thread_.join();
  const std::lock_guard<std::mutex> lock(mutex_);
  if (ctx_) {
    whisper_free(ctx_);
    ctx_ = nullptr;
  }
  loadedPath_.clear();
}

bool SpeechRecognizer::start(const QString& modelPath, std::vector<float> samples, const QString& hint) {
  if (busy_)
    return false;
  if (thread_.joinable())
    thread_.join();
  busy_ = true;
  abort_ = false;
  thread_ = std::thread([this, modelPath, samples = std::move(samples), hint]() {
    QString text;
    QString error;
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      if (!cpuSupported()) {
        error = QStringLiteral("This processor is too old for speech to text (it needs AVX2, 2013 or later).");
      } else if (!QFileInfo::exists(modelPath)) {
        error = QStringLiteral("The speech model is not installed. Download one in Options, Speech, Transmit.");
      } else {
        if (ctx_ && loadedPath_ != modelPath) {
          whisper_free(ctx_);
          ctx_ = nullptr;
        }
        if (!ctx_) {
          whisper_context_params cparams = whisper_context_default_params();
          cparams.use_gpu = false;
          const QByteArray path = modelPath.toUtf8();
          ctx_ = whisper_init_from_file_with_params(path.constData(), cparams);
          loadedPath_ = ctx_ ? modelPath : QString();
        }
        if (!ctx_) {
          error = QStringLiteral("The speech model would not load. Remove it and download it again.");
        } else {
          whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
          const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
          params.n_threads = int(std::min(8u, cores));
          params.language = "en";
          params.translate = false;
          params.no_timestamps = true;
          params.print_progress = false;
          params.print_realtime = false;
          params.print_special = false;
          params.print_timestamps = false;
          params.suppress_blank = true;
          const QByteArray prompt = hint.toUtf8();
          params.initial_prompt = prompt.isEmpty() ? nullptr : prompt.constData();
          params.abort_callback = [](void* flag) { return static_cast<std::atomic<bool>*>(flag)->load(); };
          params.abort_callback_user_data = &abort_;
          if (whisper_full(ctx_, params, samples.data(), int(samples.size())) != 0) {
            error = abort_ ? QStringLiteral("Speech to text was stopped.")
                           : QStringLiteral("Speech to text failed on this recording.");
          } else {
            const int segments = whisper_full_n_segments(ctx_);
            for (int i = 0; i < segments; ++i)
              text += QString::fromUtf8(whisper_full_get_segment_text(ctx_, i));
            text = text.trimmed();
          }
        }
      }
    }
    busy_ = false;
    emit done(text, error);
  });
  return true;
}

QString speechHint(const QString& ownCall, const QString& extra) {
  QString hint = QStringLiteral("CQ CQ de %1. QRZ? Your report is 599. Name, QTH, rig, 73.")
                     .arg(ownCall.isEmpty() ? QStringLiteral("N0CALL") : ownCall.toUpper());
  if (!extra.trimmed().isEmpty())
    hint += QLatin1Char(' ') + extra.simplified();
  return hint;
}

QString speechToTxText(const QString& heard) {
  static const QHash<QString, QString> spoken = {
      {"alpha", "A"},    {"alfa", "A"},    {"bravo", "B"},   {"charlie", "C"},  {"delta", "D"},
      {"echo", "E"},     {"foxtrot", "F"}, {"golf", "G"},    {"hotel", "H"},    {"india", "I"},
      {"juliet", "J"},   {"juliett", "J"}, {"kilo", "K"},    {"lima", "L"},     {"mike", "M"},
      {"november", "N"}, {"oscar", "O"},   {"papa", "P"},    {"quebec", "Q"},   {"romeo", "R"},
      {"sierra", "S"},   {"tango", "T"},   {"uniform", "U"}, {"victor", "V"},   {"whiskey", "W"},
      {"whisky", "W"},   {"xray", "X"},    {"x-ray", "X"},   {"yankee", "Y"},   {"zulu", "Z"},
      {"zero", "0"},     {"one", "1"},     {"two", "2"},     {"three", "3"},    {"four", "4"},
      {"five", "5"},     {"six", "6"},     {"seven", "7"},   {"eight", "8"},    {"nine", "9"},
      {"niner", "9"},    {"slash", "/"},   {"stroke", "/"},
  };
  QString text = heard;
  // Recognizer notes such as [BLANK_AUDIO] or (static), and filler.
  text.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)")));
  text.remove(QRegularExpression(QStringLiteral("\\b(um+|uh+|er+m?|ah+)\\b[,.]?"),
                                 QRegularExpression::CaseInsensitiveOption));
  const auto ci = QRegularExpression::CaseInsensitiveOption;
  text.replace(QRegularExpression(QStringLiteral("\\bseventy[- ]three\\b"), ci), QStringLiteral("73"));
  text.replace(QRegularExpression(QStringLiteral("\\beighty[- ]eight\\b"), ci), QStringLiteral("88"));
  text.replace(QRegularExpression(QStringLiteral("\\b(see|sea) (queue|q)\\b|\\bseek you\\b"), ci),
               QStringLiteral("CQ"));

  // Each word: the bare token, any trailing punctuation, and whether it
  // spells a single letter or digit (weak: A and I, which are also words).
  struct Word {
    QString core;
    QString tail;
    QString spelled;
    bool weak = false;
  };
  QList<Word> words;
  for (const QString& raw : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
    Word w;
    int end = int(raw.size());
    while (end > 0 && QStringLiteral(",.!?;:").contains(raw.at(end - 1)))
      --end;
    w.core = raw.left(end);
    w.tail = raw.mid(end);
    const QString key = w.core.toLower();
    if (spoken.contains(key)) {
      w.spelled = spoken.value(key);
    } else if (QRegularExpression(QStringLiteral("^[0-9]+$")).match(w.core).hasMatch()) {
      w.spelled = w.core;
    } else if (QRegularExpression(QStringLiteral("^[A-Za-z0-9](-[A-Za-z0-9])+$")).match(w.core).hasMatch()) {
      w.spelled = QString(w.core).remove(QLatin1Char('-'));  // K-8-A-B
    } else if (const auto glued = QRegularExpression(QStringLiteral("^([A-Za-z-]+)([0-9]+)$|^([0-9]+)([A-Za-z-]+)$"))
                                      .match(w.core);
               glued.hasMatch()) {
      // A spoken letter run into digits, as the recognizer writes "Kilo8".
      const bool letterFirst = !glued.captured(1).isEmpty();
      const QString word = (letterFirst ? glued.captured(1) : glued.captured(4)).toLower();
      const QString digits = letterFirst ? glued.captured(2) : glued.captured(3);
      if (spoken.contains(word) && !spoken.value(word).at(0).isDigit())
        w.spelled = letterFirst ? spoken.value(word) + digits : digits + spoken.value(word);
    } else if (w.core.size() == 1 && w.core.at(0).isLetter()) {
      w.spelled = w.core;
      w.weak = key == QStringLiteral("a") || key == QStringLiteral("i");
    }
    words.append(w);
  }

  QStringList out;
  int i = 0;
  while (i < words.size()) {
    if (words.at(i).spelled.isEmpty()) {
      out << words.at(i).core + words.at(i).tail;
      ++i;
      continue;
    }
    // A run of spelled words, broken by end-of-sentence punctuation.
    int j = i;
    while (j < words.size() && !words.at(j).spelled.isEmpty()) {
      const QString tail = words.at(j).tail;
      ++j;
      if (tail.contains(QLatin1Char('.')) || tail.contains(QLatin1Char('!')) || tail.contains(QLatin1Char('?')))
        break;
    }
    // A leading A or I is an ordinary word ("I am", "a kilo"), not a letter.
    while (i < j - 1 && words.at(i).weak) {
      out << words.at(i).core + words.at(i).tail;
      ++i;
    }
    if (j - i == 1) {
      const Word& w = words.at(i);
      // On its own, a spoken digit or slash becomes the symbol; a letter word stays.
      out << ((w.spelled.at(0).isDigit() || w.spelled == QStringLiteral("/")) && !w.weak ? w.spelled : w.core) +
                 w.tail;
    } else {
      QString joined;
      for (int k = i; k < j; ++k)
        joined += words.at(k).spelled;
      // A call said two or three times in a row ("K8AB K8AB") stays separate.
      for (int times : {3, 2}) {
        const int part = int(joined.size()) / times;
        if (part >= 3 && part * times == joined.size() && joined == joined.left(part).repeated(times)) {
          QStringList copies;
          for (int t = 0; t < times; ++t)
            copies << joined.left(part);
          joined = copies.join(QLatin1Char(' '));
          break;
        }
      }
      out << joined + QString(words.at(j - 1).tail).remove(QLatin1Char(','));
    }
    i = j;
  }
  QString result = out.join(QLatin1Char(' '));
  // "slash portable" / "slash mobile": the usual call suffixes.
  result.replace(QRegularExpression(QStringLiteral("/\\s*portable\\b"), ci), QStringLiteral("/P"));
  result.replace(QRegularExpression(QStringLiteral("/\\s*mobile\\b"), ci), QStringLiteral("/M"));
  result.replace(QRegularExpression(QStringLiteral("\\s+/\\s*([A-Za-z0-9])")), QStringLiteral("/\\1"));
  return result.simplified().toUpper();
}
