// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeScreen.h"

#include "DigitalMode.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>

namespace {

QFont uiFont() {
  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(13);
  return font;
}

}  // namespace

RadeScreen::RadeScreen(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("radeScreen"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 4, 0, 0);
  layout->setSpacing(6);

  auto* title = new QLabel(QStringLiteral("RADE"), this);
  title->setObjectName(QStringLiteral("radeTitle"));
  QFont titleFont = uiFont();
  titleFont.setPixelSize(16);
  titleFont.setBold(true);
  title->setFont(titleFont);
  title->setToolTip(QStringLiteral(
      "HF digital voice through analog SSB. V1 is the on-air mode. "
      "V2 is experimental and does not talk to V1. This screen does not key. "
      "START is never sent."));
  layout->addWidget(title);

  auto* intro = new QLabel(QStringLiteral(
      "V1 is the default waveform, about 1500 Hz wide. V2 is experimental, "
      "about 860 Hz, and will not decode V1. Mic, speaker, and volume are under the band chips. "
      "They are the Speech devices. Identity stays in Options, User. Speech is 16 kHz. The modem is 8 kHz."),
      this);
  intro->setObjectName(QStringLiteral("radeIntro"));
  intro->setFont(uiFont());
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto* formHost = new QWidget(this);
  auto* form = new QFormLayout(formHost);
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(6);

  variant_ = new QComboBox(formHost);
  variant_->setObjectName(QStringLiteral("radeVariant"));
  variant_->setFont(uiFont());
  variant_->setFocusPolicy(Qt::NoFocus);
  variant_->addItem(radeV1Name(), radeV1Name());
  variant_->addItem(radeV2Name(), radeV2Name());
  variant_->setToolTip(QStringLiteral(
      "V1 is the calling-frequency mode. V2 is experimental, will not talk to V1, "
      "and may change after a retrain. Switching does not key. START is never sent."));
  variant_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  variant_->setFixedWidth(220);

  analog_ = new QCheckBox(QStringLiteral("Analog passthrough"), formHost);
  analog_->setObjectName(QStringLiteral("radeAnalog"));
  analog_->setFont(uiFont());
  analog_->setFocusPolicy(Qt::NoFocus);
  analog_->setChecked(QSettings().value(QStringLiteral("rade/analog"), false).toBool());
  analog_->setToolTip(QStringLiteral(
      "Off plays decoded speech only, and stays silent until sync. "
      "On plays the radio SSB on the speaker while sync is idle, and decoded speech when sync is up. "
      "This does not key. START is never sent."));
  form->addRow(QStringLiteral("Analog"), analog_);

  bpf_ = new QCheckBox(QStringLiteral("TX band-pass"), formHost);
  bpf_->setObjectName(QStringLiteral("radeBpf"));
  bpf_->setFont(uiFont());
  bpf_->setFocusPolicy(Qt::NoFocus);
  bpf_->setToolTip(QStringLiteral(
      "TX band-pass inside the modem. V1 has no band-pass switch in this build, "
      "so the box is remembered and starts off. V2 is about 860 Hz and starts on. "
      "Turning V2 off sends the modem without that band-pass. "
      "This does not change the radio filter. This does not key. START is never sent."));
  form->addRow(QStringLiteral("BPF"), bpf_);

  auto* driveRow = new QWidget(formHost);
  auto* driveLayout = new QHBoxLayout(driveRow);
  driveLayout->setContentsMargins(0, 0, 0, 0);
  drive_ = new QSlider(Qt::Horizontal, driveRow);
  drive_->setObjectName(QStringLiteral("radeDrive"));
  drive_->setRange(0, 100);
  drive_->setValue(std::clamp(QSettings().value(QStringLiteral("rade/drive"), 70).toInt(), 0, 100));
  drive_->setFocusPolicy(Qt::NoFocus);
  drive_->setToolTip(QStringLiteral(
      "Modem level sent to the radio. 0 is silent. 100 multiplies the modem by one half, "
      "so a full reading stays about 6 dB under full scale. "
      "Changing it does not key. START is never sent."));
  driveValue_ = new QLabel(driveRow);
  driveValue_->setObjectName(QStringLiteral("radeDriveValue"));
  driveValue_->setFont(uiFont());
  driveValue_->setMinimumWidth(28);
  driveValue_->setText(QString::number(drive_->value()));
  driveLayout->addWidget(drive_, 1);
  driveLayout->addWidget(driveValue_);
  form->addRow(QStringLiteral("Drive"), driveRow);
  layout->addWidget(formHost);

  digu_ = new QPushButton(QStringLiteral("Off"), this);
  digu_->setObjectName(QStringLiteral("radeDigu"));
  digu_->setFont(uiFont());
  digu_->setFocusPolicy(Qt::NoFocus);
  digu_->setCheckable(true);
  digu_->setChecked(false);
  digu_->setFixedWidth(64);
  digu_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  digu_->setToolTip(QStringLiteral(
      "DIGU helper. On sends modulation:0,digu and stays lit. Off sends the band sideband. "
      "It does not set the radio filter and it does not move the VFO. "
      "A useful passband is about 750 to 2250 Hz. Leave radio NR and compression off. "
      "On also moves the waterfall cursor to 1500 Hz. This does not key. START is never sent."));
  auto* waveLabel = new QLabel(QStringLiteral("Waveform"), this);
  waveLabel->setFont(uiFont());
  auto* radioLabel = new QLabel(QStringLiteral("Radio"), this);
  radioLabel->setFont(uiFont());
  auto* pair = new QHBoxLayout;
  pair->setContentsMargins(0, 0, 0, 0);
  pair->setSpacing(8);
  pair->addWidget(waveLabel);
  pair->addWidget(variant_);
  pair->addSpacing(16);
  pair->addWidget(radioLabel);
  pair->addWidget(digu_);
  pair->addStretch(1);
  layout->insertLayout(2, pair);

  auto* meters = new QHBoxLayout();
  sync_ = new QLabel(QStringLiteral("Sync  idle"), this);
  sync_->setObjectName(QStringLiteral("radeSync"));
  sync_->setFont(uiFont());
  sync_->setToolTip(QStringLiteral(
      "Frame sync. Audio opens when sync is likely. There is no SNR squelch slider. "
      "This does not key. START is never sent."));
  snr_ = new QLabel(QStringLiteral("SNR  —"), this);
  snr_->setObjectName(QStringLiteral("radeSnr"));
  snr_->setFont(uiFont());
  snr_->setToolTip(QStringLiteral(
      "SNR estimate, shown only. It does not mute. This does not key. START is never sent."));
  meters->addWidget(sync_);
  meters->addSpacing(16);
  meters->addWidget(snr_);
  meters->addStretch(1);
  layout->addLayout(meters);

  link_ = new QLabel(QStringLiteral(
      "librade is not loaded yet. Sync stays idle and SNR stays blank. "
      "Connect still sends AUDIO_START:0. TX uses trx with tci. START is never sent."),
      this);
  link_->setObjectName(QStringLiteral("radeLink"));
  link_->setToolTip(QStringLiteral(
      "freedv/rade_c c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9. "
      "This does not key. START is never sent."));
  link_->setFont(uiFont());
  link_->setWordWrap(true);
  layout->addWidget(link_);
  layout->addStretch(1);

  connect(variant_, &QComboBox::currentIndexChanged, this, [this](int) {
    const QString mode = variant_->currentData().toString();
    rememberBpf(bpf_->isChecked());
    emit variantChosen(mode);
  });
  connect(analog_, &QCheckBox::toggled, this, [](bool on) {
    QSettings().setValue(QStringLiteral("rade/analog"), on);
  });
  connect(bpf_, &QCheckBox::toggled, this, [this](bool on) {
    rememberBpf(on);
    emit optionsChanged();
  });
  connect(drive_, &QSlider::valueChanged, this, [this](int value) {
    driveValue_->setText(QString::number(value));
    QSettings().setValue(QStringLiteral("rade/drive"), value);
  });
  connect(digu_, &QPushButton::toggled, this, [this](bool on) {
    digu_->setText(on ? QStringLiteral("On") : QStringLiteral("Off"));
    emit diguHelper(on);
  });

  showMode(radeV1Name());
}

void RadeScreen::loadBpf(bool v2) {
  const QSignalBlocker block(bpf_);
  const QString key = v2 ? QStringLiteral("rade/bpfV2") : QStringLiteral("rade/bpfV1");
  bpf_->setChecked(QSettings().value(key, v2).toBool());
}

void RadeScreen::rememberBpf(bool on) {
  QSettings().setValue(showingV2_ ? QStringLiteral("rade/bpfV2") : QStringLiteral("rade/bpfV1"), on);
}

void RadeScreen::setLinkReady(bool ready) {
  if (!link_)
    return;
  link_->setText(ready ? QStringLiteral(
                             "librade and FARGAN are linked. Speech is 16 kHz. The modem is 8 kHz. "
                             "Connect sends AUDIO_START:0. TX uses trx with tci. START is never sent.")
                       : QStringLiteral(
                             "librade did not load. TX stays off. Sync stays idle. "
                             "Connect still sends AUDIO_START:0. START is never sent."));
}

void RadeScreen::setSync(bool on, float snrDb) {
  if (sync_)
    sync_->setText(on ? QStringLiteral("Sync  yes") : QStringLiteral("Sync  idle"));
  if (!snr_)
    return;
  if (on)
    snr_->setText(QStringLiteral("SNR  %1 dB").arg(QString::number(double(snrDb), 'f', 1)));
  else
    snr_->setText(QStringLiteral("SNR  —"));
}

void RadeScreen::setDiguOn(bool on) {
  if (!digu_)
    return;
  const QSignalBlocker block(digu_);
  digu_->setChecked(on);
  digu_->setText(on ? QStringLiteral("On") : QStringLiteral("Off"));
}

void RadeScreen::showMode(const QString& modeName) {
  showingV2_ = radeV2(modeName);
  const QSignalBlocker block(variant_);
  const int index = variant_->findData(showingV2_ ? radeV2Name() : radeV1Name());
  if (index >= 0)
    variant_->setCurrentIndex(index);
  loadBpf(showingV2_);
}
