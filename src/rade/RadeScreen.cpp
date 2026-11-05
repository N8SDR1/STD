// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeScreen.h"
#include "rade/RadeMeters.h"

#include "DigitalMode.h"
#include "ModeProfile.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QTimeZone>
#include <QFont>
#include <QBoxLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QCursor>
#include <QMenu>
#include <QWidgetAction>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QResizeEvent>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QFont uiFont() {
  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(13);
  return font;
}

class RadeFreqItem : public QTableWidgetItem {
public:
  bool operator<(const QTableWidgetItem& other) const override {
    return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
  }
};

class RadeSnrItem : public QTableWidgetItem {
public:
  bool operator<(const QTableWidgetItem& other) const override {
    return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
  }
};

constexpr int kSavedMessages = 15;

void detachLayout(QLayout* layout) {
  if (!layout)
    return;
  while (layout->count() > 0) {
    QLayoutItem* item = layout->takeAt(0);
    if (QLayout* child = item->layout())
      detachLayout(child);
    delete item;
  }
}

QBoxLayout* freshBox(QWidget* host, bool vertical) {
  if (QLayout* old = host->layout()) {
    detachLayout(old);
    delete old;
  }
  QBoxLayout* lay = vertical ? static_cast<QBoxLayout*>(new QVBoxLayout(host))
                             : static_cast<QBoxLayout*>(new QHBoxLayout(host));
  lay->setContentsMargins(8, 6, 8, 6);
  lay->setSpacing(8);
  return lay;
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
      "V2 is experimental and does not talk to V1. Hover a control for the detail. "
      "This screen does not key. START is never sent."));
  layoutPick_ = new QPushButton(QStringLiteral("Show rows"), this);
  layoutPick_->setObjectName(QStringLiteral("radeLayoutPick"));
  layoutPick_->setFont(uiFont());
  layoutPick_->setFocusPolicy(Qt::NoFocus);
  layoutPick_->setToolTip(QStringLiteral(
      "Switch between three columns and two rows. Columns puts audio, status, and waveform "
      "side by side, audio on the left. Rows puts waveform and meters on top, with audio underneath. "
      "A narrow window moves status onto the next line and wraps the audio strip. "
      "This does not key. START is never sent."));
  auto* header = new QHBoxLayout();
  header->setContentsMargins(0, 0, 0, 0);
  header->addWidget(title);
  header->addSpacing(16);
  header->addWidget(layoutPick_);
  header->addStretch(1);
  layout->addLayout(header);
  connect(layoutPick_, &QPushButton::clicked, this, [this] { setColumns(!columns_); });
  columns_ = QSettings().value(QStringLiteral("rade/layout"), QStringLiteral("columns")).toString() !=
             QLatin1String("rows");

  variant_ = new QComboBox(this);
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

  bpf_ = new QCheckBox(QStringLiteral("BPF"), this);
  bpf_->setObjectName(QStringLiteral("radeBpf"));
  bpf_->setFont(uiFont());
  bpf_->setFocusPolicy(Qt::NoFocus);
  bpf_->setToolTip(QStringLiteral(
      "TX band-pass inside the modem, V2 only. V1 sends without one, the same as the "
      "FreeDV app, so the box is greyed out on V1. V2 is about 860 Hz and starts on. "
      "Turning V2 off sends the modem without that band-pass. "
      "This does not change the radio filter. This does not key. START is never sent."));

  waveLabel_ = new QLabel(QStringLiteral("Waveform"), this);
  waveLabel_->setFont(uiFont());
  auto* keySlot = new QWidget(this);
  keySlot->setObjectName(QStringLiteral("radeKeySlot"));
  auto* keyLayout = new QHBoxLayout(keySlot);
  keyLayout->setContentsMargins(6, 6, 6, 6);
  keyLayout->setSpacing(8);
  // RX and TX work like the radio's switch. TX is added after this from
  // the transmit panel. They sit first, larger than the other controls.
  rx_ = new QPushButton(QStringLiteral("RX"), keySlot);
  rx_->setObjectName(QStringLiteral("radeRx"));
  rx_->setFont(uiFont());
  rx_->setMinimumSize(104, 44);
  rx_->setFocusPolicy(Qt::NoFocus);
  rx_->setToolTip(QStringLiteral(
      "Green while receiving. While you transmit, click RX to end your over: the end-of-over with "
      "your callsign goes out first, the way FreeDV stations and FreeDV Reporter expect, RX shows "
      "amber for that moment, then turns green. Clicking TX again does the same. "
      "This does not key. START is never sent."));
  keyLayout->addWidget(rx_);
  connect(rx_, &QPushButton::clicked, this, [this] { emit rxRequested(); });
  keySlot->setStyleSheet(QStringLiteral(
      "QWidget#radeKeySlot { background: #142028; border: 1px solid #3d6a4a; border-radius: 6px; }"));

  // Activity scan: steps the FreeDV Reporter calling dials, low band to
  // high, and listens for a RADE signal to sync on each one.
  scan_ = new QPushButton(QStringLiteral("Scan"), this);
  scan_->setObjectName(QStringLiteral("radeScan"));
  scan_->setFont(uiFont());
  scan_->setCheckable(true);
  scan_->setFocusPolicy(Qt::NoFocus);
  scan_->setToolTip(QStringLiteral(
      "Scan the FreeDV Reporter calling dials, 160 m through 10 m, for RADE activity. Each dial "
      "is held for the Dwell time. When a station syncs, Stay on sync stops the scan there; with it "
      "off STD listens while the signal is in sync and then moves on. Click again to stop, or to "
      "start again from the next dial. Right-click picks the bands to scan, one or several. Dials "
      "you add with Extra are scanned too, after that band's reporter dials. "
      "Clicking a band button, TX, or changing mode also stops it. "
      "Scanning only moves the dial over TCI. It does not key. START is never sent."));
  scanDwell_ = new QSpinBox(this);
  scanDwell_->setObjectName(QStringLiteral("radeScanDwell"));
  scanDwell_->setFont(uiFont());
  scanDwell_->setRange(5, 30);
  scanDwell_->setSingleStep(5);
  scanDwell_->setSuffix(QStringLiteral(" s"));
  scanDwell_->setAlignment(Qt::AlignCenter);
  scanDwell_->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  scanDwell_->setMinimumWidth(96);
  scanDwell_->setFixedHeight(28);
  scanDwell_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  scanDwell_->setFocusPolicy(Qt::ClickFocus);
  scanDwell_->setStyleSheet(QStringLiteral(
      "QSpinBox#radeScanDwell { background: #162230; color: #e8eef4; "
      "border: 1px solid #4d8ab0; border-radius: 4px; padding: 1px 4px 1px 8px; }"
      "QSpinBox#radeScanDwell::up-button, QSpinBox#radeScanDwell::down-button {"
      " width: 18px; background: #1e3348; border-left: 1px solid #4d8ab0; }"));
  dwellLabel_ = new QLabel(QStringLiteral("Dwell"), this);
  dwellLabel_->setObjectName(QStringLiteral("radeDwellLabel"));
  dwellLabel_->setFont(uiFont());
  scanDwell_->setValue(std::clamp(QSettings().value(QStringLiteral("rade/scanDwell"), 15).toInt() / 5 * 5, 5, 30));
  scanDwell_->setToolTip(QStringLiteral(
      "How long Scan listens on each dial: 5 to 30 seconds in 5 second steps. RADE takes a second "
      "or two to sync, and stations pause between overs, so 15 s is a good start. Does not key."));
  dwellLabel_->setToolTip(scanDwell_->toolTip());
  scanStay_ = new QCheckBox(QStringLiteral("Stay on sync"), this);
  scanStay_->setObjectName(QStringLiteral("radeScanStay"));
  scanStay_->setFont(uiFont());
  scanStay_->setFocusPolicy(Qt::NoFocus);
  scanStay_->setChecked(QSettings().value(QStringLiteral("rade/scanStay"), true).toBool());
  scanStay_->setToolTip(QStringLiteral(
      "On: Scan stops on the first dial where a RADE station syncs, so you can listen and call. "
      "Off: Scan keeps looping; it stays while the station is in sync and moves on a few seconds "
      "after the signal drops. Does not key."));
  connect(scan_, &QPushButton::toggled, this, [this](bool on) { emit scanToggled(on); });
  scan_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(scan_, &QPushButton::customContextMenuRequested, this, [this](const QPoint&) { showScanBandMenu(); });
  refreshScanLabel();
  connect(scanDwell_, qOverload<int>(&QSpinBox::valueChanged), this,
          [](int value) { QSettings().setValue(QStringLiteral("rade/scanDwell"), value); });
  connect(scanStay_, &QCheckBox::toggled, this,
          [](bool on) { QSettings().setValue(QStringLiteral("rade/scanStay"), on); });

  lamp_ = new RadeSyncLamp(this);
  sync_ = new QLabel(QStringLiteral("Sync  idle"), this);
  sync_->setObjectName(QStringLiteral("radeSync"));
  sync_->setFont(uiFont());
  sync_->setToolTip(QStringLiteral(
      "Frame sync. Audio opens when sync is likely. There is no SNR squelch slider. "
      "The lamp is green while synced and dark red while not. "
      "This does not key. START is never sent."));
  snr_ = new QLabel(QStringLiteral("SNR  —"), this);
  snr_->setObjectName(QStringLiteral("radeSnr"));
  QFont snrFont = uiFont();
  snrFont.setPixelSize(22);
  snrFont.setBold(true);
  snr_->setFont(snrFont);
  snr_->setMinimumWidth(78);
  snr_->setToolTip(QStringLiteral(
      "SNR estimate, a whole number of dB. It does not mute. This does not key. START is never sent."));
  heard_ = new QLabel(QStringLiteral("Heard  —"), this);
  heard_->setObjectName(QStringLiteral("radeHeard"));
  heard_->setFont(uiFont());
  heard_->setToolTip(QStringLiteral(
      "The callsign in the last end-of-over that decoded, with the UTC time. FreeDV stations "
      "send it when they un-key. This does not key. START is never sent."));
  heardClock_ = new QLabel(QStringLiteral("—"), this);
  heardClock_->setObjectName(QStringLiteral("radeHeardClock"));
  heardClock_->setFont(uiFont());
  heardClock_->setToolTip(QStringLiteral(
      "When the last decode arrived, and how long ago. "
      "This does not key. START is never sent."));
  snrBar_ = new RadeSnrBar(this);
  snrTrace_ = new RadeSnrTrace(this);
  offset_ = new QLabel(QStringLiteral("Offset  —"), this);
  offset_->setObjectName(QStringLiteral("radeOffset"));
  offset_->setFont(uiFont());
  offset_->setToolTip(QStringLiteral(
      "How far the station you are receiving is from where it should be, in Hz, the same "
      "Freq offset the FreeDV app shows. Plus means it is above, minus below. RADE tracks only "
      "about 50 Hz either way, so keep it small: if every station reads the same offset, your "
      "own radio is off by that much in the other direction. This does not key. "
      "START is never sent."));
  autoRx_ = new QCheckBox(QStringLiteral("Auto RX"), this);
  autoRx_->setObjectName(QStringLiteral("radeAutoRx"));
  autoRx_->setFont(uiFont());
  autoRx_->setFocusPolicy(Qt::NoFocus);
  autoRx_->setToolTip(QStringLiteral(
      "Lowers the decoder input by 2 dB when the RX bar clips or its peaks go above -3 dBFS, "
      "and raises it by 1 dB when the peaks stay under -12. It stops at -30 dB. "
      "Off leaves the input alone. This does not key. START is never sent."));
  {
    const QSignalBlocker block(autoRx_);
    autoRx_->setChecked(QSettings().value(QStringLiteral("rade/autoRx"), true).toBool());
  }
  autoValue_ = new QLabel(autoRx_->isChecked() ? QStringLiteral("0") : QStringLiteral("off"), this);
  autoValue_->setObjectName(QStringLiteral("radeAutoRxValue"));
  autoValue_->setFont(uiFont());
  autoValue_->setMinimumWidth(28);
  autoValue_->setToolTip(autoRx_->toolTip());
  connect(autoRx_, &QCheckBox::toggled, this, [this](bool on) {
    QSettings().setValue(QStringLiteral("rade/autoRx"), on);
    if (autoValue_)
      autoValue_->setText(on ? QStringLiteral("0") : QStringLiteral("off"));
  });
  rxBar_ = new RadeRxBar(this);
  auto* heardTimer = new QTimer(this);
  heardTimer->setInterval(1000);
  connect(heardTimer, &QTimer::timeout, this, &RadeScreen::refreshHeardClock);
  heardTimer->start();

  auto* messageRow = new QHBoxLayout();
  auto* messageLabel = new QLabel(QStringLiteral("Reporter message"), this);
  messageLabel->setFont(uiFont());
  message_ = new QLineEdit(this);
  message_->setObjectName(QStringLiteral("radeReporterMessage"));
  message_->setFont(uiFont());
  message_->setMaxLength(80);
  message_->setMaximumWidth(420);
  message_->setPlaceholderText(QStringLiteral("e.g. Rick, Ohio, IC-7300 dipole"));
  message_->setToolTip(QStringLiteral(
      "The Message column on FreeDV Reporter, the same text as Options → Network Configuration. "
      "Press Enter or leave the box to send it; it goes out within a second while FreeDV Reporter "
      "is on. This does not key. START is never sent."));
  messageLabel->setToolTip(message_->toolTip());
  refreshMessage();
  connect(message_, &QLineEdit::editingFinished, this, [this] {
    QSettings().setValue(QStringLiteral("dx/freedvReporterMessage"), message_->text().trimmed());
    emit reporterMessageChanged();
  });
  auto* saveMessage = new QPushButton(QStringLiteral("Save"), this);
  saveMessage->setObjectName(QStringLiteral("radeMessageSave"));
  saveMessage->setFont(uiFont());
  saveMessage->setFocusPolicy(Qt::NoFocus);
  saveMessage->setToolTip(QStringLiteral(
      "Keep this reporter message. Up to 15 are saved, newest first. "
      "This does not key. START is never sent."));
  auto* dropMessage = new QPushButton(QStringLiteral("Remove"), this);
  dropMessage->setObjectName(QStringLiteral("radeMessageDrop"));
  dropMessage->setFont(uiFont());
  dropMessage->setFocusPolicy(Qt::NoFocus);
  dropMessage->setToolTip(QStringLiteral(
      "Drop the saved message shown in the list. The line above stays. "
      "This does not key. START is never sent."));
  savedMessages_ = new QComboBox(this);
  savedMessages_->setObjectName(QStringLiteral("radeSavedMessages"));
  savedMessages_->setFont(uiFont());
  savedMessages_->setFocusPolicy(Qt::NoFocus);
  savedMessages_->setMinimumWidth(180);
  savedMessages_->setMaximumWidth(280);
  savedMessages_->setToolTip(QStringLiteral(
      "Saved reporter messages, up to 15. Pick one to put it in the line. "
      "This does not key. START is never sent."));
  messageRow->addWidget(messageLabel);
  messageRow->addWidget(message_, 1);
  messageRow->addWidget(saveMessage);
  messageRow->addWidget(savedMessages_);
  messageRow->addWidget(dropMessage);
  layout->addLayout(messageRow);
  connect(saveMessage, &QPushButton::clicked, this, [this] {
    if (message_)
      storeReporterMessage(message_->text());
  });
  connect(dropMessage, &QPushButton::clicked, this, [this] {
    if (!savedMessages_ || savedMessages_->currentIndex() <= 0)
      return;
    QStringList saved = QSettings().value(QStringLiteral("dx/freedvReporterMessages")).toStringList();
    const int at = savedMessages_->currentIndex() - 1;
    if (at >= 0 && at < saved.size())
      saved.removeAt(at);
    QSettings().setValue(QStringLiteral("dx/freedvReporterMessages"), saved);
    reloadSavedMessages();
  });
  connect(savedMessages_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
    if (!message_ || !savedMessages_ || index <= 0)
      return;
    message_->setText(savedMessages_->itemText(index));
    QSettings().setValue(QStringLiteral("dx/freedvReporterMessage"), message_->text().trimmed());
    emit reporterMessageChanged();
  });
  reloadSavedMessages();

  auto* reporter = new QWidget(this);
  reporter->setObjectName(QStringLiteral("radeReporter"));
  auto* reporterLay = new QVBoxLayout(reporter);
  reporterLay->setContentsMargins(0, 0, 0, 0);
  reporterLay->setSpacing(4);
  auto* tools = new QHBoxLayout();
  tools->setSpacing(8);
  reporterFilter_ = new QLineEdit(reporter);
  reporterFilter_->setObjectName(QStringLiteral("radeReporterFilter"));
  reporterFilter_->setFont(uiFont());
  reporterFilter_->setPlaceholderText(QStringLiteral("Filter stations"));
  reporterFilter_->setClearButtonEnabled(true);
  reporterFilter_->setMaximumWidth(220);
  reporterFilter_->setToolTip(QStringLiteral(
      "Show only rows whose station, grid, mode, or message contains this text. "
      "Double-click a row to tune there. This does not key. START is never sent."));
  requestQsy_ = new QPushButton(QStringLiteral("Request QSY"), reporter);
  requestQsy_->setObjectName(QStringLiteral("radeRequestQsy"));
  requestQsy_->setFont(uiFont());
  requestQsy_->setFocusPolicy(Qt::NoFocus);
  requestQsy_->setEnabled(false);
  requestQsy_->setToolTip(QStringLiteral(
      "Ask the selected station to move to your dial. One request until you pick another row. "
      "FreeDV Reporter has to be on. This does not key. START is never sent."));
  ignoreQsy_ = new QCheckBox(QStringLiteral("Ignore requests"), reporter);
  ignoreQsy_->setObjectName(QStringLiteral("radeIgnoreQsy"));
  ignoreQsy_->setFont(uiFont());
  ignoreQsy_->setFocusPolicy(Qt::NoFocus);
  ignoreQsy_->setChecked(QSettings().value(QStringLiteral("dx/freedvReporterIgnoreQsy"), false).toBool());
  ignoreQsy_->setToolTip(QStringLiteral(
      "On: inbound QSY requests are dropped, including ones from a station you have not muted. "
      "Off: a request asks whether to tune there. Right-click a row to mute just that station. "
      "This does not key. START is never sent."));
  auto* logContact = new QPushButton(QStringLiteral("Log contact"), reporter);
  logContact->setObjectName(QStringLiteral("radeLogContact"));
  logContact->setFont(uiFont());
  logContact->setFocusPolicy(Qt::NoFocus);
  logContact->setToolTip(QStringLiteral(
      "Log the selected station. With no row selected, logs the last one heard. "
      "This does not key. START is never sent."));
  auto* limitLabel = new QLabel(QStringLiteral("TX limit"), reporter);
  limitLabel->setObjectName(QStringLiteral("radeTxLimitLabel"));
  limitLabel->setFont(uiFont());
  txLimit_ = new QSpinBox(reporter);
  txLimit_->setObjectName(QStringLiteral("radeTxLimit"));
  txLimit_->setFont(uiFont());
  txLimit_->setRange(0, 180);
  txLimit_->setSingleStep(15);
  txLimit_->setSuffix(QStringLiteral(" s"));
  txLimit_->setSpecialValueText(QStringLiteral("off"));
  txLimit_->setAlignment(Qt::AlignCenter);
  txLimit_->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  txLimit_->setMinimumWidth(96);
  txLimit_->setFixedHeight(28);
  txLimit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  txLimit_->setFocusPolicy(Qt::ClickFocus);
  txLimit_->setStyleSheet(QStringLiteral(
      "QSpinBox#radeTxLimit { background: #162230; color: #e8eef4; "
      "border: 1px solid #4d8ab0; border-radius: 4px; padding: 1px 4px 1px 8px; }"
      "QSpinBox#radeTxLimit::up-button, QSpinBox#radeTxLimit::down-button {"
      " width: 18px; background: #1e3348; border-left: 1px solid #4d8ab0; }"));
  {
    const int saved = QSettings().value(QStringLiteral("rade/txLimitSec"), 180).toInt();
    const int clamped = std::clamp(saved, 0, 180);
    txLimit_->setValue(clamped);
    if (clamped != saved)
      QSettings().setValue(QStringLiteral("rade/txLimitSec"), clamped);
  }
  txLimit_->setToolTip(QStringLiteral(
      "Longest RADE transmit, at most 3 minutes. Off leaves it unlimited. "
      "About 15 seconds before the end, a note appears, then RX ends the over. "
      "This does not send START."));
  limitLabel->setToolTip(txLimit_->toolTip());
  reporterCount_ = new QLabel(QStringLiteral("No stations yet"), reporter);
  reporterCount_->setObjectName(QStringLiteral("radeReporterCount"));
  reporterCount_->setFont(uiFont());
  tools->addWidget(reporterFilter_, 1);
  tools->addWidget(requestQsy_);
  tools->addWidget(ignoreQsy_);
  tools->addWidget(logContact);
  tools->addSpacing(8);
  tools->addWidget(limitLabel);
  tools->addWidget(txLimit_);
  tools->addStretch(1);
  tools->addWidget(reporterCount_);
  reporterLay->addLayout(tools);
  reporterTable_ = new QTableWidget(0, 7, reporter);
  reporterTable_->setObjectName(QStringLiteral("radeReporterTable"));
  reporterTable_->setFont(uiFont());
  reporterTable_->setHorizontalHeaderLabels(
      {QStringLiteral("Station"), QStringLiteral("Grid"), QStringLiteral("kHz"), QStringLiteral("Mode"),
       QStringLiteral("SNR"), QStringLiteral("TX"), QStringLiteral("Message")});
  reporterTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  reporterTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  reporterTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  reporterTable_->setAlternatingRowColors(true);
  reporterTable_->setShowGrid(false);
  reporterTable_->verticalHeader()->setVisible(false);
  reporterTable_->setMinimumHeight(140);
  reporterTable_->setToolTip(QStringLiteral(
      "Stations on FreeDV Reporter. Double-click tunes to that dial and sets DIGL on 160, 80, and 40. "
      "Right-click mutes QSY requests from that station. This does not key. START is never sent."));
  reporterTable_->setStyleSheet(QStringLiteral(
      "QTableWidget#radeReporterTable { background: #0e141c; color: #e8eef4; "
      "alternate-background-color: #121a24; selection-background-color: #1d4e89; "
      "selection-color: #ffffff; border: 1px solid #243240; }"
      "QHeaderView::section { background: #162230; color: #c5d4e0; border: 0; padding: 4px; }"));
  reporterTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  reporterTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  reporterTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  reporterTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
  reporterTable_->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
  reporterLay->addWidget(reporterTable_, 1);
  layout->addWidget(reporter, 1);
  connect(reporterFilter_, &QLineEdit::textChanged, this, [this] { applyReporterFilter(); });
  connect(ignoreQsy_, &QCheckBox::toggled, this, [](bool on) {
    QSettings().setValue(QStringLiteral("dx/freedvReporterIgnoreQsy"), on);
  });
  connect(txLimit_, qOverload<int>(&QSpinBox::valueChanged), this,
          [](int value) { QSettings().setValue(QStringLiteral("rade/txLimitSec"), value); });
  connect(requestQsy_, &QPushButton::clicked, this, &RadeScreen::requestSelectedQsy);
  connect(logContact, &QPushButton::clicked, this, &RadeScreen::logSelectedStation);
  connect(reporterTable_, &QTableWidget::itemSelectionChanged, this, [this] {
    const QString sid = selectedSid();
    if (requestQsy_)
      requestQsy_->setEnabled(!sid.isEmpty() && sid != qsySentSid_);
  });
  connect(reporterTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (!reporterTable_ || row < 0)
      return;
    auto* freq = reporterTable_->item(row, 2);
    const qint64 hz = freq ? freq->data(Qt::UserRole).toLongLong() : 0;
    if (hz > 0)
      emit stationTune(hz);
  });
  connect(reporterTable_->viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    if (!reporterTable_)
      return;
    const int row = reporterTable_->rowAt(pos.y());
    if (row < 0)
      return;
    auto* stationItem = reporterTable_->item(row, 0);
    if (!stationItem)
      return;
    const QString station = stationItem->text().trimmed();
    if (station.isEmpty())
      return;
    QStringList muted = QSettings().value(QStringLiteral("dx/freedvReporterMuteQsy")).toStringList();
    const bool mutedNow = muted.contains(station, Qt::CaseInsensitive);
    QMenu menu(this);
    QAction* action = menu.addAction(mutedNow ? QStringLiteral("Hear QSY from this station")
                                              : QStringLiteral("Ignore QSY from this station"));
    if (menu.exec(reporterTable_->viewport()->mapToGlobal(pos)) != action)
      return;
    if (mutedNow) {
      for (int i = muted.size() - 1; i >= 0; --i)
        if (muted.at(i).compare(station, Qt::CaseInsensitive) == 0)
          muted.removeAt(i);
    } else if (!muted.contains(station, Qt::CaseInsensitive)) {
      muted.append(station);
    }
    QSettings().setValue(QStringLiteral("dx/freedvReporterMuteQsy"), muted);
  });

  link_ = new QLabel(QStringLiteral("Not linked"), this);
  link_->setObjectName(QStringLiteral("radeLink"));
  link_->setToolTip(QStringLiteral(
      "librade, FARGAN, RNNoise and the bandwidth expander. "
      "freedv/rade_c c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9. "
      "Speech plays at 48 kHz. The modem is 8 kHz. "
      "Connect sends AUDIO_START:0. TX uses trx with tci. START is never sent."));
  link_->setFont(uiFont());
  link_->setWordWrap(false);

  const auto groupStyle = QStringLiteral(
      "QWidget#radeOperate, QWidget#radeMeterCol, QWidget#radeAudioHost {"
      " background: #0e141c; border: 1px solid #243240; border-radius: 6px; }");
  operate_ = new QWidget(this);
  operate_->setObjectName(QStringLiteral("radeOperate"));
  operate_->setAttribute(Qt::WA_StyledBackground, true);
  operate_->setStyleSheet(groupStyle);
  auto* operateHead = new QLabel(QStringLiteral("Waveform"), operate_);
  operateHead->setObjectName(QStringLiteral("radeOperateHead"));
  operateHead->setFont(uiFont());
  meterCol_ = new QWidget(this);
  meterCol_->setObjectName(QStringLiteral("radeMeterCol"));
  meterCol_->setAttribute(Qt::WA_StyledBackground, true);
  meterCol_->setStyleSheet(groupStyle);
  auto* meterHead = new QLabel(QStringLiteral("Status"), meterCol_);
  meterHead->setObjectName(QStringLiteral("radeMeterHead"));
  meterHead->setFont(uiFont());
  audioHost_ = new QWidget(this);
  audioHost_->setObjectName(QStringLiteral("radeAudioHost"));
  audioHost_->setAttribute(Qt::WA_StyledBackground, true);
  audioHost_->setStyleSheet(groupStyle);
  auto* audioLay = new QVBoxLayout(audioHost_);
  audioLay->setContentsMargins(8, 6, 8, 6);
  audioLay->setSpacing(4);
  auto* audioHead = new QLabel(QStringLiteral("Audio"), audioHost_);
  audioHead->setFont(uiFont());
  audioLay->addWidget(audioHead);
  body_ = new QWidget(this);
  body_->setObjectName(QStringLiteral("radeBody"));
  layout->insertWidget(1, body_);
  applyArrangement();
  refreshLayoutButton();

  connect(variant_, &QComboBox::currentIndexChanged, this, [this](int) {
    const QString mode = variant_->currentData().toString();
    if (showingV2_)
      rememberBpf(bpf_->isChecked());
    emit variantChosen(mode);
  });
  connect(bpf_, &QCheckBox::toggled, this, [this](bool on) {
    if (!showingV2_)
      return;
    rememberBpf(on);
    emit optionsChanged();
  });

  showMode(radeV1Name());
  setTxState(0);
}

void RadeScreen::refreshLayoutButton() {
  if (layoutPick_)
    layoutPick_->setText(columns_ ? QStringLiteral("Show rows") : QStringLiteral("Show columns"));
}

void RadeScreen::setColumns(bool columns) {
  columns_ = columns;
  QSettings().setValue(QStringLiteral("rade/layout"),
                       columns ? QStringLiteral("columns") : QStringLiteral("rows"));
  applyArrangement();
  refreshLayoutButton();
  if (audioProfile_)
    audioProfile_->setRadeAudioStacked(columns);
}

void RadeScreen::placeAudio(QWidget* bar, ModeProfile* profile) {
  audioBar_ = bar;
  audioProfile_ = profile;
  if (bar && audioHost_) {
    if (auto* lay = qobject_cast<QVBoxLayout*>(audioHost_->layout())) {
      if (lay->indexOf(bar) < 0)
        lay->addWidget(bar);
    }
    // Moving the strip onto this screen hides it. Show it again.
    bar->setVisible(true);
  }
  if (profile)
    profile->setRadeAudioStacked(columns_);
}

void RadeScreen::applyArrangement() {
  if (!operate_ || !meterCol_ || !body_)
    return;
  auto* keySlot = findChild<QWidget*>(QStringLiteral("radeKeySlot"));
  auto* operateHead = operate_->findChild<QLabel*>(QStringLiteral("radeOperateHead"));
  auto* meterHead = meterCol_->findChild<QLabel*>(QStringLiteral("radeMeterHead"));
  if (!keySlot || !operateHead || !meterHead)
    return;

  auto* operateLay = freshBox(operate_, columns_);
  operateHead->setVisible(columns_);
  if (columns_)
    operateLay->addWidget(operateHead);
  operateLay->addWidget(keySlot);
  if (columns_) {
    auto* wave = new QHBoxLayout();
    wave->setSpacing(8);
    wave->addWidget(waveLabel_);
    wave->addWidget(variant_, 1);
    wave->addWidget(bpf_);
    operateLay->addLayout(wave);
    operateLay->addWidget(scan_);
    auto* dwellRow = new QHBoxLayout();
    dwellRow->setSpacing(8);
    dwellRow->addWidget(dwellLabel_);
    dwellRow->addWidget(scanDwell_);
    dwellRow->addWidget(scanStay_);
    dwellRow->addStretch(1);
    operateLay->addLayout(dwellRow);
  } else {
    operateLay->addWidget(waveLabel_);
    operateLay->addWidget(variant_);
    operateLay->addWidget(bpf_);
    operateLay->addWidget(scan_);
    operateLay->addWidget(dwellLabel_);
    operateLay->addWidget(scanDwell_);
    operateLay->addWidget(scanStay_);
  }
  operateLay->addWidget(link_);
  if (!columns_)
    operateLay->addStretch(1);

  auto* meterLay = freshBox(meterCol_, columns_);
  meterHead->setVisible(columns_);
  if (columns_)
    meterLay->addWidget(meterHead);
  auto* syncRow = new QHBoxLayout();
  syncRow->setSpacing(6);
  syncRow->addWidget(lamp_);
  syncRow->addWidget(sync_);
  syncRow->addStretch(1);
  if (columns_)
    meterLay->addLayout(syncRow);
  else {
    meterLay->addWidget(lamp_);
    meterLay->addWidget(sync_);
  }
  if (columns_) {
    auto* snrRow = new QHBoxLayout();
    snrRow->setSpacing(8);
    snrRow->addWidget(snr_);
    snrRow->addWidget(snrBar_);
    snrRow->addStretch(1);
    meterLay->addLayout(snrRow);
    meterLay->addWidget(snrTrace_, 0, Qt::AlignLeft);
    meterLay->addWidget(offset_);
    auto* rxRow = new QHBoxLayout();
    rxRow->setSpacing(8);
    rxRow->addWidget(rxBar_);
    rxRow->addWidget(autoRx_);
    rxRow->addWidget(autoValue_);
    rxRow->addStretch(1);
    meterLay->addLayout(rxRow);
    auto* heardRow = new QHBoxLayout();
    heardRow->setSpacing(8);
    heardRow->addWidget(heard_);
    heardRow->addWidget(heardClock_);
    heardRow->addStretch(1);
    meterLay->addLayout(heardRow);
    meterLay->addStretch(1);
  } else {
    meterLay->addWidget(snr_);
    meterLay->addWidget(snrBar_);
    meterLay->addWidget(snrTrace_);
    meterLay->addWidget(offset_);
    meterLay->addWidget(rxBar_);
    meterLay->addWidget(autoRx_);
    meterLay->addWidget(autoValue_);
    meterLay->addWidget(heard_);
    meterLay->addWidget(heardClock_);
    meterLay->addStretch(1);
  }

  if (QLayout* old = body_->layout()) {
    detachLayout(old);
    delete old;
  }
  if (columns_) {
    auto* row = new QHBoxLayout(body_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(audioHost_, 3);
    row->addWidget(meterCol_, 4);
    row->addWidget(operate_, 3);
  } else {
    auto* col = new QVBoxLayout(body_);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(8);
    auto* top = new QHBoxLayout();
    top->setSpacing(8);
    top->addWidget(operate_, 1);
    top->addWidget(meterCol_, 1);
    col->addLayout(top);
    col->addWidget(audioHost_);
    rowsStacked_ = false;
    fitRows();
  }
}

void RadeScreen::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  fitRows();
}

void RadeScreen::fitRows() {
  if (columns_ || !body_ || !operate_ || !meterCol_)
    return;
  auto* outer = qobject_cast<QVBoxLayout*>(body_->layout());
  if (!outer || outer->count() < 2)
    return;
  const int width = body_->width();
  if (width < 80)
    return;
  const int need = operate_->minimumSizeHint().width() + meterCol_->minimumSizeHint().width() + 24;
  const bool stack = rowsStacked_ ? width < need + 48 : width < need;
  if (stack == rowsStacked_)
    return;
  QLayoutItem* top = outer->takeAt(0);
  if (!top)
    return;
  // The item owns its child layout. Empty that layout, then delete the item once.
  if (QLayout* lay = top->layout())
    detachLayout(lay);
  delete top;
  rowsStacked_ = stack;
  if (stack) {
    auto* col = new QVBoxLayout();
    col->setSpacing(8);
    col->addWidget(operate_);
    col->addWidget(meterCol_);
    outer->insertLayout(0, col);
  } else {
    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(operate_, 1);
    row->addWidget(meterCol_, 1);
    outer->insertLayout(0, row);
  }
  operate_->setVisible(true);
  meterCol_->setVisible(true);
  if (audioHost_)
    audioHost_->setVisible(true);
}

void RadeScreen::loadBpf(bool v2) {
  const QSignalBlocker block(bpf_);
  const QString key = v2 ? QStringLiteral("rade/bpfV2") : QStringLiteral("rade/bpfV1");
  // FreeDV's V1 transmitter has no band-pass switch, so the box is only live on V2.
  bpf_->setChecked(v2 && QSettings().value(key, true).toBool());
  bpf_->setEnabled(v2);
}

void RadeScreen::rememberBpf(bool on) {
  QSettings().setValue(showingV2_ ? QStringLiteral("rade/bpfV2") : QStringLiteral("rade/bpfV1"), on);
}

void RadeScreen::setLinkReady(bool ready) {
  if (!link_)
    return;
  link_->setText(ready ? QStringLiteral("Linked") : QStringLiteral("Not linked"));
}

void RadeScreen::setSync(bool on, float snrDb, float offsetHz) {
  if (sync_)
    sync_->setText(on ? QStringLiteral("Sync  yes") : QStringLiteral("Sync  idle"));
  if (auto* lamp = findChild<RadeSyncLamp*>(QStringLiteral("radeSyncLamp")))
    lamp->setSynced(on);
  if (offset_)
    offset_->setText(on ? QStringLiteral("Offset  %1%2 Hz")
                              .arg(offsetHz >= 0.0f ? QStringLiteral("+") : QString())
                              .arg(QString::number(double(offsetHz), 'f', 0))
                        : QStringLiteral("Offset  —"));
  if (snr_)
    snr_->setText(on ? QStringLiteral("SNR  %1").arg(qRound(snrDb)) : QStringLiteral("SNR  —"));
  if (auto* bar = findChild<RadeSnrBar*>(QStringLiteral("radeSnrBar")))
    bar->setSnr(on, snrDb);
  if (auto* trace = findChild<RadeSnrTrace*>(QStringLiteral("radeSnrTrace")))
    trace->push(on, snrDb);
}

void RadeScreen::setRxLevel(float db, bool clip, int autoDb) {
  if (auto* bar = findChild<RadeRxBar*>(QStringLiteral("radeRxLevel")))
    bar->setLevel(db, clip);
  if (auto* value = findChild<QLabel*>(QStringLiteral("radeAutoRxValue"))) {
    auto* box = findChild<QCheckBox*>(QStringLiteral("radeAutoRx"));
    value->setText(box && box->isChecked() ? QString::number(autoDb) : QStringLiteral("off"));
  }
}

void RadeScreen::setTxState(int state) {
  if (!rx_)
    return;
  if (rx_->property("txState").toInt() == state && !rx_->styleSheet().isEmpty())
    return;
  rx_->setProperty("txState", state);
  if (state == 0)
    rx_->setStyleSheet(QStringLiteral(
        "QPushButton#radeRx { background: #0f5132; color: #d1ffe0; border: 1px solid #2ecc71; "
        "border-radius: 4px; padding: 8px 22px; font-weight: bold; font-size: 15px; }"));
  else if (state == 2)
    rx_->setStyleSheet(QStringLiteral(
        "QPushButton#radeRx { background: #5c4400; color: #ffe08a; border: 1px solid #f0b400; "
        "border-radius: 4px; padding: 8px 22px; font-weight: bold; font-size: 15px; }"));
  else
    rx_->setStyleSheet(QStringLiteral(
        "QPushButton#radeRx { background: transparent; color: #8aa0a8; border: 1px solid #33464d; "
        "border-radius: 4px; padding: 8px 22px; font-size: 15px; }"));
  rx_->setText(state == 2 ? QStringLiteral("RX…") : QStringLiteral("RX"));
}

void RadeScreen::setScanning(bool on) {
  if (!scan_)
    return;
  const QSignalBlocker block(scan_);
  scan_->setChecked(on);
  refreshScanLabel();
}

QStringList RadeScreen::scanBands() const {
  const QStringList all = ModeProfile::radeScanBands();
  QStringList chosen;
  for (const QString& band : QSettings().value(QStringLiteral("rade/scanBands")).toStringList())
    if (all.contains(band) && !chosen.contains(band))
      chosen << band;
  return chosen.size() == all.size() ? QStringList() : chosen;
}

void RadeScreen::refreshScanLabel() {
  if (!scan_)
    return;
  const QStringList bands = scanBands();
  QString which;
  if (bands.size() == 1)
    which = QStringLiteral(" %1 m").arg(bands.first());
  else if (!bands.isEmpty())
    which = QStringLiteral(" %1 bands").arg(bands.size());
  scan_->setText((scan_->isChecked() ? QStringLiteral("Scanning") : QStringLiteral("Scan")) + which);
}

void RadeScreen::showScanBandMenu() {
  // Check boxes in the menu, so it stays open while bands are picked.
  QMenu menu(this);
  const QStringList all = ModeProfile::radeScanBands();
  QStringList chosen = scanBands();
  if (chosen.isEmpty())
    chosen = all;
  QList<QCheckBox*> boxes;
  auto* allBox = new QCheckBox(QStringLiteral("All bands"), &menu);
  auto addBox = [&menu](QCheckBox* box) {
    box->setFont(uiFont());
    box->setStyleSheet(QStringLiteral("QCheckBox { padding: 3px 12px; }"));
    auto* action = new QWidgetAction(&menu);
    action->setDefaultWidget(box);
    menu.addAction(action);
  };
  addBox(allBox);
  menu.addSeparator();
  for (const QString& band : all) {
    auto* box = new QCheckBox(QStringLiteral("%1 m").arg(band), &menu);
    box->setProperty("band", band);
    box->setChecked(chosen.contains(band));
    boxes << box;
    addBox(box);
  }
  allBox->setChecked(chosen.size() == all.size());
  auto save = [this, &boxes, allBox] {
    QStringList picked;
    for (QCheckBox* box : boxes)
      if (box->isChecked())
        picked << box->property("band").toString();
    const QSignalBlocker block(allBox);
    allBox->setChecked(picked.size() == boxes.size());
    QSettings().setValue(QStringLiteral("rade/scanBands"), picked);
    refreshScanLabel();
    emit scanBandsChanged();
  };
  connect(allBox, &QCheckBox::toggled, &menu, [&boxes, save](bool on) {
    for (QCheckBox* box : boxes) {
      const QSignalBlocker block(box);
      box->setChecked(on);
    }
    // Scan needs a band. Unchecking All leaves 20 m, the busiest.
    if (!on)
      for (QCheckBox* box : boxes)
        if (box->property("band").toString() == QLatin1String("20")) {
          const QSignalBlocker block(box);
          box->setChecked(true);
        }
    save();
  });
  for (QCheckBox* box : boxes) {
    connect(box, &QCheckBox::toggled, &menu, [&boxes, box, save](bool on) {
      if (!on) {
        bool any = false;
        for (QCheckBox* other : boxes)
          any = any || other->isChecked();
        if (!any) {
          // The last band stays on.
          const QSignalBlocker block(box);
          box->setChecked(true);
          return;
        }
      }
      save();
    });
  }
  menu.exec(QCursor::pos());
}

int RadeScreen::scanDwellSeconds() const {
  return scanDwell_ ? scanDwell_->value() : 15;
}

bool RadeScreen::scanStay() const {
  return !scanStay_ || scanStay_->isChecked();
}

void RadeScreen::refreshMessage() {
  if (!message_ || message_->hasFocus())
    return;
  message_->setText(QSettings().value(QStringLiteral("dx/freedvReporterMessage")).toString());
}

void RadeScreen::setHeard(const QString& call, const QString& utc) {
  lastHeard_ = call.trimmed();
  if (heard_)
    heard_->setText(call.isEmpty() ? QStringLiteral("Heard  —")
                                   : QStringLiteral("Heard  %1  %2").arg(call, utc));
  heardAtMs_ = call.isEmpty() ? 0 : QDateTime::currentMSecsSinceEpoch();
  refreshHeardClock();
}

int RadeScreen::savedMessageCount() const {
  return QSettings().value(QStringLiteral("dx/freedvReporterMessages")).toStringList().size();
}

void RadeScreen::storeReporterMessage(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty())
    return;
  QStringList saved = QSettings().value(QStringLiteral("dx/freedvReporterMessages")).toStringList();
  saved.removeAll(trimmed);
  saved.prepend(trimmed);
  while (saved.size() > kSavedMessages)
    saved.removeLast();
  QSettings().setValue(QStringLiteral("dx/freedvReporterMessages"), saved);
  reloadSavedMessages();
}

void RadeScreen::reloadSavedMessages() {
  if (!savedMessages_)
    return;
  const QString current = savedMessages_->currentIndex() > 0 ? savedMessages_->currentText() : QString();
  const QSignalBlocker block(savedMessages_);
  savedMessages_->clear();
  savedMessages_->addItem(QStringLiteral("Saved messages"));
  const QStringList saved = QSettings().value(QStringLiteral("dx/freedvReporterMessages")).toStringList();
  for (int i = 0; i < saved.size() && i < kSavedMessages; ++i)
    savedMessages_->addItem(saved.at(i));
  const int at = savedMessages_->findText(current);
  savedMessages_->setCurrentIndex(at > 0 ? at : 0);
}

void RadeScreen::releaseQsyRequest() {
  qsySentSid_.clear();
  if (requestQsy_)
    requestQsy_->setEnabled(!selectedSid().isEmpty());
}

bool RadeScreen::ignoreQsy() const {
  return ignoreQsy_ && ignoreQsy_->isChecked();
}

int RadeScreen::txLimitSeconds() const {
  return txLimit_ ? txLimit_->value() : 180;
}

QString RadeScreen::selectedSid() const {
  if (!reporterTable_)
    return QString();
  const int row = reporterTable_->currentRow();
  if (row < 0)
    return QString();
  const auto* item = reporterTable_->item(row, 0);
  return item ? item->data(Qt::UserRole).toString() : QString();
}

void RadeScreen::applyReporterFilter() {
  if (!reporterTable_ || !reporterFilter_)
    return;
  const QString needle = reporterFilter_->text().trimmed();
  int shown = 0;
  for (int row = 0; row < reporterTable_->rowCount(); ++row) {
    bool match = needle.isEmpty();
    if (!match) {
      for (int col : {0, 1, 3, 6}) {
        const auto* item = reporterTable_->item(row, col);
        if (item && item->text().contains(needle, Qt::CaseInsensitive)) {
          match = true;
          break;
        }
      }
    }
    reporterTable_->setRowHidden(row, !match);
    if (match)
      ++shown;
  }
  if (reporterCount_)
    reporterCount_->setText(reporterTable_->rowCount() == 0
                                ? QStringLiteral("No stations yet")
                                : QStringLiteral("%1 shown").arg(shown));
}

void RadeScreen::showReporter(const QVector<FreeDvReporter::Station>& rows) {
  if (!reporterTable_)
    return;
  const QString keep = selectedSid();
  reporterTable_->setSortingEnabled(false);
  reporterTable_->setRowCount(rows.size());
  for (int i = 0; i < rows.size(); ++i) {
    const FreeDvReporter::Station& row = rows.at(i);
    auto* station = new QTableWidgetItem(row.station);
    station->setData(Qt::UserRole, row.sid);
    auto* grid = new QTableWidgetItem(row.grid);
    auto* freq = new RadeFreqItem;
    freq->setData(Qt::UserRole, row.freqHz);
    freq->setText(row.freqHz > 0 ? QString::number(double(row.freqHz) / 1000.0, 'f', 1) : QString());
    freq->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* mode = new QTableWidgetItem(row.mode);
    auto* snr = new RadeSnrItem;
    snr->setData(Qt::UserRole, row.snrKnown ? row.snr : -999);
    snr->setText(row.snrKnown ? QString::number(row.snr) : QStringLiteral("—"));
    snr->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* tx = new QTableWidgetItem(row.transmitting ? QStringLiteral("TX") : QString());
    auto* message = new QTableWidgetItem(row.message);
    QTableWidgetItem* cells[] = {station, grid, freq, mode, snr, tx, message};
    for (int col = 0; col < 7; ++col) {
      cells[col]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
      reporterTable_->setItem(i, col, cells[col]);
    }
    if (!keep.isEmpty() && row.sid == keep)
      reporterTable_->setCurrentCell(i, 0);
  }
  reporterTable_->setSortingEnabled(true);
  applyReporterFilter();
  if (requestQsy_)
    requestQsy_->setEnabled(!selectedSid().isEmpty() && selectedSid() != qsySentSid_);
}

void RadeScreen::requestSelectedQsy() {
  const QString sid = selectedSid();
  if (sid.isEmpty())
    return;
  qsySentSid_ = sid;
  if (requestQsy_)
    requestQsy_->setEnabled(false);
  emit stationQsy(sid);
}

void RadeScreen::logSelectedStation() {
  QString station = lastHeard_;
  QString grid;
  int snr = -999;
  qint64 hz = 0;
  if (reporterTable_ && reporterTable_->currentRow() >= 0) {
    const int row = reporterTable_->currentRow();
    auto text = [this, row](int col) {
      const auto* item = reporterTable_->item(row, col);
      return item ? item->text().trimmed() : QString();
    };
    if (!text(0).isEmpty())
      station = text(0);
    grid = text(1);
    const auto* freq = reporterTable_->item(row, 2);
    hz = freq ? freq->data(Qt::UserRole).toLongLong() : 0;
    const auto* snrItem = reporterTable_->item(row, 4);
    if (snrItem && snrItem->text() != QLatin1String("—"))
      snr = snrItem->data(Qt::UserRole).toInt();
  }
  emit stationLog(station, grid, snr, hz);
}

void RadeScreen::refreshHeardClock() {
  if (!heardClock_)
    return;
  if (heardAtMs_ <= 0) {
    heardClock_->setText(QStringLiteral("—"));
    return;
  }
  const QDateTime at = QDateTime::fromMSecsSinceEpoch(heardAtMs_, QTimeZone::UTC);
  const qint64 secs = std::max(qint64(0), at.secsTo(QDateTime::currentDateTimeUtc()));
  QString age = QStringLiteral("just now");
  if (secs >= 3600)
    age = QStringLiteral("%1 h ago").arg(secs / 3600);
  else if (secs >= 45)
    age = QStringLiteral("%1 min ago").arg(std::max(qint64(1), secs / 60));
  heardClock_->setText(QStringLiteral("%1 UTC  %2").arg(at.toString(QStringLiteral("HH:mm")), age));
}

void RadeScreen::showMode(const QString& modeName) {
  showingV2_ = radeV2(modeName);
  const QSignalBlocker block(variant_);
  const int index = variant_->findData(showingV2_ ? radeV2Name() : radeV1Name());
  if (index >= 0)
    variant_->setCurrentIndex(index);
  loadBpf(showingV2_);
}
