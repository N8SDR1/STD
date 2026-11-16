// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Options -> Speech: received text read aloud with a Windows voice on a
// sound device the operator picks. It does not go through TCI and never
// keys the radio. Hell (a picture), JS8, and the FT8 family are not read.
// Talk records a chosen microphone for speech to text in the keyboard modes.

#include "MainWindow.h"

#include "DigitalMode.h"
#include "ModeProfile.h"
#include "js8/Js8Modem.h"
#include "speech/SpeechInput.h"
#include "speech/SpeechModel.h"
#include "speech/SpeechRecognizer.h"
#include "speech/SpeechReader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Text the receiver prints: the keyboard modes except Hell and JS8.
bool speakableMode(const QString& mode) {
  return keyboardTextMode(mode) && !hellFamily(mode) && !js8Mode(mode);
}

int savedGateDb() {
  return std::clamp(QSettings().value(QStringLiteral("speech/gateDb"), -45).toInt(), -70, -10);
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
  // Hot list alerts: same voice, rate, volume, and output device, never
  // muted here. Visual + Voice in the hot list is their switch.
  if (hotVoice_) {
    hotVoice_->setVoice(store.value(QStringLiteral("speech/voice")).toString());
    hotVoice_->setOutput(store.value(QStringLiteral("speech/output")).toString());
    hotVoice_->setRate(std::clamp(store.value(QStringLiteral("speech/rate"), 0).toInt(), -10, 10));
    hotVoice_->setVolume(volume);
    hotVoice_->setMuted(false);
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
  // Talk works in every keyboard mode, Hell and JS8 included.
  const bool talkOn = store.value(QStringLiteral("speech/txOn"), false).toBool() && keyboardTextMode(mode);
  if (auto* talk = findChild<QPushButton*>(QStringLiteral("txTalk")))
    talk->setVisible(talkOn);
  if (!talkOn && talkActive_)
    endTalk();
  // RADE uses the Speech devices itself. Stop a phrase that was already queued.
  if (radeMode(mode) && speech_)
    speech_->stop();
  if (profile_)
    profile_->refreshRadeDevices();
  applyRadePlaybackVolume(volume);
}

void MainWindow::openSpeechDialog() {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("speechDialog"));
  dialog.setWindowTitle(QStringLiteral("Speech"));
  dialog.setMinimumWidth(520);
  auto* layout = new QVBoxLayout(&dialog);
  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName(QStringLiteral("speechTabs"));
  layout->addWidget(tabs);
  QSettings store;

  // Receive: read text aloud.
  auto* rxPage = new QWidget(tabs);
  auto* rxLayout = new QVBoxLayout(rxPage);
  auto* rxIntro = new QLabel(QStringLiteral(
      "Read received text aloud with a Windows voice. It plays on the device below, not through the "
      "radio, and stops while you transmit. RTTY, PSK, QPSK, Olivia, Contestia, MFSK, DominoEX, Thor, "
      "and MT63 are read. Hell is a picture and is not read; nor are JS8 or the FT8 family. "
      "Nothing here keys, and START is never sent."),
      rxPage);
  rxIntro->setWordWrap(true);
  rxLayout->addWidget(rxIntro);
  auto* form = new QFormLayout;
  auto* enable = new QCheckBox(QStringLiteral("Read received text aloud"), rxPage);
  enable->setObjectName(QStringLiteral("speechRxOn"));
  enable->setChecked(store.value(QStringLiteral("speech/rxOn"), false).toBool());
  form->addRow(enable);

  auto* voice = new QComboBox(rxPage);
  voice->setObjectName(QStringLiteral("speechVoice"));
  voice->addItem(QStringLiteral("Windows default"), QString());
  for (const auto& choice : SpeechReader::voices())
    voice->addItem(choice.name, choice.id);
  voice->setCurrentIndex(std::max(0, voice->findData(store.value(QStringLiteral("speech/voice")).toString())));
  form->addRow(QStringLiteral("Voice"), voice);

  auto* output = new QComboBox(rxPage);
  output->setObjectName(QStringLiteral("speechOutput"));
  for (const auto& choice : SpeechReader::outputs())
    output->addItem(choice.name, choice.id);
  output->setCurrentIndex(std::max(0, output->findData(store.value(QStringLiteral("speech/output")).toString())));
  form->addRow(QStringLiteral("Play on"), output);

  auto* rate = new QSlider(Qt::Horizontal, rxPage);
  rate->setObjectName(QStringLiteral("speechRate"));
  rate->setRange(-10, 10);
  rate->setValue(std::clamp(store.value(QStringLiteral("speech/rate"), 0).toInt(), -10, 10));
  rate->setToolTip(QStringLiteral("Slower to the left, faster to the right. The middle is normal speed."));
  form->addRow(QStringLiteral("Speed"), rate);

  auto* volume = new QSlider(Qt::Horizontal, rxPage);
  volume->setObjectName(QStringLiteral("speechDialogVolume"));
  volume->setRange(0, 100);
  volume->setValue(std::clamp(store.value(QStringLiteral("speech/volume"), 80).toInt(), 0, 100));
  form->addRow(QStringLiteral("Volume"), volume);

  auto* keepUp = new QCheckBox(QStringLiteral("Keep up with the band"), rxPage);
  keepUp->setObjectName(QStringLiteral("speechKeepUp"));
  keepUp->setChecked(store.value(QStringLiteral("speech/keepUp"), true).toBool());
  keepUp->setToolTip(QStringLiteral(
      "When text arrives faster than it can be read, skip to the newest line. Off reads every word "
      "and falls further behind."));
  form->addRow(keepUp);

  auto* skip = new QCheckBox(QStringLiteral("Skip garbled words"), rxPage);
  skip->setObjectName(QStringLiteral("speechSkipGarbled"));
  skip->setChecked(store.value(QStringLiteral("speech/skipGarbled"), true).toBool());
  skip->setToolTip(QStringLiteral(
      "Leave out runs that are mostly symbols, as noise decodes are. Callsigns and reports are spelled out."));
  form->addRow(skip);
  auto* test = new QPushButton(QStringLiteral("Test"), rxPage);
  test->setObjectName(QStringLiteral("speechTest"));
  test->setToolTip(QStringLiteral("Speak a sample line with these settings. This does not key."));
  form->addRow(QString(), test);
  rxLayout->addLayout(form);
  rxLayout->addStretch(1);
  tabs->addTab(rxPage, QStringLiteral("Receive"));

  // Transmit: speech to text through the Talk chip.
  auto* txPage = new QWidget(tabs);
  auto* txLayout = new QVBoxLayout(txPage);
  auto* txIntro = new QLabel(QStringLiteral(
      "Talk turns your voice into text in the transmit box. Hold Talk on the Transmit panel and speak, "
      "then let go. It records the microphone below, never the radio's audio, and it does not key. "
      "Pick the PC microphone here if another one feeds the radio."),
      txPage);
  txIntro->setWordWrap(true);
  txLayout->addWidget(txIntro);
  auto* txForm = new QFormLayout;
  auto* txOn = new QCheckBox(QStringLiteral("Speech to text (show Talk)"), txPage);
  txOn->setObjectName(QStringLiteral("speechTxOn"));
  txOn->setChecked(store.value(QStringLiteral("speech/txOn"), false).toBool());
  txForm->addRow(txOn);

  auto* mic = new QComboBox(txPage);
  mic->setObjectName(QStringLiteral("speechMic"));
  for (const auto& choice : SpeechInput::inputs())
    mic->addItem(choice.name, choice.id);
  mic->setCurrentIndex(std::max(0, mic->findData(store.value(QStringLiteral("speech/mic")).toString())));
  txForm->addRow(QStringLiteral("Microphone"), mic);

  auto* gateRow = new QWidget(txPage);
  auto* gateLayout = new QHBoxLayout(gateRow);
  gateLayout->setContentsMargins(0, 0, 0, 0);
  auto* gate = new QSlider(Qt::Horizontal, gateRow);
  gate->setObjectName(QStringLiteral("speechGate"));
  gate->setRange(-70, -10);
  gate->setValue(savedGateDb());
  gate->setToolTip(QStringLiteral(
      "Sound below this level is left out, so room noise between words is not sent to the recognizer. "
      "Set it just above the level bar while you are quiet."));
  auto* gateValue = new QLabel(gateRow);
  gateValue->setMinimumWidth(70);
  gateLayout->addWidget(gate, 1);
  gateLayout->addWidget(gateValue);
  txForm->addRow(QStringLiteral("Gate"), gateRow);

  auto* meterRow = new QWidget(txPage);
  auto* meterLayout = new QHBoxLayout(meterRow);
  meterLayout->setContentsMargins(0, 0, 0, 0);
  auto* meter = new QProgressBar(meterRow);
  meter->setObjectName(QStringLiteral("speechMicLevel"));
  meter->setRange(-70, 0);
  meter->setValue(-70);
  meter->setTextVisible(false);
  meter->setFixedHeight(12);
  auto* gateState = new QLabel(QStringLiteral("-"), meterRow);
  gateState->setMinimumWidth(48);
  auto* listen = new QPushButton(QStringLiteral("Check level"), meterRow);
  listen->setObjectName(QStringLiteral("speechMicCheck"));
  listen->setCheckable(true);
  listen->setToolTip(QStringLiteral(
      "Open the microphone and show its level against the gate. Speak normally, then stay quiet. "
      "Nothing is recorded or sent, and nothing keys."));
  meterLayout->addWidget(meter, 1);
  meterLayout->addWidget(gateState);
  meterLayout->addWidget(listen);
  txForm->addRow(QStringLiteral("Level"), meterRow);
  auto* micError = new QLabel(txPage);
  micError->setObjectName(QStringLiteral("speechMicError"));
  micError->setWordWrap(true);
  micError->setStyleSheet(QStringLiteral("color: #ff8a65;"));
  micError->hide();
  txForm->addRow(micError);

  auto* clickMode = new QCheckBox(QStringLiteral("Click Talk to start and stop, instead of holding it"), txPage);
  clickMode->setObjectName(QStringLiteral("speechTalkToggle"));
  clickMode->setChecked(store.value(QStringLiteral("speech/talkToggle"), false).toBool());
  clickMode->setToolTip(QStringLiteral("For operators who cannot hold a button down. This does not key."));
  txForm->addRow(clickMode);

  // My words: what Talk should expect. Corrections: what it keeps getting wrong.
  auto* myWords = new QPlainTextEdit(txPage);
  myWords->setObjectName(QStringLiteral("speechMyWords"));
  myWords->setFixedHeight(64);
  myWords->setPlaceholderText(QStringLiteral("One per line or with commas: W8AJT, Kenwood, Hexbeam, Dayton"));
  myWords->setPlainText(store.value(QStringLiteral("speech/myWords")).toStringList().join(QLatin1Char('\n')));
  myWords->setToolTip(QStringLiteral(
      "Words Talk should expect: calls you work often, club calls, names, towns, your rig and antenna. "
      "Your name and QTH from Options → User and the station in the QSO bar are added by themselves. "
      "Saved as you type. This does not key."));
  txForm->addRow(QStringLiteral("My words"), myWords);
  auto* fixes = new QPlainTextEdit(txPage);
  fixes->setObjectName(QStringLiteral("speechCorrections"));
  fixes->setFixedHeight(64);
  fixes->setPlaceholderText(QStringLiteral("heard = send, one per line:  HEX BEAM = HEXBEAM"));
  fixes->setPlainText(store.value(QStringLiteral("speech/corrections")).toStringList().join(QLatin1Char('\n')));
  fixes->setToolTip(QStringLiteral(
      "Fixes for words Talk keeps getting wrong, one per line as heard = send. The heard words are "
      "replaced wherever they stand as whole words, ignoring case. You can also select text in the "
      "transmit box, right-click, and teach a fix there. Saved as you type. This does not key."));
  txForm->addRow(QStringLiteral("Corrections"), fixes);
  auto lines = [](const QPlainTextEdit* edit) {
    QStringList out;
    for (const QString& line : edit->toPlainText().split(QLatin1Char('\n')))
      if (!line.simplified().isEmpty())
        out << line.simplified();
    return out;
  };
  connect(myWords, &QPlainTextEdit::textChanged, &dialog,
          [myWords, lines] { QSettings().setValue(QStringLiteral("speech/myWords"), lines(myWords)); });
  connect(fixes, &QPlainTextEdit::textChanged, &dialog,
          [fixes, lines] { QSettings().setValue(QStringLiteral("speech/corrections"), lines(fixes)); });

  // The recognizer model: downloaded only when picked here, never shipped.
  auto* modelRow = new QWidget(txPage);
  auto* modelLayout = new QHBoxLayout(modelRow);
  modelLayout->setContentsMargins(0, 0, 0, 0);
  auto* model = new QComboBox(modelRow);
  model->setObjectName(QStringLiteral("speechModel"));
  model->setToolTip(QStringLiteral(
      "Larger models make fewer mistakes but take longer to turn speech into text. "
      "English-only models; Base suits most PCs."));
  const QString savedModel = store.value(QStringLiteral("speech/model"), QStringLiteral("base.en")).toString();
  for (const SpeechModelInfo& info : speechModels()) {
    model->addItem(QStringLiteral("%1 (%2)").arg(info.label, speechModelSize(info.bytes)), info.id);
    if (info.id == savedModel)
      model->setCurrentIndex(model->count() - 1);
  }
  auto* modelGet = new QPushButton(modelRow);
  modelGet->setObjectName(QStringLiteral("speechModelGet"));
  auto* modelCancel = new QPushButton(QStringLiteral("Cancel"), modelRow);
  modelCancel->setObjectName(QStringLiteral("speechModelCancel"));
  modelLayout->addWidget(model, 1);
  modelLayout->addWidget(modelGet);
  modelLayout->addWidget(modelCancel);
  txForm->addRow(QStringLiteral("Model"), modelRow);
  auto* modelBar = new QProgressBar(txPage);
  modelBar->setObjectName(QStringLiteral("speechModelProgress"));
  modelBar->setRange(0, 1000);
  modelBar->setFixedHeight(12);
  modelBar->setTextVisible(false);
  txForm->addRow(QString(), modelBar);
  txLayout->addLayout(txForm);
  auto* txNote = new QLabel(txPage);
  txNote->setObjectName(QStringLiteral("speechModelNote"));
  txNote->setWordWrap(true);
  txLayout->addWidget(txNote);
  auto showModel = [this, model, modelGet, modelCancel, modelBar, txNote](const QString& message) {
    const SpeechModelInfo info = speechModel(model->currentData().toString());
    const bool busy = modelDl_ && modelDl_->busy();
    const bool thisOne = busy && modelDl_->modelId() == info.id;
    const bool installed = speechModelInstalled(info);
    modelGet->setText(installed ? QStringLiteral("Remove") : QStringLiteral("Download"));
    modelGet->setEnabled(!busy);
    modelGet->setToolTip(installed ? QStringLiteral("Delete this model from the PC. You can download it again.")
                                   : QStringLiteral("Download this model (%1) from Hugging Face. It is checked "
                                                    "before use, and STD keeps working while it downloads.")
                                         .arg(speechModelSize(info.bytes)));
    modelCancel->setVisible(thisOne);
    modelBar->setVisible(thisOne);
    QString note = message;
    if (note.isEmpty()) {
      if (thisOne)
        note = QStringLiteral("Downloading. You can close this window; it carries on.");
      else if (busy)
        note = QStringLiteral("Another model is downloading.");
      else if (installed)
        note = QStringLiteral("Installed. Hold Talk, speak, and let go: the words land at the end of the "
                              "transmit box. Nothing keys; you send it with TX as usual.");
      else
        note = QStringLiteral("Not installed. Nothing is downloaded until you press Download.");
    }
    txNote->setText(note);
  };
  showModel(QString());
  txLayout->addStretch(1);
  tabs->addTab(txPage, QStringLiteral("Transmit"));

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  layout->addWidget(buttons);

  auto showGate = [gate, gateValue] { gateValue->setText(QStringLiteral("%1 dBFS").arg(gate->value())); };
  showGate();
  auto save = [&] {
    QSettings out;
    out.setValue(QStringLiteral("speech/rxOn"), enable->isChecked());
    out.setValue(QStringLiteral("speech/voice"), voice->currentData().toString());
    out.setValue(QStringLiteral("speech/output"), output->currentData().toString());
    out.setValue(QStringLiteral("speech/rate"), rate->value());
    out.setValue(QStringLiteral("speech/volume"), volume->value());
    out.setValue(QStringLiteral("speech/keepUp"), keepUp->isChecked());
    out.setValue(QStringLiteral("speech/skipGarbled"), skip->isChecked());
    out.setValue(QStringLiteral("speech/txOn"), txOn->isChecked());
    out.setValue(QStringLiteral("speech/mic"), mic->currentData().toString());
    out.setValue(QStringLiteral("speech/gateDb"), gate->value());
    out.setValue(QStringLiteral("speech/talkToggle"), clickMode->isChecked());
    out.setValue(QStringLiteral("speech/model"), model->currentData().toString());
    showGate();
    applySpeechSettings();
  };
  connect(enable, &QCheckBox::toggled, &dialog, save);
  connect(voice, &QComboBox::currentIndexChanged, &dialog, save);
  connect(output, &QComboBox::currentIndexChanged, &dialog, save);
  connect(rate, &QSlider::valueChanged, &dialog, save);
  connect(volume, &QSlider::valueChanged, &dialog, save);
  connect(keepUp, &QCheckBox::toggled, &dialog, save);
  connect(skip, &QCheckBox::toggled, &dialog, save);
  connect(txOn, &QCheckBox::toggled, &dialog, save);
  connect(gate, &QSlider::valueChanged, &dialog, save);
  connect(clickMode, &QCheckBox::toggled, &dialog, save);
  connect(model, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    save();
    showModel(QString());
  });
  connect(modelGet, &QPushButton::clicked, &dialog, [&] {
    const SpeechModelInfo info = speechModel(model->currentData().toString());
    if (speechModelInstalled(info)) {
      if (recognizer_ && recognizer_->busy()) {
        showModel(QStringLiteral("Speech is being turned into text with this model; try again in a moment."));
        return;
      }
      if (recognizer_)
        recognizer_->unload();
      showModel(removeSpeechModel(info) ? QStringLiteral("Removed.")
                                        : QStringLiteral("Could not delete it; is a file open? Try again."));
      return;
    }
    modelBar->setValue(0);
    if (!modelDl_->start(info)) {
      showModel(QStringLiteral("Could not start the download; the model folder could not be written."));
      return;
    }
    showModel(QString());
  });
  connect(modelCancel, &QPushButton::clicked, &dialog, [this] { modelDl_->cancel(); });
  connect(modelDl_, &SpeechModelDownload::progress, &dialog, [modelBar, txNote](qint64 got, qint64 total) {
    if (total > 0)
      modelBar->setValue(int(got * 1000 / total));
    txNote->setText(QStringLiteral("Downloading %1 of %2. You can close this window; it carries on.")
                        .arg(speechModelSize(got), speechModelSize(total)));
  });
  connect(modelDl_, &SpeechModelDownload::finished, &dialog,
          [&](const QString&, bool, const QString& message) { showModel(message); });
  connect(test, &QPushButton::clicked, &dialog, [this] {
    const QString call = stationCall().isEmpty() ? QStringLiteral("N0CALL") : stationCall();
    if (speech_)
      speech_->say(QStringLiteral("CQ CQ de %1, your report is 599. This is the speech test.").arg(call));
  });

  // Level check: a microphone of the dialog's own, closed with it.
  SpeechInput monitor;
  connect(&monitor, &SpeechInput::level, &dialog, [meter, gate, gateState](double db) {
    meter->setValue(int(std::clamp(db, -70.0, 0.0)));
    gateState->setText(db >= gate->value() ? QStringLiteral("open") : QStringLiteral("shut"));
  });
  auto showMicError = [micError](const QString& text) {
    micError->setText(text);
    micError->setVisible(!text.isEmpty());
  };
  auto stopMonitor = [&] {
    monitor.stop();
    meter->setValue(-70);
    gateState->setText(QStringLiteral("-"));
  };
  auto startMonitor = [&] {
    if (talkActive_)
      endTalk();
    showMicError(QString());
    if (monitor.start(mic->currentData().toString()))
      return;
    gateState->setText(QStringLiteral("no mic"));
    showMicError(monitor.error());
    const QSignalBlocker block(listen);
    listen->setChecked(false);
  };
  connect(listen, &QPushButton::toggled, &dialog, [&](bool on) {
    if (on)
      startMonitor();
    else
      stopMonitor();
  });
  connect(mic, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    save();
    if (listen->isChecked()) {
      stopMonitor();
      startMonitor();
    }
  });
  // A microphone that drops out after it opened stops sending levels; say why.
  QTimer lostCheck;
  lostCheck.setInterval(500);
  connect(&lostCheck, &QTimer::timeout, &dialog, [&] {
    if (!listen->isChecked())
      return;
    const QString lost = monitor.lostError();
    if (lost.isEmpty())
      return;
    stopMonitor();
    gateState->setText(QStringLiteral("no mic"));
    showMicError(lost);
    const QSignalBlocker block(listen);
    listen->setChecked(false);
  });
  lostCheck.start();
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  dialog.exec();
  monitor.stop();
}

void MainWindow::startTalk() {
  if (talkActive_ || !talkIn_)
    return;
  if (recognizer_ && recognizer_->busy()) {
    setTxStatus(QStringLiteral("Still turning the last speech into text. Nothing keyed."));
    return;
  }
  const QSettings store;
  if (!talkIn_->start(store.value(QStringLiteral("speech/mic")).toString())) {
    setTxStatus(talkIn_->error() + QStringLiteral(" Talk did not record. Nothing keyed."));
    return;
  }
  talkActive_ = true;
  if (auto* talk = findChild<QPushButton*>(QStringLiteral("txTalk")))
    talk->setText(QStringLiteral("Listening"));
  setTxStatus(store.value(QStringLiteral("speech/talkToggle"), false).toBool()
                  ? QStringLiteral("Listening. Click Talk again when you finish. Nothing keyed.")
                  : QStringLiteral("Listening. Let go of Talk when you finish. Nothing keyed."));
}

void MainWindow::endTalk() {
  if (!talkActive_ || !talkIn_)
    return;
  talkActive_ = false;
  talkIn_->stop();
  if (auto* talk = findChild<QPushButton*>(QStringLiteral("txTalk")))
    talk->setText(QStringLiteral("Talk"));
  const QSettings store;
  const int gateDb = savedGateDb();
  const QString lost = talkIn_->lostError();
  const std::vector<float> recorded = talkIn_->take();
  if (!lost.isEmpty() && recorded.empty()) {
    setTxStatus(lost + QStringLiteral(" Nothing was added. Nothing keyed."));
    return;
  }
  const std::vector<float> speech = gateSpeech(recorded, double(gateDb));
  if (speech.empty()) {
    setTxStatus(QStringLiteral("Heard nothing above the %1 dBFS gate in %2 s. Nothing was added. Nothing keyed.")
                    .arg(gateDb)
                    .arg(double(recorded.size()) / double(SpeechInput::kRate), 0, 'f', 1));
    return;
  }
  const double seconds = double(speech.size()) / double(SpeechInput::kRate);
  const SpeechModelInfo model =
      speechModel(store.value(QStringLiteral("speech/model"), QStringLiteral("base.en")).toString());
  if (!speechModelInstalled(model)) {
    setTxStatus(QStringLiteral("Heard %1 s of speech. No speech model is installed; download one in "
                               "Options, Speech, Transmit. Nothing was added. Nothing keyed.")
                    .arg(seconds, 0, 'f', 1));
    return;
  }
  // The hint: the station being worked, your name and QTH, and My words.
  QStringList expect;
  for (const char* field : {"qsoBarCall", "qsoBarName", "qsoBarQth"})
    if (const auto* edit = findChild<QLineEdit*>(QLatin1String(field)))
      if (!edit->text().trimmed().isEmpty())
        expect << edit->text().trimmed();
  for (const char* key : {"user/name", "user/qth"}) {
    const QString v = store.value(QLatin1String(key)).toString().trimmed();
    if (!v.isEmpty())
      expect << v;
  }
  expect << store.value(QStringLiteral("speech/myWords")).toStringList();
  const QString worked = speechMyWords(expect);
  if (!recognizer_ || !recognizer_->start(speechModelPath(model), speech, speechHint(stationCall(), worked))) {
    setTxStatus(QStringLiteral("Still turning the last speech into text. Nothing was added. Nothing keyed."));
    return;
  }
  setTxStatus(QStringLiteral("Turning %1 s of speech into text. Nothing keyed.").arg(seconds, 0, 'f', 1));
}

void MainWindow::insertHeardText(const QString& heard, const QString& error) {
  if (!error.isEmpty()) {
    setTxStatus(error + QStringLiteral(" Nothing was added. Nothing keyed."));
    return;
  }
  const QString text = speechApplyCorrections(speechToTxText(heard),
                                              QSettings().value(QStringLiteral("speech/corrections")).toStringList());
  if (text.isEmpty()) {
    setTxStatus(QStringLiteral("No words were recognized. Nothing was added. Nothing keyed."));
    return;
  }
  auto* box = findChild<QPlainTextEdit*>(QStringLiteral("txHell"));
  if (!box) {
    setTxStatus(QStringLiteral("There is no transmit box in this mode. Nothing was added."));
    return;
  }
  // Speech joins the end of the box, as typing would; it never keys.
  QTextCursor tail(box->document());
  tail.movePosition(QTextCursor::End);
  const QString existing = box->toPlainText();
  const bool needSpace = !existing.isEmpty() && !existing.back().isSpace();
  tail.insertText((needSpace ? QStringLiteral(" ") : QString()) + text + QLatin1Char(' '));
  box->setTextCursor(tail);
  const bool ended = text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char('!')) ||
                     text.endsWith(QLatin1Char('?'));
  setTxStatus(QStringLiteral("Added from speech: %1%2 Nothing keyed; TX sends it.")
                  .arg(text, ended ? QString() : QStringLiteral(".")));
}

// Right-click on selected text in the transmit box teaches Talk: send this
// as something else from now on, or expect this word. Nothing keys.
void MainWindow::installTalkMenu(QPlainTextEdit* box) {
  box->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(box, &QWidget::customContextMenuRequested, this, [this, box](const QPoint& pos) {
    QMenu* menu = box->createStandardContextMenu(pos);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QString picked = box->textCursor().selectedText().simplified();
    if (!picked.isEmpty() && picked.size() <= 60 &&
        QSettings().value(QStringLiteral("speech/txOn"), false).toBool()) {
      menu->addSeparator();
      const QString shown = picked.size() > 24 ? picked.left(24) + QStringLiteral("…") : picked;
      menu->addAction(QStringLiteral("Talk: always send \"%1\" as …").arg(shown), this, [this, picked] {
        bool ok = false;
        const QString send = QInputDialog::getText(
                                 this, QStringLiteral("Talk correction"),
                                 QStringLiteral("When Talk hears \"%1\", send this instead:").arg(picked),
                                 QLineEdit::Normal, picked, &ok)
                                 .simplified();
        if (!ok || send.isEmpty() || send.compare(picked, Qt::CaseInsensitive) == 0)
          return;
        QSettings store;
        QStringList fixes = store.value(QStringLiteral("speech/corrections")).toStringList();
        fixes.erase(std::remove_if(fixes.begin(), fixes.end(),
                                   [&](const QString& line) {
                                     return line.section(QLatin1Char('='), 0, 0).simplified().compare(
                                                picked, Qt::CaseInsensitive) == 0;
                                   }),
                    fixes.end());
        fixes << QStringLiteral("%1 = %2").arg(picked, send.toUpper());
        store.setValue(QStringLiteral("speech/corrections"), fixes);
        setTxStatus(QStringLiteral("Talk will send %1 for %2 from now on. Options → Speech → Transmit lists "
                                   "the corrections. Nothing keyed.")
                        .arg(send.toUpper(), picked));
      });
      menu->addAction(QStringLiteral("Talk: add \"%1\" to My words").arg(shown), this, [this, picked] {
        QSettings store;
        QStringList words = store.value(QStringLiteral("speech/myWords")).toStringList();
        if (!words.contains(picked, Qt::CaseInsensitive))
          words << picked;
        store.setValue(QStringLiteral("speech/myWords"), words);
        setTxStatus(QStringLiteral("Talk now expects %1. Options → Speech → Transmit lists My words. "
                                   "Nothing keyed.")
                        .arg(picked));
      });
    }
    menu->popup(box->viewport()->mapToGlobal(pos));
  });
}
