// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// WSPR-2, rewritten from the WSJT-X sources that JTDX-improved ships.
// GPL-3. Not MSHV. The callsign and the locator are not stored here.

#include "wspr/WsprModem.h"

#include "DigitalMode.h"
#include "wspr/WsprMetric.h"

#include <QRegularExpression>
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QSettings>
#include <QSpinBox>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

constexpr int kSymbols = 162;
constexpr double kDf = 12000.0 / 8192.0;
constexpr uint32_t kPoly1 = 0xf2d05351u;
constexpr uint32_t kPoly2 = 0xe4613c47u;

// wsprsim_utils.c sync vector.
constexpr unsigned char kSync[kSymbols] = {
    1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1,
    0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1,
    0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
    0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0,
    0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0,
    1, 1, 0, 0, 0};

constexpr int kNu[10] = {0, -1, 1, 0, -1, 2, 1, 0, -1, 1};

constexpr int kPowerDbm[] = {0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60};

struct Band {
  const char* name;
  qint64 dial;
};

constexpr Band kBands[10] = {
    {"160m", 136000},    {"80m", 1836600},   {"60m", 5357000},   {"40m", 7038600},
    {"30m", 10138700},   {"20m", 14095600},  {"17m", 18104600},  {"15m", 21094600},
    {"12m", 24924600},   {"10m", 28124600},
};

const char* kPeriods[6] = {"Sunrise grayline", "Day", "Sunset grayline", "Night", "Tune", "Rx only"};

char gSchedule[60];
int gScheduleSlot = -1;
int gSchedulePct = -1;

int parity32(uint32_t x) {
  x ^= x >> 16;
  x ^= x >> 8;
  x ^= x >> 4;
  x ^= x >> 2;
  x ^= x >> 1;
  return int(x & 1u);
}

int locatorCode(QChar ch) {
  const ushort u = ch.unicode();
  if (u >= '0' && u <= '9')
    return int(u - '0');
  if (u == ' ')
    return 36;
  if (u >= 'A' && u <= 'Z')
    return int(u - 'A');
  return -1;
}

int callCode(QChar ch) {
  const ushort u = ch.unicode();
  if (u >= '0' && u <= '9')
    return int(u - '0');
  if (u == ' ')
    return 36;
  if (u >= 'A' && u <= 'Z')
    return int(u - 'A') + 10;
  return -1;
}

uint32_t packCall(const QString& callsign) {
  if (callsign.size() > 6 || callsign.isEmpty())
    return 0;
  QChar call6[6] = {QLatin1Char(' '), QLatin1Char(' '), QLatin1Char(' '),
                    QLatin1Char(' '), QLatin1Char(' '), QLatin1Char(' ')};
  const bool digitThird = callsign.size() > 2 && callsign.at(2).isDigit();
  const bool digitSecond = callsign.size() > 1 && callsign.at(1).isDigit();
  if (digitThird) {
    for (int i = 0; i < callsign.size(); ++i)
      call6[i] = callsign.at(i);
  } else if (digitSecond) {
    for (int i = 0; i < callsign.size(); ++i)
      call6[i + 1] = callsign.at(i);
  } else {
    return 0;
  }
  int codes[6];
  for (int i = 0; i < 6; ++i) {
    codes[i] = callCode(call6[i]);
    if (codes[i] < 0)
      return 0;
  }
  uint32_t n = uint32_t(codes[0]);
  n = n * 36u + uint32_t(codes[1]);
  n = n * 10u + uint32_t(codes[2]);
  n = n * 27u + uint32_t(codes[3] - 10);
  n = n * 27u + uint32_t(codes[4] - 10);
  n = n * 27u + uint32_t(codes[5] - 10);
  return n;
}

uint32_t packGridPower(const int grid[4], int power) {
  const uint32_t m =
      uint32_t((179 - 10 * grid[0] - grid[2]) * 180 + 10 * grid[1] + grid[3]);
  return m * 128u + uint32_t(power + 64);
}

int clampPower(int power) {
  if (power < 0)
    power = 0;
  if (power > 60)
    power = 60;
  return power;
}

bool packType1(const QString& call, const QString& grid, int power, uint32_t* n, uint32_t* m) {
  if (grid.size() < 4)
    return false;
  int g[4];
  for (int i = 0; i < 4; ++i) {
    g[i] = locatorCode(grid.at(i));
    if (g[i] < 0)
      return false;
  }
  *n = packCall(call);
  if (*n == 0)
    return false;
  *m = packGridPower(g, clampPower(power));
  return true;
}

bool packType2(QString call, int power, uint32_t* n, uint32_t* m) {
  const int slash = call.indexOf(QLatin1Char('/'));
  if (slash <= 0 || slash + 1 >= call.size())
    return false;
  const QString left = call.left(slash);
  const QString right = call.mid(slash + 1);
  power = clampPower(power);
  power += kNu[power % 10];
  int nadd = 0;
  int ng = 0;
  uint32_t n1 = 0;
  if (right.size() == 1 || right.size() == 2) {
    n1 = packCall(left);
    if (n1 == 0)
      return false;
    nadd = 1;
    if (right.size() == 1) {
      const QChar nc = right.at(0);
      if (nc.isDigit())
        ng = nc.unicode() - '0';
      else if (nc.isLetter())
        ng = nc.unicode() - 'A' + 10;
      else
        ng = 38;
      ng = 60000 - 32768 + ng;
    } else {
      if (!right.at(0).isDigit() || !right.at(1).isDigit())
        return false;
      ng = 10 * (right.at(0).unicode() - '0') + (right.at(1).unicode() - '0');
      ng = 60000 + 26 + ng;
    }
  } else {
    n1 = packCall(right);
    if (n1 == 0)
      return false;
    const int plen = left.size();
    if (plen == 1)
      ng = 37 * 36 + 36;
    else if (plen == 2)
      ng = 36;
    else
      ng = 0;
    for (int i = 0; i < plen; ++i) {
      const QChar nc = left.at(i);
      int code = 36;
      if (nc.isDigit())
        code = nc.unicode() - '0';
      else if (nc.isLetter())
        code = nc.unicode() - 'A' + 10;
      ng = 37 * ng + code;
    }
    nadd = 0;
    if (ng > 32768) {
      ng -= 32768;
      nadd = 1;
    }
  }
  const int ntype = power + 1 + nadd;
  *n = n1;
  *m = uint32_t(128 * ng + ntype + 64);
  return true;
}

void pack50(uint32_t n, uint32_t m, unsigned char data[11]) {
  std::memset(data, 0, 11);
  data[0] = static_cast<unsigned char>((n >> 20) & 0xffu);
  data[1] = static_cast<unsigned char>((n >> 12) & 0xffu);
  data[2] = static_cast<unsigned char>((n >> 4) & 0xffu);
  data[3] = static_cast<unsigned char>(((n & 0x0fu) << 4) + ((m >> 18) & 0x0fu));
  data[4] = static_cast<unsigned char>((m >> 10) & 0xffu);
  data[5] = static_cast<unsigned char>((m >> 2) & 0xffu);
  data[6] = static_cast<unsigned char>((m & 0x03u) << 6);
}

// j is an unsigned char in wsprsim_utils.c, so the 64-bit product is
// truncated to 8 bits before the "j < 162" test.
int interleaveIndex(int i) {
  const uint64_t wide =
      ((uint64_t(i) * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
  return int(wide & 0xffu);
}

void interleave(unsigned char* sym) {
  unsigned char tmp[kSymbols];
  int p = 0;
  int i = 0;
  while (p < kSymbols) {
    const int j = interleaveIndex(i);
    if (j < kSymbols) {
      tmp[j] = sym[p];
      ++p;
    }
    ++i;
  }
  std::memcpy(sym, tmp, kSymbols);
}

void deinterleave(unsigned char* sym) {
  unsigned char tmp[kSymbols];
  int p = 0;
  int i = 0;
  while (p < kSymbols) {
    const int j = interleaveIndex(i);
    if (j < kSymbols) {
      tmp[p] = sym[j];
      ++p;
    }
    ++i;
  }
  std::memcpy(sym, tmp, kSymbols);
}

void encode162(const unsigned char data[11], unsigned char channel[kSymbols]) {
  unsigned char raw[11 * 8 * 2];
  std::memset(raw, 0, sizeof raw);
  uint32_t state = 0;
  int k = 0;
  for (int b = 0; b < 11; ++b) {
    for (int bit = 7; bit >= 0; --bit) {
      state = (state << 1) | ((data[b] >> bit) & 1u);
      raw[k++] = static_cast<unsigned char>(parity32(state & kPoly1));
      raw[k++] = static_cast<unsigned char>(parity32(state & kPoly2));
    }
  }
  interleave(raw);
  std::memcpy(channel, raw, kSymbols);
}

bool symbolsFor(const QString& message, unsigned char symbols[kSymbols]) {
  const QStringList parts =
      message.trimmed().toUpper().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (parts.size() < 2)
    return false;
  const QString call = parts.at(0);
  if (call.contains(QLatin1Char('<')) || call.contains(QLatin1Char('>')))
    return false;
  uint32_t n = 0;
  uint32_t m = 0;
  bool packed = false;
  if (call.contains(QLatin1Char('/'))) {
    packed = packType2(call, parts.at(1).toInt(), &n, &m);
  } else if (parts.size() >= 3) {
    packed = packType1(call, parts.at(1), parts.at(2).toInt(), &n, &m);
  }
  if (!packed)
    return false;
  unsigned char data[11];
  pack50(n, m, data);
  unsigned char channel[kSymbols];
  encode162(data, channel);
  for (int i = 0; i < kSymbols; ++i)
    symbols[i] = static_cast<unsigned char>(2 * channel[i] + kSync[i]);
  return true;
}

int symbolCount(int rate) {
  return int(std::lround(8192.0 * double(rate) / 12000.0));
}

void unpack50(const unsigned char* dat, uint32_t* n1, uint32_t* n2) {
  *n1 = (uint32_t(dat[0]) << 20) + (uint32_t(dat[1]) << 12) + (uint32_t(dat[2]) << 4) +
        ((uint32_t(dat[3]) >> 4) & 15u);
  *n2 = ((uint32_t(dat[3]) & 15u) << 18) + (uint32_t(dat[4]) << 10) + (uint32_t(dat[5]) << 2) +
        ((uint32_t(dat[6]) >> 6) & 3u);
}

QString unpackCall(uint32_t ncall) {
  static const char c[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
  if (ncall >= 262177560u)
    return {};
  char tmp[7];
  uint32_t n = ncall;
  tmp[5] = c[n % 27u + 10u];
  n /= 27u;
  tmp[4] = c[n % 27u + 10u];
  n /= 27u;
  tmp[3] = c[n % 27u + 10u];
  n /= 27u;
  tmp[2] = c[n % 10u];
  n /= 10u;
  tmp[1] = c[n % 36u];
  n /= 36u;
  tmp[0] = c[n];
  tmp[6] = 0;
  int start = 0;
  while (start < 5 && tmp[start] == ' ')
    ++start;
  QString call = QString::fromLatin1(tmp + start);
  while (call.endsWith(QLatin1Char(' ')))
    call.chop(1);
  return call;
}

QString unpackGrid(uint32_t ngrid) {
  static const char c[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
  ngrid >>= 7;
  if (ngrid >= 32400u)
    return {};
  int dlat = int(ngrid % 180u) - 90;
  int dlong = int(ngrid / 180u) * 2 - 180 + 2;
  if (dlong < -180)
    dlong += 360;
  if (dlong > 180)
    dlong += 360;
  const int nlong = int(60.0 * (180.0 - double(dlong)) / 5.0);
  const int nlat = int(60.0 * (double(dlat) + 90.0) / 2.5);
  const int lon1 = nlong / 240;
  const int lon2 = (nlong - 240 * lon1) / 24;
  const int lat1 = nlat / 240;
  const int lat2 = (nlat - 240 * lat1) / 24;
  QString grid;
  grid += QLatin1Char(c[10 + lon1]);
  grid += QLatin1Char(c[10 + lat1]);
  grid += QLatin1Char(c[lon2]);
  grid += QLatin1Char(c[lat2]);
  return grid;
}

QString unpackPrefix(int nprefix, const QString& base) {
  if (nprefix < 60000) {
    int n = nprefix;
    QChar pfx[3];
    for (int i = 2; i >= 0; --i) {
      const int nc = n % 37;
      if (nc >= 0 && nc <= 9)
        pfx[i] = QLatin1Char('0' + nc);
      else if (nc >= 10 && nc <= 35)
        pfx[i] = QLatin1Char('A' + nc - 10);
      else
        pfx[i] = QLatin1Char(' ');
      n /= 37;
    }
    QString prefix;
    for (QChar ch : pfx)
      prefix += ch;
    const int space = prefix.lastIndexOf(QLatin1Char(' '));
    if (space >= 0)
      prefix = prefix.mid(space + 1);
    return prefix + QLatin1Char('/') + base;
  }
  const int nc = nprefix - 60000;
  QString suffix;
  if (nc >= 0 && nc <= 9)
    suffix = QString(QLatin1Char('0' + nc));
  else if (nc >= 10 && nc <= 35)
    suffix = QString(QLatin1Char('A' + nc - 10));
  else if (nc >= 36 && nc <= 125) {
    suffix = QString(QLatin1Char('0' + (nc - 26) / 10));
    suffix += QLatin1Char('0' + (nc - 26) % 10);
  } else {
    return {};
  }
  return base + QLatin1Char('/') + suffix;
}

QString formatMessage(const QString& call, const QString& grid, int dbm) {
  return QStringLiteral("%1 %2 %3").arg(call.trimmed(), grid.trimmed()).arg(dbm);
}

bool legalPower(int dbm) {
  const int nu = dbm % 10;
  return dbm >= 0 && dbm <= 60 && (nu == 0 || nu == 3 || nu == 7);
}

// The six packed callsign characters, spaces kept. Type 3 hides the
// 6-character locator here, rotated by one.
QString unpackCall6(uint32_t ncall) {
  static const char c[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
  if (ncall >= 262177560u)
    return {};
  char tmp[7];
  uint32_t n = ncall;
  tmp[5] = c[n % 27u + 10u];
  n /= 27u;
  tmp[4] = c[n % 27u + 10u];
  n /= 27u;
  tmp[3] = c[n % 27u + 10u];
  n /= 27u;
  tmp[2] = c[n % 10u];
  n /= 10u;
  tmp[1] = c[n % 36u];
  n /= 36u;
  tmp[0] = c[n];
  tmp[6] = 0;
  return QString::fromLatin1(tmp);
}

uint32_t rot32(uint32_t x, int k) {
  return (x << k) | (x >> (32 - k));
}

// nhash() from lib/wsprd/nhash.c (Bob Jenkins lookup3, public domain),
// byte at a time, masked to 15 bits. initval is 146 for WSPR.
int wsprHash(const QByteArray& key) {
  size_t length = size_t(key.size());
  const auto* k = reinterpret_cast<const uint8_t*>(key.constData());
  uint32_t a = 0xdeadbeefu + uint32_t(length) + 146u;
  uint32_t b = a;
  uint32_t c = a;
  while (length > 12) {
    a += k[0] + (uint32_t(k[1]) << 8) + (uint32_t(k[2]) << 16) + (uint32_t(k[3]) << 24);
    b += k[4] + (uint32_t(k[5]) << 8) + (uint32_t(k[6]) << 16) + (uint32_t(k[7]) << 24);
    c += k[8] + (uint32_t(k[9]) << 8) + (uint32_t(k[10]) << 16) + (uint32_t(k[11]) << 24);
    a -= c; a ^= rot32(c, 4);  c += b;
    b -= a; b ^= rot32(a, 6);  a += c;
    c -= b; c ^= rot32(b, 8);  b += a;
    a -= c; a ^= rot32(c, 16); c += b;
    b -= a; b ^= rot32(a, 19); a += c;
    c -= b; c ^= rot32(b, 4);  b += a;
    length -= 12;
    k += 12;
  }
  switch (length) {
  case 12: c += uint32_t(k[11]) << 24; [[fallthrough]];
  case 11: c += uint32_t(k[10]) << 16; [[fallthrough]];
  case 10: c += uint32_t(k[9]) << 8; [[fallthrough]];
  case 9: c += k[8]; [[fallthrough]];
  case 8: b += uint32_t(k[7]) << 24; [[fallthrough]];
  case 7: b += uint32_t(k[6]) << 16; [[fallthrough]];
  case 6: b += uint32_t(k[5]) << 8; [[fallthrough]];
  case 5: b += k[4]; [[fallthrough]];
  case 4: a += uint32_t(k[3]) << 24; [[fallthrough]];
  case 3: a += uint32_t(k[2]) << 16; [[fallthrough]];
  case 2: a += uint32_t(k[1]) << 8; [[fallthrough]];
  case 1: a += k[0]; break;
  case 0: return int(c & 32767u);
  }
  c ^= b; c -= rot32(b, 14);
  a ^= c; a -= rot32(c, 11);
  b ^= a; b -= rot32(a, 25);
  c ^= b; c -= rot32(b, 16);
  a ^= c; a -= rot32(c, 4);
  b ^= a; b -= rot32(a, 14);
  c ^= b; c -= rot32(b, 24);
  return int(c & 32767u);
}

void hashRemember(WsprHashTable* hash, const QString& call, const QString& grid) {
  if (!hash || call.isEmpty())
    return;
  const int ihash = wsprHash(call.toLatin1());
  hash->call[size_t(ihash)] = call;
  if (!grid.isEmpty())
    hash->grid[size_t(ihash)] = grid;
}

// Same checks as unpk_() in wsprd_utils.c. Empty when wsprd would not print it.
// Type 1 and type 2 calls go into hash; type 3 looks its call up there.
QString unpackMessage(const unsigned char data[11], WsprHashTable* hash = nullptr) {
  uint32_t n1 = 0;
  uint32_t n2 = 0;
  unpack50(data, &n1, &n2);
  QString call = unpackCall(n1);
  const QString grid = unpackGrid(n2);
  if (call.isEmpty() || grid.size() != 4)
    return {};
  const int ntype = int(n2 & 127u) - 64;
  if (ntype >= 0 && ntype <= 62) {
    const int nu = ntype % 10;
    if (nu == 0 || nu == 3 || nu == 7) {
      hashRemember(hash, call, grid);
      return formatMessage(call, grid, ntype);
    }
    int nadd = nu;
    if (nu > 3)
      nadd = nu - 3;
    if (nu > 7)
      nadd = nu - 7;
    const int n3 = int(n2 / 128u) + 32768 * (nadd - 1);
    call = unpackPrefix(n3, call);
    if (call.isEmpty())
      return {};
    const int dbm = ntype - nadd;
    if (!legalPower(dbm))
      return {};
    hashRemember(hash, call, QString());
    return QStringLiteral("%1 %2").arg(call).arg(dbm);
  }
  if (ntype < 0 && ntype != -64) {
    // Type 3: hashed call, 6-character locator, power. The call prints as
    // <CALL> once a type 1 or type 2 message from it is in the hash table.
    const int dbm = -(ntype + 1);
    const QString raw = unpackCall6(n1);
    if (raw.size() != 6 || !legalPower(dbm))
      return {};
    const QString grid6 = (raw.mid(5, 1) + raw.left(5)).trimmed();
    if (grid6.size() < 4 || !grid6.at(0).isLetter() || !grid6.at(1).isLetter() ||
        !grid6.at(2).isDigit() || !grid6.at(3).isDigit())
      return {};
    const int ihash = int((n2 - uint32_t(ntype) - 64u) / 128u) & 32767;
    QString name = QStringLiteral("...");
    if (hash && !hash->call[size_t(ihash)].isEmpty())
      name = hash->call[size_t(ihash)];
    return formatMessage(QLatin1Char('<') + name + QLatin1Char('>'), grid6, dbm);
  }
  return {};
}

// After an OSD decode wsprd keeps the message only when the call is already
// in the hash table (and, for type 1, with the same locator).
bool osdCallKnown(const unsigned char data[11], const WsprHashTable* hash) {
  if (!hash)
    return false;
  uint32_t n1 = 0;
  uint32_t n2 = 0;
  unpack50(data, &n1, &n2);
  QString call = unpackCall(n1);
  const QString grid = unpackGrid(n2);
  if (call.isEmpty() || grid.size() != 4)
    return false;
  const int ntype = int(n2 & 127u) - 64;
  if (ntype < 0 || ntype > 62)
    return false;
  const int nu = ntype % 10;
  bool type1 = true;
  if (!(nu == 0 || nu == 3 || nu == 7)) {
    int nadd = nu;
    if (nu > 3)
      nadd = nu - 3;
    if (nu > 7)
      nadd = nu - 7;
    call = unpackPrefix(int(n2 / 128u) + 32768 * (nadd - 1), call);
    if (call.isEmpty())
      return false;
    type1 = false;
  }
  const size_t ihash = size_t(wsprHash(call.toLatin1()));
  if (hash->call[ihash] != call)
    return false;
  return !type1 || hash->grid[ihash] == grid;
}

// ---------------------------------------------------------------------------
// Ordered-statistics decoding, a port of lib/wsprd/osdwspr.f90 (K9AN, GPL-3).
// WSJT-X runs it at depth 4 on its Normal and Deep settings when Fano fails.
// Arrays here are 1-based, as in the Fortran, to keep the port checkable.
// ---------------------------------------------------------------------------

class WsprOsd {
public:
  static constexpr int N = 162;
  static constexpr int K = 50;

  WsprOsd() {
    static const unsigned char gg[64] = {1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1,
                                         0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1,
                                         1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1};
    std::memset(gen_, 0, sizeof gen_);
    for (int j = 1; j <= 64; ++j)
      gen_[1][j] = gg[j - 1];
    for (int i = 2; i <= K; ++i) {
      for (int j = 1; j <= N; ++j) {
        int src = j - 2;
        if (src < 1)
          src += N;
        gen_[i][j] = gen_[i - 1][src];
      }
    }
  }

  // ss: 162 deinterleaved soft symbols, symbol - 128. cw: 0/1 codeword out.
  void decode(const float ss[N], int ndeep, unsigned char cwOut[N]) {
    float rx[N + 1];
    float absrx[N + 1];
    unsigned char hdec[N + 1];
    for (int i = 1; i <= N; ++i) {
      rx[i] = ss[i - 1] / 127.0f;
      hdec[i] = rx[i] >= 0.0f ? 1 : 0;
      absrx[i] = std::fabs(rx[i]);
    }
    // indexx(): ascending order of reliability.
    int indx[N + 1];
    {
      std::vector<int> order(N);
      for (int i = 0; i < N; ++i)
        order[i] = i + 1;
      std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return absrx[x] < absrx[y]; });
      for (int i = 1; i <= N; ++i)
        indx[i] = order[size_t(i - 1)];
    }
    int indices[N + 1];
    for (int i = 1; i <= N; ++i) {
      for (int r = 1; r <= K; ++r)
        mrb_[r][i] = gen_[r][indx[N + 1 - i]];
      indices[i] = indx[N + 1 - i];
    }
    for (int id = 1; id <= K; ++id) {
      for (int icol = id; icol <= K + 20; ++icol) {
        if (mrb_[id][icol] != 1)
          continue;
        if (icol != id) {
          for (int r = 1; r <= K; ++r)
            std::swap(mrb_[r][id], mrb_[r][icol]);
          std::swap(indices[id], indices[icol]);
        }
        for (int ii = 1; ii <= K; ++ii) {
          if (ii != id && mrb_[ii][id] == 1) {
            for (int c = 1; c <= N; ++c)
              mrb_[ii][c] ^= mrb_[id][c];
          }
        }
        break;
      }
    }
    // g2(n, k) = mrb_[k][n]
    unsigned char hd[N + 1];
    float ar[N + 1];
    for (int i = 1; i <= N; ++i) {
      hd[i] = hdec[indices[i]];
      ar[i] = absrx[indices[i]];
    }
    unsigned char m0[K + 1];
    for (int i = 1; i <= K; ++i)
      m0[i] = hd[i];
    unsigned char c0[N + 1];
    encode(m0, c0);
    float dmin = 0.0f;
    for (int i = 1; i <= N; ++i)
      dmin += float(c0[i] ^ hd[i]) * ar[i];
    unsigned char cw[N + 1];
    std::memcpy(cw, c0, sizeof cw);

    int nord = 0;
    int npre1 = 0;
    int npre2 = 0;
    int nt = 66;
    int ntheta = 16;
    int ntau = 16;
    if (ndeep > 5)
      ndeep = 5;
    if (ndeep == 1) {
      nord = 1;
    } else if (ndeep == 2) {
      nord = 1; npre1 = 1; ntheta = 22;
    } else if (ndeep == 3) {
      nord = 1; npre1 = 1; npre2 = 1; ntheta = 22;
    } else if (ndeep == 4) {
      nord = 2; npre1 = 1; npre2 = 1; ntheta = 22;
    } else if (ndeep == 5) {
      nord = 3; npre1 = 1; npre2 = 1; ntheta = 22;
    }

    unsigned char misub[K + 1];
    unsigned char mi[K + 1];
    unsigned char me[K + 1];
    unsigned char ce[N + 1];
    unsigned char e2sub[N - K + 1];
    unsigned char e2[N - K + 1];
    for (int iorder = 1; iorder <= nord; ++iorder) {
      for (int i = 1; i <= K; ++i)
        misub[i] = i > K - iorder ? 1 : 0;
      int iflag = K - iorder + 1;
      while (iflag >= 0) {
        const int iend = (iorder == nord && npre1 == 0) ? iflag : 1;
        float d1 = 0.0f;
        for (int n1 = iflag; n1 >= iend; --n1) {
          std::memcpy(mi, misub, sizeof mi);
          mi[n1] = 1;
          for (int i = 1; i <= K; ++i)
            me[i] = m0[i] ^ mi[i];
          int nd1Kpt = 0;
          if (n1 == iflag) {
            encode(me, ce);
            for (int i = 1; i <= N - K; ++i)
              e2sub[i] = e2[i] = ce[K + i] ^ hd[K + i];
            for (int i = 1; i <= nt; ++i)
              nd1Kpt += e2sub[i];
            nd1Kpt += 1;
            d1 = 0.0f;
            for (int i = 1; i <= K; ++i)
              d1 += float(me[i] ^ hd[i]) * ar[i];
          } else {
            for (int i = 1; i <= N - K; ++i)
              e2[i] = e2sub[i] ^ mrb_[n1][K + i];
            for (int i = 1; i <= nt; ++i)
              nd1Kpt += e2[i];
            nd1Kpt += 2;
          }
          if (nd1Kpt > ntheta)
            continue;
          encode(me, ce);
          float dd = d1;
          if (n1 == iflag) {
            for (int i = 1; i <= N - K; ++i)
              dd += float(e2sub[i]) * ar[K + i];
          } else {
            dd += float(ce[n1] ^ hd[n1]) * ar[n1];
            for (int i = 1; i <= N - K; ++i)
              dd += float(e2[i]) * ar[K + i];
          }
          if (dd < dmin) {
            dmin = dd;
            std::memcpy(cw, ce, sizeof cw);
          }
        }
        nextpat(misub, K, iorder, &iflag);
      }
    }

    if (npre2 == 1) {
      boxReset();
      int ntotal = 0;
      unsigned char pat[N - K + 1];
      for (int i1 = K; i1 >= 1; --i1) {
        for (int i2 = i1 - 1; i2 >= 1; --i2) {
          ++ntotal;
          for (int i = 1; i <= ntau; ++i)
            pat[i] = mrb_[i1][K + i] ^ mrb_[i2][K + i];
          boxit(pat, ntau, ntotal, i1, i2);
        }
      }
      lastpat_ = -1;
      for (int i = 1; i <= K; ++i)
        misub[i] = i > K - nord ? 1 : 0;
      int iflag = K - nord + 1;
      unsigned char r2pat[N - K + 1];
      while (iflag >= 0) {
        for (int i = 1; i <= K; ++i)
          me[i] = m0[i] ^ misub[i];
        encode(me, ce);
        for (int i = 1; i <= N - K; ++i)
          e2sub[i] = ce[K + i] ^ hd[K + i];
        for (int i2 = 0; i2 <= ntau; ++i2) {
          for (int i = 1; i <= N - K; ++i)
            r2pat[i] = e2sub[i] ^ ((i2 > 0 && i == i2) ? 1 : 0);
          for (;;) {
            int in1 = -1;
            int in2 = -1;
            fetchit(r2pat, ntau, &in1, &in2);
            if (in1 <= 0 || in2 <= 0)
              break;
            std::memcpy(mi, misub, sizeof mi);
            mi[in1] = 1;
            mi[in2] = 1;
            int weight = 0;
            for (int i = 1; i <= K; ++i)
              weight += mi[i];
            if (weight < nord + npre1 + npre2)
              break;
            for (int i = 1; i <= K; ++i)
              me[i] = m0[i] ^ mi[i];
            encode(me, ce);
            float dd = 0.0f;
            for (int i = 1; i <= N; ++i)
              dd += float(ce[i] ^ hd[i]) * ar[i];
            if (dd < dmin) {
              dmin = dd;
              std::memcpy(cw, ce, sizeof cw);
            }
          }
        }
        nextpat(misub, K, nord, &iflag);
      }
    }

    for (int i = 1; i <= N; ++i)
      cwOut[indices[i] - 1] = cw[i];
  }

private:
  void encode(const unsigned char* m, unsigned char* cw) const {
    std::memset(cw, 0, N + 1);
    for (int i = 1; i <= K; ++i) {
      if (m[i] != 1)
        continue;
      for (int n = 1; n <= N; ++n)
        cw[n] ^= mrb_[i][n];
    }
  }

  static void nextpat(unsigned char* mi, int k, int iorder, int* iflag) {
    int ind = -1;
    for (int i = 1; i <= k - 1; ++i) {
      if (mi[i] == 0 && mi[i + 1] == 1)
        ind = i;
    }
    if (ind < 0) {
      *iflag = ind;
      return;
    }
    unsigned char ms[K + 1];
    std::memset(ms, 0, sizeof ms);
    for (int i = 1; i <= ind - 1; ++i)
      ms[i] = mi[i];
    ms[ind] = 1;
    ms[ind + 1] = 0;
    if (ind + 1 < k) {
      int sum = 0;
      for (int i = 1; i <= k; ++i)
        sum += ms[i];
      const int nz = iorder - sum;
      for (int i = k - nz + 1; i <= k; ++i)
        ms[i] = 1;
    }
    std::memcpy(mi, ms, size_t(k + 1));
    for (int i = 1; i <= k; ++i) {
      if (mi[i] == 1) {
        *iflag = i;
        break;
      }
    }
  }

  static int pattern(const unsigned char* e2, int ntau) {
    int ipat = 0;
    for (int i = 1; i <= ntau; ++i) {
      if (e2[i] == 1)
        ipat += 1 << (ntau - i);
    }
    return ipat;
  }

  void boxReset() {
    fp_.assign(65536, -1);
    np_.assign(4000, -1);
    idx1_.assign(4000, -1);
    idx2_.assign(4000, -1);
  }

  void boxit(const unsigned char* e2, int ntau, int npindex, int i1, int i2) {
    idx1_[size_t(npindex)] = i1;
    idx2_[size_t(npindex)] = i2;
    const int ipat = pattern(e2, ntau);
    int ip = fp_[size_t(ipat)];
    if (ip == -1) {
      fp_[size_t(ipat)] = npindex;
      return;
    }
    while (np_[size_t(ip)] != -1)
      ip = np_[size_t(ip)];
    np_[size_t(ip)] = npindex;
  }

  void fetchit(const unsigned char* e2, int ntau, int* i1, int* i2) {
    const int ipat = pattern(e2, ntau);
    const int index = fp_[size_t(ipat)];
    if (lastpat_ != ipat && index > 0) {
      *i1 = idx1_[size_t(index)];
      *i2 = idx2_[size_t(index)];
      inext_ = np_[size_t(index)];
    } else if (lastpat_ == ipat && inext_ > 0) {
      *i1 = idx1_[size_t(inext_)];
      *i2 = idx2_[size_t(inext_)];
      inext_ = np_[size_t(inext_)];
    } else {
      *i1 = -1;
      *i2 = -1;
      inext_ = -1;
    }
    lastpat_ = ipat;
  }

  unsigned char gen_[K + 1][N + 1];
  unsigned char mrb_[K + 1][N + 1];
  std::vector<int> fp_;
  std::vector<int> np_;
  std::vector<int> idx1_;
  std::vector<int> idx2_;
  int lastpat_ = -1;
  int inext_ = -1;
};

// ---------------------------------------------------------------------------
// Receive. A port of lib/wsprd/wsprd.c and fano.c from WSJT-X (GPL-3,
// K1JT, K9AN, KA9Q): 375 Hz complex baseband with 1500 Hz at DC, candidate
// search over +/-150 Hz, coarse and fine sync, noncoherent block detection,
// Fano sequential decoding, signal subtraction, three passes.
// ---------------------------------------------------------------------------

constexpr int kBufPoints = 65536;   // wsprd maxpts
constexpr int kNp = 46080;          // wsprd npoints (nfft2)
constexpr int kDataPoints = 114 * 375;
constexpr int kNbits = 81;
constexpr float kPiF = 3.14159265358979323846f;
constexpr float kDt375 = 1.0f / 375.0f;
constexpr float kDfTone = 375.0f / 256.0f;
constexpr float kFmin = -110.0f;    // wsprd default: 1390 to 1610 Hz audio
constexpr float kFmax = 110.0f;

struct Candidate {
  float freq = 0.0f;
  float snr = 0.0f;
  int shift = 0;
  float drift = 0.0f;
  float sync = 0.0f;
};

void fft512(float* re, float* im) {
  constexpr int n = 512;
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * 3.141592653589793 / double(len);
    const float wr = float(std::cos(ang));
    const float wi = float(std::sin(ang));
    for (int i = 0; i < n; i += len) {
      float cr = 1.0f;
      float ci = 0.0f;
      for (int k = 0; k < len / 2; ++k) {
        const int a = i + k;
        const int b = a + len / 2;
        const float tr = re[b] * cr - im[b] * ci;
        const float ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        const float nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
}

// Audio from the start of the 2-minute period to 375 Hz complex baseband,
// 1500 Hz at DC, upper sideband positive. Two stages so any capture rate works.
void downconvert(const float* x, int count, int rate, std::vector<float>& id, std::vector<float>& qd) {
  id.assign(kBufPoints, 0.0f);
  qd.assign(kBufPoints, 0.0f);
  const int n = int(std::min<qint64>(qint64(rate) * 114, qint64(count)));
  if (n < rate)
    return;
  const int d1 = std::max(1, rate / 1500);
  const double r1 = double(rate) / double(d1);
  std::vector<float> mi(static_cast<size_t>(n));
  std::vector<float> mq(static_cast<size_t>(n));
  const double w0 = 2.0 * 3.141592653589793 * 1500.0 / double(rate);
  for (int i = 0; i < n; ++i) {
    const double ph = std::fmod(w0 * double(i), 2.0 * 3.141592653589793);
    mi[i] = float(x[i] * std::cos(ph));
    mq[i] = float(-x[i] * std::sin(ph));
  }
  // Stage 1: low pass at r1/2, keep every d1-th sample.
  const int m1 = n / d1;
  std::vector<float> yi(static_cast<size_t>(m1), 0.0f);
  std::vector<float> yq(static_cast<size_t>(m1), 0.0f);
  if (d1 == 1) {
    yi = mi;
    yq = mq;
  } else {
    const double trans = std::max(200.0, r1 - 400.0);
    const int taps = int(std::ceil(6.0 * double(rate) / trans)) | 1;
    const int half = taps / 2;
    const double fc = 0.5 * r1 / double(rate);
    std::vector<float> h(static_cast<size_t>(taps));
    double sum = 0.0;
    for (int k = 0; k < taps; ++k) {
      const double u = double(k - half);
      const double s = u == 0.0 ? 2.0 * fc : std::sin(2.0 * 3.141592653589793 * fc * u) / (3.141592653589793 * u);
      const double w = 0.42 - 0.5 * std::cos(2.0 * 3.141592653589793 * k / (taps - 1)) +
                       0.08 * std::cos(4.0 * 3.141592653589793 * k / (taps - 1));
      h[k] = float(s * w);
      sum += s * w;
    }
    for (float& v : h)
      v = float(v / sum);
    for (int m = 0; m < m1; ++m) {
      const int c = m * d1;
      const int k0 = std::max(0, half - c);
      const int k1 = std::min(taps, n - c + half);
      float ai = 0.0f;
      float aq = 0.0f;
      for (int k = k0; k < k1; ++k) {
        const int idx = c + k - half;
        ai += h[k] * mi[idx];
        aq += h[k] * mq[idx];
      }
      yi[m] = ai;
      yq[m] = aq;
    }
  }
  mi.clear();
  mi.shrink_to_fit();
  mq.clear();
  mq.shrink_to_fit();
  // Stage 2: windowed sinc at 187.5 Hz, sampled at 375 Hz.
  const double fc2 = 187.5 / r1;
  const int hw = int(std::ceil(2.9 * r1 / 75.0));
  const double span = double(hw + 1);
  for (int k = 0; k < kDataPoints; ++k) {
    const double pos = double(k) * r1 / 375.0;
    const int base = int(std::floor(pos));
    if (base - hw >= m1)
      break;
    double ai = 0.0;
    double aq = 0.0;
    double wsum = 0.0;
    for (int m = base - hw; m <= base + hw + 1; ++m) {
      const double u = double(m) - pos;
      if (std::fabs(u) >= span)
        continue;
      const double s = u == 0.0 ? 2.0 * fc2 : std::sin(2.0 * 3.141592653589793 * fc2 * u) / (3.141592653589793 * u);
      const double w = 0.42 + 0.5 * std::cos(3.141592653589793 * u / span) +
                       0.08 * std::cos(2.0 * 3.141592653589793 * u / span);
      const double g = s * w;
      wsum += g;
      if (m >= 0 && m < m1) {
        ai += g * yi[m];
        aq += g * yq[m];
      }
    }
    if (wsum != 0.0) {
      id[k] = float(ai / wsum);
      qd[k] = float(aq / wsum);
    }
  }
}

// Reference tones of the four FSK levels for one symbol at frequency fp.
struct ToneBank {
  float c[4][257];
  float s[4][257];
  void fill(float fp) {
    const float off[4] = {-1.5f, -0.5f, 0.5f, 1.5f};
    for (int t = 0; t < 4; ++t) {
      const double dphi = 2.0 * 3.141592653589793 * kDt375 * (fp + off[t] * kDfTone);
      const double cd = std::cos(dphi);
      const double sd = std::sin(dphi);
      double cr = 1.0;
      double si = 0.0;
      for (int j = 0; j < 257; ++j) {
        c[t][j] = float(cr);
        s[t][j] = float(si);
        const double nc = cr * cd - si * sd;
        si = cr * sd + si * cd;
        cr = nc;
      }
    }
  }
};

// Mode 0: best lag. Mode 1: best frequency. As sync_and_demodulate().
void syncDemod(const float* id, const float* qd, int np, float* f1, int ifmin, int ifmax, float fstep,
               int* shift1, int lagmin, int lagmax, int lagstep, float drift1, float* sync, int mode) {
  if (mode == 0) {
    ifmin = 0;
    ifmax = 0;
    fstep = 0.0f;
  } else {
    lagmin = *shift1;
    lagmax = *shift1;
  }
  if (lagstep < 1)
    lagstep = 1;
  float syncmax = -1e30f;
  int bestShift = *shift1;
  float fbest = *f1;
  static thread_local std::vector<ToneBank> banks(kSymbols);
  for (int ifreq = ifmin; ifreq <= ifmax; ++ifreq) {
    const float f0 = *f1 + float(ifreq) * fstep;
    for (int i = 0; i < kSymbols; ++i)
      banks[i].fill(f0 + (drift1 / 2.0f) * (float(i) - 81.0f) / 81.0f);
    for (int lag = lagmin; lag <= lagmax; lag += lagstep) {
      float ss = 0.0f;
      float totp = 0.0f;
      for (int i = 0; i < kSymbols; ++i) {
        const ToneBank& b = banks[i];
        float p[4];
        for (int t = 0; t < 4; ++t) {
          float ai = 0.0f;
          float aq = 0.0f;
          for (int j = 0; j < 256; ++j) {
            const int k = lag + i * 256 + j;
            if (k > 0 && k < np) {
              ai += id[k] * b.c[t][j] + qd[k] * b.s[t][j];
              aq += -id[k] * b.s[t][j] + qd[k] * b.c[t][j];
            }
          }
          p[t] = std::sqrt(ai * ai + aq * aq);
        }
        totp += p[0] + p[1] + p[2] + p[3];
        const float cmet = (p[1] + p[3]) - (p[0] + p[2]);
        ss = kSync[i] == 1 ? ss + cmet : ss - cmet;
      }
      if (totp > 0.0f)
        ss /= totp;
      if (ss > syncmax) {
        syncmax = ss;
        bestShift = lag;
        fbest = f0;
      }
    }
  }
  *sync = syncmax;
  *shift1 = bestShift;
  *f1 = fbest;
}

// noncoherent_sequence_detection(): soft symbols 0..255, 128 is unknown.
void softSymbols(const float* id, const float* qd, int np, unsigned char* symbols, float f0, int lag,
                 float drift1, int symfac, int nblock, bool bitmetric) {
  static thread_local std::vector<ToneBank> banks(kSymbols);
  float is[4][kSymbols];
  float qs[4][kSymbols];
  float cf[4][kSymbols];
  float sf[4][kSymbols];
  for (int i = 0; i < kSymbols; ++i) {
    ToneBank& b = banks[i];
    b.fill(f0 + (drift1 / 2.0f) * (float(i) - 81.0f) / 81.0f);
    for (int t = 0; t < 4; ++t) {
      cf[t][i] = b.c[t][256];
      sf[t][i] = b.s[t][256];
      float ai = 0.0f;
      float aq = 0.0f;
      for (int j = 0; j < 256; ++j) {
        const int k = lag + i * 256 + j;
        if (k > 0 && k < np) {
          ai += id[k] * b.c[t][j] + qd[k] * b.s[t][j];
          aq += -id[k] * b.s[t][j] + qd[k] * b.c[t][j];
        }
      }
      is[t][i] = ai;
      qs[t][i] = aq;
    }
  }
  const int nseq = 1 << nblock;
  float fsymb[kSymbols];
  for (int i = 0; i < kSymbols; i += nblock) {
    float p[512];
    for (int j = 0; j < nseq; ++j) {
      float xi = 0.0f;
      float xq = 0.0f;
      float cm = 1.0f;
      float sm = 0.0f;
      for (int ib = 0; ib < nblock; ++ib) {
        const int b = (j >> (nblock - 1 - ib)) & 1;
        const int itone = kSync[i + ib] + 2 * b;
        xi += is[itone][i + ib] * cm + qs[itone][i + ib] * sm;
        xq += qs[itone][i + ib] * cm - is[itone][i + ib] * sm;
        const float cmp = cf[itone][i + ib] * cm - sf[itone][i + ib] * sm;
        const float smp = sf[itone][i + ib] * cm + cf[itone][i + ib] * sm;
        cm = cmp;
        sm = smp;
      }
      p[j] = std::sqrt(xi * xi + xq * xq);
    }
    for (int ib = 0; ib < nblock; ++ib) {
      const int imask = 1 << (nblock - 1 - ib);
      float xm1 = 0.0f;
      float xm0 = 0.0f;
      for (int j = 0; j < nseq; ++j) {
        if (j & imask)
          xm1 = std::max(xm1, p[j]);
        else
          xm0 = std::max(xm0, p[j]);
      }
      fsymb[i + ib] = xm1 - xm0;
      if (bitmetric) {
        const float den = std::max(xm1, xm0);
        fsymb[i + ib] = den > 0.0f ? fsymb[i + ib] / den : 0.0f;
      }
    }
  }
  float fsum = 0.0f;
  float f2sum = 0.0f;
  for (int i = 0; i < kSymbols; ++i) {
    fsum += fsymb[i] / 162.0f;
    f2sum += fsymb[i] * fsymb[i] / 162.0f;
  }
  const float var = f2sum - fsum * fsum;
  const float fac = var > 0.0f ? std::sqrt(var) : 1.0f;
  for (int i = 0; i < kSymbols; ++i) {
    float v = float(symfac) * fsymb[i] / fac;
    v = std::clamp(v, -128.0f, 127.0f);
    symbols[i] = static_cast<unsigned char>(int(v + 128.0f));
  }
}

unsigned encodeSym(uint32_t state) {
  return unsigned(parity32(state & kPoly1) << 1) | unsigned(parity32(state & kPoly2));
}

// fano.c, K=32 r=1/2 Layland-Lushbaugh. 0 on success.
int fano(unsigned char* data, const unsigned char* symbols, int nbits, const int mettab[2][256], int delta,
         unsigned maxcycles) {
  struct Node {
    uint32_t encstate;
    long gamma;
    int metrics[4];
    int tm[2];
    int i;
  };
  std::vector<Node> nodes(static_cast<size_t>(nbits + 1));
  Node* first = nodes.data();
  Node* lastnode = first + nbits - 1;
  Node* tail = first + nbits - 31;
  const unsigned char* sym = symbols;
  for (Node* np = first; np <= lastnode; ++np) {
    np->metrics[0] = mettab[0][sym[0]] + mettab[0][sym[1]];
    np->metrics[1] = mettab[0][sym[0]] + mettab[1][sym[1]];
    np->metrics[2] = mettab[1][sym[0]] + mettab[0][sym[1]];
    np->metrics[3] = mettab[1][sym[0]] + mettab[1][sym[1]];
    sym += 2;
  }
  Node* np = first;
  np->encstate = 0;
  unsigned lsym = encodeSym(np->encstate);
  int m0 = np->metrics[lsym];
  int m1 = np->metrics[3 ^ lsym];
  if (m0 > m1) {
    np->tm[0] = m0;
    np->tm[1] = m1;
  } else {
    np->tm[0] = m1;
    np->tm[1] = m0;
    np->encstate++;
  }
  np->i = 0;
  maxcycles *= unsigned(nbits);
  np->gamma = 0;
  long t = 0;
  unsigned i = 1;
  for (; i <= maxcycles; ++i) {
    const long ngamma = np->gamma + np->tm[np->i];
    if (ngamma >= t) {
      if (np->gamma < t + delta) {
        while (ngamma >= t + delta)
          t += delta;
      }
      np[1].gamma = ngamma;
      np[1].encstate = np->encstate << 1;
      if (++np == lastnode + 1)
        break;
      lsym = encodeSym(np->encstate);
      if (np >= tail) {
        np->tm[0] = np->metrics[lsym];
      } else {
        m0 = np->metrics[lsym];
        m1 = np->metrics[3 ^ lsym];
        if (m0 > m1) {
          np->tm[0] = m0;
          np->tm[1] = m1;
        } else {
          np->tm[0] = m1;
          np->tm[1] = m0;
          np->encstate++;
        }
      }
      np->i = 0;
      continue;
    }
    for (;;) {
      if (np == first || np[-1].gamma < t) {
        t -= delta;
        if (np->i != 0) {
          np->i = 0;
          np->encstate ^= 1u;
        }
        break;
      }
      if (--np < tail && np->i != 1) {
        np->i++;
        np->encstate ^= 1u;
        break;
      }
    }
  }
  int nbytes = nbits >> 3;
  Node* src = first + 7;
  for (int k = 0; k < nbytes; ++k) {
    data[k] = static_cast<unsigned char>(src->encstate & 0xffu);
    src += 8;
  }
  return i >= maxcycles ? -1 : 0;
}

// subtract_signal2(): remove a decoded signal so a weaker one under it shows.
void subtractSignal(float* id, float* qd, int np, float f0, int shift0, float drift0,
                    const unsigned char* channelSymbols) {
  constexpr int nsym = kSymbols;
  constexpr int nsps = 256;
  constexpr int nfilt = 360;
  constexpr int nsig = nsym * nsps;
  constexpr int nc2 = 45000;
  std::vector<float> refi(nc2, 0.0f), refq(nc2, 0.0f), ci(nc2, 0.0f), cq(nc2, 0.0f), cfi(nc2, 0.0f),
      cfq(nc2, 0.0f);
  double phi = 0.0;
  for (int i = 0; i < nsym; ++i) {
    const double cs = double(channelSymbols[i]);
    const double dphi = 2.0 * 3.141592653589793 * kDt375 *
                        (f0 + (drift0 / 2.0) * (double(i) - nsym / 2.0) / (nsym / 2.0) + (cs - 1.5) * kDfTone);
    for (int j = 0; j < nsps; ++j) {
      const int ii = nsps * i + j;
      refi[ii] = float(std::cos(phi));
      refq[ii] = float(std::sin(phi));
      phi += dphi;
    }
  }
  float w[nfilt];
  float partial[nfilt];
  float norm = 0.0f;
  for (int i = 0; i < nfilt; ++i) {
    w[i] = std::sin(kPiF * float(i) / float(nfilt - 1));
    norm += w[i];
  }
  for (int i = 0; i < nfilt; ++i)
    w[i] /= norm;
  partial[0] = 0.0f;
  for (int i = 1; i < nfilt; ++i)
    partial[i] = partial[i - 1] + w[i];
  for (int i = 0; i < nsig; ++i) {
    const int k = shift0 + i;
    if (k > 0 && k < np) {
      ci[i + nfilt] = id[k] * refi[i] + qd[k] * refq[i];
      cq[i + nfilt] = qd[k] * refi[i] - id[k] * refq[i];
    }
  }
  for (int i = nfilt / 2; i < nc2 - nfilt / 2; ++i) {
    float ai = 0.0f;
    float aq = 0.0f;
    for (int j = 0; j < nfilt; ++j) {
      ai += w[j] * ci[i - nfilt / 2 + j];
      aq += w[j] * cq[i - nfilt / 2 + j];
    }
    cfi[i] = ai;
    cfq[i] = aq;
  }
  for (int i = 0; i < nsig; ++i) {
    float nrm = 1.0f;
    if (i < nfilt / 2)
      nrm = partial[nfilt / 2 + i];
    else if (i > nsig - 1 - nfilt / 2)
      nrm = partial[nfilt / 2 + nsig - 1 - i];
    if (nrm <= 0.0f)
      continue;
    const int k = shift0 + i;
    const int j = i + nfilt;
    if (k > 0 && k < np) {
      id[k] -= (cfi[j] * refi[i] - cfq[j] * refq[i]) / nrm;
      qd[k] -= (cfi[j] * refq[i] + cfq[j] * refi[i]) / nrm;
    }
  }
}

// depth follows the WSJT-X WSPR decode menu:
//   1 Fast    wsprd -qB         two passes, no block detection, no DT jitter
//   2 Normal  wsprd -C 500 -o 4 three passes, block detection, OSD depth 4
//   3 Deep    as Normal plus -d (more candidates)
std::vector<WsprSpot> decodeBaseband(std::vector<float>& id, std::vector<float>& qd, int depth,
                                     WsprHashTable* hash) {
  depth = std::clamp(depth, 1, 3);
  const unsigned maxcycles = depth == 1 ? 10000u : 500u;
  const bool quickmode = depth == 1;
  const bool moreCandidates = depth == 3;
  const int ndepth = depth == 1 ? -1 : 4;
  const int npasses = depth == 1 ? 2 : 3;
  constexpr float minsync1 = 0.10f;
  constexpr int iifac = 8;
  constexpr int symfac = 50;
  constexpr float minrms = 52.0f * (50.0f / 64.0f);
  constexpr int delta = 60;
  constexpr float bias = 0.45f;
  constexpr float df = 375.0f / 256.0f / 2.0f;
  const int np = kNp;
  int mettab[2][256];
  for (int i = 0; i < 256; ++i) {
    mettab[0][i] = int(std::lround(10.0f * (wsprmetric::kTable[i] - bias)));
    mettab[1][i] = int(std::lround(10.0f * (wsprmetric::kTable[255 - i] - bias)));
  }
  const int nffts = 4 * (np / 512) - 1;
  std::vector<float> ps(static_cast<size_t>(512) * nffts);
  float win[512];
  for (int i = 0; i < 512; ++i)
    win[i] = std::sin(0.006147931f * float(i));

  WsprHashTable scratch;
  if (!hash)
    hash = &scratch;
  WsprOsd osd;
  std::vector<WsprSpot> out;
  int ndecodesPass = 0;
  for (int ipass = 0; ipass < npasses; ++ipass) {
    if (ipass == 1 && ndecodesPass == 0 && npasses > 2)
      ipass = 2;
    int nblocksize = 1;
    int maxdrift = 4;
    float minsync2 = 0.12f;
    if (ipass == 2) {
      nblocksize = 4;
      maxdrift = 0;
      minsync2 = 0.10f;
    }
    ndecodesPass = 0;

    for (int i = 0; i < nffts; ++i) {
      float re[512];
      float im[512];
      for (int j = 0; j < 512; ++j) {
        const int k = i * 128 + j;
        re[j] = id[k] * win[j];
        im[j] = qd[k] * win[j];
      }
      fft512(re, im);
      for (int j = 0; j < 512; ++j) {
        int k = j + 256;
        if (k > 511)
          k -= 512;
        ps[size_t(j) * nffts + i] = re[k] * re[k] + im[k] * im[k];
      }
    }
    float psavg[512] = {};
    for (int j = 0; j < 512; ++j) {
      for (int i = 0; i < nffts; ++i)
        psavg[j] += ps[size_t(j) * nffts + i];
    }
    float smspec[411];
    for (int i = 0; i < 411; ++i) {
      smspec[i] = 0.0f;
      for (int j = -3; j <= 3; ++j)
        smspec[i] += psavg[256 - 205 + i + j];
    }
    float sorted[411];
    std::copy(smspec, smspec + 411, sorted);
    std::sort(sorted, sorted + 411);
    const float noise = sorted[122] > 0.0f ? sorted[122] : 1e-30f;
    const float minSnr = std::pow(10.0f, -8.0f / 10.0f);
    for (float& v : smspec) {
      v = v / noise - 1.0f;
      if (v < minSnr)
        v = 0.1f * minSnr;
    }

    std::vector<Candidate> all;
    for (int j = 1; j < 410 && all.size() < 200; ++j) {
      if (smspec[j] > smspec[j - 1] && smspec[j] > smspec[j + 1]) {
        Candidate c;
        c.freq = float(j - 205) * df;
        c.snr = 10.0f * std::log10(smspec[j]) - 26.3f;
        all.push_back(c);
      }
    }
    if (moreCandidates) {
      for (int j = 0; j < 411 && all.size() < 200; j += 3) {
        if (smspec[j] > minSnr) {
          Candidate c;
          c.freq = float(j - 205) * df;
          c.snr = 10.0f * std::log10(smspec[j]) - 26.3f;
          all.push_back(c);
        }
      }
    }
    std::vector<Candidate> cands;
    for (const Candidate& c : all) {
      if (c.freq >= kFmin && c.freq <= kFmax)
        cands.push_back(c);
    }
    std::stable_sort(cands.begin(), cands.end(),
                     [](const Candidate& a, const Candidate& b) { return a.snr > b.snr; });

    // Coarse time, frequency, and drift from the spectrogram.
    for (Candidate& c : cands) {
      float smax = -1e30f;
      const int if0 = int(c.freq / df + 256.0f);
      for (int ifr = if0 - 2; ifr <= if0 + 2; ++ifr) {
        for (int k0 = -10; k0 < 22; ++k0) {
          for (int idrift = -maxdrift; idrift <= maxdrift; ++idrift) {
            float ss = 0.0f;
            float pow = 0.0f;
            for (int k = 0; k < kSymbols; ++k) {
              const int ifd = int(float(ifr) + (float(k) - 81.0f) / 81.0f * float(idrift) / (2.0f * df));
              const int kindex = k0 + 2 * k;
              if (kindex < 0 || kindex >= nffts || ifd < 3 || ifd > 508)
                continue;
              const float p0 = std::sqrt(ps[size_t(ifd - 3) * nffts + kindex]);
              const float p1 = std::sqrt(ps[size_t(ifd - 1) * nffts + kindex]);
              const float p2 = std::sqrt(ps[size_t(ifd + 1) * nffts + kindex]);
              const float p3 = std::sqrt(ps[size_t(ifd + 3) * nffts + kindex]);
              ss += float(2 * kSync[k] - 1) * ((p1 + p3) - (p0 + p2));
              pow += p0 + p1 + p2 + p3;
            }
            const float sync1 = pow > 0.0f ? ss / pow : 0.0f;
            if (sync1 > smax) {
              smax = sync1;
              c.shift = 128 * (k0 + 1);
              c.drift = float(idrift);
              c.freq = float(ifr - 256) * df;
              c.sync = sync1;
            }
          }
        }
      }
    }

    // Refine lag, frequency, and drift on the baseband.
    for (Candidate& c : cands) {
      float f1 = c.freq;
      float drift1 = c.drift;
      int shift1 = c.shift;
      float sync1 = c.sync;
      int lagmin = shift1 - 128;
      int lagmax = shift1 + 128;
      int lagstep = 64;
      syncDemod(id.data(), qd.data(), np, &f1, 0, 0, 0.0f, &shift1, lagmin, lagmax, lagstep, drift1, &sync1, 0);
      syncDemod(id.data(), qd.data(), np, &f1, -2, 2, 0.25f, &shift1, lagmin, lagmax, lagstep, drift1, &sync1, 1);
      if (ipass < 2) {
        float syncp = 0.0f;
        float syncm = 0.0f;
        float fp = f1;
        float fm = f1;
        int sp = shift1;
        int sm = shift1;
        syncDemod(id.data(), qd.data(), np, &fp, 0, 0, 0.0f, &sp, lagmin, lagmax, lagstep, drift1 + 0.5f, &syncp, 1);
        syncDemod(id.data(), qd.data(), np, &fm, 0, 0, 0.0f, &sm, lagmin, lagmax, lagstep, drift1 - 0.5f, &syncm, 1);
        if (syncp > sync1) {
          drift1 += 0.5f;
          sync1 = syncp;
        } else if (syncm > sync1) {
          drift1 -= 0.5f;
          sync1 = syncm;
        }
      }
      if (sync1 > minsync1) {
        lagmin = shift1 - 32;
        lagmax = shift1 + 32;
        lagstep = 16;
        syncDemod(id.data(), qd.data(), np, &f1, 0, 0, 0.0f, &shift1, lagmin, lagmax, lagstep, drift1, &sync1, 0);
        syncDemod(id.data(), qd.data(), np, &f1, -2, 2, 0.05f, &shift1, lagmin, lagmax, lagstep, drift1, &sync1, 1);
        c.freq = f1;
        c.shift = shift1;
        c.drift = drift1;
        c.sync = sync1;
      }
    }

    // Drop duplicates and weak sync.
    std::vector<Candidate> wat;
    for (const Candidate& c : cands) {
      bool dupe = false;
      for (Candidate& w : wat) {
        if (std::fabs(c.freq - w.freq) < 0.05f && std::abs(c.shift - w.shift) < 16) {
          if (c.sync > w.sync)
            w = c;
          dupe = true;
          break;
        }
      }
      if (!dupe && c.sync > minsync2)
        wat.push_back(c);
    }

    for (const Candidate& c : wat) {
      unsigned char symbols[kSymbols];
      unsigned char decdata[11];
      std::memset(decdata, 0, sizeof decdata);
      const float f1 = c.freq;
      const int shift1 = c.shift;
      const float drift1 = c.drift;
      bool decoded = false;
      for (int ib = 1; ib <= nblocksize && !decoded; ++ib) {
        const int blocksize = ib < 4 ? ib : 1;
        const bool bitmetric = ib == 4;
        for (int idt = 0; !decoded && idt <= 128 / iifac; ++idt) {
          int ii = (idt + 1) / 2;
          if (idt % 2 == 1)
            ii = -ii;
          ii *= iifac;
          softSymbols(id.data(), qd.data(), np, symbols, f1, shift1 + ii, drift1, symfac, blocksize, bitmetric);
          float sq = 0.0f;
          for (int i = 0; i < kSymbols; ++i) {
            const float y = float(symbols[i]) - 128.0f;
            sq += y * y;
          }
          if (std::sqrt(sq / 162.0f) > minrms) {
            deinterleave(symbols);
            std::memset(decdata, 0, sizeof decdata);
            decoded = fano(decdata, symbols, kNbits, mettab, delta, maxcycles) == 0;
            if (!decoded && ndepth >= 0) {
              float fsymbs[kSymbols];
              for (int i = 0; i < kSymbols; ++i)
                fsymbs[i] = float(symbols[i]) - 128.0f;
              unsigned char cw[kSymbols];
              osd.decode(fsymbs, ndepth, cw);
              unsigned char hard[kSymbols];
              for (int i = 0; i < kSymbols; ++i)
                hard[i] = static_cast<unsigned char>(255 * cw[i]);
              std::memset(decdata, 0, sizeof decdata);
              fano(decdata, hard, kNbits, mettab, delta, maxcycles);
              uint32_t n1 = 0;
              uint32_t n2 = 0;
              unpack50(decdata, &n1, &n2);
              // wsprd leaves the DT search when the OSD word will not unpack.
              if (unpackCall(n1).isEmpty() || unpackGrid(n2).size() != 4)
                break;
              decoded = osdCallKnown(decdata, hash);
            }
          }
          if (quickmode)
            break;
        }
      }
      if (!decoded)
        continue;
      const QString message = unpackMessage(decdata, hash);
      if (message.isEmpty())
        continue;
      ++ndecodesPass;
      unsigned char channel[kSymbols];
      encode162(decdata, channel);
      unsigned char tones[kSymbols];
      for (int i = 0; i < kSymbols; ++i)
        tones[i] = static_cast<unsigned char>(2 * channel[i] + kSync[i]);
      subtractSignal(id.data(), qd.data(), np, f1, shift1, drift1, tones);
      const QString call = message.section(QLatin1Char(' '), 0, 0);
      bool dupe = false;
      for (const WsprSpot& s : out) {
        if (s.message.section(QLatin1Char(' '), 0, 0) == call && std::fabs(s.audioHz - (1500.0 + f1)) < 4.0)
          dupe = true;
      }
      if (dupe)
        continue;
      WsprSpot spot;
      spot.message = message;
      spot.snrDb = int(std::lround(c.snr));
      spot.dtSec = double(shift1) * kDt375 - 1.0;
      spot.audioHz = 1500.0 + double(f1);
      spot.drift = int(std::lround(drift1));
      out.push_back(spot);
    }
  }
  return out;
}

double wrap24(double hour) {
  hour = std::fmod(hour, 24.0);
  if (hour < 0.0)
    hour += 24.0;
  return hour;
}

bool gridLatLon(const QString& token, double* lat, double* lon) {
  const QString grid = token.trimmed().toUpper();
  if (grid.size() != 4 && grid.size() != 6)
    return false;
  if (grid.at(0) < QLatin1Char('A') || grid.at(0) > QLatin1Char('R'))
    return false;
  if (grid.at(1) < QLatin1Char('A') || grid.at(1) > QLatin1Char('R'))
    return false;
  if (!grid.at(2).isDigit() || !grid.at(3).isDigit())
    return false;
  double lonv = (grid.at(0).unicode() - u'A') * 20.0 - 180.0;
  double latv = (grid.at(1).unicode() - u'A') * 10.0 - 90.0;
  lonv += (grid.at(2).unicode() - u'0') * 2.0;
  latv += (grid.at(3).unicode() - u'0') * 1.0;
  if (grid.size() == 6) {
    lonv += (grid.at(4).unicode() - u'A') * (5.0 / 60.0);
    latv += (grid.at(5).unicode() - u'A') * (2.5 / 60.0);
    lonv += 2.5 / 60.0;
    latv += 1.25 / 60.0;
  } else {
    lonv += 1.0;
    latv += 0.5;
  }
  *lat = latv;
  *lon = lonv;
  return true;
}

int txSum() {
  int sum = 0;
  for (char bit : gSchedule)
    sum += bit;
  return sum;
}

void txTrim(int ntxlim) {
  if (gSchedule[0])
    gSchedule[0] = 0;
  int run = 0;
  for (int i = 0; i < 60; ++i) {
    if (gSchedule[i]) {
      ++run;
      if (run > ntxlim) {
        gSchedule[i] = 0;
        run = 0;
      }
    } else {
      run = 0;
    }
  }
}

bool txAddToBand(int band) {
  for (int i = 0; i < 10; ++i) {
    const int slot = std::rand() % 6;
    if (!gSchedule[slot * 10 + band]) {
      gSchedule[slot * 10 + band] = 1;
      return true;
    }
  }
  return false;
}

bool txAddOne() {
  for (int ngap = 13; ngap >= 3; ngap -= 2) {
    for (int i = 0; i < 60 - ngap; ++i) {
      bool busy = false;
      for (int j = 0; j < ngap; ++j) {
        if (gSchedule[i + j])
          busy = true;
      }
      if (!busy) {
        gSchedule[i + ngap / 2] = 1;
        return true;
      }
    }
  }
  if (!gSchedule[58] && !gSchedule[59]) {
    gSchedule[59] = 1;
    return true;
  }
  return false;
}

void createSchedule(int pctx) {
  std::memset(gSchedule, 0, sizeof gSchedule);
  if (pctx <= 0)
    return;
  if (pctx <= 25) {
    const float rxavg = 100.0f / float(pctx) - 1.0f;
    int i = 0;
    while (true) {
      const float x = float(std::rand() % 100) / 100.0f;
      const int nrx = int(rxavg + 3.0f * x - 1.0f);
      i = i + nrx + 1;
      if (i < 60)
        gSchedule[i] = 1;
      else
        break;
    }
    return;
  }
  int ntxlim = 3;
  int ntxbandmin = 4;
  if (pctx < 33) {
    ntxlim = 1;
    ntxbandmin = 1;
  } else if (pctx < 50) {
    ntxlim = 1;
    ntxbandmin = 2;
  } else if (pctx < 60) {
    ntxlim = 2;
    ntxbandmin = 3;
  }
  for (int i = 0; i < ntxbandmin; ++i) {
    for (int band = 0; band < 10; ++band)
      txAddToBand(band);
  }
  txTrim(ntxlim);
  for (int pass = 0; pass < 100; ++pass) {
    int bandSum[10] = {};
    for (int row = 0; row < 6; ++row) {
      for (int band = 0; band < 10; ++band)
        bandSum[band] += gSchedule[row * 10 + band];
    }
    bool done = true;
    for (int band = 0; band < 10; ++band) {
      if (bandSum[band] < ntxbandmin) {
        done = false;
        for (int k = 0; k < ntxbandmin - bandSum[band]; ++k)
          txAddToBand(band);
      }
    }
    txTrim(ntxlim);
    if (done)
      break;
  }
  const int needed = int(60.0 * (double(pctx) / 100.0) + 0.5);
  int sum = txSum();
  for (int j = 0; j < needed - sum; ++j)
    txAddOne();
}

}  // namespace

QString wsprPowerLabel(int dbm) {
  const struct {
    int dbm;
    const char* watts;
  } rows[] = {
      {0, "1 mW"},   {3, "2 mW"},   {7, "5 mW"},    {10, "10 mW"}, {13, "20 mW"}, {17, "50 mW"},
      {20, "100 mW"}, {23, "200 mW"}, {27, "500 mW"}, {30, "1 W"},   {33, "2 W"},   {37, "5 W"},
      {40, "10 W"},  {43, "20 W"},  {47, "50 W"},   {50, "100 W"}, {53, "200 W"}, {57, "500 W"},
      {60, "1 kW"},
  };
  for (const auto& row : rows) {
    if (row.dbm == dbm)
      return QStringLiteral("%1 dBm (%2)").arg(dbm).arg(QLatin1String(row.watts));
  }
  const double watts = std::pow(10.0, (dbm - 30) / 10.0);
  if (watts >= 1.0)
    return QStringLiteral("%1 dBm (%2 W)").arg(dbm).arg(watts, 0, 'f', watts >= 10.0 ? 0 : 1);
  return QStringLiteral("%1 dBm (%2 mW)").arg(dbm).arg(int(std::lround(watts * 1000.0)));
}

namespace {

QString activityCell(const QString& text, int width) {
  const QString cut = text.size() > width ? text.left(width) : text;
  return cut.leftJustified(width, QLatin1Char(' '));
}

bool activityGrid(const QString& token) {
  static const QRegularExpression re(QStringLiteral("^[A-R]{2}[0-9]{2}(?:[A-X]{2})?$"));
  return re.match(token.toUpper()).hasMatch();
}

}  // namespace

bool wsprSplitMessage(const QString& message, QString* call, QString* grid, int* dbm) {
  if (call)
    call->clear();
  if (grid)
    grid->clear();
  if (dbm)
    *dbm = 0;
  const QStringList words = message.trimmed().toUpper().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (words.isEmpty())
    return false;
  int end = words.size();
  bool powerOk = false;
  const int power = words.last().toInt(&powerOk);
  if (powerOk && power >= 0 && power <= 60) {
    if (dbm)
      *dbm = power;
    --end;
  }
  if (end > 0 && activityGrid(words.at(end - 1))) {
    if (grid)
      *grid = words.at(end - 1);
    --end;
  }
  if (end <= 0)
    return false;
  if (call) {
    QStringList parts;
    for (int i = 0; i < end; ++i)
      parts << words.at(i);
    *call = parts.join(QLatin1Char(' '));
  }
  return true;
}

QString wsprActivityHeader() {
  return activityCell(QStringLiteral("Time"), 6) + activityCell(QStringLiteral("dB"), 4) +
         activityCell(QStringLiteral("DT"), 6) + activityCell(QStringLiteral("Frequency"), 12) +
         activityCell(QStringLiteral("Drift"), 6) + activityCell(QStringLiteral("Call"), 14) +
         activityCell(QStringLiteral("Locator"), 8) + activityCell(QStringLiteral("Report"), 6);
}

QString wsprActivityLine(const QString& time, int snrDb, double dtSec, const QString& freqMhz, int drift,
                         const QString& message) {
  QString call;
  QString grid;
  int dbm = 0;
  wsprSplitMessage(message, &call, &grid, &dbm);
  const bool havePower = message.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts).size() >= 2;
  QString report;
  if (havePower) {
    bool ok = false;
    message.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts).last().toInt(&ok);
    if (ok)
      report = QString::number(dbm);
  }
  return activityCell(time, 6) + activityCell(QString::number(snrDb), 4) +
         activityCell(QString::number(dtSec, 'f', 1), 6) + activityCell(freqMhz, 12) +
         activityCell(QString::number(drift), 6) + activityCell(call, 14) + activityCell(grid, 8) +
         activityCell(report, 6);
}

std::vector<float> wsprModulate(const QString& message, int toneHz) {
  unsigned char symbols[kSymbols];
  if (!symbolsFor(message, symbols))
    return {};
  constexpr int kRate = 48000;
  const int nsym = symbolCount(kRate);
  const int lead = kRate;
  std::vector<float> out(static_cast<size_t>(lead + kSymbols * nsym), 0.0f);
  const int ramp = std::min(384, nsym / 8);
  double phase = 0.0;
  for (int s = 0; s < kSymbols; ++s) {
    const double freq = double(toneHz) + (double(symbols[s]) - 1.5) * kDf;
    const double dphi = 2.0 * 3.141592653589793 * freq / double(kRate);
    float* dst = out.data() + lead + s * nsym;
    for (int i = 0; i < nsym; ++i) {
      float amp = 0.8f;
      if (s == 0 && i < ramp)
        amp *= float(i) / float(ramp);
      if (s == kSymbols - 1 && i >= nsym - ramp)
        amp *= float(nsym - 1 - i) / float(ramp);
      dst[i] = amp * float(std::sin(phase));
      phase += dphi;
      if (phase > 6.283185307179586)
        phase -= 6.283185307179586;
    }
  }
  return out;
}

std::vector<WsprSpot> wsprDecodePeriod(const float* samples, int count, int sampleRateHz, int depth,
                                       WsprHashTable* hash) {
  if (!samples || count < sampleRateHz || sampleRateHz < 8000)
    return {};
  std::vector<float> id;
  std::vector<float> qd;
  downconvert(samples, count, sampleRateHz, id, qd);
  return decodeBaseband(id, qd, depth, hash);
}

int wsprCallHash(const QString& call) {
  return wsprHash(call.toLatin1());
}

bool wsprOsdSelfTest() {
  // Encode a message, make every symbol weak, push the weakest 20 across
  // zero, and check that OSD depth 4 returns the sent codeword.
  unsigned char symbols[kSymbols];
  if (!symbolsFor(QStringLiteral("K1JT FN20 30"), symbols))
    return false;
  unsigned char channel[kSymbols];
  for (int i = 0; i < kSymbols; ++i)
    channel[i] = static_cast<unsigned char>((symbols[i] - kSync[i]) / 2);
  deinterleave(channel);
  float soft[kSymbols];
  uint32_t seed = 12345u;
  for (int i = 0; i < kSymbols; ++i) {
    seed = seed * 1664525u + 1013904223u;
    const float mag = 10.0f + float(seed % 90u);
    soft[i] = channel[i] ? mag : -mag;
  }
  for (int i = 0; i < 20; ++i) {
    const int k = (i * 37 + 11) % kSymbols;
    soft[k] = channel[k] ? -4.0f : 4.0f;
  }
  WsprOsd osd;
  unsigned char cw[kSymbols];
  osd.decode(soft, 4, cw);
  for (int i = 0; i < kSymbols; ++i) {
    if (cw[i] != channel[i])
      return false;
  }
  return true;
}

QString wsprHashTablePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + QStringLiteral("/wspr_hashtable.txt");
}

// Same format as the WSJT-X hashtable.txt: "%5d CALL GRID".
void wsprLoadHashTable(WsprHashTable* hash) {
  if (!hash)
    return;
  QFile file(wsprHashTablePath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return;
  while (!file.atEnd()) {
    const QStringList parts =
        QString::fromLatin1(file.readLine()).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 2)
      continue;
    bool ok = false;
    const int n = parts.at(0).toInt(&ok);
    if (!ok || n < 0 || n > 32767)
      continue;
    hash->call[size_t(n)] = parts.at(1);
    if (parts.size() > 2)
      hash->grid[size_t(n)] = parts.at(2);
  }
}

void wsprSaveHashTable(const WsprHashTable& hash) {
  QSaveFile file(wsprHashTablePath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return;
  for (size_t i = 0; i < hash.call.size(); ++i) {
    if (hash.call[i].isEmpty())
      continue;
    file.write(QStringLiteral("%1 %2 %3\n").arg(int(i), 5).arg(hash.call[i], hash.grid[i]).toLatin1());
  }
  file.commit();
}

QString wsprDecode(const float* samples, int count, int sampleRateHz) {
  const std::vector<WsprSpot> spots = wsprDecodePeriod(samples, count, sampleRateHz);
  return spots.empty() ? QString() : spots.front().message;
}

int wsprClampTxHz(int hz) {
  return std::clamp(hz, kWsprTxLowHz, kWsprTxHighHz);
}

bool wsprTxThisSlot(int txPercent) {
  const QDateTime utc = QDateTime::currentDateTimeUtc().addSecs(30);
  const int hour = utc.time().hour();
  const int minute = utc.time().minute();
  const int twoHour = hour / 2;
  const int row = (hour - twoHour * 2) * 3 + minute / 20;
  const int col = (minute % 20) / 2;
  const int pct = std::clamp(txPercent, 0, 100);
  if (twoHour != gScheduleSlot || pct != gSchedulePct) {
    createSchedule(pct);
    gScheduleSlot = twoHour;
    gSchedulePct = pct;
  }
  if (row < 0 || row > 5 || col < 0 || col > 9)
    return false;
  return gSchedule[row * 10 + col] != 0;
}

int wsprBandIndex(int utcMinute) {
  int minute = utcMinute % 60;
  if (minute < 0)
    minute += 60;
  return (minute % 20) / 2;
}

qint64 wsprDialHz(int bandIndex) {
  if (bandIndex < 0 || bandIndex > 9)
    return 0;
  return kBands[bandIndex].dial;
}

QString wsprBandName(int bandIndex) {
  if (bandIndex < 0 || bandIndex > 9)
    return {};
  return QString::fromLatin1(kBands[bandIndex].name);
}

int wsprGrayPeriod(const QString& grid, int grayMinutes, const QDateTime& utc) {
  double lat = 0.0;
  double lon = 0.0;
  if (!gridLatLon(grid, &lat, &lon))
    return 1;
  const int doy = utc.date().dayOfYear();
  const double b = 2.0 * 3.141592653589793 * double(doy - 81) / 365.0;
  const double eot = 9.87 * std::sin(2.0 * b) - 7.53 * std::cos(b) - 1.5 * std::sin(b);
  const double decl = 23.45 * 3.141592653589793 / 180.0 * std::sin(b);
  const double latr = lat * 3.141592653589793 / 180.0;
  const double arg = -std::tan(latr) * std::tan(decl);
  if (arg <= -1.0)
    return 1;
  if (arg >= 1.0)
    return 3;
  const double ha = std::acos(arg) * 180.0 / 3.141592653589793 / 15.0;
  const double noon = wrap24(12.0 - lon / 15.0 - eot / 60.0);
  const double sunrise = wrap24(noon - ha);
  const double sunset = wrap24(noon + ha);
  const double now = utc.time().hour() + utc.time().minute() / 60.0 + utc.time().second() / 3600.0;
  const double window = std::clamp(grayMinutes, 1, 120) / 120.0;
  auto apart = [](double a, double b) {
    double d = std::fmod(std::fabs(a - b), 24.0);
    if (d > 12.0)
      d = 24.0 - d;
    return d;
  };
  if (apart(now, sunrise) <= window)
    return 0;
  if (apart(now, sunset) <= window)
    return 2;
  bool day = false;
  if (sunrise <= sunset)
    day = now >= sunrise && now < sunset;
  else
    day = now >= sunrise || now < sunset;
  return day ? 1 : 3;
}

int wsprPickBand(int period, int coordinated, bool wantTx, const bool bits[60]) {
  if (!bits || coordinated < 0 || coordinated > 9)
    return -1;
  if (period < 0 || period > 3)
    period = 1;
  if (bits[period * 10 + coordinated])
    return coordinated;
  int choices[10];
  int n = 0;
  for (int i = 0; i < 10; ++i) {
    if (!bits[period * 10 + i])
      continue;
    const bool rxOnly = bits[50 + i];
    if (wantTx && rxOnly)
      continue;
    if (!wantTx && !rxOnly)
      continue;
    choices[n++] = i;
  }
  if (n == 0) {
    for (int i = 0; i < 10; ++i) {
      if (bits[period * 10 + i])
        choices[n++] = i;
    }
  }
  if (n == 0)
    return -1;
  if (n == 1)
    return choices[0];
  return choices[std::rand() % n];
}

bool wsprTuneBand(int bandIndex, const bool bits[60]) {
  return bits && bandIndex >= 0 && bandIndex < 10 && bits[40 + bandIndex];
}

void wsprLoadBands(bool bits[60]) {
  std::fill(bits, bits + 60, false);
  const QString saved = QSettings().value(QStringLiteral("wspr/phases")).toString();
  for (int i = 0; i < 60 && i < saved.size(); ++i)
    bits[i] = saved.at(i) == QLatin1Char('1');
}

int wsprGrayMinutes() {
  return std::clamp(QSettings().value(QStringLiteral("wspr/grayMin"), 60).toInt(), 1, 120);
}

void showWsprBandHopping(QWidget* parent) {
  if (!parent)
    return;
  if (auto* open = parent->findChild<QDialog*>(QStringLiteral("wsprSchedule"))) {
    open->show();
    open->raise();
    open->activateWindow();
    return;
  }
  auto* dialog = new QDialog(parent);
  dialog->setObjectName(QStringLiteral("wsprSchedule"));
  dialog->setWindowTitle(QStringLiteral("WSPR Band Hopping"));
  dialog->setModal(false);
  auto* layout = new QVBoxLayout(dialog);
  auto* table = new QTableWidget(6, 10, dialog);
  table->setObjectName(QStringLiteral("wsprHopTable"));
  bool bits[60];
  wsprLoadBands(bits);
  for (int col = 0; col < 10; ++col) {
    auto* header = new QTableWidgetItem(wsprBandName(col));
    table->setHorizontalHeaderItem(col, header);
  }
  for (int row = 0; row < 6; ++row) {
    auto* header = new QTableWidgetItem(QString::fromLatin1(kPeriods[row]));
    header->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table->setVerticalHeaderItem(row, header);
    for (int col = 0; col < 10; ++col) {
      auto* item = new QTableWidgetItem;
      item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
      item->setCheckState(bits[row * 10 + col] ? Qt::Checked : Qt::Unchecked);
      item->setBackground(QColor(QStringLiteral("#f6e27a")));
      table->setItem(row, col, item);
    }
  }
  table->resizeColumnsToContents();
  table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  layout->addWidget(table);
  auto* form = new QFormLayout;
  auto* gray = new QSpinBox(dialog);
  gray->setObjectName(QStringLiteral("wsprGrayTime"));
  gray->setRange(1, 120);
  gray->setSuffix(QStringLiteral("min"));
  gray->setValue(wsprGrayMinutes());
  gray->setToolTip(QStringLiteral(
      "Gray time, as in WSJT-X and JTDX. Sunrise and sunset use the locator from Options → User."));
  form->addRow(QStringLiteral("Gray time:"), gray);
  layout->addLayout(form);
  auto save = [table, gray] {
    QString phases(60, QLatin1Char('0'));
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 10; ++col) {
        const auto* item = table->item(row, col);
        if (item && item->checkState() == Qt::Checked)
          phases[row * 10 + col] = QLatin1Char('1');
      }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("wspr/phases"), phases);
    settings.setValue(QStringLiteral("wspr/grayMin"), gray->value());
  };
  QObject::connect(table, &QTableWidget::itemChanged, dialog, [save](QTableWidgetItem*) { save(); });
  QObject::connect(gray, qOverload<int>(&QSpinBox::valueChanged), dialog, [save](int) { save(); });
  dialog->resize(720, 280);
  dialog->show();
}

WsprEngine::WsprEngine(QObject* parent) : QObject(parent) {
  wsprLoadHashTable(&hash_);
}

WsprEngine::~WsprEngine() {
  if (worker_) {
    worker_->wait();
    delete worker_;
  }
}

void WsprEngine::setArmed(bool armed) {
  if (armed_ == armed)
    return;
  armed_ = armed;
  clear();
}

void WsprEngine::clear() {
  pending_.clear();
  epochMs_ = 0;
  decodedPeriod_ = -1;
}

void WsprEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!armed_ || !samples || count < 1 || sampleRateHz < 8000)
    return;
  if (sampleRateHz != rate_) {
    pending_.clear();
    rate_ = sampleRateHz;
  }
  // epochMs_ is the wall-clock time of pending_[0].
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (pending_.empty())
    epochMs_ = nowMs - qint64(count) * 1000 / rate_;
  pending_.insert(pending_.end(), samples, samples + count);
  const int cap = rate_ * 130;
  if (int(pending_.size()) > cap) {
    const int drop = int(pending_.size()) - cap;
    pending_.erase(pending_.begin(), pending_.begin() + drop);
    epochMs_ += qint64(drop) * 1000 / rate_;
  }
  // Decode once 114 s of the 2-minute period are in, as WSJT-X does.
  const qint64 periodStart = (nowMs / 120000) * 120000;
  const qint64 into = nowMs - periodStart;
  if (into < 114000)
    return;
  const int period = int(periodStart / 120000);
  if (period == decodedPeriod_)
    return;
  if (worker_ && worker_->isRunning())
    return;
  decodedPeriod_ = period;
  const qint64 offset = (periodStart - epochMs_) * rate_ / 1000;
  const int want = rate_ * 114;
  std::vector<float> slice(static_cast<size_t>(want), 0.0f);
  int filled = 0;
  for (int i = 0; i < want; ++i) {
    const qint64 src = offset + i;
    if (src >= 0 && src < qint64(pending_.size())) {
      slice[size_t(i)] = pending_[size_t(src)];
      ++filled;
    }
  }
  // WSJT-X skips a period that holds less than about half the transmission.
  if (filled < rate_ * 60)
    return;
  if (worker_) {
    worker_->wait();
    delete worker_;
    worker_ = nullptr;
  }
  const int rate = rate_;
  const int depth = std::clamp(QSettings().value(QStringLiteral("wspr/depth"), 2).toInt(), 1, 3);
  QPointer<WsprEngine> self(this);
  auto table = std::make_shared<WsprHashTable>(hash_);
  worker_ = QThread::create([self, slice = std::move(slice), rate, periodStart, depth, table]() {
    const std::vector<WsprSpot> spots = wsprDecodePeriod(slice.data(), int(slice.size()), rate, depth, table.get());
    // The engine waits for this thread before it is destroyed.
    QMetaObject::invokeMethod(
        self.data(),
        [self, spots, periodStart, table]() {
          if (!self)
            return;
          if (!spots.empty()) {
            self->hash_ = *table;
            wsprSaveHashTable(self->hash_);
          }
          for (const WsprSpot& spot : spots)
            emit self->spotDecoded(spot, periodStart);
          emit self->periodDecoded(int(spots.size()), periodStart);
        },
        Qt::QueuedConnection);
  });
  worker_->start(QThread::LowPriority);
}
