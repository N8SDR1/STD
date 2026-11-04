// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rade/RadeScreen.h"
#include "rade/RadeMeters.h"

#include "DigitalMode.h"
#include "ModeProfile.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCursor>
#include <QMenu>
#include <QWidgetAction>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

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
      "about 860 Hz, and will not decode V1. The speech-level bar is beside Mic. Drive is between Mic and Speaker. "
      "Analog is after Volume. BPF is after Waveform. RX and TX are on this row; click RX to end your over. The band buttons set DIGU or DIGL. "
      "The mic is 16 kHz, speech plays at 48 kHz. The modem is 8 kHz."),
      this);
  intro->setObjectName(QStringLiteral("radeIntro"));
  intro->setFont(uiFont());
  intro->setWordWrap(true);
  layout->addWidget(intro);

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

  auto* waveLabel = new QLabel(QStringLiteral("Waveform"), this);
  waveLabel->setFont(uiFont());
  auto* pair = new QHBoxLayout;
  pair->setContentsMargins(0, 0, 0, 0);
  pair->setSpacing(8);
  auto* keySlot = new QWidget(this);
  keySlot->setObjectName(QStringLiteral("radeKeySlot"));
  auto* keyLayout = new QHBoxLayout(keySlot);
  keyLayout->setContentsMargins(0, 0, 0, 0);
  keyLayout->setSpacing(6);
  // RX and TX work like the radio's switch. TX is added after this from
  // the transmit panel.
  rx_ = new QPushButton(QStringLiteral("RX"), keySlot);
  rx_->setObjectName(QStringLiteral("radeRx"));
  rx_->setFont(uiFont());
  rx_->setFocusPolicy(Qt::NoFocus);
  rx_->setToolTip(QStringLiteral(
      "Green while receiving. While you transmit, click RX to end your over: the end-of-over with "
      "your callsign goes out first, the way FreeDV stations and FreeDV Reporter expect, RX shows "
      "amber for that moment, then turns green. Clicking TX again does the same. "
      "This does not key. START is never sent."));
  keyLayout->addWidget(rx_);
  connect(rx_, &QPushButton::clicked, this, [this] { emit rxRequested(); });
  pair->addWidget(waveLabel);
  pair->addWidget(variant_);
  pair->addSpacing(12);
  pair->addWidget(bpf_);
  pair->addSpacing(16);
  pair->addWidget(keySlot);
  pair->addSpacing(24);

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
  scanDwell_->setPrefix(QStringLiteral("Dwell "));
  scanDwell_->setSuffix(QStringLiteral(" s"));
  scanDwell_->setFocusPolicy(Qt::ClickFocus);
  scanDwell_->setValue(std::clamp(QSettings().value(QStringLiteral("rade/scanDwell"), 15).toInt() / 5 * 5, 5, 30));
  scanDwell_->setToolTip(QStringLiteral(
      "How long Scan listens on each dial: 5 to 30 seconds in 5 second steps. RADE takes a second "
      "or two to sync, and stations pause between overs, so 15 s is a good start. Does not key."));
  scanStay_ = new QCheckBox(QStringLiteral("Stay on sync"), this);
  scanStay_->setObjectName(QStringLiteral("radeScanStay"));
  scanStay_->setFont(uiFont());
  scanStay_->setFocusPolicy(Qt::NoFocus);
  scanStay_->setChecked(QSettings().value(QStringLiteral("rade/scanStay"), true).toBool());
  scanStay_->setToolTip(QStringLiteral(
      "On: Scan stops on the first dial where a RADE station syncs, so you can listen and call. "
      "Off: Scan keeps looping; it stays while the station is in sync and moves on a few seconds "
      "after the signal drops. Does not key."));
  pair->addWidget(scan_);
  pair->addWidget(scanDwell_);
  pair->addWidget(scanStay_);
  pair->addStretch(1);
  connect(scan_, &QPushButton::toggled, this, [this](bool on) { emit scanToggled(on); });
  scan_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(scan_, &QPushButton::customContextMenuRequested, this, [this](const QPoint&) { showScanBandMenu(); });
  refreshScanLabel();
  connect(scanDwell_, qOverload<int>(&QSpinBox::valueChanged), this,
          [](int value) { QSettings().setValue(QStringLiteral("rade/scanDwell"), value); });
  connect(scanStay_, &QCheckBox::toggled, this,
          [](bool on) { QSettings().setValue(QStringLiteral("rade/scanStay"), on); });
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
  heard_ = new QLabel(QStringLiteral("Heard  —"), this);
  heard_->setObjectName(QStringLiteral("radeHeard"));
  heard_->setFont(uiFont());
  heard_->setToolTip(QStringLiteral(
      "The callsign in the last end-of-over that decoded, with the UTC time. FreeDV stations "
      "send it when they un-key. This does not key. START is never sent."));
  meters->addWidget(sync_);
  meters->addSpacing(16);
  meters->addWidget(snr_);
  meters->addSpacing(16);
  auto* snrTrace = new RadeSnrTrace(this);
  offset_ = new QLabel(QStringLiteral("Offset  —"), this);
  offset_->setObjectName(QStringLiteral("radeOffset"));
  offset_->setFont(uiFont());
  offset_->setToolTip(QStringLiteral(
      "How far the station you are receiving is from where it should be, in Hz, the same "
      "Freq offset the FreeDV app shows. Plus means it is above, minus below. RADE tracks only "
      "about 50 Hz either way, so keep it small: if every station reads the same offset, your "
      "own radio is off by that much in the other direction. This does not key. "
      "START is never sent."));
  meters->addWidget(offset_);
  meters->addSpacing(16);
  meters->addWidget(heard_);
  meters->addSpacing(16);
  meters->addWidget(snrTrace, 0, Qt::AlignVCenter);
  meters->addStretch(1);
  layout->addLayout(meters);

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
  });
  messageRow->addWidget(messageLabel);
  messageRow->addWidget(message_, 1);
  messageRow->addStretch(1);
  layout->addLayout(messageRow);

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
  link_->setText(ready ? QStringLiteral(
                             "librade, FARGAN, RNNoise and the bandwidth expander are linked. "
                             "Speech plays at 48 kHz. The modem is 8 kHz. "
                             "Connect sends AUDIO_START:0. TX uses trx with tci. START is never sent.")
                       : QStringLiteral(
                             "librade did not load. TX stays off. Sync stays idle. "
                             "Connect still sends AUDIO_START:0. START is never sent."));
}

void RadeScreen::setSync(bool on, float snrDb, float offsetHz) {
  if (sync_)
    sync_->setText(on ? QStringLiteral("Sync  yes") : QStringLiteral("Sync  idle"));
  if (offset_)
    offset_->setText(on ? QStringLiteral("Offset  %1%2 Hz")
                              .arg(offsetHz >= 0.0f ? QStringLiteral("+") : QString())
                              .arg(QString::number(double(offsetHz), 'f', 0))
                        : QStringLiteral("Offset  —"));
  if (!snr_)
    return;
  if (on)
    snr_->setText(QStringLiteral("SNR  %1 dB").arg(QString::number(double(snrDb), 'f', 1)));
  else
    snr_->setText(QStringLiteral("SNR  —"));
  if (auto* trace = findChild<RadeSnrTrace*>(QStringLiteral("radeSnrTrace")))
    trace->push(on, snrDb);
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
        "border-radius: 4px; padding: 3px 12px; font-weight: bold; }"));
  else if (state == 2)
    rx_->setStyleSheet(QStringLiteral(
        "QPushButton#radeRx { background: #5c4400; color: #ffe08a; border: 1px solid #f0b400; "
        "border-radius: 4px; padding: 3px 12px; font-weight: bold; }"));
  else
    rx_->setStyleSheet(QStringLiteral(
        "QPushButton#radeRx { background: transparent; color: #8aa0a8; border: 1px solid #33464d; "
        "border-radius: 4px; padding: 3px 12px; }"));
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
  if (heard_)
    heard_->setText(call.isEmpty() ? QStringLiteral("Heard  —")
                                   : QStringLiteral("Heard  %1  %2").arg(call, utc));
}

void RadeScreen::showMode(const QString& modeName) {
  showingV2_ = radeV2(modeName);
  const QSignalBlocker block(variant_);
  const int index = variant_->findData(showingV2_ ? radeV2Name() : radeV1Name());
  if (index >= 0)
    variant_->setCurrentIndex(index);
  loadBpf(showingV2_);
}
