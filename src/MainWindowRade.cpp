// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// RADE voice. librade modulates 8 kHz real SSB. FARGAN speaks at 16 kHz and
// the bandwidth expander turns that into 48 kHz for the speaker.
// The microphone and speaker are the Speech devices. End-of-over bits
// carry the Options -> User callsign the way FreeDV sends it.

#include "MainWindow.h"

#include "DigitalMode.h"
#include "ModeProfile.h"
#include "TciClient.h"
#include "rade/RadeScreen.h"
#include "rade/RadeMeters.h"
#include "rade/RadeSpeaker.h"
#include "spot/FreeDvReporter.h"
#include "speech/SpeechInput.h"
#include "WaterfallWidget.h"

#include <QCheckBox>
#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

int radeDrivePercent(QWidget* window) {
  if (auto* slider = window->findChild<QSlider*>(QStringLiteral("radeDrive")))
    return std::clamp(slider->value(), 0, 100);
  return std::clamp(QSettings().value(QStringLiteral("rade/drive"), 70).toInt(), 0, 100);
}

// Extra microphone gain, in dB, before RNNoise and the leveler. The leveler
// itself adds at most 12 dB and stays frozen below -33 LUFS, so a quiet
// Windows level never reaches the speech bar. 0 leaves the mic unchanged.
// Down to -10 cuts it.
int radeMicGainDb(QWidget* window) {
  if (auto* slider = window->findChild<QSlider*>(QStringLiteral("radeMicGain")))
    return std::clamp(slider->value(), -10, 30);
  return std::clamp(QSettings().value(QStringLiteral("rade/micGain"), 0).toInt(), -10, 30);
}

bool radeAutoRxOn(QWidget* window) {
  if (auto* box = window->findChild<QCheckBox*>(QStringLiteral("radeAutoRx")))
    return box->isChecked();
  return QSettings().value(QStringLiteral("rade/autoRx"), true).toBool();
}

bool radeBox(QWidget* window, const char* name, const char* key) {
  if (auto* box = window->findChild<QCheckBox*>(QString::fromLatin1(name)))
    return box->isChecked();
  return QSettings().value(QString::fromLatin1(key), true).toBool();
}

int radeTxLimitSeconds(QWidget* window) {
  if (auto* box = window->findChild<QSpinBox*>(QStringLiteral("radeTxLimit")))
    return std::clamp(box->value(), 0, 180);
  return std::clamp(QSettings().value(QStringLiteral("rade/txLimitSec"), 180).toInt(), 0, 180);
}

bool radeAnalogOn(QWidget* window) {
  if (auto* box = window->findChild<QCheckBox*>(QStringLiteral("radeAnalog")))
    return box->isChecked();
  return QSettings().value(QStringLiteral("rade/analog"), false).toBool();
}

}  // namespace

void MainWindow::applyRadeOptions() {
  const int want = (radeBox(this, "radeDenoise", "rade/denoise") ? 1 : 0) |
                   (radeBox(this, "radeWide", "rade/wide") ? 2 : 0);
  if (want == radeOptions_)
    return;
  radeOptions_ = want;
  rade_.setOptions((want & 1) != 0, (want & 2) != 0);
}

void MainWindow::updateFreedvReporter(bool now) {
  const qint64 ms = QDateTime::currentMSecsSinceEpoch();
  if (!now && ms - freedvReporterMs_ < 500)
    return;
  freedvReporterMs_ = ms;
  const bool rade = profile_ && radeMode(profile_->digitalMode());
  const bool on = rade && tci_ && tci_->isOpen() &&
                  QSettings().value(QStringLiteral("dx/freedvReporterEnabled"), false).toBool();
  if (!freedvReporter_) {
    if (!on)
      return;
    freedvReporter_ = new FreeDvReporter(this);
    connect(freedvReporter_, &FreeDvReporter::status, this, [this](const QString& text) {
      if (profile_ && radeMode(profile_->digitalMode()))
        setTxStatus(text);
    });
    connect(freedvReporter_, &FreeDvReporter::stationsChanged, this, [this] { showFreedvStations(); });
    connect(freedvReporter_, &FreeDvReporter::qsyAsked, this,
            [this](const QString& station, qint64 hz, const QString& note) { onFreedvQsy(station, hz, note); });
  }
  if (on) {
    freedvReporter_->setFrequency(liveDialHz_);
    freedvReporter_->setTransmit(freedvReporterModeName(radeV2(profile_->digitalMode())), txRade_);
    freedvReporter_->setMessage(QSettings().value(QStringLiteral("dx/freedvReporterMessage")).toString());
  }
  freedvReporter_->setActive(on, stationCall(), stationGrid());
}

namespace {

QString scanDialText(qint64 hz) {
  return QString::number(double(hz) / 1e6, 'f', hz % 1000 ? 4 : 3);
}

}  // namespace

QString MainWindow::radeTxLoopback() {
  if (!rade_.load())
    return QStringLiteral("librade did not load");
  if (!rade_.open(false, false))
    return QStringLiteral("rade_open failed");
  rade_.setCallsign(QStringLiteral("N8SDR"));
  rade_.setOptions(true, true);
  // A voiced buzz with a slow envelope, 6 s, fed 20 ms at a time as the
  // microphone does, while the radio takes 20 ms of 48 kHz each tick.
  constexpr int kTick = 320;
  constexpr int kTicks = 300;
  std::vector<float> out;
  radeModem_.clear();
  radeRead_ = 0;
  radePrimed_ = false;
  radeFinished_ = false;
  int reprimes = 0;
  bool wasPrimed = false;
  std::vector<float> modem(65536);
  auto pull = [&] {
    std::vector<float> block(960);
    fillRadeTx(block.data(), 960, 48000);
    if (wasPrimed && !radePrimed_)
      ++reprimes;
    wasPrimed = radePrimed_;
    out.insert(out.end(), block.begin(), block.end());
  };
  for (int t = 0; t < kTicks; ++t) {
    float speech[kTick];
    for (int i = 0; i < kTick; ++i) {
      const int n = t * kTick + i;
      const float time = float(n) / 16000.0f;
      const float phase = time * 140.0f;
      const float frac = phase - std::floor(phase);
      const float env = 0.5f + 0.5f * std::sin(2.0f * 3.14159265f * 2.5f * time);
      speech[i] = 0.3f * env * (frac - 0.5f);
    }
    const int n = rade_.tx(speech, kTick, modem.data(), int(modem.size()));
    if (n < 0)
      return QStringLiteral("rade_tx failed: %1").arg(rade_.error());
    radeModem_.insert(radeModem_.end(), modem.begin(), modem.begin() + n);
    pull();
  }
  const int tail = rade_.finish(modem.data(), int(modem.size()));
  if (tail > 0)
    radeModem_.insert(radeModem_.end(), modem.begin(), modem.begin() + tail);
  radeFinished_ = true;
  for (int i = 0; i < 400 && radeRead_ + 1.0 < double(radeModem_.size()); ++i)
    pull();
  // The 200 ms of silence after the end-of-over.
  out.insert(out.end(), 9600, 0.0f);
  radeModem_.clear();
  radeRead_ = 0;
  radeFinished_ = false;
  double peak = 0.0, energy = 0.0;
  for (float v : out) {
    peak = std::max(peak, double(std::fabs(v)));
    energy += double(v) * double(v);
  }
  const double rms = out.empty() ? 0.0 : std::sqrt(energy / double(out.size()));
  // Decode it the way pushRadeRx does, from a fresh receiver.
  rade_.close();
  if (!rade_.open(false, false))
    return QStringLiteral("rade_open failed for RX");
  RateConvert conv;
  int synced = 0;
  QString heard;
  int heardSeq = rade_.heard(nullptr);
  std::vector<float> speech(65536);
  for (size_t at = 0; at < out.size(); at += 960) {
    const int n = int(std::min<size_t>(960, out.size() - at));
    std::vector<float> m8;
    conv.process(out.data() + at, n, 48000, 8000, m8);
    for (float& v : m8)
      v *= 4.0f;
    if (!m8.empty())
      rade_.rx(m8.data(), int(m8.size()), speech.data(), int(speech.size()));
    if (rade_.sync())
      ++synced;
    QString call;
    const int seq = rade_.heard(&call);
    if (seq != heardSeq) {
      heardSeq = seq;
      heard = call;
    }
  }
  rade_.close();
  const QString levels = QStringLiteral("peak %1, rms %2, %3 restarts")
                             .arg(peak, 0, 'f', 3)
                             .arg(rms, 0, 'f', 3)
                             .arg(reprimes);
  if (reprimes > 0)
    return QStringLiteral("TX ran dry mid-over (%1)").arg(levels);
  if (synced < 50)
    return QStringLiteral("receiver did not sync on STD's TX audio (%1)").arg(levels);
  if (heard != QLatin1String("N8SDR"))
    return QStringLiteral("callsign did not read back, got '%1' (%2)").arg(heard, levels);
  qInfo("RADE TX loopback ok: %s, sync %d of %d blocks", qPrintable(levels), synced,
        int(out.size() / 960));
  return QString();
}

void MainWindow::refreshRadeRxChip() {
  const int state = !txRade_ ? 0 : (radeStopping_ ? 2 : 1);
  if (state == radeRxState_)
    return;
  radeRxState_ = state;
  if (auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen")))
    screen->setTxState(state);
}

void MainWindow::tuneRadeStation(qint64 hz) {
  if (hz < 100000)
    return;
  if (radeScanOn_ && !radeScanTuning_)
    stopRadeScan(QStringLiteral("Scan stopped: a station was chosen."));
  if (profile_)
    profile_->followRadeDial(hz);
  qsyDial(hz);
  setTxStatus(QStringLiteral("Tuned to %1. START was not sent.")
                  .arg(QString::number(double(hz) / 1e6, 'f', hz % 1000 ? 6 : 3)));
}

void MainWindow::showFreedvStations() {
  auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen"));
  if (!screen || !freedvReporter_)
    return;
  screen->showReporter(freedvReporter_->stations());
}

void MainWindow::onFreedvQsy(const QString& station, qint64 hz, const QString& note) {
  if (qEnvironmentVariableIsSet("STD_SELFTEST") || hz < 100000)
    return;
  if (auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen")); screen && screen->ignoreQsy())
    return;
  const QStringList muted = QSettings().value(QStringLiteral("dx/freedvReporterMuteQsy")).toStringList();
  if (muted.contains(station, Qt::CaseInsensitive))
    return;
  QMessageBox box(this);
  box.setWindowTitle(QStringLiteral("Request QSY"));
  QString body = QStringLiteral("%1 asks you to move to %2.")
                     .arg(station, QString::number(double(hz) / 1e6, 'f', 3));
  const QString extra = note.trimmed();
  if (!extra.isEmpty())
    body += QLatin1Char('\n') + extra;
  box.setText(body);
  QPushButton* go = box.addButton(QStringLiteral("Tune there"), QMessageBox::AcceptRole);
  box.addButton(QStringLiteral("Stay"), QMessageBox::RejectRole);
  box.exec();
  if (box.clickedButton() == go)
    tuneRadeStation(hz);
}

void MainWindow::logRadeContact(const QString& station, const QString& grid, int snr, qint64 hz) {
  const QString who = station.trimmed().toUpper();
  if (who.isEmpty()) {
    setTxStatus(QStringLiteral(
        "Nothing logged: pick a station first. Nothing was keyed. START was not sent."));
    return;
  }
  QsoLog::Record record;
  record.call = who;
  record.grid = grid.trimmed().toUpper();
  record.mode = profile_ ? profile_->digitalMode() : QString();
  record.rstSent = QStringLiteral("59");
  record.rstRcvd = QStringLiteral("59");
  record.myCall = stationCall();
  record.myGrid = stationGrid();
  if (snr > -900)
    record.comment = QStringLiteral("SNR %1").arg(snr);
  record.freqHz = hz > 0 ? hz : liveDialHz_;
  record.timeOff = QDateTime::currentDateTimeUtc();
  record.timeOn = record.timeOff;
  if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
    setTxStatus(QStringLiteral("Logged %1. Nothing was keyed. START was not sent.").arg(who));
    return;
  }
  const QsoLog::Result result = qsoLog_ ? qsoLog_->store(record) : QsoLog::Result{};
  if (!result.wrote)
    setTxStatus(QStringLiteral("%1 was not logged. %2 Nothing was keyed. START was not sent.")
                    .arg(who, result.detail));
  else
    setTxStatus(QStringLiteral("Logged %1 to %2. Nothing was keyed. START was not sent.").arg(who, result.path));
}

void MainWindow::radeRxClicked() {
  // RX always ends the over the proper way. While it is already ending, or
  // nothing is on the air, it only keeps the chip lit.
  if (txRade_ && txKeySent_ && !radeStopping_) {
    if (auto* tx = findChild<QPushButton*>(QStringLiteral("txSend")))
      tx->setChecked(false);
  }
  radeRxState_ = -1;
  refreshRadeRxChip();
}

void MainWindow::startRadeScan() {
  auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen"));
  QString why;
  if (!profile_ || !radeMode(profile_->digitalMode()))
    why = QStringLiteral("Scan works in RADE V1 and V2.");
  else if (!tci_ || !tci_->isOpen())
    why = QStringLiteral("Connect the radio before Scan.");
  else if (txRade_ || txKeySent_)
    why = QStringLiteral("Scan waits until TX is off.");
  if (!why.isEmpty()) {
    if (screen)
      screen->setScanning(false);
    setTxStatus(why + QStringLiteral(" This does not key. START was not sent."));
    return;
  }
  const auto dials = profile_->radeScanDials(screen ? screen->scanBands() : QStringList());
  if (dials.isEmpty()) {
    if (screen)
      screen->setScanning(false);
    return;
  }
  // Start on the dial after the one the radio is on, so a stopped scan
  // continues instead of repeating.
  int next = 0;
  for (int i = 0; i < dials.size(); ++i) {
    if (dials.at(i).second == liveDialHz_) {
      next = (i + 1) % int(dials.size());
      break;
    }
  }
  radeScanIndex_ = next;
  radeScanOn_ = true;
  if (screen)
    screen->setScanning(true);
  if (!radeScanTimer_) {
    radeScanTimer_ = new QTimer(this);
    connect(radeScanTimer_, &QTimer::timeout, this, [this] { radeScanTick(); });
  }
  radeScanTune();
  radeScanTimer_->start(250);
}

void MainWindow::stopRadeScan(const QString& why) {
  if (!radeScanOn_)
    return;
  radeScanOn_ = false;
  if (radeScanTimer_)
    radeScanTimer_->stop();
  if (auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen")))
    screen->setScanning(false);
  if (!why.isEmpty())
    setTxStatus(why + QStringLiteral(" This does not key. START was not sent."));
}

void MainWindow::radeScanTune() {
  auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen"));
  // Read every step, so a band picked mid-scan is used on the next dial.
  const auto dials = profile_->radeScanDials(screen ? screen->scanBands() : QStringList());
  if (dials.isEmpty())
    return;
  radeScanIndex_ = ((radeScanIndex_ % int(dials.size())) + int(dials.size())) % int(dials.size());
  const auto& dial = dials.at(radeScanIndex_);
  radeScanTuning_ = true;
  profile_->selectDial(dial.first, dial.second);
  radeScanTuning_ = false;
  radeScanTuneMs_ = QDateTime::currentMSecsSinceEpoch();
  radeScanSyncMs_ = 0;
  setTxStatus(QStringLiteral("Scanning %1 MHz %2 (%3 of %4). This does not key. START was not sent.")
                  .arg(scanDialText(dial.second), profile_->sideband())
                  .arg(radeScanIndex_ + 1)
                  .arg(dials.size()));
}

void MainWindow::radeScanTick() {
  if (!radeScanOn_)
    return;
  if (!profile_ || !radeMode(profile_->digitalMode())) {
    stopRadeScan(QStringLiteral("Scan stopped: the mode changed."));
    return;
  }
  if (!tci_ || !tci_->isOpen()) {
    stopRadeScan(QStringLiteral("Scan stopped: the radio disconnected."));
    return;
  }
  if (txRade_ || txKeySent_) {
    stopRadeScan(QStringLiteral("Scan stopped for TX."));
    return;
  }
  auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen"));
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  // Sync left over from the last dial needs a moment to drop.
  constexpr qint64 kSettleMs = 1500;
  if (now - radeScanTuneMs_ < kSettleMs)
    return;
  if (radeWasSync_) {
    radeScanSyncMs_ = now;
    if (!screen || screen->scanStay()) {
      stopRadeScan(QStringLiteral("Scan stopped on %1 MHz: a RADE station is in sync.")
                       .arg(scanDialText(profile_->dialHz())));
      return;
    }
  }
  const qint64 dwellMs = qint64(screen ? screen->scanDwellSeconds() : 15) * 1000;
  qint64 leave = radeScanTuneMs_ + dwellMs;
  // Continue mode stays while the station is in sync and a few seconds
  // after, so a pause between overs does not move the dial.
  if (radeScanSyncMs_ > 0)
    leave = std::max(leave, radeScanSyncMs_ + 4000);
  if (now < leave)
    return;
  ++radeScanIndex_;
  radeScanTune();
}

void MainWindow::applyRadePlaybackVolume(int percent) {
  if (radeSpeaker_)
    radeSpeaker_->setVolume(std::clamp(percent, 0, 100));
}

void MainWindow::applyRadeCapture(const QString& id) {
  if (!txRade_ || !radeMic_)
    return;
  if (!radeMic_->start(id))
    setTxStatus(QStringLiteral("%1 The radio stays as it is. START was not sent.").arg(radeMic_->error()));
}

void MainWindow::applyRadeOutput(const QString& id) {
  if (!radeSpeaker_)
    return;
  if (radeSpeaker_->running() && radeSpeaker_->deviceId() == id)
    return;
  radeSpeakerId_ = id;
  radeSpeakerFail_ = !radeSpeaker_->start(id);
}

void MainWindow::ensureRadeCodec() {
  updateFreedvReporter(true);
  auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen"));
  const bool want = profile_ && radeMode(profile_->digitalMode());
  if (!want) {
    closeRadeCodec();
    return;
  }
  if (!rade_.load()) {
    radeCodecReady_ = false;
    if (screen)
      screen->setLinkReady(false);
    return;
  }
  // The offscreen test opens the codec itself. Loading the DLL here is
  // enough for the TX button and the link line.
  if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
    radeCodecReady_ = true;
    if (screen)
      screen->setLinkReady(true);
    return;
  }
  const bool v2 = radeV2(profile_->digitalMode());
  bool bpf = v2;
  if (auto* box = findChild<QCheckBox*>(QStringLiteral("radeBpf")))
    bpf = box->isChecked();
  const int noBpf = (v2 && !bpf) ? 1 : 0;
  if (rade_.opened() && radeOpenV2_ == int(v2) && radeOpenNoBpf_ == noBpf) {
    radeCodecReady_ = true;
    if (screen)
      screen->setLinkReady(true);
    return;
  }
  if (txRade_)
    return;
  if (rade_.open(v2, noBpf != 0)) {
    radeOpenV2_ = int(v2);
    radeOpenNoBpf_ = noBpf;
    radeCodecReady_ = true;
    radeRxConv_.reset();
    radeAnalogConv_.reset();
    if (screen)
      screen->setLinkReady(true);
  } else {
    radeCodecReady_ = false;
    radeOpenV2_ = -1;
    if (screen)
      screen->setLinkReady(false);
  }
}

void MainWindow::closeRadeCodec() {
  if (txRade_)
    return;
  rade_.close();
  radeOpenV2_ = -1;
  radeOpenNoBpf_ = -1;
  radeRxConv_.reset();
  radeAnalogConv_.reset();
  if (radeSpeaker_)
    radeSpeaker_->stop();
}

void MainWindow::beginRadeTx() {
  auto* button = findChild<QPushButton*>(QStringLiteral("txSend"));
  auto release = [button] {
    if (!button)
      return;
    const QSignalBlocker block(button);
    button->setChecked(false);
    button->setText(QStringLiteral("TX"));
  };
  ensureRadeCodec();
  if (!radeCodecReady_ || !rade_.opened()) {
    release();
    setTxStatus(rade_.error().isEmpty()
                    ? QStringLiteral("librade did not load. TX did not key. START was not sent.")
                    : rade_.error());
    return;
  }
  if (!tci_ || !tci_->isOpen() || !txTimer_) {
    release();
    setTxStatus(QStringLiteral("Radio is not connected. TX did not key. START was not sent."));
    return;
  }
  if (txTune_)
    stopTune(true);
  stopRadeScan(QStringLiteral("Scan stopped for TX."));
  // FreeDV stations and FreeDV Reporter read the call from the end-of-over.
  rade_.setCallsign(stationCall());
  applyRadeOptions();
  if (!radeMic_)
    radeMic_ = new SpeechInput(this);
  const QString micId = QSettings().value(QStringLiteral("speech/mic")).toString();
  if (!radeMic_->start(micId)) {
    release();
    setTxStatus(QStringLiteral("%1 TX did not key. START was not sent.").arg(radeMic_->error()));
    return;
  }
  radeModem_.clear();
  radeRead_ = 0;
  radePrimed_ = false;
  radeStopping_ = false;
  radeFinished_ = false;
  radeTailMs_ = 0;
  radeOriginMs_ = QDateTime::currentMSecsSinceEpoch();
  radePushed_ = 0;
  txLastPullMs_ = 0;
  if (speech_)
    speech_->stop();
  if (!tci_->setTransmit(true)) {
    radeMic_->stop();
    release();
    setTxStatus(QStringLiteral("Radio is not connected. TX did not key. START was not sent."));
    return;
  }
  txRade_ = true;
  txKeySent_ = true;
  radeTxBeganMs_ = QDateTime::currentMSecsSinceEpoch();
  radeTxLimitWarned_ = false;
  updateFreedvReporter(true);
  refreshRadeRxChip();
  markWaterfallTx();
  if (button)
    button->setText(QStringLiteral("ON AIR"));
  setTxStatus(QStringLiteral(
      "On the air. Speech from the Speech microphone. TRX uses tci. START was not sent."));
  txTimer_->start(20);
}

void MainWindow::pumpRadeTx() {
  updateFreedvReporter(false);
  refreshRadeRxChip();
  if (txRade_ && !radeStopping_ && radeTxBeganMs_ > 0) {
    const int limit = radeTxLimitSeconds(this);
    if (limit > 0) {
      const qint64 left = limit * 1000LL - (QDateTime::currentMSecsSinceEpoch() - radeTxBeganMs_);
      if (left <= 15000 && left > 0 && !radeTxLimitWarned_) {
        radeTxLimitWarned_ = true;
        setTxStatus(QStringLiteral(
            "TX limit in 15 seconds. The over will end. START was not sent."));
      }
      if (left <= 0) {
        radeRxClicked();
        setTxStatus(QStringLiteral("TX limit reached. The over is ending. START was not sent."));
      }
    }
  }
  if (!tci_ || !tci_->isTransmitting()) {
    stopUserTx(true);
    setTxStatus(QStringLiteral("RADE stopped. TRX is off. START was not sent."));
    return;
  }
  if (!radeStopping_) {
    if (radeMic_) {
      std::vector<float> pcm = radeMic_->take();
      if (!pcm.empty()) {
        const int gainDb = radeMicGainDb(this);
        if (gainDb > 0) {
          const float gain = std::pow(10.0f, float(gainDb) / 20.0f);
          for (float& sample : pcm)
            sample *= gain;
        }
        std::vector<float> modem(65536);
        const int n = rade_.tx(pcm.data(), int(pcm.size()), modem.data(), int(modem.size()));
        if (n > 0)
          radeModem_.insert(radeModem_.end(), modem.begin(), modem.begin() + n);
        else if (n < 0)
          setTxStatus(QStringLiteral("RADE encode failed. TRX is still up. START was not sent."));
        if (auto* bar = findChild<RadeMicBar*>(QStringLiteral("radeMicLevel")))
          bar->note(rade_.micPeak());
      }
    }
  } else if (!radeFinished_) {
    if (radeMic_)
      radeMic_->stop();
    std::vector<float> modem(65536);
    const int n = rade_.finish(modem.data(), int(modem.size()));
    if (n > 0)
      radeModem_.insert(radeModem_.end(), modem.begin(), modem.begin() + n);
    radeFinished_ = true;
  }
  const bool drained = radeModem_.empty() || radeRead_ + 1.0 >= double(radeModem_.size());
  if (radeFinished_ && drained) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (radeTailMs_ == 0)
      radeTailMs_ = now;
    // 200 ms of silence after the end-of-over, as the FreeDV app sends, so
    // the radio does not drop TX before the last frame is on the air.
    if (now - radeTailMs_ >= 200) {
      stopUserTx(true);
      setTxStatus(QStringLiteral("RADE ended. TRX is off. START was not sent."));
      return;
    }
  }
  pushRadeRealtime();
}

void MainWindow::pushRadeRealtime() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (txLastPullMs_ > 0 && now - txLastPullMs_ < 80)
    return;
  constexpr int kRate = 48000;
  constexpr int kChunk = 960;
  if (radeOriginMs_ <= 0)
    radeOriginMs_ = now;
  const int allowed = int((now - radeOriginMs_) * kRate / 1000) + kChunk;
  if (radePushed_ >= allowed)
    return;
  int frames = std::min(kChunk, allowed - radePushed_);
  if (frames < 1)
    return;
  std::vector<float> mono(static_cast<size_t>(frames));
  fillRadeTx(mono.data(), frames, kRate);
  sendTxBlock(mono.data(), frames, kRate, 1);
  radePushed_ += frames;
}

void MainWindow::fillRadeTx(float* dst, int frames, int rate) {
  if (!dst || frames < 1)
    return;
  if (rate < 1000)
    rate = 48000;
  const double step = 8000.0 / double(rate);
  radeTxKernel_.build(8000, rate);
  // Drive 100 is the FreeDV app's level: the modem's nominal amplitude of 1.
  const float scale = float(radeDrivePercent(this)) / 100.0f;
  // OFDM cannot take a gap inside a frame. librade hands over 120 ms at a
  // time, so play nothing until 240 ms is queued, and start over cleanly
  // if the queue runs dry. The tail after TX is released always plays.
  constexpr int kPrime = 1920;
  if (!radePrimed_) {
    if (int(radeModem_.size()) - int(radeRead_) < kPrime && !radeFinished_) {
      std::fill(dst, dst + frames, 0.0f);
      return;
    }
    radePrimed_ = true;
  }
  for (int i = 0; i < frames; ++i) {
    if (radeRead_ + double(radeTxKernel_.hw) >= double(radeModem_.size()) && !radeFinished_) {
      // Ran dry mid-over: hold silence and prime again instead of
      // cutting zeros into the middle of a modem frame.
      std::fill(dst + i, dst + frames, 0.0f);
      radePrimed_ = false;
      break;
    }
    dst[i] = radeTxKernel_.at(radeModem_.data(), int(radeModem_.size()), radeRead_) * scale;
    radeRead_ += step;
  }
  // Keep the filter's history behind the read position.
  const int drop = int(radeRead_) - radeTxKernel_.hw;
  if (drop > 0) {
    if (drop >= int(radeModem_.size())) {
      radeModem_.clear();
      radeRead_ = 0;
    } else {
      radeModem_.erase(radeModem_.begin(), radeModem_.begin() + drop);
      radeRead_ -= drop;
    }
  }
}

void MainWindow::pushRadeRx(const float* samples, int count, int rate) {
  if (!samples || count < 1 || !profile_ || !radeMode(profile_->digitalMode()))
    return;
  if (txRade_ || txKeySent_)
    return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!rade_.opened()) {
    if (now - radeOpenTryMs_ < 2000)
      return;
    radeOpenTryMs_ = now;
    ensureRadeCodec();
  }
  if (!rade_.opened())
    return;
  if (rate < 1000)
    rate = 48000;
  applyRadeOptions();
  updateFreedvReporter(false);
  std::vector<float> modem;
  radeRxConv_.process(samples, count, rate, 8000, modem);
  if (!radeAutoRxOn(this))
    radeRxAutoDb_ = 0.0;
  if (!modem.empty()) {
    const float pre = 4.0f * static_cast<float>(std::pow(10.0, radeRxAutoDb_ / 20.0));
    float peak = 0.0f;
    for (float& sample : modem) {
      sample *= pre;
      peak = std::max(peak, std::fabs(sample));
    }
    const bool clip = peak >= 0.98f;
    const float db = peak > 1.0e-6f ? 20.0f * std::log10(peak) : -120.0f;
    radeRxShownDb_ = db;
    radeRxShownClip_ = clip;
    if (db > radeRxPeakDb_)
      radeRxPeakDb_ = db;
    if (radeAutoRxOn(this)) {
      if (radeRxAutoAt_ == 0)
        radeRxAutoAt_ = now;
      const double elapsed = double(now - radeRxAutoAt_) / 1000.0;
      bool stepped = false;
      if (clip || radeRxPeakDb_ > -3.0f) {
        if (elapsed >= 0.8 && radeRxAutoDb_ > -30.0) {
          radeRxAutoDb_ = std::max(-30.0, radeRxAutoDb_ - 2.0);
          stepped = true;
        }
      } else if (elapsed >= 3.0 && radeRxPeakDb_ < -12.0f && radeRxPeakDb_ > -100.0f &&
                 radeRxAutoDb_ < 0.0) {
        radeRxAutoDb_ = std::min(0.0, radeRxAutoDb_ + 1.0);
        stepped = true;
      }
      if (stepped || elapsed >= 3.0) {
        radeRxAutoAt_ = now;
        radeRxPeakDb_ = -120.0f;
      }
    } else {
      radeRxAutoAt_ = now;
      radeRxPeakDb_ = -120.0f;
    }
  }
  const bool syncBefore = radeWasSync_;
  int speechN = 0;
  std::vector<float> speech;
  if (!modem.empty()) {
    // 8 kHz modem to 48 kHz speech is 6x; leave room for a whole frame more.
    speech.resize(static_cast<size_t>(modem.size()) * 12 + 24576);
    speechN = rade_.rx(modem.data(), int(modem.size()), speech.data(), int(speech.size()));
    if (speechN < 0)
      speechN = 0;
  }
  const bool sync = rade_.sync() != 0;
  QString heardCall;
  const int heardSeq = rade_.heard(&heardCall);
  if (heardSeq != radeHeardSeq_) {
    radeHeardSeq_ = heardSeq;
    const QString utc = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm"));
    if (auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen")))
      screen->setHeard(heardCall, utc);
    if (!heardCall.isEmpty()) {
      setTxStatus(QStringLiteral("RADE heard %1 at %2 UTC. This does not key. START was not sent.")
                      .arg(heardCall, utc));
      if (freedvReporter_)
        freedvReporter_->reportReceive(heardCall, freedvReporterModeName(radeV2(profile_->digitalMode())),
                                       int(std::lround(rade_.snr())));
      radeSyncReportMs_ = now;
    }
  }
  // While in sync with no callsign yet, FreeDV Reporter is told once a
  // second that something is being received, as the FreeDV app does.
  if (sync && freedvReporter_ && now - radeSyncReportMs_ >= 1000) {
    radeSyncReportMs_ = now;
    freedvReporter_->reportReceive(QString(), freedvReporterModeName(radeV2(profile_->digitalMode())),
                                   int(std::lround(rade_.snr())));
  }
  if (now - radeMeterMs_ > 100) {
    radeMeterMs_ = now;
    if (auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen"))) {
      screen->setSync(sync, rade_.snr(), rade_.freqOffset());
      screen->setRxLevel(radeRxShownDb_, radeRxShownClip_, int(std::lround(radeRxAutoDb_)));
    }
    if (waterfall_) {
      const QString chip =
          sync ? QStringLiteral("RADE   Sync   SNR %1   RX %2 dB%3")
                     .arg(qRound(rade_.snr()))
                     .arg(qRound(radeRxShownDb_))
                     .arg(radeRxShownClip_ ? QStringLiteral("   CLIP") : QString())
               : QStringLiteral("RADE   idle");
      waterfall_->setRadeReadout(chip);
    }
  }
  if (sync != syncBefore) {
    radeWasSync_ = sync;
    if (radeSpeaker_)
      radeSpeaker_->clear();
    radeAnalogConv_.reset();
  } else {
    radeWasSync_ = sync;
  }
  if (sync && speechN > 0) {
    playRadePcm(speech.data(), speechN);
    return;
  }
  if (!sync && radeAnalogOn(this)) {
    std::vector<float> analog;
    radeAnalogConv_.process(samples, count, rate, 48000, analog);
    if (!analog.empty())
      playRadePcm(analog.data(), int(analog.size()));
  }
}

void MainWindow::playRadePcm(const float* samples, int count) {
  if (!samples || count < 1)
    return;
  if (!radeSpeaker_)
    radeSpeaker_ = new RadeSpeaker(this);
  const QString id = QSettings().value(QStringLiteral("speech/output")).toString();
  if (!radeSpeaker_->running() || radeSpeaker_->deviceId() != id) {
    if (radeSpeakerFail_ && radeSpeakerId_ == id)
      return;
    radeSpeakerId_ = id;
    if (!radeSpeaker_->start(id)) {
      radeSpeakerFail_ = true;
      return;
    }
    radeSpeakerFail_ = false;
  }
  radeSpeaker_->setVolume(
      std::clamp(QSettings().value(QStringLiteral("speech/volume"), 80).toInt(), 0, 100));
  radeSpeaker_->write(samples, count);
}
