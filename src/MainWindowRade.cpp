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
#include "rade/RadeSpeaker.h"
#include "spot/FreeDvReporter.h"
#include "speech/SpeechInput.h"

#include <QCheckBox>
#include <QDateTime>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>
#include <cmath>

namespace {

int radeDrivePercent(QWidget* window) {
  if (auto* slider = window->findChild<QSlider*>(QStringLiteral("radeDrive")))
    return std::clamp(slider->value(), 0, 100);
  return std::clamp(QSettings().value(QStringLiteral("rade/drive"), 70).toInt(), 0, 100);
}

bool radeBox(QWidget* window, const char* name, const char* key) {
  if (auto* box = window->findChild<QCheckBox*>(QString::fromLatin1(name)))
    return box->isChecked();
  return QSettings().value(QString::fromLatin1(key), true).toBool();
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
  }
  if (on) {
    freedvReporter_->setFrequency(liveDialHz_);
    freedvReporter_->setTransmit(freedvReporterModeName(radeV2(profile_->digitalMode())), txRade_);
  }
  freedvReporter_->setActive(on, stationCall(), stationGrid());
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
  updateFreedvReporter(true);
  markWaterfallTx();
  if (button)
    button->setText(QStringLiteral("ON AIR"));
  setTxStatus(QStringLiteral(
      "On the air. Speech from the Speech microphone. TRX uses tci. START was not sent."));
  txTimer_->start(20);
}

void MainWindow::pumpRadeTx() {
  updateFreedvReporter(false);
  if (!tci_ || !tci_->isTransmitting()) {
    stopUserTx(true);
    setTxStatus(QStringLiteral("RADE stopped. TRX is off. START was not sent."));
    return;
  }
  if (!radeStopping_) {
    if (radeMic_) {
      const std::vector<float> pcm = radeMic_->take();
      if (!pcm.empty()) {
        std::vector<float> modem(65536);
        const int n = rade_.tx(pcm.data(), int(pcm.size()), modem.data(), int(modem.size()));
        if (n > 0)
          radeModem_.insert(radeModem_.end(), modem.begin(), modem.begin() + n);
        else if (n < 0)
          setTxStatus(QStringLiteral("RADE encode failed. TRX is still up. START was not sent."));
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
  for (float& sample : modem)
    sample *= 4.0f;
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
    if (auto* screen = findChild<RadeScreen*>(QStringLiteral("radeScreen")))
      screen->setSync(sync, rade_.snr());
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
