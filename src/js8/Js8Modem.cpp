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
#include <complex>
#include <cstdint>
#include <cstring>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QRegularExpression>
#include <QThreadPool>

namespace {

constexpr int kRate = 48000;
constexpr int kSymbol = 7680;
constexpr int kDelay = 24000;
constexpr int kSymbols = 79;
// Receive runs at 12 kHz, the JS8Call rate: 1920 samples a symbol.
constexpr int kRx = 12000;
constexpr int kRxSym = 1920;
constexpr int kRxDelay = 6000;
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

// boost::augmented_crc<12, 0xc06>, then xor 42, as JS8Call and
// JS8Call-improved CRC12() do. "Augmented" means the buffer's own trailing
// zero bits are the augmentation: each bit shifts in at the bottom of the
// register and nothing is appended. Over the 11-byte frame that is the CRC
// of the first 76 bits (75 message bits and a zero).
uint16_t crc12(const uint8_t* data, int count) {
  uint16_t rem = 0;
  for (int i = 0; i < count; ++i) {
    for (int b = 7; b >= 0; --b) {
      const bool top = (rem & 0x800) != 0;
      rem = static_cast<uint16_t>(((rem << 1) | ((data[i] >> b) & 1)) & 0x0FFF);
      if (top)
        rem = static_cast<uint16_t>(rem ^ 0x0C06);
    }
  }
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

// ---- Receive side of the JS8Call message formats ----------------------
// From JS8Call-improved JS8_Main/Varicode.cpp (unpack*Message, unpackCallsign,
// unpackAlphaNumeric50, unpackGrid, unpackCmd), JS8_JSC/JSC.cpp decompress()
// and JS8_Mode/DecodedText.cpp, which picks the frame type and builds the
// text. The word table is JS8_JSC/JSC_map.cpp, stored in resources/js8.

constexpr char kAlphaNumeric[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@";
constexpr uint32_t kBaseCall = 37u * 36 * 10 * 27 * 27 * 27;
constexpr int kBaseGrid = 180 * 180;
constexpr int kUserGrid = kBaseGrid + 10;
constexpr int kMaxGrid = (1 << 15) - 1;

// basecalls in Varicode.cpp: name n is kBaseCall + 1 + n.
const char* const kGroups[] = {
    "<....>",     "@ALLCALL",  "@JS8NET",   "@DX/NA",    "@DX/SA",    "@DX/EU",    "@DX/AS",
    "@DX/AF",     "@DX/OC",    "@DX/AN",    "@REGION/1", "@REGION/2", "@REGION/3", "@GROUP/0",
    "@GROUP/1",   "@GROUP/2",  "@GROUP/3",  "@GROUP/4",  "@GROUP/5",  "@GROUP/6",  "@GROUP/7",
    "@GROUP/8",   "@GROUP/9",  "@COMMAND",  "@CONTROL",  "@NET",      "@NTS",      "@RESERVE/0",
    "@RESERVE/1", "@RESERVE/2", "@RESERVE/3", "@RESERVE/4", "@APRSIS",  "@RAGCHEW",  "@JS8",
    "@EMCOMM",    "@ARES",     "@MARS",     "@AMRRON",   "@RACES",    "@RAYNET",   "@RADAR",
    "@SKYWARN",   "@CQ",       "@HB",       "@QSO",      "@QSOPARTY", "@CONTEST",  "@FIELDDAY",
    "@SOTA",      "@IOTA",     "@POTA",     "@QRP",      "@QRO",
};

// directed_cmds.key(n): the first key in QMap order with value n.
const char* const kCommands[32] = {
    " SNR?",  " DIT DIT", " NACK", " HEARING?", " GRID?", ">",     " STATUS?",  " STATUS",
    " HEARING", " MSG",   " MSG TO:", " QUERY", " QUERY MSGS", " QUERY CALL", " ACK", " GRID",
    " INFO?", " INFO",    " FB",   " HW CPY?", " SK",    " RR",    " QSL?",     " QSL",
    " CMD",   " SNR",     " NO",   " YES",     " 73",    " HEARTBEAT SNR", " AGN?", " ",
};

const char* const kCqs[8] = {"CQ CQ CQ", "CQ DX", "CQ QRP", "CQ CONTEST", "CQ FIELD", "CQ FD", "CQ CQ", "CQ"};

bool snrCommand(int cmd) {
  return cmd == 25 || cmd == 29;
}

uint64_t bitsValue(const std::string& bits, std::size_t from, int count) {
  uint64_t value = 0;
  for (int i = 0; i < count; ++i)
    value = (value << 1) | (bits[from + static_cast<std::size_t>(i)] == '1' ? 1u : 0u);
  return value;
}

struct JscWords {
  QByteArray data;
  std::vector<int> offset;
};

constexpr int kJscSize = 262144;

const JscWords& jscWords() {
  static const JscWords table = [] {
    JscWords words;
    QFile file(QStringLiteral(":/std/js8/jsc_words.bin"));
    if (file.open(QIODevice::ReadOnly))
      words.data = file.readAll();
    int start = 0;
    for (int i = 0; i < words.data.size() && int(words.offset.size()) < kJscSize; ++i) {
      if (words.data.at(i) == '\0') {
        words.offset.push_back(start);
        start = i + 1;
      }
    }
    if (int(words.offset.size()) != kJscSize)
      words.offset.clear();
    return words;
  }();
  return table;
}

// JSC::decompress(): 4-bit nibbles, (s,c)-dense with s = 7, c = 9. A
// nibble below 7 ends a word and is followed by a separator bit.
QString jscDecompress(const std::string& bits) {
  const JscWords& words = jscWords();
  if (words.offset.empty())
    return {};
  constexpr uint32_t s = 7;
  constexpr uint32_t c = 9;
  uint32_t base[8];
  base[0] = 0;
  uint32_t step = s;
  for (int k = 1; k < 8; ++k) {
    base[k] = base[k - 1] + step;
    step *= c;
  }
  std::vector<uint32_t> nibbles;
  std::vector<std::size_t> separators;
  std::size_t i = 0;
  while (i + 4 <= bits.size()) {
    const uint32_t nibble = uint32_t(bitsValue(bits, i, 4));
    nibbles.push_back(nibble);
    i += 4;
    if (nibble < s) {
      if (i < bits.size() && bits[i] == '1')
        separators.push_back(nibbles.size() - 1);
      i += 1;
    }
  }
  QString text;
  std::size_t sep = 0;
  std::size_t start = 0;
  while (start < nibbles.size()) {
    std::size_t k = 0;
    uint64_t j = 0;
    while (start + k < nibbles.size() && nibbles[start + k] >= s) {
      j = j * c + (nibbles[start + k] - s);
      ++k;
    }
    if (j >= uint64_t(kJscSize) || start + k >= nibbles.size() || k >= 8)
      break;
    j = j * s + nibbles[start + k] + base[k];
    if (j >= uint64_t(kJscSize))
      break;
    text.append(QString::fromLatin1(words.data.constData() + words.offset[static_cast<std::size_t>(j)]));
    if (sep < separators.size() && separators[sep] == start + k) {
      text.append(QLatin1Char(' '));
      ++sep;
    }
    start += k + 1;
  }
  return text;
}

QString unpackCallsign(uint32_t value, bool portable) {
  if (value > kBaseCall) {
    const uint32_t group = value - kBaseCall - 1;
    if (group < sizeof(kGroups) / sizeof(kGroups[0]))
      return QString::fromLatin1(kGroups[group]);
  }
  char word[7] = {};
  for (int i = 5; i >= 3; --i) {
    word[i] = kAlphaNumeric[value % 27 + 10];
    value /= 27;
  }
  word[2] = kAlphaNumeric[value % 10];
  value /= 10;
  word[1] = kAlphaNumeric[value % 36];
  value /= 36;
  if (value > 38)
    return {};
  word[0] = kAlphaNumeric[value];
  QString call = QString::fromLatin1(word);
  if (call.startsWith(QLatin1String("3D0")))
    call = QLatin1String("3DA0") + call.mid(3);
  if (call.startsWith(QLatin1Char('Q')) && call.at(1) >= QLatin1Char('A') && call.at(1) <= QLatin1Char('Z'))
    call = QLatin1String("3X") + call.mid(1);
  if (portable)
    call = call.trimmed() + QLatin1String("/P");
  return call.trimmed();
}

// [39][38][38][2][38][38][38][2][38][38][38], spaces dropped.
QString unpackAlphaNumeric50(uint64_t packed) {
  char word[12] = {};
  for (int i = 10; i >= 8; --i) {
    word[i] = kAlphaNumeric[packed % 38];
    packed /= 38;
  }
  word[7] = packed % 2 ? '/' : ' ';
  packed /= 2;
  for (int i = 6; i >= 4; --i) {
    word[i] = kAlphaNumeric[packed % 38];
    packed /= 38;
  }
  word[3] = packed % 2 ? '/' : ' ';
  packed /= 2;
  for (int i = 2; i >= 1; --i) {
    word[i] = kAlphaNumeric[packed % 38];
    packed /= 38;
  }
  word[0] = kAlphaNumeric[packed % 39];
  return QString::fromLatin1(word).remove(QLatin1Char(' '));
}

// unpackGrid() through deg2grid(), four characters.
QString unpackGrid(int value) {
  if (value > kBaseGrid)
    return {};
  const float dlat = float(value % 180 - 90);
  float dlong = float(value / 180 * 2 - 180 + 2);
  if (dlong > 180)
    dlong -= 360;
  const int nlong = int(60.0 * (180.0 - dlong) / 5);
  const int nlat = int(60.0 * (dlat + 90) / 2.5);
  const char grid[5] = {char('A' + nlong / 240), char('A' + nlat / 240),
                        char('0' + (nlong - 240 * (nlong / 240)) / 24),
                        char('0' + (nlat - 240 * (nlat / 240)) / 24), 0};
  return QString::fromLatin1(grid);
}

// ---- Packing: the inverses of the unpack functions above. Each result is
// checked by unpacking it again, so a call or grid that does not survive
// the round trip is refused instead of going out wrong.

int alphaIndex(QChar ch) {
  const char c = ch.toLatin1();
  if (!c)
    return -1;
  const char* at = std::strchr(kAlphaNumeric, c);
  return at ? int(at - kAlphaNumeric) : -1;
}

// 28-bit standard call or @group. Returns false when it does not fit.
bool packCallsign28(const QString& input, uint32_t* out, bool* portable) {
  QString call = input.trimmed().toUpper();
  *portable = false;
  if (call.startsWith(QLatin1Char('@'))) {
    for (std::size_t n = 1; n < sizeof(kGroups) / sizeof(kGroups[0]); ++n) {
      if (call == QLatin1String(kGroups[n])) {
        *out = kBaseCall + 1 + uint32_t(n);
        return true;
      }
    }
    return false;
  }
  if (call.endsWith(QLatin1String("/P"))) {
    *portable = true;
    call.chop(2);
  }
  const QString original = call;
  if (call.startsWith(QLatin1String("3DA0")))
    call = QLatin1String("3D0") + call.mid(4);
  else if (call.startsWith(QLatin1String("3X")) && call.size() > 2 && call.at(2).isLetter())
    call = QLatin1Char('Q') + call.mid(2);
  if (call.size() >= 3 && call.at(2).isDigit()) {
  } else if (call.size() >= 2 && call.at(1).isDigit()) {
    call.prepend(QLatin1Char(' '));
  } else {
    return false;
  }
  if (call.size() > 6)
    return false;
  while (call.size() < 6)
    call.append(QLatin1Char(' '));
  const int a = alphaIndex(call.at(0));
  const int b = alphaIndex(call.at(1));
  if (a < 0 || a > 36 || b < 0 || b > 35 || !call.at(2).isDigit())
    return false;
  uint32_t value = uint32_t(a);
  value = value * 36 + uint32_t(b);
  value = value * 10 + uint32_t(call.at(2).digitValue());
  for (int i = 3; i < 6; ++i) {
    const int c = alphaIndex(call.at(i));
    if (c < 10 || c > 36)
      return false;
    value = value * 27 + uint32_t(c - 10);
  }
  if (unpackCallsign(value, false) != original)
    return false;
  *out = value;
  return true;
}

// Varicode::packAlphaNumeric50: [39][38][38][2][38][38][38][2][38][38][38].
bool packAlphaNumeric50(const QString& input, uint64_t* out) {
  QString word = input.trimmed().toUpper();
  if (word.isEmpty() || word.size() > 11)
    return false;
  if (word.size() > 3 && word.at(3) != QLatin1Char('/'))
    word.insert(3, QLatin1Char(' '));
  if (word.size() > 7 && word.at(7) != QLatin1Char('/'))
    word.insert(7, QLatin1Char(' '));
  if (word.size() > 11)
    return false;
  while (word.size() < 11)
    word.append(QLatin1Char(' '));
  uint64_t value = 0;
  for (int i = 0; i < 11; ++i) {
    const QChar ch = word.at(i);
    if (i == 3 || i == 7) {
      if (ch != QLatin1Char(' ') && ch != QLatin1Char('/'))
        return false;
      value = value * 2 + (ch == QLatin1Char('/') ? 1u : 0u);
      continue;
    }
    const int index = alphaIndex(ch);
    const int radix = i == 0 ? 39 : 38;
    if (index < 0 || index >= radix)
      return false;
    value = value * uint64_t(radix) + uint64_t(index);
  }
  if (unpackAlphaNumeric50(value) != input.trimmed().toUpper())
    return false;
  *out = value;
  return true;
}

// Four-character grid to its JS8 number, found through unpackGrid itself.
int packGrid(const QString& input) {
  static const QHash<QString, int> table = [] {
    QHash<QString, int> map;
    for (int v = 0; v <= kBaseGrid; ++v) {
      const QString grid = unpackGrid(v);
      if (!grid.isEmpty() && !map.contains(grid))
        map.insert(grid, v);
    }
    return map;
  }();
  return table.value(input.trimmed().toUpper().left(4), -1);
}

void appendBits(std::string* bits, uint64_t value, int count) {
  for (int i = count - 1; i >= 0; --i)
    bits->push_back((value >> i) & 1u ? '1' : '0');
}

QString formatSnr(int snr) {
  if (snr < -60 || snr > 60)
    return {};
  return QString::asprintf("%c%02d", snr >= 0 ? '+' : '-', std::abs(snr));
}

// Heartbeat (000), compound (001) and compound directed (010):
// [3 type][50 callsign][11 num high], then [5 num low][3 bits3].
QString unpackCompound(const std::string& bits72, int type) {
  const QString call = unpackAlphaNumeric50(bitsValue(bits72, 3, 50));
  const int num = int((bitsValue(bits72, 53, 11) << 5) | bitsValue(bits72, 64, 5));
  const int bits3 = int(bitsValue(bits72, 69, 3));
  if (type == 0) {
    const QString grid = unpackGrid(num & kMaxGrid);
    if (num & (1 << 15))
      return call + QLatin1String(": @ALLCALL ") + QLatin1String(kCqs[bits3]) + QLatin1Char(' ') + grid +
             QLatin1Char(' ');
    return call + QLatin1String(": @HB HEARTBEAT ") + grid + QLatin1Char(' ');
  }
  if (type == 1)
    return call + QLatin1String(": ");
  QStringList extra;
  if (num <= kBaseGrid) {
    extra.append(QLatin1Char(' ') + unpackGrid(num));
  } else if (num >= kUserGrid && num < kMaxGrid) {
    const int value = (num - kUserGrid) & 0xFF;
    const int cmd = value & 0x80 ? (value & 0x40 ? 29 : 25) : value & 0x7F;
    extra.append(cmd < 32 ? QLatin1String(kCommands[cmd]) : QString());
    if (snrCommand(cmd))
      extra.append(formatSnr((value & 0x3F) - 31));
  }
  return call + extra.join(QLatin1Char(' ')) + QLatin1Char(' ');
}

// Directed (011): [3 type][28 from][28 to][5 cmd], then
// [1 from /P][1 to /P][6 num].
QString unpackDirected(const std::string& bits72) {
  const int extra = int(bitsValue(bits72, 64, 8));
  const QString from = unpackCallsign(uint32_t(bitsValue(bits72, 3, 28)), (extra & 0x80) != 0);
  const QString to = unpackCallsign(uint32_t(bitsValue(bits72, 31, 28)), (extra & 0x40) != 0);
  if (from.isEmpty() || to.isEmpty())
    return {};
  const int cmd = int(bitsValue(bits72, 59, 5));
  QString text = from + QLatin1String(": ") + to + QLatin1String(kCommands[cmd]);
  const int num = extra % 64;
  if (num != 0)
    text += QLatin1Char(' ') + (snrCommand(cmd) ? formatSnr(num - 31) : QString::number(num - 31));
  return text + QLatin1Char(' ');
}

// DecodedText's unpack order: fast data, data, heartbeat, compound,
// directed. flags are the frame's 3 transmission bits (4 = JS8CallData).
QString unpackFrame(const std::string& bits72, int flags) {
  if (bits72.size() < 72)
    return {};
  if (flags & 4) {
    const std::size_t pad = bits72.rfind('0');
    if (pad == std::string::npos)
      return {};
    return jscDecompress(bits72.substr(0, pad));
  }
  if (bits72[0] == '1') {
    // Drop the data flag and the compression flag. The pad zero and the
    // ones after it are not payload.
    const std::size_t pad = bits72.rfind('0');
    if (pad == std::string::npos || pad < 2)
      return {};
    const std::string payload = bits72.substr(2, pad - 2);
    return bits72[1] == '1' ? jscDecompress(payload) : huffDecode(payload);
  }
  const int type = int(bitsValue(bits72, 0, 3));
  if (type <= 2)
    return unpackCompound(bits72, type);
  return unpackDirected(bits72);
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

// 48 kHz to 12 kHz. Blackman-windowed sinc, 4.5 kHz cut-off, centred so
// output sample i lines up with input sample 4 i. Reading every 8th or
// 32nd sample of the 48 kHz audio folded tones 1.5 or 6 kHz apart onto
// each other; filtering first keeps each JS8 tone in its own bin.
std::vector<float> to12k(const float* samples, int count) {
  constexpr int factor = kRate / kRx;
  constexpr int taps = 16 * factor + 1;
  constexpr int half = taps / 2;
  static const std::array<double, taps> coeff = [] {
    std::array<double, taps> c{};
    constexpr double pi = 3.141592653589793;
    const double fc = 4500.0 / double(kRate);
    double sum = 0.0;
    for (int k = 0; k < taps; ++k) {
      const double u = double(k - half);
      const double s = u == 0.0 ? 2.0 * fc : std::sin(2.0 * pi * fc * u) / (pi * u);
      const double w = 0.42 - 0.5 * std::cos(2.0 * pi * k / (taps - 1)) + 0.08 * std::cos(4.0 * pi * k / (taps - 1));
      c[static_cast<std::size_t>(k)] = s * w;
      sum += s * w;
    }
    for (double& v : c)
      v /= sum;
    return c;
  }();
  std::vector<float> out;
  if (!samples || count < factor)
    return out;
  out.resize(static_cast<std::size_t>(count / factor));
  for (int i = 0; i < int(out.size()); ++i) {
    const int centre = i * factor;
    double acc = 0.0;
    for (int k = 0; k < taps; ++k) {
      const int at = centre + k - half;
      if (at >= 0 && at < count)
        acc += coeff[static_cast<std::size_t>(k)] * samples[at];
    }
    out[static_cast<std::size_t>(i)] = float(acc);
  }
  return out;
}

// Goertzel power of one JS8 tone over count samples at 12 kHz.
double tonePowerHz(const float* samples, int count, double baseHz, int tone) {
  if (!samples || count < 8)
    return 0.0;
  const double freq = baseHz + tone * kSpacing;
  const double coeff = 2.0 * std::cos(2.0 * 3.141592653589793 * freq / double(kRx));
  double s1 = 0.0;
  double s2 = 0.0;
  for (int n = 0; n < count; ++n) {
    const double s0 = samples[n] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

double tonePower(const float* samples, int count, int toneHz, int tone) {
  return tonePowerHz(samples, count, double(toneHz), tone);
}

// Excess of the chosen tone over the other seven, then referred to
// 2500 Hz the way FT8 and JS8Call report SNR. One bin is 6.25 Hz.
int frameSnrDb(const float* samples, int count, int offset, int toneHz) {
  if (!samples || offset < 0 || offset + kSymbols * kRxSym > count)
    return -30;
  double sig = 0.0;
  double noise = 0.0;
  for (int i = 0; i < kSymbols; ++i) {
    const float* symbol = samples + offset + i * kRxSym;
    int best = 0;
    double bestMag = -1.0;
    double rest = 0.0;
    for (int tone = 0; tone < kTones; ++tone) {
      const double mag = tonePower(symbol, kRxSym, toneHz, tone);
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

struct LdpcGraph {
  std::vector<std::vector<int>> checks;
  std::vector<std::vector<int>> vars;
};

const LdpcGraph& ldpcGraph() {
  static LdpcGraph graph;
  if (!graph.checks.empty())
    return graph;
  graph.checks.resize(87);
  graph.vars.resize(174);
  for (int row = 0; row < 87; ++row) {
    for (int col = 0; col < 87; ++col) {
      if (!parityBit(row, col))
        continue;
      graph.checks[static_cast<std::size_t>(row)].push_back(col);
      graph.vars[static_cast<std::size_t>(col)].push_back(row);
    }
    graph.checks[static_cast<std::size_t>(row)].push_back(87 + row);
    graph.vars[static_cast<std::size_t>(87 + row)].push_back(row);
  }
  return graph;
}

// The 3 transmission bits of the frame textFromBits last accepted on this
// thread (1 first, 2 last, 4 data). Decodes run on pool threads, so each
// keeps its own. collectFrame reads it right after a frame prints.
thread_local int tlFrameFlags = 0;

QString textFromBits(const int* bits) {
  if (!bits)
    return {};
  std::array<uint8_t, 11> bytes{};
  std::string textBits;
  textBits.reserve(87);
  for (int i = 0; i < 87; ++i) {
    const bool one = bits[i] != 0;
    textBits.push_back(one ? '1' : '0');
    if (one) {
      bytes[static_cast<std::size_t>(i / 8)] = static_cast<uint8_t>(
          bytes[static_cast<std::size_t>(i / 8)] | (0x80 >> (i % 8)));
    }
  }
  const uint16_t got = static_cast<uint16_t>(((bytes[9] & 0x1F) << 7) | (bytes[10] >> 1));
  bytes[9] = static_cast<uint8_t>(bytes[9] & 0xE0);
  bytes[10] = 0;
  if (got != crc12(bytes.data(), int(bytes.size())))
    return {};
  const int flags = (bits[72] ? 4 : 0) | (bits[73] ? 2 : 0) | (bits[74] ? 1 : 0);
  const QString text = unpackFrame(textBits.substr(0, 72), flags);
  if (!text.isEmpty())
    tlFrameFlags = flags;
  return text;
}

bool ldpcBelief(const double* channel, int* bits) {
  const LdpcGraph& graph = ldpcGraph();
  std::vector<double> message(static_cast<std::size_t>(87 * 174), 0.0);
  auto at = [&](int check, int var) -> double& {
    return message[static_cast<std::size_t>(check * 174 + var)];
  };
  for (int iter = 0; iter < 25; ++iter) {
    for (int check = 0; check < 87; ++check) {
      const std::vector<int>& neighbors = graph.checks[static_cast<std::size_t>(check)];
      std::vector<double> inbound(neighbors.size(), 0.0);
      for (std::size_t n = 0; n < neighbors.size(); ++n) {
        const int var = neighbors[n];
        double sum = channel[var];
        for (int other : graph.vars[static_cast<std::size_t>(var)]) {
          if (other != check)
            sum += at(other, var);
        }
        inbound[n] = sum;
      }
      for (std::size_t n = 0; n < neighbors.size(); ++n) {
        double mag = 1e9;
        int sign = 1;
        for (std::size_t other = 0; other < neighbors.size(); ++other) {
          if (other == n)
            continue;
          const double value = inbound[other];
          if (value < 0.0)
            sign = -sign;
          mag = std::min(mag, std::fabs(value));
        }
        if (mag > 1e8)
          mag = 0.0;
        at(check, neighbors[n]) = 0.75 * double(sign) * mag;
      }
    }
    for (int var = 0; var < 87; ++var) {
      double sum = channel[var];
      for (int check : graph.vars[static_cast<std::size_t>(var)])
        sum += at(check, var);
      bits[var] = sum < 0.0 ? 1 : 0;
    }
    if (!textFromBits(bits).isEmpty())
      return true;
  }
  return !textFromBits(bits).isEmpty();
}

void symbolPowers(const float* symbol, double baseHz, double* power) {
  for (int tone = 0; tone < kTones; ++tone)
    power[tone] = tonePowerHz(symbol, kRxSym, baseHz, tone);
}

double syncScore(const float* samples, int count, int offset, double baseHz) {
  const int need = kSymbols * kRxSym;
  if (!samples || offset < 0 || offset + need > count)
    return 0.0;
  double score = 0.0;
  double power[kTones];
  for (int block = 0; block < 3; ++block) {
    for (int i = 0; i < 7; ++i) {
      symbolPowers(samples + offset + (kCostasAt[block] + i) * kRxSym, baseHz, power);
      double total = 0.0;
      for (int tone = 0; tone < kTones; ++tone)
        total += power[tone];
      score += power[kCostas[i]] / (total + 1e-12);
    }
  }
  return score;
}

QString decodeAt(const float* samples, int count, int offset, double baseHz) {
  const int need = kSymbols * kRxSym;
  if (!samples || offset < 0 || offset + need > count)
    return {};
  double channel[174];
  for (int i = 0; i < 174; ++i)
    channel[i] = 0.0;
  auto llr = [](const double* power, int bit) {
    const int mask = 4 >> bit;
    double zero = 1e-12;
    double one = 1e-12;
    for (int tone = 0; tone < kTones; ++tone) {
      if (tone & mask)
        one += power[tone];
      else
        zero += power[tone];
    }
    return std::clamp(std::log(zero) - std::log(one), -10.0, 10.0);
  };
  for (int symbol = 0; symbol < 29; ++symbol) {
    double data[kTones];
    double parity[kTones];
    symbolPowers(samples + offset + (43 + symbol) * kRxSym, baseHz, data);
    symbolPowers(samples + offset + (7 + symbol) * kRxSym, baseHz, parity);
    for (int bit = 0; bit < 3; ++bit) {
      channel[symbol * 3 + bit] = llr(data, bit);
      channel[87 + symbol * 3 + bit] = llr(parity, bit);
    }
  }
  int bits[87];
  for (int i = 0; i < 87; ++i)
    bits[i] = channel[i] < 0.0 ? 1 : 0;
  const QString hard = textFromBits(bits);
  if (!hard.isEmpty())
    return hard;
  if (syncScore(samples, count, offset, baseHz) < 8.0)
    return {};
  if (!ldpcBelief(channel, bits))
    return {};
  return textFromBits(bits);
}

int findBestTime(const float* samples, int count, double baseHz, int prefer) {
  const int need = kSymbols * kRxSym;
  if (count < need)
    return -1;
  const int last = count - need;
  const int from = std::max(0, prefer - 2 * kRx);
  const int to = std::min(last, prefer + 2 * kRx);
  const int step = std::max(1, kRxSym / 4);
  int best = -1;
  double bestScore = 8.0;
  for (int offset = from; offset <= to; offset += step) {
    const double score = syncScore(samples, count, offset, baseHz);
    if (score > bestScore) {
      bestScore = score;
      best = offset;
    }
  }
  if (best < 0)
    return -1;
  const int fine = std::max(1, step / 4);
  const int fineFrom = std::max(0, best - step);
  const int fineTo = std::min(last, best + step);
  for (int offset = fineFrom; offset <= fineTo; offset += fine) {
    const double score = syncScore(samples, count, offset, baseHz);
    if (score > bestScore) {
      bestScore = score;
      best = offset;
    }
  }
  return best;
}

struct Js8Hit {
  int freq = 0;
  QString text;
  int snr = 0;
  int time = 0;
  int flags = 0;
};

// Cursor search: the frame at the cursor gets the wide dt and df search.
QString decodeNear(const float* samples, int count, int offset, double baseHz, int* snr, int* timeOut) {
  QString text = decodeAt(samples, count, offset, baseHz);
  int bestOffset = offset;
  double bestHz = baseHz;
  if (text.isEmpty()) {
    double bestScore = 8.0;
    const int half = kRxSym / 2;
    const int fine = std::max(1, kRxSym / 8);
    for (int df = -8; df <= 8; ++df) {
      for (int dt = -half; dt <= half; dt += fine) {
        const int at = offset + dt;
        const double score = syncScore(samples, count, at, baseHz + df);
        if (score > bestScore) {
          bestScore = score;
          bestOffset = at;
          bestHz = baseHz + df;
        }
      }
    }
    if (bestScore <= 8.0)
      return {};
    text = decodeAt(samples, count, bestOffset, bestHz);
  }
  if (text.isEmpty())
    return {};
  if (snr)
    *snr = frameSnrDb(samples, count, bestOffset, int(std::lround(bestHz)));
  if (timeOut)
    *timeOut = bestOffset;
  return text;
}

// Passband scan. A quarter-symbol DFT at every half bin, phase referenced
// to the start of the slice, so four in a row add up to one symbol and the
// Costas sync can be scored at any quarter-symbol start. Each station gets
// its own best dt: JS8Call stations are routinely half a second apart.
constexpr int kQuarter = kRxSym / 4;
constexpr double kScanStepHz = kSpacing / 2.0;
constexpr double kScanLowHz = 400.0;
constexpr double kScanHighHz = 2650.0;
constexpr double kScanSync = 7.0;
constexpr int kScanMax = 30;

struct ScanCandidate {
  double hz = 0.0;
  int time = 0;
  double score = 0.0;
};

std::vector<ScanCandidate> scanPassband(const float* samples, int count, int prefer) {
  std::vector<ScanCandidate> found;
  const int chunks = count / kQuarter;
  const int bins = int((kScanHighHz - kScanLowHz) / kScanStepHz) + 1;
  const int frameSteps = kSymbols * 4;
  if (chunks < frameSteps + 4 || bins < 16)
    return found;
  constexpr double pi = 3.141592653589793;
  // q[c * bins + b]: complex sum over quarter c at bin b.
  std::vector<std::complex<float>> q(static_cast<std::size_t>(chunks) * bins);
  for (int b = 0; b < bins; ++b) {
    const double w = 2.0 * pi * (kScanLowHz + b * kScanStepHz) / double(kRx);
    const std::complex<double> rot(std::cos(w), -std::sin(w));
    std::complex<double> phasor(1.0, 0.0);
    for (int c = 0; c < chunks; ++c) {
      std::complex<double> acc(0.0, 0.0);
      const float* x = samples + static_cast<std::size_t>(c) * kQuarter;
      for (int n = 0; n < kQuarter; ++n) {
        acc += double(x[n]) * phasor;
        phasor *= rot;
      }
      phasor /= std::abs(phasor);
      q[static_cast<std::size_t>(c) * bins + b] = std::complex<float>(acc);
    }
  }
  const int steps = chunks - 3;
  std::vector<float> power(static_cast<std::size_t>(steps) * bins);
  for (int t = 0; t < steps; ++t) {
    for (int b = 0; b < bins; ++b) {
      std::complex<float> sum = 0.0f;
      for (int k = 0; k < 4; ++k)
        sum += q[static_cast<std::size_t>(t + k) * bins + b];
      power[static_cast<std::size_t>(t) * bins + b] = std::norm(sum);
    }
  }
  const int span = 2 * kRx / kQuarter;
  const int center = prefer / kQuarter;
  const int tFrom = std::max(0, center - span);
  // The last symbol is quarters t + 4 * 78 .. + 3, which power row
  // t + 4 * 78 already sums.
  const int tTo = std::min(steps - 1 - 4 * (kSymbols - 1), center + span);
  const int binsPerTone = int(std::lround(kSpacing / kScanStepHz));
  const int top = bins - binsPerTone * (kTones - 1);
  if (top < 1 || tTo < tFrom)
    return found;
  std::vector<double> bestScore(static_cast<std::size_t>(top), 0.0);
  std::vector<int> bestTime(static_cast<std::size_t>(top), 0);
  for (int b = 0; b < top; ++b) {
    for (int t = tFrom; t <= tTo; ++t) {
      double score = 0.0;
      for (int block = 0; block < 3; ++block) {
        for (int i = 0; i < 7; ++i) {
          const float* row =
              power.data() + static_cast<std::size_t>(t + 4 * (kCostasAt[block] + i)) * bins + b;
          double total = 1e-12;
          for (int tone = 0; tone < kTones; ++tone)
            total += row[tone * binsPerTone];
          score += row[kCostas[i] * binsPerTone] / total;
        }
      }
      if (score > bestScore[static_cast<std::size_t>(b)]) {
        bestScore[static_cast<std::size_t>(b)] = score;
        bestTime[static_cast<std::size_t>(b)] = t;
      }
    }
  }
  for (int b = 0; b < top; ++b) {
    const double score = bestScore[static_cast<std::size_t>(b)];
    if (score < kScanSync)
      continue;
    bool peak = true;
    for (int n = std::max(0, b - 3); n <= std::min(top - 1, b + 3) && peak; ++n) {
      const double other = bestScore[static_cast<std::size_t>(n)];
      if (other > score || (other == score && n < b))
        peak = false;
    }
    if (peak)
      found.push_back(
          ScanCandidate{kScanLowHz + b * kScanStepHz, bestTime[static_cast<std::size_t>(b)] * kQuarter, score});
  }
  std::sort(found.begin(), found.end(),
            [](const ScanCandidate& a, const ScanCandidate& b) { return a.score > b.score; });
  if (int(found.size()) > kScanMax)
    found.resize(static_cast<std::size_t>(kScanMax));
  return found;
}

// A scan candidate sits on a quarter-symbol and half-bin grid. Try it
// there, then on a finer grid around it.
QString decodeCandidate(const float* samples, int count, const ScanCandidate& cand, double* hzOut,
                        int* timeOut) {
  QString text = decodeAt(samples, count, cand.time, cand.hz);
  double bestHz = cand.hz;
  int bestTime = cand.time;
  if (text.isEmpty()) {
    double bestScore = 0.0;
    const int fine = kRxSym / 16;
    for (int dt = -2 * fine; dt <= 2 * fine; dt += fine) {
      for (int k = -2; k <= 2; ++k) {
        const double hz = cand.hz + k * kScanStepHz / 2.0;
        const double score = syncScore(samples, count, cand.time + dt, hz);
        if (score > bestScore) {
          bestScore = score;
          bestHz = hz;
          bestTime = cand.time + dt;
        }
      }
    }
    text = decodeAt(samples, count, bestTime, bestHz);
  }
  *hzOut = bestHz;
  *timeOut = bestTime;
  return text;
}

// samples are 12 kHz. prefer is where a frame on time would start.
void collectFrame(const float* samples, int count, int toneHz, int prefer, int* timeUsed,
                  std::vector<Js8Hit>* hits, int* snrOut) {
  if (timeUsed)
    *timeUsed = 0;
  if (!hits)
    return;
  hits->clear();
  const int need = kSymbols * kRxSym;
  if (!samples || count < need)
    return;
  if (prefer < 0 || prefer + need > count)
    prefer = kRxDelay + need <= count ? kRxDelay : 0;
  int cursorTime = prefer;
  int cursorSnr = 0;
  QString cursorText = decodeNear(samples, count, prefer, double(toneHz), &cursorSnr, &cursorTime);
  if (cursorText.isEmpty()) {
    const int searched = findBestTime(samples, count, double(toneHz), prefer);
    if (searched >= 0)
      cursorText = decodeNear(samples, count, searched, double(toneHz), &cursorSnr, &cursorTime);
  }
  auto taken = [&](double hz) {
    for (const Js8Hit& hit : *hits) {
      if (std::fabs(double(hit.freq) - hz) < 6.0)
        return true;
    }
    return false;
  };
  const int cursorFlags = tlFrameFlags;
  if (!cursorText.isEmpty())
    hits->push_back(Js8Hit{toneHz, cursorText, cursorSnr, cursorTime, cursorFlags});
  for (const ScanCandidate& cand : scanPassband(samples, count, prefer)) {
    if (taken(cand.hz))
      continue;
    double hz = cand.hz;
    int time = cand.time;
    const QString text = decodeCandidate(samples, count, cand, &hz, &time);
    const int flags = tlFrameFlags;
    if (text.isEmpty() || taken(hz))
      continue;
    const int found = int(std::lround(hz));
    hits->push_back(Js8Hit{found, text, frameSnrDb(samples, count, time, found), time, flags});
  }
  if (timeUsed) {
    // js8Decode steps to the next frame from here: the cursor frame when
    // it printed, else the strongest station, else the on-time start.
    *timeUsed = !cursorText.isEmpty() ? cursorTime : (!hits->empty() ? hits->front().time : prefer);
  }
  std::sort(hits->begin(), hits->end(), [](const Js8Hit& a, const Js8Hit& b) { return a.freq < b.freq; });
  if (snrOut && !hits->empty()) {
    *snrOut = hits->front().snr;
    for (const Js8Hit& hit : *hits) {
      if (std::abs(hit.freq - toneHz) <= 8) {
        *snrOut = hit.snr;
        break;
      }
    }
  }
}

static_assert(kDelay + kSymbols * kSymbol == 24000 + 79 * 7680, "JS8 frame length");

}  // namespace

int js8FrameSamples() {
  return 24000 + 79 * 7680;
}

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

namespace {

// The 72 bits of a heartbeat or CQ frame (000): [3 type][50 call][16 num][3 bits3].
std::string compoundFrame(uint64_t call50, int num, int bits3) {
  std::string bits;
  appendBits(&bits, 0, 3);
  appendBits(&bits, call50, 50);
  appendBits(&bits, uint64_t(num) >> 5, 11);
  appendBits(&bits, uint64_t(num) & 31u, 5);
  appendBits(&bits, uint64_t(bits3), 3);
  return bits;
}

int gridNumber(const QString& grid) {
  static const QRegularExpression shape(QStringLiteral("^[A-R]{2}[0-9]{2}$"));
  const QString g = grid.trimmed().toUpper().left(4);
  if (!shape.match(g).hasMatch())
    return kMaxGrid;
  const int n = packGrid(g);
  return n < 0 ? kMaxGrid : n;
}

}  // namespace

Js8Header js8Header(const QString& text, const QString& myCall, const QString& myGrid) {
  Js8Header head;
  const QString upper = text.toUpper();
  const QString me = myCall.trimmed().toUpper();
  if (me.isEmpty())
    return head;
  // Leading spaces belong to the header so the character count stays right.
  int lead = 0;
  while (lead < upper.size() && upper.at(lead) == QLatin1Char(' '))
    ++lead;
  const QString body = upper.mid(lead);
  const QString trimmed = body.trimmed();
  const QString myGrid4 = myGrid.trimmed().toUpper().left(4);

  // Heartbeat and CQ go out as one frame carrying the call and grid.
  static const QRegularExpression hb(
      QStringLiteral("^(?:@HB\\s+)?(?:HB|HEARTBEAT)(?:\\s+([A-R]{2}[0-9]{2}))?$"));
  static const QRegularExpression cq(
      QStringLiteral("^(?:@ALLCALL\\s+)?(CQ CQ CQ|CQ DX|CQ QRP|CQ CONTEST|CQ FIELD|CQ FD|CQ CQ|CQ)"
                     "(?:\\s+([A-R]{2}[0-9]{2}))?$"));
  uint64_t call50 = 0;
  if (packAlphaNumeric50(me, &call50)) {
    if (const auto m = hb.match(trimmed); m.hasMatch()) {
      const QString grid = m.captured(1).isEmpty() ? myGrid4 : m.captured(1);
      head.bits = compoundFrame(call50, gridNumber(grid), 0);
      head.consumed = text.size();
      head.display = (me + QLatin1String(": @HB HEARTBEAT ") + grid).trimmed();
      return head;
    }
    if (const auto m = cq.match(trimmed); m.hasMatch()) {
      int which = 7;
      for (int i = 0; i < 8; ++i) {
        if (m.captured(1) == QLatin1String(kCqs[i])) {
          which = i;
          break;
        }
      }
      const QString grid = m.captured(2).isEmpty() ? myGrid4 : m.captured(2);
      head.bits = compoundFrame(call50, gridNumber(grid) | (1 << 15), which);
      head.consumed = text.size();
      head.display = (me + QLatin1String(": @ALLCALL ") + m.captured(1) + QLatin1Char(' ') + grid).trimmed();
      return head;
    }
  }

  // Directed: TO, then a JS8Call command, then any free text. A first word
  // that is not a call or @group leaves the message as plain text.
  uint32_t from = 0;
  bool fromP = false;
  if (!packCallsign28(me, &from, &fromP))
    return head;
  const int space = body.indexOf(QLatin1Char(' '));
  const QString to = space < 0 ? body : body.left(space);
  uint32_t toValue = 0;
  bool toP = false;
  if (to.isEmpty() || !packCallsign28(to, &toValue, &toP))
    return head;
  const QString after = space < 0 ? QString() : body.mid(space);  // starts with ' '
  int cmd = 31;
  int cmdLength = 0;
  for (int i = 0; i < 31; ++i) {
    if (i == 5)
      continue;  // ">" relay is not built here
    const QString name = QString::fromLatin1(kCommands[i]);
    if (name.size() <= cmdLength || !after.startsWith(name))
      continue;
    if (name.size() < after.size() && after.at(name.size()) != QLatin1Char(' '))
      continue;
    cmd = i;
    cmdLength = int(name.size());
  }
  int num = 0;
  int taken = cmdLength;
  if (cmd != 31 && snrCommand(cmd)) {
    static const QRegularExpression snr(QStringLiteral("^ ([+-]?\\d{1,2})(?= |$)"));
    const auto m = snr.match(after.mid(cmdLength));
    if (!m.hasMatch())
      return head;
    num = std::clamp(m.captured(1).toInt(), -30, 32) + 31;
    taken += int(m.capturedLength(0));
  }
  if (cmd == 31) {
    taken = after.isEmpty() ? 0 : 1;  // the space after TO
  } else if (taken < after.size() && after.at(taken) == QLatin1Char(' ')) {
    ++taken;  // one space after the command rides with it
  }
  std::string bits;
  appendBits(&bits, 3, 3);
  appendBits(&bits, from, 28);
  appendBits(&bits, toValue, 28);
  appendBits(&bits, uint64_t(cmd), 5);
  appendBits(&bits, fromP ? 1 : 0, 1);
  appendBits(&bits, toP ? 1 : 0, 1);
  appendBits(&bits, uint64_t(num), 6);
  head.bits = bits;
  head.consumed = lead + int(to.size()) + taken;
  head.display = me + QLatin1String(": ") + trimmed;
  head.toCall = to;
  return head;
}

Js8Plan planJs8(const QString& text, int toneHz, int maxFrames, int* consumed, bool messageStart,
                const QString& myCall, const QString& myGrid) {
  Js8Plan plan;
  const int tone = std::clamp(toneHz, 200, 3000);
  QString left = text.toUpper();
  plan.echoAt.assign(static_cast<std::size_t>(text.size()), -1);
  bool first = messageStart;
  int frames = 0;
  int total = 0;
  Js8Header head;
  if (messageStart && !myCall.trimmed().isEmpty())
    head = js8Header(text, myCall, myGrid);
  while (!left.isEmpty()) {
    if (maxFrames > 0 && frames >= maxFrames)
      break;
    int used = 0;
    std::string bits;
    if (frames == 0 && !head.bits.empty()) {
      bits = head.bits;
      used = std::min<int>(head.consumed, int(left.size()));
    } else {
      bits = pack72(left, &used);
      if (used < 1)
        break;
    }
    if (bits.size() != 72)
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
    const int at = int(plan.samples.size()) + kDelay;
    for (int i = 0; i < used; ++i) {
      const int index = text.size() - left.size() + i;
      if (index >= 0 && index < text.size())
        plan.echoAt[static_cast<std::size_t>(index)] = at;
    }
    writeFrame(tones, tone, &plan.samples);
    left = left.mid(used);
    total += used;
    first = false;
    ++frames;
  }
  if (consumed)
    *consumed = total;
  return plan;
}

std::vector<float> js8FrameAudio(const QString& frame12, int type, int toneHz) {
  std::vector<float> out;
  int tones[kSymbols];
  if (!encodeFrame(frame12, type, tones))
    return out;
  writeFrame(tones, std::clamp(toneHz, 200, 3000), &out);
  return out;
}

QString js8Decode(const float* samples, int count, int sampleRateHz, int toneHz, int* snrDb) {
  if (snrDb)
    *snrDb = 0;
  if (!samples || count < kSymbols * kSymbol || sampleRateHz != kRate)
    return {};
  const int tone = std::clamp(toneHz, 200, 3000);
  const std::vector<float> rx = to12k(samples, count);
  const int rxCount = int(rx.size());
  QString text;
  int offset = 0;
  bool haveSnr = false;
  const int frame = kSymbols * kRxSym;
  while (offset + frame <= rxCount) {
    std::vector<Js8Hit> hits;
    int time = 0;
    int snr = 0;
    collectFrame(rx.data() + offset, rxCount - offset, tone, kRxDelay, &time, &hits, &snr);
    if (hits.empty())
      break;
    if (!haveSnr && snrDb) {
      *snrDb = snr;
      haveSnr = true;
    }
    for (const Js8Hit& hit : hits)
      text.append(hit.text);
    const int step = time + frame;
    if (step < frame)
      break;
    offset += step;
  }
  return text;
}

std::vector<Js8Heard> js8DecodePeriod(const float* samples, int count, int sampleRateHz, int toneHz,
                                      int leadMs) {
  std::vector<Js8Heard> heard;
  if (!samples || count < 1 || sampleRateHz != kRate || leadMs < 0)
    return heard;
  const std::vector<float> rx = to12k(samples, count);
  std::vector<Js8Hit> hits;
  int time = 0;
  int snr = 0;
  const int prefer = int(qint64(leadMs) * kRx / 1000) + kRxDelay;
  collectFrame(rx.data(), int(rx.size()), std::clamp(toneHz, 200, 3000), prefer, &time, &hits, &snr);
  for (const Js8Hit& hit : hits)
    heard.push_back(Js8Heard{hit.freq, hit.text, hit.snr, (hit.flags & 2) != 0});
  return heard;
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
  ring_.clear();
  total_ = 0;
  newestMs_ = 0;
  decodedPeriod_ = -1;
  heard_.clear();
  ++generation_;
}

void Js8Engine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!armed_ || !samples || count < 1 || sampleRateHz < 1000)
    return;
  if (sampleRateHz != rate_) {
    clear();
    rate_ = sampleRateHz;
    armed_ = true;
  }
  if (rate_ != 48000)
    return;
  constexpr int kRing = 48000 * 22;
  constexpr qint64 kPeriod = 15000;
  if (int(ring_.size()) != kRing)
    ring_.assign(static_cast<std::size_t>(kRing), 0.0f);
  for (int i = 0; i < count; ++i) {
    ring_[static_cast<std::size_t>(total_ % kRing)] = samples[i];
    ++total_;
  }
  newestMs_ = QDateTime::currentMSecsSinceEpoch();
  qint64 periodStart = (newestMs_ / kPeriod) * kPeriod;
  // The frame ends 13.14 s after the boundary. Wait until 16 s so a late
  // start of up to 2 s is still inside the ring.
  if (newestMs_ < periodStart + 16000)
    periodStart -= kPeriod;
  if (periodStart < 0 || periodStart == decodedPeriod_)
    return;
  const qint64 sliceStart = periodStart - 2000;
  const int slice = 18 * 48000;
  const qint64 ago = (newestMs_ - sliceStart) * 48000 / 1000;
  const qint64 start = (total_ - 1) - ago;
  const qint64 oldest = total_ > kRing ? total_ - kRing : 0;
  if (start < oldest || start + slice > total_)
    return;
  decodedPeriod_ = periodStart;
  std::vector<float> window(static_cast<std::size_t>(slice));
  for (int i = 0; i < slice; ++i)
    window[static_cast<std::size_t>(i)] = ring_[static_cast<std::size_t>((start + i) % kRing)];
  // The passband scan takes a few hundred ms. Run it off the GUI thread so
  // the waterfall and the other receivers keep going; print back here.
  const QPointer<Js8Engine> self(this);
  const int tone = toneHz_;
  const int generation = generation_;
  QThreadPool::globalInstance()->start([self, window = std::move(window), tone, periodStart, generation]() {
    std::vector<Js8Heard> hits = js8DecodePeriod(window.data(), int(window.size()), 48000, tone, 2000);
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [self, hits = std::move(hits), periodStart, generation]() {
          if (self)
            self->publish(periodStart, generation, hits);
        },
        Qt::QueuedConnection);
  });
}

void Js8Engine::publish(qint64 periodStart, int generation, const std::vector<Js8Heard>& hits) {
  // A Clear, a disarm, or a rate change since the slice was taken drops it.
  if (!armed_ || generation != generation_)
    return;
  for (const Js8Heard& hit : hits) {
    const QString key = QString::number(periodStart) + QLatin1Char('|') + QString::number(hit.freqHz) +
                        QLatin1Char('|') + hit.text;
    if (heard_.contains(key))
      continue;
    heard_.append(key);
    while (heard_.size() > 32)
      heard_.removeFirst();
    emit textReady(hit.text, hit.snrDb, hit.freqHz);
    emit frameHeard(hit.text, hit.snrDb, hit.freqHz, hit.last);
  }
}
