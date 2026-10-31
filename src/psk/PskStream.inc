// Streaming PSK receiver, from fldigi psk::rx_process / rx_symbol / rx_bit
// (Dave Freese W1HKJ, GPL-3). One carrier. 48 kHz audio is decimated to
// 8 kHz, mixed to baseband, then run through the same two FIRs fldigi
// builds in psk::init: a decimator down to 16 samples/symbol and a matched
// filter. The bit clock tracks the symbol envelope. Each character is
// emitted once, when its 00 arrives, and only while DCD is open.

constexpr int kPskFirLen = 64;
constexpr double kPi = 3.141592653589793;

double pskDecay(double average, double input, double weight) {
  if (weight <= 1.0)
    return input;
  return average + (input - average) / weight;
}

std::vector<double> pskRaisedCos(int len) {
  std::vector<double> taps(static_cast<size_t>(len + 1));
  const double k1 = 2.0 * kPi / double(len);
  const double k2 = 2.0 * double(len);
  for (int i = 0; i <= len; ++i)
    taps[static_cast<size_t>(i)] = (1.0 - std::cos(k1 * double(i))) / k2;
  return taps;
}

std::vector<double> pskWSinc(double fc, int len) {
  std::vector<double> taps(static_cast<size_t>(len + 1));
  const double k1 = 2.0 * kPi / double(len);
  const double k2 = 2.0 * kPi * fc;
  const double mid = double(len) / 2.0;
  double norm = 0.0;
  for (int i = 0; i <= len; ++i) {
    const double x = double(i) - mid;
    double v = (i == int(mid)) ? 1.0 : std::sin(k2 * x) / (k2 * x);
    v *= 0.42 - 0.5 * std::cos(k1 * double(i)) + 0.08 * std::cos(2.0 * k1 * double(i));
    taps[static_cast<size_t>(i)] = v;
    norm += v;
  }
  if (norm != 0.0) {
    for (double& v : taps)
      v /= norm;
  }
  return taps;
}

// gmfsk / fldigi pskcore_filter, 65 taps. Symmetric, so tap direction
// does not change the baseband.
const double kPskCore[kPskFirLen + 1] = {
    4.3453566e-005,  -0.00049122414, -0.00078771292, -0.0013507826,  -0.0021287814, -0.003133466,
    -0.004366817,   -0.0058112187,  -0.0074249976,  -0.0091398882,  -0.010860157,  -0.012464086,
    -0.013807772,   -0.014731191,   -0.015067057,   -0.014650894,   -0.013333425,  -0.01099166,
    -0.0075431246,  -0.0029527849,  0.0027546292,   0.0094932775,   0.017113308,   0.025403511,
    0.034099681,    0.042895839,    0.051458575,    0.059444853,    0.066521003,   0.072381617,
    0.076767694,    0.079481619,    0.080420311,    0.079481619,    0.076767694,   0.072381617,
    0.066521003,    0.059444853,    0.051458575,    0.042895839,    0.034099681,   0.025403511,
    0.017113308,    0.0094932775,   0.0027546292,   -0.0029527849,  -0.0075431246, -0.01099166,
    -0.013333425,   -0.014650894,   -0.015067057,   -0.014731191,   -0.013807772,  -0.012464086,
    -0.010860157,   -0.0091398882,  -0.0074249976,  -0.0058112187,  -0.004366817,  -0.003133466,
    -0.0021287814,  -0.0013507826,  -0.00078771292, -0.00049122414, 4.3453566e-005};

struct PskCmpFir {
  std::vector<double> taps;
  std::vector<double> bi;
  std::vector<double> bq;
  int pos = 0;
  int dec = 1;
  int got = 0;

  void init(const std::vector<double>& inTaps, int decimate) {
    taps = inTaps;
    bi.assign(taps.size(), 0.0);
    bq.assign(taps.size(), 0.0);
    pos = 0;
    dec = std::max(1, decimate);
    got = 0;
  }

  bool run(double i, double q, double* oi, double* oq) {
    if (taps.empty())
      return false;
    bi[static_cast<size_t>(pos)] = i;
    bq[static_cast<size_t>(pos)] = q;
    pos = (pos + 1) % int(bi.size());
    if (++got < dec)
      return false;
    got = 0;
    double si = 0.0;
    double sq = 0.0;
    int p = pos;
    const int n = int(taps.size());
    for (int k = 0; k < n; ++k) {
      si += taps[static_cast<size_t>(k)] * bi[static_cast<size_t>(p)];
      sq += taps[static_cast<size_t>(k)] * bq[static_cast<size_t>(p)];
      p = (p + 1) % n;
    }
    *oi = si;
    *oq = sq;
    return true;
  }
};

struct PskStream {
  bool qpsk = false;
  bool ready = false;
  bool squelchOn = false;
  bool afcOn = false;
  int squelch = 10;
  int center = 1500;
  double tuneHz = 1500.0;
  int trackHz = -1;
  int rate = 48000;
  int symbollen = 256;
  int bitsteps = 16;
  int dcdBits = 32;
  double phase = 0.0;
  double step = 0.0;
  double bitclk = 0.0;
  double syncbuf[16] = {};
  double histI[16] = {};
  double histQ[16] = {};
  double prevI = 0.0;
  double prevQ = 0.0;
  double qualI = 0.0;
  double qualQ = 0.0;
  double metric = 0.0;
  double afcMetric = 0.0;
  bool havePrev = false;
  bool dcd = false;
  unsigned shreg = 0;
  int dcdOff = 0;
  int zeros = 0;
  QString word;
  QString emitted;
  QpskViterbi vit;
  PskCmpFir fir1;
  PskCmpFir fir2;
  int decimFactor = -1;
  std::vector<double> decimTaps;
  std::vector<double> decimHist;
  int decimPos = 0;
  int decimPhase = 0;
  double bucket = 0.0;
  int bucketCount = 0;
  double held = 0.0;

  void setSquelch(bool on, int level) {
    if (level < 0)
      level = 0;
    if (level > 100)
      level = 100;
    squelchOn = on;
    squelch = level;
  }

  void setCenter(int hz) {
    center = std::clamp(hz, 200, 3000);
    tuneHz = double(center);
    step = 2.0 * kPi * tuneHz / 8000.0;
    trackHz = -1;
  }

  // fldigi psk::phaseafc. Residual phase error, in Hz, pulled in by 1/dcdbits
  // per symbol. PSK500 and QPSK500 are skipped, same as fldigi. The capture
  // window is one baud (samplerate / symbollen).
  void phaseAfc(double ang, int bits) {
    // fldigi also requires afcmetric >= 0.05. With squelch off, DCD stays
    // true, and without this gate the loop chases noise.
    if (!afcOn || !dcd || symbollen == 16 || afcMetric < 0.05)
      return;
    double error = ang - double(bits) * (kPi / 2.0);
    if (error < -kPi / 2.0 || error > kPi / 2.0)
      return;
    error *= 8000.0 / (2.0 * kPi * double(symbollen));
    if (std::fabs(error) >= 8000.0 / double(symbollen))
      return;
    tuneHz -= error / double(std::max(1, dcdBits));
    if (tuneHz < 200.0)
      tuneHz = 200.0;
    if (tuneHz > 3000.0)
      tuneHz = 3000.0;
    step = 2.0 * kPi * tuneHz / 8000.0;
    const int shown = int(std::lround(tuneHz));
    if (shown != center) {
      center = shown;
      trackHz = shown;
    }
  }

  bool configure(const QString& mode, int sampleRateHz, int centerHz) {
    const double bpsk = pskBaud(mode);
    const double qb = qpskBaud(mode);
    const double baud = bpsk > 0.0 ? bpsk : qb;
    if (baud <= 0.0 || sampleRateHz < 1000)
      return false;
    qpsk = qb > 0.0;
    if (baud >= 900.0)
      symbollen = 8;
    else if (baud >= 400.0)
      symbollen = 16;
    else if (baud >= 200.0)
      symbollen = 32;
    else if (baud >= 100.0)
      symbollen = 64;
    else if (baud >= 50.0)
      symbollen = 128;
    else
      symbollen = 256;
    bitsteps = symbollen >= 16 ? 16 : symbollen;
    dcdBits = qpsk ? std::max(32, int(std::lround(baud * 32.0 / 31.25))) : bpskDcdBits(baud);
    const bool core = symbollen >= 128;
    const int dec = symbollen > 15 ? symbollen / 16 : 1;
    std::vector<double> t1;
    std::vector<double> t2;
    if (core) {
      t1 = pskRaisedCos(kPskFirLen);
      t2.assign(kPskCore, kPskCore + kPskFirLen + 1);
      fir1.init(t1, dec);
      fir2.init(t2, 1);
    } else {
      t1 = pskWSinc(1.0 / double(symbollen), kPskFirLen);
      t2 = pskWSinc(1.0 / 16.0, kPskFirLen);
      t1.pop_back();
      t2.pop_back();
      fir1.init(t1, dec);
      fir2.init(t2, 1);
    }
    rate = sampleRateHz;
    setCenter(centerHz);
    resetState();
    armDecimator();
    ready = true;
    return true;
  }

  void resetState() {
    phase = 0.0;
    bitclk = 0.0;
    for (double& s : syncbuf)
      s = 0.0;
    prevI = prevQ = 0.0;
    qualI = qualQ = 0.0;
    metric = 0.0;
    afcMetric = 0.0;
    havePrev = false;
    dcd = false;
    shreg = 0;
    dcdOff = 0;
    zeros = 0;
    word.clear();
    emitted.clear();
    vit = QpskViterbi();
    bucket = 0.0;
    bucketCount = 0;
    held = 0.0;
    decimPhase = 0;
  }

  void armDecimator() {
    decimFactor = (rate % 8000 == 0) ? rate / 8000 : 0;
    if (decimFactor > 1) {
      const int taps = 16 * decimFactor + 1;
      const int half = taps / 2;
      const double fc = 3600.0 / (8000.0 * double(decimFactor));
      decimTaps.assign(static_cast<size_t>(taps), 0.0);
      double sum = 0.0;
      for (int k = 0; k < taps; ++k) {
        const double u = double(k - half);
        const double s = u == 0.0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * u) / (kPi * u);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * double(k) / double(taps - 1)) +
                         0.08 * std::cos(4.0 * kPi * double(k) / double(taps - 1));
        decimTaps[static_cast<size_t>(k)] = s * w;
        sum += s * w;
      }
      if (sum != 0.0) {
        for (double& v : decimTaps)
          v /= sum;
      }
      decimHist.assign(decimTaps.size(), 0.0);
      decimPos = 0;
    } else {
      decimTaps.clear();
      decimHist.clear();
    }
    decimPhase = 0;
  }

  void rxBit(int bit) {
    if (bit == 0) {
      ++zeros;
      if (zeros == 2) {
        emitted += lookup(word);
        word.clear();
        zeros = 0;
      }
      return;
    }
    if (zeros == 1)
      word.append(QLatin1Char('0'));
    zeros = 0;
    word.append(QLatin1Char('1'));
  }

  void rxSymbol(double zi, double zq) {
    if (!havePrev) {
      prevI = zi;
      prevQ = zq;
      havePrev = true;
      return;
    }
    const double dot = prevI * zi + prevQ * zq;
    const double cross = prevI * zq - prevQ * zi;
    prevI = zi;
    prevQ = zq;
    double ang = std::atan2(cross, dot);
    if (ang < 0.0)
      ang += 2.0 * kPi;
    const int n = qpsk ? 4 : 2;
    int bits = 0;
    if (qpsk)
      bits = int(std::floor(ang / (kPi / 2.0) + 0.5)) & 3;
    else
      bits = (int(std::floor(ang / kPi + 0.5)) & 1) << 1;

    const double cval = std::cos(double(n) * ang);
    const double sval = std::sin(double(n) * ang);
    qualI = pskDecay(qualI, cval, cval > qualI ? 50.0 : 50.0);
    qualQ = pskDecay(qualQ, sval, sval > qualI ? 50.0 : 50.0);
    metric = 100.0 * (qualI * qualI + qualQ * qualQ);
    if (metric > 100.0)
      metric = 100.0;
    afcMetric = pskDecay(afcMetric, qualI * qualI + qualQ * qualQ, 50.0);

    const int shift = (qpsk ? 2 : 1) + 1;
    shreg = ((shreg << shift) | unsigned(bits)) & 0xffffffffu;
    int setDcd = -1;
    if (!qpsk && shreg == 0xAAAAAAAAu)
      setDcd = 1;
    else if (!qpsk && shreg == 0u)
      setDcd = 0;

    if (setDcd == 1) {
      dcd = true;
      dcdOff = 0;
      qualI = 1.0;
      qualQ = 0.0;
    } else if (!squelchOn) {
      dcd = true;
    } else if (setDcd == 0) {
      if (++dcdOff > 5) {
        dcdOff = 0;
        dcd = false;
        qualI = 0.0;
        qualQ = 0.0;
      }
    } else {
      if (dcdOff > 0)
        --dcdOff;
      dcd = metric > double(squelch);
    }

    if (dcd) {
      if (qpsk) {
        const int mapped = (4 - bits) & 3;
        const unsigned char sym0 = (mapped & 1) ? 255 : 0;
        const unsigned char sym1 = (mapped & 2) ? 0 : 255;
        const int packed = vit.decode(sym0, sym1);
        if (packed >= 0) {
          for (int mask = 0x80; mask != 0; mask >>= 1)
            rxBit((packed & mask) ? 1 : 0);
        }
      } else {
        rxBit(bits ? 0 : 1);
      }
    }
    // The rest of a BPSK reversal preamble keeps 0xAAAAAAAA. Clearing the
    // varicode shifter on that pattern drops the reversals so the first
    // real character is not prefixed by a stuck zero.
    if (setDcd == 1) {
      word.clear();
      zeros = 0;
    }
    phaseAfc(ang, bits);
  }

  void on8k(double sample) {
    const double c = std::cos(phase);
    const double s = std::sin(phase);
    phase += step;
    if (phase > 2.0 * kPi)
      phase -= 2.0 * kPi;
    double i = sample * c;
    double q = sample * s;
    if (!fir1.run(i, q, &i, &q))
      return;
    fir2.run(i, q, &i, &q);
    int idx = int(bitclk);
    if (idx < 0)
      idx = 0;
    if (idx >= bitsteps)
      idx = bitsteps - 1;
    const double mag = std::sqrt(i * i + q * q);
    syncbuf[idx] = 0.8 * syncbuf[idx] + 0.2 * mag;
    histI[idx] = i;
    histQ[idx] = q;
    const int half = bitsteps / 2;
    double sum = 0.0;
    double ampsum = 0.0;
    for (int n = 0; n < half; ++n) {
      sum += syncbuf[n] - syncbuf[n + half];
      ampsum += syncbuf[n] + syncbuf[n + half];
    }
    sum = ampsum == 0.0 ? 0.0 : sum / ampsum;
    bitclk -= sum / (5.0 * 16.0 / double(bitsteps));
    bitclk += 1.0;
    if (bitclk < 0.0)
      bitclk += double(bitsteps);
    if (bitclk >= double(bitsteps)) {
      bitclk -= double(bitsteps);
      // The bit clock wraps a couple of samples before the QPSK constellation
      // has settled when the symbol is only 32 bins long (QPSK250 / QPSK500).
      // Two samples later is still inside the symbol and on the point.
      // QPSK250's 32-bin symbol settles two samples after the bit-clock wrap.
      // The other speeds are already on the constellation at the wrap.
      if (qpsk && symbollen == 32) {
        const int pick = (idx + 2) % bitsteps;
        rxSymbol(histI[pick], histQ[pick]);
      } else {
        rxSymbol(i, q);
      }
    }
  }

  QString push(const float* samples, int count) {
    emitted.clear();
    if (!ready || !samples || count < 1)
      return {};
    if (decimFactor == 1) {
      for (int n = 0; n < count; ++n)
        on8k(samples[n]);
      return emitted;
    }
    if (decimFactor > 1) {
      const int taps = int(decimTaps.size());
      for (int n = 0; n < count; ++n) {
        decimHist[static_cast<size_t>(decimPos)] = samples[n];
        decimPos = (decimPos + 1) % taps;
        if (++decimPhase < decimFactor)
          continue;
        decimPhase = 0;
        double acc = 0.0;
        int p = decimPos;
        for (int k = 0; k < taps; ++k) {
          acc += decimTaps[static_cast<size_t>(k)] * decimHist[static_cast<size_t>(p)];
          p = (p + 1) % taps;
        }
        on8k(acc);
      }
      return emitted;
    }
    const double ratio = 8000.0 / double(rate);
    for (int n = 0; n < count; ++n) {
      bucket += samples[n];
      ++bucketCount;
      held += ratio;
      if (held >= 1.0) {
        on8k(bucket / double(std::max(1, bucketCount)));
        bucket = 0.0;
        bucketCount = 0;
        held -= 1.0;
      }
    }
    return emitted;
  }
};
