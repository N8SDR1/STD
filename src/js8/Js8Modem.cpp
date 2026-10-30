// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// JS8 Normal from JS8Call-improved (Jordan Sherer KN4CRD, Allan Bazinet
// W6BAZ, GPL-3). The frame builder follows JS8_Mode/JS8.cpp encode()
// and JS8_Main/Varicode.cpp packHuffMessage(). The tone clock follows
// JS8_Mode/Modulator.cpp: 1920 samples at 12 kHz, four times that at
// 48 kHz, 6.25 Hz spacing, 500 ms of silence, then 79 symbols.

#include "js8/Js8Modem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kRate = 48000;
constexpr int kSymbol = 7680;
constexpr int kDelay = 24000;
constexpr int kSymbols = 79;
constexpr int kTones = 8;
constexpr double kSpacing = 6.25;
constexpr char kAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+";

const int kCostas[7] = {4, 2, 5, 6, 1, 3, 0};
const int kCostasAt[3] = {0, 36, 72};

struct Huff {
  char ch;
  const char* bits;
};

const Huff kHuff[] = {
    {' ', "01"},      {'E', "100"},     {'T', "1101"},    {'A', "0011"},    {'O', "11111"},
    {'I', "11100"},   {'N', "10111"},   {'S', "10100"},   {'H', "00011"},   {'R', "00000"},
    {'D', "111011"},  {'L', "110011"},  {'C', "110001"},  {'U', "101101"},  {'M', "101011"},
    {'W', "001011"},  {'F', "001001"},  {'G', "000101"},  {'Y', "000011"},  {'P', "1111011"},
    {'B', "1111001"}, {'.', "1110100"}, {'V', "1100101"}, {'K', "1100100"}, {'-', "1100001"},
    {'+', "1100000"}, {'?', "1011001"}, {'!', "1011000"}, {'"', "1010101"}, {'X', "1010100"},
    {'0', "0010101"}, {'J', "0010100"}, {'1', "0010001"}, {'Q', "0010000"}, {'2', "0001001"},
    {'Z', "0001000"}, {'3', "0000101"}, {'5', "0000100"}, {'4', "11110101"}, {'9', "11110100"},
    {'8', "11110001"}, {'6', "11110000"}, {'7', "11101011"}, {'/', "11101010"},
};

const char* kParityHex[87] = {
    "23bba830e23b6b6f50982e", "1f8e55da218c5df3309052", "ca7b3217cd92bd59a5ae20",
    "56f78313537d0f4382964e", "6be396b5e2e819e373340c", "293548a138858328af4210",
    "cb6c6afcdc28bb3f7c6e86", "3f2a86f5c5bd225c961150", "849dd2d63673481860f62c",
    "56cdaec6e7ae14b43feeee", "04ef5cfa3766ba778f45a4", "c525ae4bd4f627320a3974",
    "41fd9520b2e4abeb2f989c", "7fb36c24085a34d8c1dbc4", "40fc3e44bb7d2bb2756e44",
    "d38ab0a1d2e52a8ec3bc76", "3d0f929ef3949bd84d4734", "45d3814f504064f80549ae",
    "f14dbf263825d0bd04b05e", "db714f8f64e8ac7af1a76e", "8d0274de71e7c1a8055eb0",
    "51f81573dd4049b082de14", "d8f937f31822e57c562370", "b6537f417e61d1a7085336",
    "ecbd7c73b9cd34c3720c8a", "3d188ea477f6fa41317a4e", "1ac4672b549cd6dba79bcc",
    "a377253773ea678367c3f6", "0dbd816fba1543f721dc72", "ca4186dd44c3121565cf5c",
    "29c29dba9c545e267762fe", "1616d78018d0b4745ca0f2", "fe37802941d66dde02b99c",
    "a9fa8e50bcb032c85e3304", "83f640f1a48a8ebc0443ea", "3776af54ccfbae916afde6",
    "a8fc906976c35669e79ce0", "f08a91fb2e1f78290619a8", "cc9da55fe046d0cb3a770c",
    "d36d662a69ae24b74dcbd8", "40907b01280f03c0323946", "d037db825175d851f3af00",
    "1bf1490607c54032660ede", "0af7723161ec223080be86", "eca9afa0f6b01d92305edc",
    "7a8dec79a51e8ac5388022", "9059dfa2bb20ef7ef73ad4", "6abb212d9739dfc02580f2",
    "f6ad4824b87c80ebfce466", "d747bfc5fd65ef70fbd9bc", "612f63acc025b6ab476f7c",
    "05209a0abb530b9e7e34b0", "45b7ab6242b77474d9f11a", "6c280d2a0523d9c4bc5946",
    "f1627701a2d692fd9449e6", "8d9071b7e7a6a2eed6965e", "bf4f56e073271f6ab4bf80",
    "c0fc3ec4fb7d2bb2756644", "57da6d13cb96a7689b2790", "a9fa2eefa6f8796a355772",
    "164cc861bdd803c547f2ac", "cc6de59755420925f90ed2", "a0c0033a52ab6299802fd2",
    "b274db8abd3c6f396ea356", "97d4169cb33e7435718d90", "81cfc6f18c35b1e1f17114",
    "481a2a0df8a23583f82d6c", "081c29a10d468ccdbcecb6", "2c4142bf42b01e71076acc",
    "a6573f3dc8b16c9d19f746", "c87af9a5d5206abca532a8", "012dee2198eba82b19a1da",
    "b1ca4ea2e3d173bad4379c", "b33ec97be83ce413f9acc8", "5b0f7742bca86b8012609a",
    "37d8e0af9258b9e8c5f9b2", "35ad3fb0faeb5f1b0c30dc", "6114e08483043fd3f38a8a",
    "cd921fdf59e882683763f6", "95e45ecd0135aca9d6e6ae", "2e547dd7a05f6597aac516",
    "14cd0f642fc0c5fe3a65ca", "3a0a1dfd7eee29c2e827e0", "c8b5dffc335095dcdcaf2a",
    "3dd01a59d86310743ec752", "8abdb889efbe39a510a118", "3f231f212055371cf3e2a2",
};

int alphabetIndex(char ch) {
  for (int i = 0; kAlphabet[i]; ++i) {
    if (kAlphabet[i] == ch)
      return i;
  }
  return -1;
}

bool parityBit(int row, int col) {
  const char hex = kParityHex[row][col / 4];
  int value = 0;
  if (hex >= '0' && hex <= '9')
    value = hex - '0';
  else if (hex >= 'a' && hex <= 'f')
    value = hex - 'a' + 10;
  else if (hex >= 'A' && hex <= 'F')
    value = hex - 'A' + 10;
  const int shift = 3 - (col % 4);
  return (value & (1 << shift)) != 0;
}

// boost::augmented_crc<12, 0xc06>, then JS8Call-improved xor 42.
// Width-12, poly 0xC06, init 0, no reflection, then 12 zero bits.
uint16_t crc12(const uint8_t* data, int count) {
  uint16_t rem = 0;
  auto bit = [&](int one) {
    const int top = (rem >> 11) & 1;
    rem = static_cast<uint16_t>((rem << 1) & 0x0FFF);
    if (top ^ one)
      rem = static_cast<uint16_t>(rem ^ 0x0C06);
  };
  for (int i = 0; i < count; ++i) {
    for (int b = 7; b >= 0; --b)
      bit((data[i] >> b) & 1);
  }
  for (int i = 0; i < 12; ++i)
    bit(0);
  return static_cast<uint16_t>(rem ^ 42);
}

const char* huffBits(char ch) {
  for (const Huff& row : kHuff) {
    if (row.ch == ch)
      return row.bits;
  }
  return nullptr;
}

QString huffDecode(const std::string& bits) {
  QString text;
  std::size_t at = 0;
  while (at < bits.size()) {
    const char* found = nullptr;
    char ch = 0;
    std::size_t length = 0;
    for (const Huff& row : kHuff) {
      const std::size_t n = std::strlen(row.bits);
      if (n > length && bits.compare(at, n, row.bits) == 0) {
        found = row.bits;
        ch = row.ch;
        length = n;
      }
    }
    if (!found)
      break;
    text.append(QLatin1Char(ch));
    at += length;
  }
  return text;
}

QString unpack72(const std::string& bits72) {
  if (bits72.size() < 72 || bits72[0] != '1')
    return {};
  const int pad = static_cast<int>(bits72.rfind('0'));
  if (pad <= 2)
    return {};
  // Drop the data flag and the compression flag. The pad zero and the
  // ones after it are not payload.
  if (bits72[1] != '0')
    return {};
  return huffDecode(bits72.substr(2, static_cast<std::size_t>(pad - 2)));
}

std::string pack72(const QString& text, int* consumed) {
  std::string bits = "10";
  int used = 0;
  const QString upper = text.toUpper();
  for (int i = 0; i < upper.size(); ++i) {
    const char ch = upper.at(i).toLatin1();
    const char* code = huffBits(ch);
    if (!code)
      break;
    const std::size_t n = std::strlen(code);
    if (bits.size() + n >= 72)
      break;
    bits.append(code);
    ++used;
  }
  if (consumed)
    *consumed = used;
  if (used < 1)
    return {};
  bits.push_back('0');
  while (bits.size() < 72)
    bits.push_back('1');
  return bits;
}

QString symbols12(const std::string& bits72) {
  uint64_t value = 0;
  for (int i = 0; i < 64; ++i)
    value = (value << 1) | (bits72[static_cast<std::size_t>(i)] == '1' ? 1ull : 0ull);
  uint8_t rem = 0;
  for (int i = 0; i < 8; ++i)
    rem = static_cast<uint8_t>((rem << 1) | (bits72[static_cast<std::size_t>(64 + i)] == '1' ? 1 : 0));
  const uint8_t remHigh = static_cast<uint8_t>(((value & 0x0F) << 2) | (rem >> 6));
  const uint8_t remLow = static_cast<uint8_t>(rem & 0x3F);
  value >>= 4;
  char packed[12];
  packed[11] = kAlphabet[remLow];
  packed[10] = kAlphabet[remHigh];
  for (int i = 0; i < 10; ++i) {
    packed[9 - i] = kAlphabet[value & 0x3F];
    value >>= 6;
  }
  return QString::fromLatin1(packed, 12);
}

bool encodeFrame(const QString& symbols, int type, int* tones) {
  if (symbols.size() < 12)
    return false;
  std::array<uint8_t, 11> bytes{};
  for (int i = 0, j = 0; i < 12; i += 4, j += 3) {
    const int a = alphabetIndex(symbols.at(i).toLatin1());
    const int b = alphabetIndex(symbols.at(i + 1).toLatin1());
    const int c = alphabetIndex(symbols.at(i + 2).toLatin1());
    const int d = alphabetIndex(symbols.at(i + 3).toLatin1());
    if (a < 0 || b < 0 || c < 0 || d < 0)
      return false;
    const uint32_t words = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12) |
                           (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);
    bytes[static_cast<std::size_t>(j)] = static_cast<uint8_t>(words >> 16);
    bytes[static_cast<std::size_t>(j + 1)] = static_cast<uint8_t>(words >> 8);
    bytes[static_cast<std::size_t>(j + 2)] = static_cast<uint8_t>(words);
  }
  bytes[9] = static_cast<uint8_t>((type & 7) << 5);
  const uint16_t crc = crc12(bytes.data(), int(bytes.size()));
  bytes[9] = static_cast<uint8_t>(bytes[9] | ((crc >> 7) & 0x1F));
  bytes[10] = static_cast<uint8_t>((crc & 0x7F) << 1);

  std::array<int, 87> message{};
  int bitAt = 0;
  for (int i = 0; i < 87; ++i) {
    const int byte = bitAt / 8;
    const int mask = 0x80 >> (bitAt % 8);
    message[static_cast<std::size_t>(i)] = (bytes[static_cast<std::size_t>(byte)] & mask) ? 1 : 0;
    ++bitAt;
  }
  std::fill_n(tones, kSymbols, 0);
  for (int block = 0; block < 3; ++block) {
    for (int i = 0; i < 7; ++i)
      tones[kCostasAt[block] + i] = kCostas[i];
  }
  int parityWord = 0;
  int outputWord = 0;
  int got = 0;
  int parityAt = 7;
  int outputAt = 43;
  for (int i = 0; i < 87; ++i) {
    int parity = 0;
    for (int j = 0; j < 87; ++j) {
      if (parityBit(i, j) && message[static_cast<std::size_t>(j)])
        parity ^= 1;
    }
    parityWord = (parityWord << 1) | parity;
    outputWord = (outputWord << 1) | message[static_cast<std::size_t>(i)];
    if (++got == 3) {
      tones[parityAt++] = parityWord;
      tones[outputAt++] = outputWord;
      parityWord = 0;
      outputWord = 0;
      got = 0;
    }
  }
  return parityAt == 36 && outputAt == 72;
}

void writeFrame(const int* tones, int toneHz, std::vector<float>* out) {
  const int origin = int(out->size());
  out->resize(static_cast<std::size_t>(origin + kDelay + kSymbols * kSymbol), 0.0f);
  double phi = 0.0;
  const double fadeAt = (kSymbols - 0.017) * double(kSymbol);
  float amp = 0.8f;
  int n = 0;
  for (int sym = 0; sym < kSymbols; ++sym) {
    const double freq = double(toneHz) + tones[sym] * kSpacing;
    const double dphi = 2.0 * 3.141592653589793 * freq / double(kRate);
    for (int k = 0; k < kSymbol; ++k, ++n) {
      phi += dphi;
      if (phi > 6.283185307179586)
        phi -= 6.283185307179586;
      if (n > fadeAt)
        amp *= 0.98f;
      (*out)[static_cast<std::size_t>(origin + kDelay + n)] = float(amp * std::sin(phi));
    }
  }
}

double tonePower(const float* samples, int count, int toneHz, int tone) {
  const double freq = double(toneHz) + tone * kSpacing;
  const double w = 2.0 * 3.141592653589793 * freq / double(kRate);
  double re = 0.0;
  double im = 0.0;
  for (int n = 0; n < count; n += 8) {
    re += samples[n] * std::cos(w * n);
    im += samples[n] * std::sin(w * n);
  }
  return re * re + im * im;
}

// Excess of the chosen tone over the other seven, then referred to
// 2500 Hz the way FT8 and JS8Call report SNR. One bin is 6.25 Hz.
int frameSnrDb(const float* samples, int count, int offset, int toneHz) {
  if (!samples || offset < 0 || offset + kSymbols * kSymbol > count)
    return -30;
  double sig = 0.0;
  double noise = 0.0;
  for (int i = 0; i < kSymbols; ++i) {
    const float* symbol = samples + offset + i * kSymbol;
    int best = 0;
    double bestMag = -1.0;
    double rest = 0.0;
    for (int tone = 0; tone < kTones; ++tone) {
      const double mag = tonePower(symbol, kSymbol, toneHz, tone);
      rest += mag;
      if (mag > bestMag) {
        bestMag = mag;
        best = tone;
      }
    }
    sig += bestMag;
    noise += (rest - bestMag) / double(kTones - 1);
  }
  if (sig <= 0.0 || noise <= 1e-12)
    return 40;
  const double bin = 10.0 * std::log10(sig / noise);
  const double snr = bin - 10.0 * std::log10(2500.0 / kSpacing);
  return int(std::lround(std::clamp(snr, -40.0, 40.0)));
}

int bestTone(const float* samples, int count, int toneHz) {
  int best = 0;
  double bestMag = -1.0;
  for (int tone = 0; tone < kTones; ++tone) {
    const double freq = double(toneHz) + tone * kSpacing;
    const double w = 2.0 * 3.141592653589793 * freq / double(kRate);
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < count; n += 8) {
      re += samples[n] * std::cos(w * n);
      im += samples[n] * std::sin(w * n);
    }
    const double mag = re * re + im * im;
    if (mag > bestMag) {
      bestMag = mag;
      best = tone;
    }
  }
  return best;
}

bool costasAt(const float* samples, int count, int offset, int toneHz) {
  if (offset < 0 || offset + kSymbols * kSymbol > count)
    return false;
  int hits = 0;
  for (int block = 0; block < 3; ++block) {
    for (int i = 0; i < 7; ++i) {
      const int at = offset + (kCostasAt[block] + i) * kSymbol;
      if (bestTone(samples + at, kSymbol, toneHz) == kCostas[i])
        ++hits;
    }
  }
  return hits >= 18;
}

QString textAt(const float* samples, int count, int offset, int toneHz) {
  if (!costasAt(samples, count, offset, toneHz))
    return {};
  int tones[kSymbols];
  for (int i = 0; i < kSymbols; ++i)
    tones[i] = bestTone(samples + offset + i * kSymbol, kSymbol, toneHz);
  std::string bits;
  bits.reserve(87);
  for (int i = 0; i < 29; ++i) {
    const int word = tones[43 + i] & 7;
    bits.push_back((word & 4) ? '1' : '0');
    bits.push_back((word & 2) ? '1' : '0');
    bits.push_back((word & 1) ? '1' : '0');
  }
  if (bits.size() < 75)
    return {};
  std::array<uint8_t, 11> bytes{};
  for (int i = 0; i < 87; ++i) {
    if (bits[static_cast<std::size_t>(i)] == '1')
      bytes[static_cast<std::size_t>(i / 8)] =
          static_cast<uint8_t>(bytes[static_cast<std::size_t>(i / 8)] | (0x80 >> (i % 8)));
  }
  const uint16_t got = static_cast<uint16_t>(((bytes[9] & 0x1F) << 7) | (bytes[10] >> 1));
  bytes[9] = static_cast<uint8_t>(bytes[9] & 0xE0);
  bytes[10] = 0;
  if (got != crc12(bytes.data(), int(bytes.size())))
    return {};
  return unpack72(bits.substr(0, 72));
}

int findOffset(const float* samples, int count, int toneHz) {
  const int need = kSymbols * kSymbol;
  if (count < need)
    return -1;
  const int candidates[] = {kDelay, 0};
  for (int offset : candidates) {
    if (textAt(samples, count, offset, toneHz).isEmpty() && !costasAt(samples, count, offset, toneHz))
      continue;
    if (!textAt(samples, count, offset, toneHz).isEmpty())
      return offset;
  }
  const int last = count - need;
  for (int offset = 0; offset <= last; offset += kSymbol) {
    if (costasAt(samples, count, offset, toneHz))
      return offset;
  }
  return -1;
}

}  // namespace

bool js8Mode(const QString& mode) {
  return mode.compare(QLatin1String("JS8"), Qt::CaseInsensitive) == 0;
}

namespace {

struct Js8Calling {
  const char* name;
  qint64 dial;
  const char* place;
};

// Same table as the Windows user guide PDF and the HTML guide.
constexpr Js8Calling kSuggested[] = {
    {"160m", 1842000, "2 kHz above FT8"},  {"80m", 3578000, "5 kHz above FT8"},
    {"40m", 7078000, "4 kHz above FT8"},   {"30m", 10130000, "6 kHz below FT8"},
    {"20m", 14078000, "4 kHz above FT8"},  {"17m", 18104000, "4 kHz above FT8"},
    {"15m", 21078000, "4 kHz above FT8"},  {"12m", 24922000, "9 kHz above FT8"},
    {"10m", 28078000, "4 kHz above FT8"},  {"6m", 50318000, "5 kHz above FT8"},
};

constexpr int kSuggestedCount = int(sizeof(kSuggested) / sizeof(kSuggested[0]));

}  // namespace

int js8SuggestedBandCount() {
  return kSuggestedCount;
}

qint64 js8SuggestedDialHz(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= kSuggestedCount)
    return 0;
  return kSuggested[bandIndex].dial;
}

QString js8SuggestedBandName(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= kSuggestedCount)
    return {};
  return QString::fromLatin1(kSuggested[bandIndex].name);
}

QString js8SuggestedPlace(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= kSuggestedCount)
    return {};
  return QString::fromLatin1(kSuggested[bandIndex].place);
}

Js8Plan planJs8(const QString& text, int toneHz) {
  Js8Plan plan;
  const int tone = std::clamp(toneHz, 200, 3000);
  QString left = text.toUpper();
  bool first = true;
  while (!left.isEmpty()) {
    int used = 0;
    const std::string bits = pack72(left, &used);
    if (used < 1 || bits.size() != 72)
      break;
    const QString symbols = symbols12(bits);
    const bool last = used >= left.size();
    int type = 0;
    if (first)
      type |= 1;
    if (last)
      type |= 2;
    int tones[kSymbols];
    if (!encodeFrame(symbols, type, tones))
      break;
    if (first)
      plan.echoAt.assign(static_cast<std::size_t>(text.size()), -1);
    const int at = int(plan.samples.size()) + kDelay;
    for (int i = 0; i < used && i < text.size(); ++i) {
      const int index = text.size() - left.size() + i;
      if (index >= 0 && index < text.size())
        plan.echoAt[static_cast<std::size_t>(index)] = at;
    }
    writeFrame(tones, tone, &plan.samples);
    left = left.mid(used);
    first = false;
  }
  return plan;
}

QString js8Decode(const float* samples, int count, int sampleRateHz, int toneHz, int* snrDb) {
  if (snrDb)
    *snrDb = 0;
  if (!samples || count < kSymbols * kSymbol || sampleRateHz != kRate)
    return {};
  const int tone = std::clamp(toneHz, 200, 3000);
  QString text;
  int offset = 0;
  bool haveSnr = false;
  while (offset + kSymbols * kSymbol <= count) {
    const int found = findOffset(samples + offset, count - offset, tone);
    if (found < 0)
      break;
    const QString part = textAt(samples + offset, count - offset, found, tone);
    if (part.isEmpty())
      break;
    if (!haveSnr && snrDb) {
      *snrDb = frameSnrDb(samples + offset, count - offset, found, tone);
      haveSnr = true;
    }
    text.append(part);
    offset += found + kSymbols * kSymbol;
  }
  return text;
}

Js8Engine::Js8Engine(QObject* parent) : QObject(parent) {}

void Js8Engine::setArmed(bool armed) {
  armed_ = armed;
  if (!armed_)
    clear();
}

void Js8Engine::setToneHz(int hz) {
  toneHz_ = std::clamp(hz, 200, 3000);
}

void Js8Engine::clear() {
  pending_.clear();
  last_.clear();
}

void Js8Engine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!armed_ || !samples || count < 1 || sampleRateHz < 1000)
    return;
  if (sampleRateHz != rate_) {
    pending_.clear();
    rate_ = sampleRateHz;
  }
  pending_.insert(pending_.end(), samples, samples + count);
  const int need = 24000 + 79 * 7680;
  if (int(pending_.size()) < need || rate_ != kRate)
    return;
  int snr = 0;
  const QString text = js8Decode(pending_.data(), int(pending_.size()), kRate, toneHz_, &snr);
  if (text.isEmpty()) {
    if (int(pending_.size()) > need + kRate)
      pending_.erase(pending_.begin(), pending_.begin() + kRate);
    return;
  }
  if (text != last_) {
    last_ = text;
    emit textReady(text, snr);
  }
  pending_.clear();
}
