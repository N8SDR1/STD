// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Options -> Speech: received text read aloud with a Windows voice on a
// sound device the operator picks. It does not go through TCI and never
// keys the radio. Hell (a picture), JS8, and the FT8 family are not read.

#include "MainWindow.h"

#include "DigitalMode.h"
#include "ModeProfile.h"
#include "js8/Js8Modem.h"
#include "speech/SpeechReader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Text the receiver prints: the keyboard modes except Hell and JS8.
bool speakableMode(const QString& mode) {
  return keyboardTextMode(mode) && !hellFamily(mode) && !js8Mode(mode);
}

}  // namespace

bool MainWindow::speechWanted() const {
  if (!speech_ || !profile_ || txKeySent_)
    return false;
  const QSettings store;
  return store.value(QStringLiteral("speech/rxOn"), false).toBool() &&
         !store.value(QStringLiteral("speech/muted"), false).toBool() &&
         speakableMode(profile_->digitalMode());
}

void MainWindow::applySpeechSettings() {
  const QSettings store;
  const bool on = store.value(QStringLiteral("speech/rxOn"), false).toBool();
  const bool muted = store.value(QStringLiteral("speech/muted"), false).toBool();
  const int volume = std::clamp(store.value(QStringLiteral("speech/volume"), 80).toInt(), 0, 100);
  const QString mode = profile_ ? profile_->digitalMode() : QString();
  const bool speakable = speakableMode(mode);
  if (speech_) {
    speech_->setVoice(store.value(QStringLiteral("speech/voice")).toString());
    speech_->setOutput(store.value(QStringLiteral("speech/output")).toString());
    speech_->setRate(std::clamp(store.value(QStringLiteral("speech/rate"), 0).toInt(), -10, 10));
    speech_->setVolume(volume);
    speech_->setKeepUp(store.value(QStringLiteral("speech/keepUp"), true).toBool());
    speech_->setSkipGarbled(store.value(QStringLiteral("speech/skipGarbled"), true).toBool());
    speech_->setMuted(muted || !on || !speakable);
  }
  if (auto* chip = findChild<QPushButton*>(QStringLiteral("speechChip"))) {
    const QSignalBlocker block(chip);
    chip->setChecked(!muted);
    chip->setText(muted ? QStringLiteral("Muted") : QStringLiteral("Speech"));
  }
  if (auto* spin = findChild<QSpinBox*>(QStringLiteral("speechVolume"))) {
    const QSignalBlocker block(spin);
    spin->setValue(volume);
  }
  if (auto* box = findChild<QWidget*>(QStringLiteral("speechBox")))
    box->setVisible(on && speakable);
}

void MainWindow::openSpeechDialog() {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("speechDialog"));
  dialog.setWindowTitle(QStringLiteral("Speech"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* intro = new QLabel(QStringLiteral(
      "Read received text aloud with a Windows voice. It plays on the device below, not through the "
      "radio, and stops while you transmit. RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, "
      "and MT63 are read. Hell is a picture and is not read; nor are JS8 or the FT8 family. "
      "Nothing here keys, and START is never sent."),
      &dialog);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  QSettings store;
  auto* form = new QFormLayout;
  auto* enable = new QCheckBox(QStringLiteral("Read received text aloud"), &dialog);
  enable->setObjectName(QStringLiteral("speechRxOn"));
  enable->setChecked(store.value(QStringLiteral("speech/rxOn"), false).toBool());
  form->addRow(enable);

  auto* voice = new QComboBox(&dialog);
  voice->setObjectName(QStringLiteral("speechVoice"));
  voice->addItem(QStringLiteral("Windows default"), QString());
  for (const auto& choice : SpeechReader::voices())
    voice->addItem(choice.name, choice.id);
  voice->setCurrentIndex(std::max(0, voice->findData(store.value(QStringLiteral("speech/voice")).toString())));
  form->addRow(QStringLiteral("Voice"), voice);

  auto* output = new QComboBox(&dialog);
  output->setObjectName(QStringLiteral("speechOutput"));
  for (const auto& choice : SpeechReader::outputs())
    output->addItem(choice.name, choice.id);
  output->setCurrentIndex(std::max(0, output->findData(store.value(QStringLiteral("speech/output")).toString())));
  form->addRow(QStringLiteral("Play on"), output);

  auto* rate = new QSlider(Qt::Horizontal, &dialog);
  rate->setObjectName(QStringLiteral("speechRate"));
  rate->setRange(-10, 10);
  rate->setValue(std::clamp(store.value(QStringLiteral("speech/rate"), 0).toInt(), -10, 10));
  rate->setToolTip(QStringLiteral("Slower to the left, faster to the right. The middle is normal speed."));
  form->addRow(QStringLiteral("Speed"), rate);

  auto* volume = new QSlider(Qt::Horizontal, &dialog);
  volume->setObjectName(QStringLiteral("speechDialogVolume"));
  volume->setRange(0, 100);
  volume->setValue(std::clamp(store.value(QStringLiteral("speech/volume"), 80).toInt(), 0, 100));
  form->addRow(QStringLiteral("Volume"), volume);

  auto* keepUp = new QCheckBox(QStringLiteral("Keep up with the band"), &dialog);
  keepUp->setObjectName(QStringLiteral("speechKeepUp"));
  keepUp->setChecked(store.value(QStringLiteral("speech/keepUp"), true).toBool());
  keepUp->setToolTip(QStringLiteral(
      "When text arrives faster than it can be read, skip to the newest line. Off reads every word "
      "and falls further behind."));
  form->addRow(keepUp);

  auto* skip = new QCheckBox(QStringLiteral("Skip garbled words"), &dialog);
  skip->setObjectName(QStringLiteral("speechSkipGarbled"));
  skip->setChecked(store.value(QStringLiteral("speech/skipGarbled"), true).toBool());
  skip->setToolTip(QStringLiteral(
      "Leave out runs that are mostly symbols, as noise decodes are. Callsigns and reports are spelled out."));
  form->addRow(skip);
  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* test = buttons->addButton(QStringLiteral("Test"), QDialogButtonBox::ActionRole);
  test->setObjectName(QStringLiteral("speechTest"));
  test->setToolTip(QStringLiteral("Speak a sample line with these settings. This does not key."));
  layout->addWidget(buttons);

  auto save = [&] {
    QSettings out;
    out.setValue(QStringLiteral("speech/rxOn"), enable->isChecked());
    out.setValue(QStringLiteral("speech/voice"), voice->currentData().toString());
    out.setValue(QStringLiteral("speech/output"), output->currentData().toString());
    out.setValue(QStringLiteral("speech/rate"), rate->value());
    out.setValue(QStringLiteral("speech/volume"), volume->value());
    out.setValue(QStringLiteral("speech/keepUp"), keepUp->isChecked());
    out.setValue(QStringLiteral("speech/skipGarbled"), skip->isChecked());
    applySpeechSettings();
  };
  connect(enable, &QCheckBox::toggled, &dialog, save);
  connect(voice, &QComboBox::currentIndexChanged, &dialog, save);
  connect(output, &QComboBox::currentIndexChanged, &dialog, save);
  connect(rate, &QSlider::valueChanged, &dialog, save);
  connect(volume, &QSlider::valueChanged, &dialog, save);
  connect(keepUp, &QCheckBox::toggled, &dialog, save);
  connect(skip, &QCheckBox::toggled, &dialog, save);
  connect(test, &QPushButton::clicked, &dialog, [this] {
    const QString call = stationCall().isEmpty() ? QStringLiteral("N0CALL") : stationCall();
    if (speech_)
      speech_->say(QStringLiteral("CQ CQ de %1, your report is 599. This is the speech test.").arg(call));
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  dialog.exec();
}
