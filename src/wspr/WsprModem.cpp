// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// WSPR-2, rewritten from the WSJT-X sources that JTDX-improved ships.
// GPL-3. Not MSHV. The callsign and the locator are not stored here.

#include "wspr/WsprModem.h"

#include "DigitalMode.h"

#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

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

int bestTone(const float* x, int n, int toneHz, int rate) {
  double best = -1.0;
  int which = 0;
  for (int k = 0; k < 4; ++k) {
    const double freq = double(toneHz) + (double(k) - 1.5) * kDf;
    const double w = 2.0 * 3.141592653589793 * freq / double(rate);
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = 0; i < n; ++i) {
      s0 = double(x[i]) + coeff * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    const double energy = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    if (energy > best) {
      best = energy;
      which = k;
    }
  }
  return which;
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

QString unpackMessage(const unsigned char data[11]) {
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
    if (nu == 0 || nu == 3 || nu == 7)
      return formatMessage(call, grid, ntype);
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
    return QStringLiteral("%1 %2").arg(call).arg(dbm);
  }
  return {};
}

bool hardDecode(const unsigned char* channel, unsigned char data[11]) {
  uint32_t state = 0;
  unsigned char bits[81];
  for (int i = 0; i < 81; ++i) {
    const int s0 = channel[2 * i];
    const int s1 = channel[2 * i + 1];
    const uint32_t zero = state << 1;
    const uint32_t one = zero | 1u;
    const bool z = parity32(zero & kPoly1) == s0 && parity32(zero & kPoly2) == s1;
    const bool o = parity32(one & kPoly1) == s0 && parity32(one & kPoly2) == s1;
    if (z == o)
      return false;
    bits[i] = o ? 1 : 0;
    state = o ? one : zero;
  }
  for (int i = 50; i < 81; ++i) {
    if (bits[i] != 0)
      return false;
  }
  std::memset(data, 0, 11);
  for (int i = 0; i < 50; ++i) {
    if (bits[i])
      data[i / 8] = static_cast<unsigned char>(data[i / 8] | (1u << (7 - (i % 8))));
  }
  return true;
}

QString decodeAt(const float* samples, int count, int rate, int toneHz) {
  const int nsym = symbolCount(rate);
  if (nsym < 64 || count < kSymbols * nsym)
    return {};
  unsigned char symbols[kSymbols];
  unsigned char channel[kSymbols];
  for (int i = 0; i < kSymbols; ++i) {
    symbols[i] = static_cast<unsigned char>(bestTone(samples + i * nsym, nsym, toneHz, rate));
    const int bit = int(symbols[i]) - int(kSync[i]);
    if (bit != 0 && bit != 2)
      return {};
    channel[i] = static_cast<unsigned char>(bit / 2);
  }
  deinterleave(channel);
  unsigned char data[11];
  if (!hardDecode(channel, data))
    return {};
  return unpackMessage(data);
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

QString wsprDecode(const float* samples, int count, int sampleRateHz, int toneHz) {
  if (!samples || count < 1000 || sampleRateHz < 8000)
    return {};
  const int frame = kSymbols * symbolCount(sampleRateHz);
  const int lead = sampleRateHz;
  if (count >= lead + frame) {
    const QString hit = decodeAt(samples + lead, frame, sampleRateHz, toneHz);
    if (!hit.isEmpty())
      return hit;
  }
  if (count >= frame)
    return decodeAt(samples, frame, sampleRateHz, toneHz);
  return {};
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

WsprEngine::WsprEngine(QObject* parent) : QObject(parent) {}

void WsprEngine::setArmed(bool armed) {
  if (armed_ == armed)
    return;
  armed_ = armed;
  clear();
}

void WsprEngine::setToneHz(int hz) {
  toneHz_ = std::clamp(hz, 200, 3000);
}

void WsprEngine::clear() {
  pending_.clear();
  epochMs_ = 0;
  last_.clear();
  decodedPeriod_ = -1;
}

void WsprEngine::pushAudio(const float* samples, int count, int sampleRateHz) {
  if (!armed_ || !samples || count < 1 || sampleRateHz < 8000)
    return;
  if (pending_.empty())
    epochMs_ = QDateTime::currentMSecsSinceEpoch();
  rate_ = sampleRateHz;
  pending_.insert(pending_.end(), samples, samples + count);
  const int cap = rate_ * 130;
  if (int(pending_.size()) > cap) {
    const int drop = int(pending_.size()) - cap;
    pending_.erase(pending_.begin(), pending_.begin() + drop);
    epochMs_ += qint64(drop) * 1000 / rate_;
  }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const int minute = now.time().minute();
  const int second = now.time().second();
  if ((minute % 2) == 0 || second < 52)
    return;
  const int period = int(now.toSecsSinceEpoch() / 120);
  if (period == decodedPeriod_)
    return;
  const qint64 periodStart = (now.toMSecsSinceEpoch() / 120000) * 120000;
  const int frame = kSymbols * symbolCount(rate_);
  const int center = int((periodStart + 1000 - epochMs_) * rate_ / 1000);
  const int step = std::max(1, symbolCount(rate_) / 4);
  QString hit;
  for (int delta = 0; delta <= step * 8; delta += step) {
    for (int sign : {0, 1, -1}) {
      if (delta == 0 && sign != 0)
        continue;
      if (delta != 0 && sign == 0)
        continue;
      const int offset = center + sign * delta;
      if (offset < 0 || offset + frame > int(pending_.size()))
        continue;
      hit = decodeAt(pending_.data() + offset, frame, rate_, toneHz_);
      if (!hit.isEmpty())
        break;
    }
    if (!hit.isEmpty())
      break;
  }
  decodedPeriod_ = period;
  if (hit.isEmpty() || hit == last_)
    return;
  last_ = hit;
  emit textReady(hit);
}
