// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sstv/SstvModem.h"

#include <QDateTime>
#include <QMap>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSyncHz = 1200.0;
constexpr double kBlackHz = 1500.0;
constexpr double kWhiteHz = 2300.0;
constexpr double kLeaderHz = 1900.0;
// The SSTV band is 1100 to 2300 Hz; mixing at its middle lets the filter be
// narrow on both sides.
constexpr double kMixHz = 1700.0;
// Below this (after the tuning offset is taken out) the tone counts as sync.
constexpr double kSyncThresholdHz = 1350.0;

// What a stretch of a scan line carries.
enum class Ch { Tone, R, G, B, Y0, Y1, Cr, Cb, Chroma };

struct Element {
  double ms = 0.0;
  double hz = 0.0;  // Tone only
  Ch ch = Ch::Tone;
};

enum class ColorModel { Rgb, RobotAlternate, Ycc };

struct Spec {
  SstvMode mode = SstvMode::None;
  const char* name = "";
  int vis = 0;
  int width = 320;
  int height = 256;
  // Picture rows carried by one sync period. PD modes send two.
  int rowsPerLine = 1;
  ColorModel color = ColorModel::Rgb;
  // A lone sync pulse before the first line (Scottie).
  double leadSyncMs = 0.0;
  double syncMs = 0.0;
  // Martin and Scottie use one time per color scan, PD one per channel,
  // Robot gives the luma scan time.
  double scanMs = 0.0;
};

const std::vector<Spec>& specs() {
  static const std::vector<Spec> table = {
      {SstvMode::Robot36, "Robot 36", 8, 320, 240, 1, ColorModel::RobotAlternate, 0.0, 9.0, 88.0},
      {SstvMode::Robot72, "Robot 72", 12, 320, 240, 1, ColorModel::Ycc, 0.0, 9.0, 138.0},
      {SstvMode::MartinM1, "Martin M1", 44, 320, 256, 1, ColorModel::Rgb, 0.0, 4.862, 146.432},
      {SstvMode::MartinM2, "Martin M2", 40, 320, 256, 1, ColorModel::Rgb, 0.0, 4.862, 73.216},
      {SstvMode::ScottieS1, "Scottie S1", 60, 320, 256, 1, ColorModel::Rgb, 9.0, 9.0, 138.240},
      {SstvMode::ScottieS2, "Scottie S2", 56, 320, 256, 1, ColorModel::Rgb, 9.0, 9.0, 88.064},
      {SstvMode::ScottieDX, "Scottie DX", 76, 320, 256, 1, ColorModel::Rgb, 9.0, 9.0, 345.600},
      {SstvMode::PD50, "PD-50", 93, 320, 256, 2, ColorModel::Ycc, 0.0, 20.0, 91.520},
      {SstvMode::PD90, "PD-90", 99, 320, 256, 2, ColorModel::Ycc, 0.0, 20.0, 170.240},
      {SstvMode::PD120, "PD-120", 95, 640, 496, 2, ColorModel::Ycc, 0.0, 20.0, 121.600},
      {SstvMode::PD160, "PD-160", 98, 512, 400, 2, ColorModel::Ycc, 0.0, 20.0, 195.584},
      {SstvMode::PD180, "PD-180", 96, 640, 496, 2, ColorModel::Ycc, 0.0, 20.0, 183.040},
      {SstvMode::PD240, "PD-240", 97, 640, 496, 2, ColorModel::Ycc, 0.0, 20.0, 244.480},
      {SstvMode::PD290, "PD-290", 94, 800, 616, 2, ColorModel::Ycc, 0.0, 20.0, 228.800},
      {SstvMode::WraaseSC2180, "Wraase SC2-180", 55, 320, 256, 1, ColorModel::Rgb, 0.0, 5.5225, 235.000},
  };
  return table;
}

const Spec* specFor(SstvMode mode) {
  for (const Spec& s : specs())
    if (s.mode == mode)
      return &s;
  return nullptr;
}

bool isMartin(const Spec& s) {
  return s.mode == SstvMode::MartinM1 || s.mode == SstvMode::MartinM2;
}
bool isWraase(const Spec& s) {
  return s.mode == SstvMode::WraaseSC2180;
}
bool isScottie(const Spec& s) {
  return s.mode == SstvMode::ScottieS1 || s.mode == SstvMode::ScottieS2 || s.mode == SstvMode::ScottieDX;
}

// One sync period. `line` only matters for Robot 36, which alternates the
// color-difference channel. With decoding set, Robot 36's alternating scan is
// reported as Chroma, since the receiver learns which one it is from the
// separator tone.
std::vector<Element> lineElements(const Spec& s, int line, bool decoding = false) {
  const double c = s.scanMs;
  if (isMartin(s)) {
    return {{s.syncMs, kSyncHz}, {0.572, kBlackHz}, {c, 0, Ch::G}, {0.572, kBlackHz},
            {c, 0, Ch::B}, {0.572, kBlackHz}, {c, 0, Ch::R}, {0.572, kBlackHz}};
  }
  if (isWraase(s)) {
    return {{s.syncMs, kSyncHz}, {0.5, kBlackHz}, {c, 0, Ch::R}, {c, 0, Ch::G}, {c, 0, Ch::B}};
  }
  if (isScottie(s)) {
    return {{1.5, kBlackHz}, {c, 0, Ch::G}, {1.5, kBlackHz}, {c, 0, Ch::B},
            {s.syncMs, kSyncHz}, {1.5, kBlackHz}, {c, 0, Ch::R}};
  }
  if (s.mode == SstvMode::Robot36) {
    const bool even = (line % 2) == 0;
    const Ch chroma = decoding ? Ch::Chroma : (even ? Ch::Cr : Ch::Cb);
    return {{s.syncMs, kSyncHz}, {3.0, kBlackHz}, {c, 0, Ch::Y0},
            {4.5, even ? kBlackHz : kWhiteHz}, {1.5, kLeaderHz}, {c / 2.0, 0, chroma}};
  }
  if (s.mode == SstvMode::Robot72) {
    return {{s.syncMs, kSyncHz}, {3.0, kBlackHz}, {c, 0, Ch::Y0},
            {4.5, kBlackHz}, {1.5, kLeaderHz}, {c / 2.0, 0, Ch::Cr},
            {4.5, kWhiteHz}, {1.5, kLeaderHz}, {c / 2.0, 0, Ch::Cb}};
  }
  // PD
  return {{s.syncMs, kSyncHz}, {2.08, kBlackHz}, {c, 0, Ch::Y0},
          {c, 0, Ch::Cr},      {c, 0, Ch::Cb},   {c, 0, Ch::Y1}};
}

double lineMs(const Spec& s) {
  double t = 0.0;
  for (const Element& e : lineElements(s, 0))
    t += e.ms;
  return t;
}

double syncOffsetMs(const Spec& s) {
  double t = 0.0;
  for (const Element& e : lineElements(s, 0)) {
    if (e.ch == Ch::Tone && e.hz == kSyncHz)
      return t;
    t += e.ms;
  }
  return 0.0;
}

int linesFor(const Spec& s) {
  return s.height / s.rowsPerLine;
}

unsigned char clampByte(double v) {
  return static_cast<unsigned char>(std::clamp(std::lround(v), 0L, 255L));
}

double toneForValue(double v) {
  return kBlackHz + std::clamp(v, 0.0, 255.0) / 255.0 * (kWhiteHz - kBlackHz);
}

double valueForTone(double hz) {
  return std::clamp((hz - kBlackHz) / (kWhiteHz - kBlackHz) * 255.0, 0.0, 255.0);
}

// ITU-R BT.601 studio swing, as SSTV software uses.
void rgbToYcc(double r, double g, double b, double* y, double* cb, double* cr) {
  *y = 16.0 + 0.257 * r + 0.504 * g + 0.098 * b;
  *cb = 128.0 - 0.148 * r - 0.291 * g + 0.439 * b;
  *cr = 128.0 + 0.439 * r - 0.368 * g - 0.071 * b;
}

QRgb yccToRgb(double y, double cb, double cr) {
  const double yy = 1.164 * (y - 16.0);
  return qRgb(clampByte(yy + 1.596 * (cr - 128.0)),
              clampByte(yy - 0.813 * (cr - 128.0) - 0.391 * (cb - 128.0)),
              clampByte(yy + 2.018 * (cb - 128.0)));
}

// Tones with continuous phase. Durations add up in time, and the sample count
// follows the running total, so long pictures do not drift.
class ToneWriter {
public:
  ToneWriter(int rate, double amplitude, std::vector<float>* out, double shiftHz = 0.0)
      : rate_(rate), amplitude_(amplitude), shift_(shiftHz), out_(out) {}

  void tone(double hz, double ms) {
    scan(ms, [hz](double) { return hz; });
  }

  template <typename F>
  void scan(double ms, F hzAt) {
    const double start = elapsedMs_;
    elapsedMs_ += ms;
    const auto end = static_cast<long long>(std::llround(elapsedMs_ * rate_ / 1000.0));
    while (static_cast<long long>(out_->size()) < end) {
      const double tMs = double(out_->size()) * 1000.0 / rate_;
      const double frac = ms > 0.0 ? std::clamp((tMs - start) / ms, 0.0, 0.999999) : 0.0;
      phase_ += 2.0 * kPi * (hzAt(frac) + shift_) / rate_;
      if (phase_ > 2.0 * kPi)
        phase_ -= 2.0 * kPi;
      out_->push_back(static_cast<float>(amplitude_ * std::sin(phase_)));
    }
  }

private:
  int rate_;
  double amplitude_;
  double shift_;
  std::vector<float>* out_;
  double phase_ = 0.0;
  double elapsedMs_ = 0.0;
};

void writeVis(ToneWriter& w, int code) {
  w.tone(kLeaderHz, 300.0);
  w.tone(kSyncHz, 10.0);
  w.tone(kLeaderHz, 300.0);
  w.tone(kSyncHz, 30.0);
  int ones = 0;
  for (int bit = 0; bit < 7; ++bit) {
    const bool one = (code >> bit) & 1;
    ones += one ? 1 : 0;
    w.tone(one ? 1100.0 : 1300.0, 30.0);
  }
  w.tone((ones % 2) ? 1100.0 : 1300.0, 30.0);
  w.tone(kSyncHz, 30.0);
}

// Frequency of a real audio signal, one value per sample. The audio is mixed
// down around 1900 Hz, low-passed, and the phase step of the result gives the
// tone. The filter delay is the same for everything, so timing still lines up.
class FmDemod {
public:
  void reset(int rate) {
    rate_ = rate;
    const int taps = std::max(15, (rate / 1000) | 1);
    taps_.assign(static_cast<size_t>(taps), 0.0);
    const double cutoff = 950.0 / rate;
    const int mid = taps / 2;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
      const int k = i - mid;
      const double sinc = k == 0 ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * k) / (kPi * k);
      const double window = 0.54 - 0.46 * std::cos(2.0 * kPi * i / (taps - 1));
      taps_[static_cast<size_t>(i)] = sinc * window;
      sum += taps_[static_cast<size_t>(i)];
    }
    for (double& t : taps_)
      t /= sum;
    histI_.assign(taps_.size(), 0.0);
    histQ_.assign(taps_.size(), 0.0);
    pos_ = 0;
    mixPhase_ = 0.0;
    prev_ = {1.0, 0.0};
  }

  int rate() const { return rate_; }

  void run(const float* samples, int count, std::vector<float>* out) {
    const size_t n = taps_.size();
    const double step = 2.0 * kPi * kMixHz / rate_;
    for (int i = 0; i < count; ++i) {
      const double x = samples[i];
      histI_[pos_] = x * std::cos(mixPhase_);
      histQ_[pos_] = -x * std::sin(mixPhase_);
      mixPhase_ += step;
      if (mixPhase_ > 2.0 * kPi)
        mixPhase_ -= 2.0 * kPi;
      double si = 0.0;
      double sq = 0.0;
      size_t h = pos_;
      for (size_t t = 0; t < n; ++t) {
        si += taps_[t] * histI_[h];
        sq += taps_[t] * histQ_[h];
        h = h == 0 ? n - 1 : h - 1;
      }
      pos_ = (pos_ + 1) % n;
      const std::complex<double> z(si, sq);
      const double dphi = std::arg(z * std::conj(prev_));
      prev_ = z;
      out->push_back(static_cast<float>(kMixHz + dphi * rate_ / (2.0 * kPi)));
    }
  }

private:
  int rate_ = 0;
  std::vector<double> taps_;
  std::vector<double> histI_;
  std::vector<double> histQ_;
  size_t pos_ = 0;
  double mixPhase_ = 0.0;
  std::complex<double> prev_{1.0, 0.0};
};

QString defaultRxDirectory() {
  const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  return QDir(documents).filePath(QStringLiteral("STD/SSTV/RX"));
}

}  // namespace

const std::vector<SstvMode>& sstvModes() {
  static const std::vector<SstvMode> modes = [] {
    std::vector<SstvMode> list;
    for (const Spec& s : specs())
      list.push_back(s.mode);
    return list;
  }();
  return modes;
}

QString sstvModeName(SstvMode mode) {
  const Spec* s = specFor(mode);
  return s ? QString::fromLatin1(s->name) : QString();
}

SstvMode sstvModeFromName(const QString& name) {
  for (const Spec& s : specs())
    if (name.compare(QLatin1String(s.name), Qt::CaseInsensitive) == 0)
      return s.mode;
  return SstvMode::None;
}

int sstvVisCode(SstvMode mode) {
  const Spec* s = specFor(mode);
  return s ? s->vis : -1;
}

SstvMode sstvModeFromVis(int code) {
  for (const Spec& s : specs())
    if (s.vis == code)
      return s.mode;
  return SstvMode::None;
}

int sstvWidth(SstvMode mode) {
  const Spec* s = specFor(mode);
  return s ? s->width : 0;
}

int sstvHeight(SstvMode mode) {
  const Spec* s = specFor(mode);
  return s ? s->height : 0;
}

double sstvSeconds(SstvMode mode) {
  const Spec* s = specFor(mode);
  if (!s)
    return 0.0;
  return (910.0 + s->leadSyncMs + lineMs(*s) * linesFor(*s)) / 1000.0;
}

std::vector<float> sstvCwId(const QString& text, int wpm, int toneHz, int sampleRateHz, double amplitude,
                            int gapMs) {
  static const QMap<QChar, const char*> morse = {
      {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},     {'F', "..-."},
      {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
      {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},
      {'S', "..."},   {'T', "-"},     {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
      {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
      {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
      {'/', "-..-."},
  };
  std::vector<float> out;
  if (sampleRateHz <= 0 || wpm <= 0)
    return out;
  const double unit = 1.2 / wpm * sampleRateHz;  // samples per dot
  const int ramp = std::max(1, sampleRateHz / 200);  // 5 ms
  double phase = 0.0;
  const double step = 2.0 * kPi * toneHz / sampleRateHz;
  auto silence = [&](double units) { out.insert(out.end(), size_t(std::lround(units * unit)), 0.0f); };
  auto tone = [&](double units) {
    const int n = int(std::lround(units * unit));
    for (int i = 0; i < n; ++i) {
      double env = 1.0;
      if (i < ramp)
        env = 0.5 - 0.5 * std::cos(kPi * i / ramp);
      else if (i >= n - ramp)
        env = 0.5 - 0.5 * std::cos(kPi * (n - 1 - i) / ramp);
      out.push_back(float(amplitude * env * std::sin(phase)));
      phase += step;
      if (phase > 2.0 * kPi)
        phase -= 2.0 * kPi;
    }
  };
  out.insert(out.end(), size_t(std::max(0, gapMs)) * size_t(sampleRateHz) / 1000, 0.0f);
  bool firstChar = true;
  for (const QChar raw : text.toUpper()) {
    if (raw.isSpace()) {
      silence(4.0);  // 7 units between words, 3 already after the last letter
      continue;
    }
    const auto it = morse.find(raw);
    if (it == morse.end())
      continue;
    if (!firstChar)
      silence(3.0);
    firstChar = false;
    bool firstElement = true;
    for (const char* e = it.value(); *e; ++e) {
      if (!firstElement)
        silence(1.0);
      firstElement = false;
      tone(*e == '-' ? 3.0 : 1.0);
    }
  }
  silence(2.0);
  return out;
}

std::vector<float> sstvFskId(const QString& call, int sampleRateHz, double amplitude) {
  std::vector<float> out;
  QString text = call.trimmed().toUpper();
  if (text.isEmpty() || sampleRateHz <= 0)
    return out;
  ToneWriter w(sampleRateHz, amplitude, &out);
  w.tone(kBlackHz, 300.0);
  w.tone(2100.0, 100.0);
  w.tone(1900.0, 22.0);
  auto symbol = [&w](int value) {
    for (int bit = 0; bit < 6; ++bit)
      w.tone(((value >> bit) & 1) ? 1900.0 : 2100.0, 22.0);
  };
  symbol(0x20);
  symbol(0x2A);
  int check = 0;
  for (const QChar c : text.left(10)) {
    const int v = (c.unicode() - 0x20) & 0x3F;
    check ^= v;
    symbol(v);
  }
  symbol(0x01);
  symbol(check);
  return out;
}

int sstvRowsSentAt(SstvMode mode, double seconds) {
  const Spec* s = specFor(mode);
  if (!s)
    return 0;
  const double into = seconds * 1000.0 - 910.0 - s->leadSyncMs;
  if (into <= 0.0)
    return 0;
  const int lines = int(into / lineMs(*s));
  return std::clamp(lines * s->rowsPerLine, 0, s->height);
}

std::vector<float> sstvModulate(SstvMode mode, const QImage& picture, int sampleRateHz, double amplitude,
                                double toneShiftHz) {
  std::vector<float> out;
  const Spec* s = specFor(mode);
  if (!s || sampleRateHz <= 0)
    return out;
  out.reserve(static_cast<size_t>(sstvSeconds(mode) * sampleRateHz) + 16);
  const QImage img = picture.isNull()
                         ? QImage()
                         : picture.convertToFormat(QImage::Format_RGB32)
                               .scaled(s->width, s->height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  auto pixel = [&](int x, int y) -> QRgb {
    if (img.isNull())
      return qRgb(0, 0, 0);
    return img.pixel(std::clamp(x, 0, s->width - 1), std::clamp(y, 0, s->height - 1));
  };
  // Color difference for a row, averaged with its partner row when the mode
  // shares one value between two rows.
  auto ycc = [&](int x, int y, double* yy, double* cb, double* cr) {
    const QRgb p = pixel(x, y);
    rgbToYcc(qRed(p), qGreen(p), qBlue(p), yy, cb, cr);
  };
  auto pairChroma = [&](int x, int row0, bool wantCr) {
    double y0, cb0, cr0, y1, cb1, cr1;
    ycc(x, row0, &y0, &cb0, &cr0);
    ycc(x, row0 + 1, &y1, &cb1, &cr1);
    return wantCr ? (cr0 + cr1) / 2.0 : (cb0 + cb1) / 2.0;
  };

  ToneWriter w(sampleRateHz, amplitude, &out, toneShiftHz);
  writeVis(w, s->vis);
  if (s->leadSyncMs > 0.0)
    w.tone(kSyncHz, s->leadSyncMs);
  const int width = s->width;
  for (int line = 0; line < linesFor(*s); ++line) {
    const int row = line * s->rowsPerLine;
    for (const Element& e : lineElements(*s, line)) {
      if (e.ch == Ch::Tone) {
        w.tone(e.hz, e.ms);
        continue;
      }
      w.scan(e.ms, [&](double frac) {
        const int x = std::min(width - 1, int(frac * width));
        const QRgb p = pixel(x, row);
        double yy, cb, cr;
        switch (e.ch) {
          case Ch::R:
            return toneForValue(qRed(p));
          case Ch::G:
            return toneForValue(qGreen(p));
          case Ch::B:
            return toneForValue(qBlue(p));
          case Ch::Y0:
            ycc(x, row, &yy, &cb, &cr);
            return toneForValue(yy);
          case Ch::Y1:
            ycc(x, row + 1, &yy, &cb, &cr);
            return toneForValue(yy);
          case Ch::Cr:
          case Ch::Cb: {
            const bool wantCr = e.ch == Ch::Cr;
            if (s->mode == SstvMode::Robot36)
              return toneForValue(pairChroma(x, row - (row % 2), wantCr));
            if (s->rowsPerLine == 2)
              return toneForValue(pairChroma(x, row, wantCr));
            ycc(x, row, &yy, &cb, &cr);
            return toneForValue(wantCr ? cr : cb);
          }
          default:
            return kBlackHz;
        }
      });
    }
  }
  // A short tail so the last line is not cut by the transmitter.
  w.tone(kBlackHz, 30.0);
  return out;
}

const std::vector<SstvWebGallery>& sstvWebGalleries() {
  // Checked 2026-11-24 by the dates on their newest pictures. Others found
  // then had nothing newer than months or years, so they are left out.
  static const std::vector<SstvWebGallery> list = {
      {"CQ SSTV live", "multi-band, 20 m on 14.230", "https://max.cqsstv.com/"},
      {"DC4ML", "Germany", "https://qsl.net/dc4ml/"},
      {"M0PWX, last 30 on 14.230", "England", "https://qsl.net/m0pwx/Last-30-20m-SSTV.htm"},
  };
  return list;
}

QString sstvRxDirectory() {
  const QString dir = QSettings().value(QStringLiteral("sstv/rxDirectory")).toString();
  return dir.isEmpty() ? defaultRxDirectory() : dir;
}

void sstvSetRxDirectory(const QString& directory) {
  QSettings().setValue(QStringLiteral("sstv/rxDirectory"), directory);
}

// ---------------------------------------------------------------------------

struct SstvEngine::Private {
  enum class Phase { Hunt, Image };

  FmDemod demod;
  bool armed = false;
  bool autoSave = true;
  Phase phase = Phase::Hunt;

  // Frequency track. freq[i] is sample base + i. sum/sumSq/sync are prefix
  // sums over it (one longer than freq) for window averages.
  std::vector<float> freq;
  std::vector<double> sum;
  std::vector<double> sumSq;
  std::vector<double> syncSum;
  long long base = 0;
  long long huntFrom = 0;
  // Tuning offset used for the sync detector, from the last VIS.
  double offset = 0.0;

  // Current picture.
  const Spec* spec = nullptr;
  std::vector<Element> elements;
  double syncAt = 0.0;   // ms from line start
  double periodSamples = 0.0;
  double nominalPeriod = 0.0;
  double lineStart = 0.0;  // absolute sample
  int line = 0;
  int locked = 0;
  int missed = 0;
  // Where the last good sync pulse was, for looking for a new VIS header
  // after the signal goes away.
  double lastSyncAt = 0.0;
  QImage image;
  int rows = 0;
  // Per-row channel values, width each; empty where not received yet.
  std::vector<std::vector<float>> chY, chA, chB, chC;
  std::vector<int> chromaKind;  // Robot 36: 1 = Cr, 2 = Cb, 0 = none
  QString lastSaved;
  QString endReason;
  bool selftest = false;
  // FSK ID: where to look after a picture ends, and how far it has looked.
  long long fskFrom = -1;
  int fskTries = 0;
  QString fskId;
  QDateTime savedAt;

  double rate() const { return double(demod.rate()); }
  long long end() const { return base + static_cast<long long>(freq.size()); }

  void resetTrack(int rate) {
    demod.reset(rate);
    freq.clear();
    sum.assign(1, 0.0);
    sumSq.assign(1, 0.0);
    syncSum.assign(1, 0.0);
    base = 0;
    huntFrom = 0;
  }

  void append(const float* samples, int count) {
    const size_t before = freq.size();
    demod.run(samples, count, &freq);
    for (size_t i = before; i < freq.size(); ++i) {
      const double f = freq[i];
      sum.push_back(sum.back() + f);
      sumSq.push_back(sumSq.back() + f * f);
      syncSum.push_back(syncSum.back() + ((f - offset) < kSyncThresholdHz ? 1.0 : 0.0));
    }
  }

  // Drop history older than `keepFrom` (absolute sample).
  void trim(long long keepFrom) {
    const long long drop = keepFrom - base;
    if (drop < static_cast<long long>(rate()) * 4)
      return;
    const auto d = static_cast<size_t>(drop);
    freq.erase(freq.begin(), freq.begin() + static_cast<std::ptrdiff_t>(d));
    auto shift = [d](std::vector<double>& v) {
      const double origin = v[d];
      v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(d));
      for (double& x : v)
        x -= origin;
    };
    shift(sum);
    shift(sumSq);
    shift(syncSum);
    base += drop;
  }

  // Mean over absolute samples [a, b).
  double meanOf(const std::vector<double>& prefix, double a, double b) const {
    long long ia = static_cast<long long>(std::floor(a)) - base;
    long long ib = static_cast<long long>(std::floor(b)) - base;
    const long long n = static_cast<long long>(freq.size());
    ia = std::clamp(ia, 0LL, n);
    ib = std::clamp(ib, 0LL, n);
    if (ib <= ia)
      ib = std::min(n, ia + 1);
    if (ib <= ia)
      return 0.0;
    return (prefix[static_cast<size_t>(ib)] - prefix[static_cast<size_t>(ia)]) / double(ib - ia);
  }
  double meanHz(double a, double b) const { return meanOf(sum, a, b); }
  double ms(double milliseconds) const { return milliseconds * rate() / 1000.0; }

  // Median of short-slice averages over [a, b). Noise makes the frequency
  // reading jump in short spikes; the median ignores them where a plain
  // average would be pulled off.
  double robustHz(double a, double b, double sliceMs = 2.0) const {
    const double slice = std::max(1.0, ms(sliceMs));
    std::vector<double> means;
    for (double x = a; x + slice <= b + 0.5; x += slice)
      means.push_back(meanHz(x, x + slice));
    if (means.empty())
      return meanHz(a, b);
    const auto mid = means.begin() + static_cast<std::ptrdiff_t>(means.size() / 2);
    std::nth_element(means.begin(), mid, means.end());
    return *mid;
  }

  // Share of 10 ms slices in [a, b) within tolerance of hz.
  double steadyShare(double a, double b, double hz, double tolerance) const {
    const double slice = ms(10.0);
    int in = 0;
    int all = 0;
    for (double x = a; x + slice <= b + 0.5; x += slice) {
      ++all;
      if (std::abs(robustHz(x, x + slice, 1.0) - hz) <= tolerance)
        ++in;
    }
    return all ? double(in) / all : 0.0;
  }

  struct Vis {
    const Spec* spec = nullptr;
    double offset = 0.0;
    double end = 0.0;  // absolute sample where the header ends
  };

  // Look for a VIS header from huntFrom on. Advances huntFrom either way.
  bool findVis(Vis* found) {
    const double step = ms(1.0);
    const double lead = ms(200.0);
    // The whole header: leader 300 ms, break 10 ms, leader 300 ms, then the
    // start bit at t. Look back far enough to check all of it.
    double t = std::max(double(huntFrom), double(base) + ms(600.0));
    const double last = double(end()) - ms(300.0);  // need the whole header after t
    for (; t < last; t += step) {
      // Start bit: 30 ms of sync right after the leader.
      const double startBit = robustHz(t + ms(5.0), t + ms(25.0));
      if (startBit > kSyncHz + 250.0)
        continue;
      const double leaderMean = robustHz(t - lead, t - ms(5.0), 5.0);
      const double off = leaderMean - kLeaderHz;
      if (std::abs(off) > 150.0)
        continue;
      if (std::abs(startBit - off - kSyncHz) > 90.0)
        continue;
      if (steadyShare(t - lead, t - ms(5.0), leaderMean, 60.0) < 0.75)
        continue;
      // Find the edge: the point that best splits leader from start bit.
      double edge = t;
      double best = -1e9;
      for (double e = t - ms(12.0); e <= t + ms(12.0); e += ms(0.25)) {
        const double score = robustHz(e - ms(15.0), e, 1.0) - robustHz(e, e + ms(15.0), 1.0);
        if (score > best) {
          best = score;
          edge = e;
        }
      }
      int code = 0;
      int ones = 0;
      bool ok = true;
      for (int bit = 0; bit < 8; ++bit) {
        const double a = edge + ms(30.0 * (bit + 1) + 6.0);
        const double hz = robustHz(a, a + ms(18.0)) - off;
        if (hz < 1000.0 || hz > 1400.0) {
          ok = false;
          break;
        }
        const bool one = hz < kSyncHz;
        ones += one ? 1 : 0;
        if (bit < 7 && one)
          code |= 1 << bit;
      }
      const double stop = robustHz(edge + ms(276.0), edge + ms(294.0)) - off;
      if (!ok || (ones % 2) != 0 || std::abs(stop - kSyncHz) > 90.0)
        continue;
      const Spec* s = specFor(sstvModeFromVis(code));
      if (!s)
        continue;
      // The first leader and the 10 ms break before the second one. A flat
      // gray stretch of a picture sits near the leader tone, so the second
      // leader alone is not enough to tell a header from picture content.
      const double brk = robustHz(edge - ms(309.0), edge - ms(303.0), 1.0) - off;
      if (brk > kBlackHz || steadyShare(edge - ms(605.0), edge - ms(315.0), leaderMean, 60.0) < 0.7)
        continue;
      found->spec = s;
      found->offset = off;
      found->end = edge + ms(300.0);
      huntFrom = static_cast<long long>(found->end);
      return true;
    }
    huntFrom = static_cast<long long>(std::max(t, double(huntFrom)));
    return false;
  }

  void beginVis(const Vis& v) {
    offset = v.offset;
    begin(v.spec, v.end + ms(v.spec->leadSyncMs));
  }

  void begin(const Spec* s, double firstLineStart) {
    spec = s;
    elements = lineElements(*s, 0, true);
    syncAt = syncOffsetMs(*s);
    nominalPeriod = ms(lineMs(*s));
    periodSamples = nominalPeriod;
    lineStart = firstLineStart;
    line = 0;
    locked = 0;
    missed = 0;
    lastSyncAt = firstLineStart;
    rows = 0;
    image = QImage(s->width, s->height, QImage::Format_RGB32);
    image.fill(qRgb(24, 24, 28));
    const auto n = static_cast<size_t>(s->height);
    chY.assign(n, {});
    chA.assign(n, {});
    chB.assign(n, {});
    chC.assign(n, {});
    chromaKind.assign(n, 0);
    // Recompute the sync column with this picture's tuning offset.
    for (size_t i = 0; i < freq.size(); ++i)
      syncSum[i + 1] = syncSum[i] + ((freq[i] - offset) < kSyncThresholdHz ? 1.0 : 0.0);
    phase = Phase::Image;
  }

  bool searchingWide() const { return missed >= 2; }

  // How far either side of the expected sync to look.
  double searchWindow() const {
    if (searchingWide())
      return periodSamples * 0.5;
    if (locked < 3)
      return ms(std::max(12.0, spec->syncMs * 1.5));
    return ms(std::max(3.0, spec->syncMs * 0.6));
  }

  // Find the sync pulse near where it is expected and nudge the timing.
  // While locked the search is narrow. After two missed pulses it opens to
  // half a line either way, so the picture picks up again after a fade, a
  // burst of QRM, or a slip, instead of drifting on.
  void track() {
    const double syncLen = ms(spec->syncMs);
    const double expect = lineStart + ms(syncAt);
    const bool wide = searchingWide();
    const double window = searchWindow();
    double best = -1.0;
    double bestAt = expect;
    const double step = std::max(1.0, ms(0.1));
    for (double a = expect - window; a <= expect + window; a += step) {
      // Inside the pulse should be sync, just before and after it should not.
      const double inside = meanOf(syncSum, a, a + syncLen);
      const double before = meanOf(syncSum, a - ms(1.0), a);
      const double after = meanOf(syncSum, a + syncLen, a + syncLen + ms(1.0));
      const double score = inside - 0.5 * (before + after);
      if (score > best) {
        best = score;
        bestAt = a;
      }
    }
    // A wide search needs a cleaner pulse, so noise does not pull the lines.
    if (best >= (wide ? 0.6 : 0.55)) {
      const double error = bestAt - expect;
      lineStart += error * (locked < 3 || wide ? 1.0 : 0.5);
      if (locked >= 2 && !wide)
        periodSamples = std::clamp(periodSamples + error * 0.05, nominalPeriod * 0.98, nominalPeriod * 1.02);
      ++locked;
      missed = 0;
      lastSyncAt = bestAt + syncLen;
    } else {
      ++missed;
    }
  }

  // A pixel's tone is the median of the readings it covers, so a noise click
  // cannot drag it toward gray the way an average would.
  double pixelHz(double a, double b) const {
    long long ia = static_cast<long long>(std::floor(a)) - base;
    long long ib = static_cast<long long>(std::ceil(b)) - base;
    const long long n = static_cast<long long>(freq.size());
    ia = std::clamp(ia, 0LL, n);
    ib = std::clamp(ib, ia, n);
    const long long count = ib - ia;
    if (count <= 0)
      return 0.0;
    if (count <= 2)
      return meanHz(a, b);
    float tmp[64];
    const int k = int(std::min<long long>(count, 64));
    const long long stride = std::max<long long>(1, count / k);
    for (int i = 0; i < k; ++i)
      tmp[i] = freq[static_cast<size_t>(ia + i * stride)];
    std::nth_element(tmp, tmp + k / 2, tmp + k);
    return tmp[k / 2];
  }

  std::vector<float> scanChannel(double startSample, double durSamples, int width) const {
    std::vector<float> v(static_cast<size_t>(width));
    const double px = durSamples / width;
    for (int x = 0; x < width; ++x) {
      const double a = startSample + x * px;
      v[static_cast<size_t>(x)] = static_cast<float>(valueForTone(pixelHz(a, a + std::max(1.0, px)) - offset));
    }
    return v;
  }

  void decodeLine() {
    const int width = spec->width;
    const int row = line * spec->rowsPerLine;
    double t = 0.0;
    for (const Element& e : elements) {
      const double a = lineStart + ms(t);
      const double len = ms(e.ms);
      t += e.ms;
      if (e.ch == Ch::Tone)
        continue;
      std::vector<float> v = scanChannel(a, len, width);
      const auto r = static_cast<size_t>(row);
      switch (e.ch) {
        case Ch::R: chA[r] = std::move(v); break;
        case Ch::G: chB[r] = std::move(v); break;
        case Ch::B: chC[r] = std::move(v); break;
        case Ch::Y0: chY[r] = std::move(v); break;
        case Ch::Y1: chY[r + 1] = std::move(v); break;
        case Ch::Cr:
          chA[r] = v;
          if (spec->rowsPerLine == 2)
            chA[r + 1] = std::move(v);
          break;
        case Ch::Cb:
          chB[r] = v;
          if (spec->rowsPerLine == 2)
            chB[r + 1] = std::move(v);
          break;
        case Ch::Chroma: {
          // The separator before this scan says which one it is.
          const double sepA = lineStart + ms(spec->syncMs + 3.0 + spec->scanMs);
          const double sep = robustHz(sepA + ms(0.5), sepA + ms(4.0), 0.5) - offset;
          const bool cr = sep < kLeaderHz;
          chromaKind[r] = cr ? 1 : 2;
          (cr ? chA[r] : chB[r]) = std::move(v);
          break;
        }
        default:
          break;
      }
    }
    for (int k = 0; k < spec->rowsPerLine; ++k)
      render(row + k);
    if (spec->color == ColorModel::RobotAlternate) {
      if (row > 0)
        render(row - 1);
    }
    rows = std::max(rows, row + spec->rowsPerLine);
  }

  const std::vector<float>* chromaFor(int row, bool cr) const {
    auto has = [&](int r) -> const std::vector<float>* {
      if (r < 0 || r >= spec->height)
        return nullptr;
      const auto& v = cr ? chA[static_cast<size_t>(r)] : chB[static_cast<size_t>(r)];
      return v.empty() ? nullptr : &v;
    };
    if (auto* v = has(row))
      return v;
    // Robot 36 pairs rows 0+1, 2+3; then try the nearest row either way.
    if (auto* v = has(row ^ 1))
      return v;
    if (auto* v = has(row - 1))
      return v;
    return has(row + 1);
  }

  void render(int row) {
    if (row < 0 || row >= spec->height)
      return;
    const auto r = static_cast<size_t>(row);
    auto* out = reinterpret_cast<QRgb*>(image.scanLine(row));
    const int width = spec->width;
    if (spec->color == ColorModel::Rgb) {
      if (chA[r].empty() || chB[r].empty() || chC[r].empty())
        return;
      for (int x = 0; x < width; ++x)
        out[x] = qRgb(clampByte(chA[r][static_cast<size_t>(x)]), clampByte(chB[r][static_cast<size_t>(x)]),
                      clampByte(chC[r][static_cast<size_t>(x)]));
      return;
    }
    if (chY[r].empty())
      return;
    const std::vector<float>* cr = chromaFor(row, true);
    const std::vector<float>* cb = chromaFor(row, false);
    for (int x = 0; x < width; ++x) {
      const auto i = static_cast<size_t>(x);
      out[x] = yccToRgb(chY[r][i], cb ? (*cb)[i] : 128.0, cr ? (*cr)[i] : 128.0);
    }
  }

  // No sync pulse for this long means the station has stopped.
  // One FSK ID bit: 1 for 1900 Hz, 0 for 2100 Hz, -1 when it is neither.
  int fskBit(double at) const {
    const double hz = meanHz(at + ms(4.0), at + ms(18.0)) - offset;
    if (std::abs(hz - 1900.0) < 80.0)
      return 1;
    if (std::abs(hz - 2100.0) < 80.0)
      return 0;
    return -1;
  }

  int fskSymbol(double at) const {
    int v = 0;
    for (int bit = 0; bit < 6; ++bit) {
      const int b = fskBit(at + ms(22.0 * bit));
      if (b < 0)
        return -1;
      v |= b << bit;
    }
    return v;
  }

  // Look for an FSK ID in the few seconds after a picture. Returns the call.
  QString scanFskId(double from, double until) const {
    const double symbolLen = ms(22.0 * 6.0);
    for (double t = from; t + symbolLen * 4 < until; t += ms(1.0)) {
      if (fskBit(t) < 0)
        continue;
      if (fskSymbol(t) != 0x20 || fskSymbol(t + symbolLen) != 0x2A)
        continue;
      QString call;
      int check = 0;
      double at = t + symbolLen * 2;
      bool ended = false;
      for (int n = 0; n < 12 && at + symbolLen <= double(end()); ++n, at += symbolLen) {
        const int v = fskSymbol(at);
        if (v < 0)
          break;
        if (v == 0x01) {
          ended = true;
          at += symbolLen;
          break;
        }
        call += QChar(v + 0x20);
        check ^= v;
      }
      if (!ended || call.size() < 3 || call.size() > 10)
        continue;
      static const QRegularExpression callShape(QStringLiteral("^[A-Z0-9/]+$"));
      if (!callShape.match(call).hasMatch() || !call.contains(QRegularExpression(QStringLiteral("[0-9]"))))
        continue;
      // A check symbol may follow; when it does, it must agree.
      const int sent = at + symbolLen <= double(end()) ? fskSymbol(at) : -1;
      if (sent >= 0 && sent != check)
        continue;
      return call;
    }
    return QString();
  }

  bool signalGone() const { return missed > 0 && double(missed) * periodSamples >= ms(12000.0); }

  // Returns true when the picture is complete (or the signal is gone).
  bool decodeAvailable(SstvEngine* q) {
    bool changed = false;
    const int lines = linesFor(*spec);
    while (line < lines) {
      const double need = lineStart + std::max(periodSamples, ms(syncAt + spec->syncMs)) + searchWindow() +
                          ms(spec->syncMs + 15.0);
      if (need >= double(end()))
        break;
      track();
      decodeLine();
      changed = true;
      lineStart += periodSamples;
      ++line;
      if (signalGone())
        break;
    }
    if (changed)
      emit q->imageChanged();
    return line >= lines || signalGone();
  }

  QString save() {
    if (image.isNull() || rows < 1 || !spec)
      return QString();
    if (selftest)
      return QStringLiteral("selftest.png");
    const QString dir = sstvRxDirectory();
    if (!QDir().mkpath(dir))
      return QString();
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString mode = QString::fromLatin1(spec->name);
    mode.remove(QLatin1Char(' '));
    const QString path = QDir(dir).filePath(QStringLiteral("%1_%2.png").arg(stamp, mode));
    const QImage picture = image.copy(0, 0, spec->width, std::max(1, rows));
    if (!picture.save(path, "PNG"))
      return QString();
    lastSaved = path;
    return path;
  }
};

SstvEngine::SstvEngine(QObject* parent) : QObject(parent), d_(new Private) {
  d_->selftest = qEnvironmentVariableIsSet("STD_SELFTEST");
}

SstvEngine::~SstvEngine() {
  delete d_;
}

void SstvEngine::setArmed(bool armed) {
  if (d_->armed == armed)
    return;
  d_->armed = armed;
  if (!armed) {
    d_->phase = Private::Phase::Hunt;
    d_->resetTrack(d_->demod.rate() > 0 ? d_->demod.rate() : 48000);
  }
}

bool SstvEngine::armed() const {
  return d_->armed;
}

void SstvEngine::setAutoSave(bool on) {
  d_->autoSave = on;
}

void SstvEngine::startNow(SstvMode mode) {
  const Spec* s = specFor(mode);
  if (!s || d_->demod.rate() <= 0)
    return;
  d_->offset = 0.0;
  d_->begin(s, double(d_->end()));
  emit pictureStarted(mode);
}

void SstvEngine::stop() {
  if (d_->phase == Private::Phase::Image) {
    d_->endReason = QStringLiteral("stopped by hand at line %1").arg(d_->rows);
    d_->phase = Private::Phase::Hunt;
    d_->huntFrom = d_->end();
    emit pictureFinished();
  }
}

void SstvEngine::clear() {
  d_->phase = Private::Phase::Hunt;
  d_->huntFrom = d_->end();
  d_->spec = nullptr;
  d_->image = QImage();
  d_->rows = 0;
  emit imageChanged();
}

void SstvEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!d_->armed || count <= 0 || sampleRateHz <= 0)
    return;
  if (d_->demod.rate() != sampleRateHz) {
    d_->resetTrack(sampleRateHz);
    d_->phase = Private::Phase::Hunt;
  }
  d_->append(samples, count);
  auto finish = [this](const QString& why) {
    d_->phase = Private::Phase::Hunt;
    d_->endReason = why;
    d_->fskFrom = static_cast<long long>(std::max(0.0, d_->lineStart - d_->ms(500.0)));
    d_->fskTries = 0;
    d_->fskId.clear();
    if (d_->autoSave && d_->rows >= 16) {
      const QString path = d_->save();
      if (!path.isEmpty())
        emit imageSaved(path);
    }
    emit pictureFinished();
  };
  for (int guard = 0; guard < 4; ++guard) {
    Private::Vis vis;
    if (d_->phase == Private::Phase::Hunt) {
      if (!d_->findVis(&vis))
        break;
      d_->beginVis(vis);
      emit pictureStarted(vis.spec->mode);
    }
    if (d_->phase == Private::Phase::Image) {
      // Once the sync has stopped for a few lines, listen for a new header
      // from where it stopped; a new picture replaces the one that ended.
      if (d_->missed >= 3) {
        d_->huntFrom = std::max(d_->huntFrom, static_cast<long long>(d_->lastSyncAt));
        if (d_->findVis(&vis)) {
          finish(QStringLiteral("a new picture started at line %1").arg(d_->rows));
          d_->beginVis(vis);
          emit pictureStarted(vis.spec->mode);
        }
      }
      if (!d_->decodeAvailable(this))
        break;
      d_->huntFrom = std::max(d_->huntFrom, static_cast<long long>(d_->lastSyncAt));
      finish(d_->signalGone() ? QStringLiteral("no sync for 12 s at line %1").arg(d_->rows)
                              : QStringLiteral("complete"));
    }
  }
  // FSK ID: look 4 and 8 seconds after the picture ended.
  if (d_->fskFrom >= 0) {
    const double waited = double(d_->end() - d_->fskFrom);
    if (d_->fskTries < 2 && waited >= d_->ms(d_->fskTries == 0 ? 4000.0 : 8000.0)) {
      ++d_->fskTries;
      const QString call = d_->scanFskId(double(d_->fskFrom), double(d_->end()));
      if (!call.isEmpty()) {
        d_->fskId = call;
        d_->fskFrom = -1;
        emit fskIdHeard(call);
      } else if (d_->fskTries >= 2) {
        d_->fskFrom = -1;
      }
    }
  }
  // Keep a few seconds behind whatever still needs reading.
  double keep = d_->phase == Private::Phase::Image ? std::min(d_->lineStart, d_->lastSyncAt) - d_->ms(2000.0)
                                                   : double(d_->huntFrom) - d_->ms(2000.0);
  if (d_->fskFrom >= 0)
    keep = std::min(keep, double(d_->fskFrom) - d_->ms(500.0));
  d_->trim(static_cast<long long>(keep));
}

bool SstvEngine::receiving() const {
  return d_->phase == Private::Phase::Image;
}

SstvMode SstvEngine::mode() const {
  return d_->spec ? d_->spec->mode : SstvMode::None;
}

const QImage& SstvEngine::image() const {
  return d_->image;
}

int SstvEngine::rowCount() const {
  return d_->rows;
}

QString SstvEngine::endReason() const {
  return d_->endReason;
}

double SstvEngine::offsetHz() const {
  return d_->offset;
}

QString SstvEngine::saveNow() {
  const QString path = d_->save();
  if (!path.isEmpty())
    emit imageSaved(path);
  return path;
}

SstvResult sstvDecode(const float* samples, int count, int sampleRateHz) {
  SstvResult result;
  SstvEngine engine;
  engine.setAutoSave(false);
  engine.setArmed(true);
  bool done = false;
  QObject::connect(&engine, &SstvEngine::pictureFinished, [&] {
    if (done)
      return;
    done = true;
    result.mode = engine.mode();
    result.image = engine.image();
    result.rows = engine.rowCount();
    result.offsetHz = engine.offsetHz();
    result.endReason = engine.endReason();
  });
  QObject::connect(&engine, &SstvEngine::pictureStarted, [&result, &done](SstvMode) {
    if (!done)
      ++result.starts;
  });
  QObject::connect(&engine, &SstvEngine::fskIdHeard, [&result](const QString& call) { result.fskId = call; });
  const int chunk = std::max(1, sampleRateHz / 20);
  for (int i = 0; i < count; i += chunk)
    engine.pushAudio(samples + i, std::min(chunk, count - i), sampleRateHz);
  // Flush with silence so the last lines are read and an FSK ID is looked for.
  std::vector<float> quiet(static_cast<size_t>(sampleRateHz), 0.0f);
  for (int k = 0; k < 9 && (!done || result.fskId.isEmpty()); ++k)
    engine.pushAudio(quiet.data(), int(quiet.size()), sampleRateHz);
  if (!done) {
    result.mode = engine.mode();
    result.image = engine.image();
    result.rows = engine.rowCount();
    result.offsetHz = engine.offsetHz();
    result.endReason = engine.endReason();
  }
  return result;
}
