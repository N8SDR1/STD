// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeScreen.h"

#include "DigitalMode.h"

#include <algorithm>
#include "speech/SpeechInput.h"
#include "speech/SpeechReader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace {

QFont uiFont() {
  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(13);
  return font;
}

QString deviceName(const QList<SpeechReader::Choice>& choices, const QString& id) {
  for (const SpeechReader::Choice& choice : choices) {
    if (choice.id == id)
      return choice.name;
  }
  if (id.isEmpty())
    return QStringLiteral("Windows default");
  return id;
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
      "about 860 Hz, and will not decode V1. Mic and speaker are the Speech devices. "
      "Identity stays in Options, User. librade and FARGAN are not linked in this build, "
      "so nothing is decoded or keyed."),
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
  form->addRow(QStringLiteral("Waveform"), variant_);

  analog_ = new QCheckBox(QStringLiteral("Analog passthrough"), formHost);
  analog_->setObjectName(QStringLiteral("radeAnalog"));
  analog_->setFont(uiFont());
  analog_->setFocusPolicy(Qt::NoFocus);
  analog_->setChecked(QSettings().value(QStringLiteral("rade/analog"), false).toBool());
  analog_->setToolTip(QStringLiteral(
      "When digital sync is quiet, play the raw SSB audio on the Speech speaker. "
      "Off stays silent. This does not key. START is never sent."));
  form->addRow(QStringLiteral("Analog"), analog_);

  bpf_ = new QCheckBox(QStringLiteral("TX band-pass"), formHost);
  bpf_->setObjectName(QStringLiteral("radeBpf"));
  bpf_->setFont(uiFont());
  bpf_->setFocusPolicy(Qt::NoFocus);
  bpf_->setToolTip(QStringLiteral(
      "TX band-pass. V1 energy is about 700 to 2300 Hz and starts off. "
      "V2 is about 860 Hz and starts on. This does not key. START is never sent."));
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
      "Modem level for a later transmission. 100 is full, and the modem keeps about 6 dB "
      "of headroom. Changing it does not key. START is never sent."));
  driveValue_ = new QLabel(driveRow);
  driveValue_->setObjectName(QStringLiteral("radeDriveValue"));
  driveValue_->setFont(uiFont());
  driveValue_->setMinimumWidth(28);
  driveValue_->setText(QString::number(drive_->value()));
  driveLayout->addWidget(drive_, 1);
  driveLayout->addWidget(driveValue_);
  form->addRow(QStringLiteral("Drive"), driveRow);

  auto* digu = new QPushButton(QStringLiteral("DIGU helper"), formHost);
  digu->setObjectName(QStringLiteral("radeDigu"));
  digu->setFont(uiFont());
  digu->setFocusPolicy(Qt::NoFocus);
  digu->setToolTip(QStringLiteral(
      "Sends modulation:0,digu when the radio is connected. A useful passband is about "
      "750 to 2250 Hz. Leave radio NR and compression off. The VFO is not moved. "
      "This does not key. START is never sent."));
  form->addRow(QStringLiteral("Radio"), digu);
  layout->addWidget(formHost);

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

  mic_ = new QLabel(this);
  mic_->setObjectName(QStringLiteral("radeMic"));
  mic_->setFont(uiFont());
  mic_->setWordWrap(true);
  mic_->setToolTip(QStringLiteral(
      "The Speech microphone. Change it in Options, Speech. This screen has no second picker. "
      "This does not key. START is never sent."));
  speaker_ = new QLabel(this);
  speaker_->setObjectName(QStringLiteral("radeSpeaker"));
  speaker_->setFont(uiFont());
  speaker_->setWordWrap(true);
  speaker_->setToolTip(QStringLiteral(
      "The Speech speaker. Decoded speech will play here, not on the radio. "
      "Change it in Options, Speech. This does not key. START is never sent."));
  layout->addWidget(mic_);
  layout->addWidget(speaker_);

  link_ = new QLabel(QStringLiteral(
      "librade and FARGAN are not linked yet. Sync stays idle and SNR stays blank. "
      "Connect still sends AUDIO_START:0. A later TX uses trx with tci. START is never sent."),
      this);
  link_->setObjectName(QStringLiteral("radeLink"));
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
  connect(bpf_, &QCheckBox::toggled, this, [this](bool on) { rememberBpf(on); });
  connect(drive_, &QSlider::valueChanged, this, [this](int value) {
    driveValue_->setText(QString::number(value));
    QSettings().setValue(QStringLiteral("rade/drive"), value);
  });
  connect(digu, &QPushButton::clicked, this, &RadeScreen::diguHelper);

  showMode(radeV1Name());
  refreshDevices();
}

void RadeScreen::loadBpf(bool v2) {
  const QSignalBlocker block(bpf_);
  const QString key = v2 ? QStringLiteral("rade/bpfV2") : QStringLiteral("rade/bpfV1");
  bpf_->setChecked(QSettings().value(key, v2).toBool());
}

void RadeScreen::rememberBpf(bool on) {
  QSettings().setValue(showingV2_ ? QStringLiteral("rade/bpfV2") : QStringLiteral("rade/bpfV1"), on);
}

void RadeScreen::showMode(const QString& modeName) {
  showingV2_ = radeV2(modeName);
  const QSignalBlocker block(variant_);
  const int index = variant_->findData(showingV2_ ? radeV2Name() : radeV1Name());
  if (index >= 0)
    variant_->setCurrentIndex(index);
  loadBpf(showingV2_);
  refreshDevices();
}

void RadeScreen::refreshDevices() {
  const QSettings store;
  const QString mic = deviceName(SpeechInput::inputs(), store.value(QStringLiteral("speech/mic")).toString());
  const QString speaker =
      deviceName(SpeechReader::outputs(), store.value(QStringLiteral("speech/output")).toString());
  if (mic_)
    mic_->setText(QStringLiteral("Mic  %1").arg(mic));
  if (speaker_)
    speaker_->setText(QStringLiteral("Speaker  %1").arg(speaker));
}
