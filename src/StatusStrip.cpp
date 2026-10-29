// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DigitalMode.h"
#include "StatusStrip.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>

namespace {

QFont monoFont(int pixelSize) {
  QFont font("JetBrains Mono");
  font.setStyleHint(QFont::Monospace);
  font.setPixelSize(pixelSize);
  return font;
}

class PttLamp : public QPushButton {
public:
  explicit PttLamp(QWidget* parent = nullptr) : QPushButton(parent) {
    setObjectName("ptt");
    setCheckable(true);
    setChecked(false);
    setEnabled(false);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setToolTip("Off. The radio is not connected.");
    setAccessibleName("PTT");
    setAccessibleDescription("Off. The radio is not connected.");
    setFixedSize(112, 28);
  }

  QSize sizeHint() const override { return QSize(112, 28); }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool onAir = isChecked();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(onAir ? QColor("#ffc48a") : QColor("#6a4a2a"), 1));
    painter.setBrush(onAir ? QColor("#e08a3d") : QColor("#14120f"));
    painter.drawRoundedRect(box, 13, 13);

    const QPointF lamp(16.0, height() / 2.0);
    if (onAir) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(255, 196, 138, 90));
      painter.drawEllipse(lamp, 9.0, 9.0);
    }
    painter.setPen(QPen(onAir ? QColor("#fff1df") : QColor("#5a4030"), 1));
    painter.setBrush(onAir ? QColor("#ffd7a8") : QColor("#2a211c"));
    painter.drawEllipse(lamp, 4.5, 4.5);

    painter.setFont(monoFont(11));
    painter.setPen(onAir ? QColor("#2a1404") : QColor("#e7c7a4"));
    painter.drawText(rect().adjusted(28, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     onAir ? QStringLiteral("ON AIR") : QStringLiteral("PTT"));
  }
};

void showDigitalMode(QPushButton* chip, const QString& mode) {
  const QString text = mode.isEmpty() ? QStringLiteral("FT8") : mode;
  chip->setText(text);
  const int width = chip->fontMetrics().horizontalAdvance(text) + 20;
  chip->setFixedWidth(std::max(52, width));
  QString fill = QStringLiteral("#245068");
  QString ink = QStringLiteral("#f4fdff");
  if (text == QLatin1String("FT8"))
    fill = QStringLiteral("#0b8f9e");
  else if (text == QLatin1String("FT2"))
    fill = QStringLiteral("#6d5efc");
  else if (text == QLatin1String("FT4")) {
    fill = QStringLiteral("#e08a3d");
    ink = QStringLiteral("#2a1404");
  } else if (text.contains(QLatin1String("Hell"))) {
    fill = QStringLiteral("#c4a35a");
    ink = QStringLiteral("#2a1404");
  } else {
    ink = QStringLiteral("#d5e6ef");
  }
  chip->setStyleSheet(QStringLiteral(
      "QPushButton#digitalModeChip { background: %1; color: %2; border: none; border-radius: 11px; padding: 0 8px; }"
      "QPushButton#digitalModeChip:hover { border: 1px solid #ffffff; }")
                          .arg(fill, ink));
  chip->setAccessibleName(QStringLiteral("Digital mode %1").arg(text));
}

QPushButton* digitalModeChip(QWidget* parent) {
  auto* chip = new QPushButton(parent);
  chip->setObjectName("digitalModeChip");
  chip->setCursor(Qt::PointingHandCursor);
  chip->setFocusPolicy(Qt::NoFocus);
  QFont font = monoFont(12);
  font.setBold(true);
  chip->setFont(font);
  chip->setFixedHeight(22);
  chip->setToolTip(QStringLiteral(
      "Active digital mode. Click to cycle FT8, FT4, FT2, the Hell list, RTTY, the PSK speeds, "
      "Olivia, Contestia, and WEFAX-576. This does not key."));
  showDigitalMode(chip, QStringLiteral("FT8"));
  return chip;
}

QWidget* readout(const QString& label, const QString& valueName, const QString& value) {
  auto* group = new QWidget;
  auto* layout = new QHBoxLayout(group);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  auto* name = new QLabel(label);
  name->setObjectName("readoutLabel");
  QFont small("Inter");
  small.setPixelSize(10);
  small.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
  name->setFont(small);

  auto* reading = new QLabel(value);
  reading->setObjectName(valueName);
  reading->setFont(monoFont(13));

  layout->addWidget(name);
  layout->addWidget(reading);
  return group;
}

class RxLevelMeter : public QWidget {
public:
  explicit RxLevelMeter(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName("rxLevelMeter");
    setFixedSize(72, 14);
    setToolTip(QStringLiteral("Averaged receive level. Green is about -1 to 0 dB. This does not key."));
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(QColor("#3a4a55"), 1));
    painter.setBrush(QColor("#14120f"));
    painter.drawRoundedRect(box, 2, 2);

    const int zone = property("zone").toInt();
    const double fraction = std::clamp(property("fraction").toDouble(), 0.0, 1.0);
    QColor fill("#5a4030");
    if (zone == 2)
      fill = QColor("#3dbe78");
    else if (zone == 3)
      fill = QColor("#e05a5a");
    else if (zone == 1)
      fill = QColor("#e0b15a");
    if (zone != 0 && fraction > 0.0) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(fill);
      const QRectF inner = box.adjusted(1, 1, -1, -1);
      painter.drawRect(QRectF(inner.left(), inner.top(), inner.width() * fraction, inner.height()));
    }
    // -1 dB on a -6..0 scale.
    const double tick = box.left() + box.width() * (5.0 / 6.0);
    painter.setPen(QPen(QColor("#d5e6ef"), 1));
    painter.drawLine(QPointF(tick, box.top() + 1), QPointF(tick, box.bottom() - 1));
  }
};

QSpinBox* gainSpin(QWidget* parent, const char* name, const QString& prefix, const char* key,
                   const QString& tip) {
  QSettings settings;
  const int saved = std::clamp(settings.value(QLatin1String(key), 0).toInt(), -20, 40);
  auto* spin = new QSpinBox(parent);
  spin->setObjectName(QLatin1String(name));
  spin->setRange(-20, 40);
  spin->setSingleStep(1);
  spin->setPrefix(prefix);
  spin->setSuffix(QStringLiteral(" dB"));
  spin->setValue(saved);
  spin->setFixedSize(96, 22);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
  spin->setFont(monoFont(11));
  spin->setToolTip(tip);
  spin->setFocusPolicy(Qt::ClickFocus);
  return spin;
}

QFrame* separator() {
  auto* line = new QFrame;
  line->setObjectName("statusSep");
  line->setFrameShape(QFrame::NoFrame);
  line->setFixedWidth(1);
  line->setMinimumHeight(18);
  return line;
}

}  // namespace

StatusStrip::StatusStrip(QWidget* parent) : QWidget(parent) {
  setObjectName("statusStrip");
  setAttribute(Qt::WA_StyledBackground, true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(40);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(12, 0, 12, 0);
  layout->setSpacing(12);

  auto* modeGroup = readout("MODE", "modeValue", QString::fromUtf8("—"));
  modeGroup->layout()->addWidget(digitalModeChip(modeGroup));
  layout->addWidget(modeGroup);
  layout->addWidget(separator());
  auto* afcGroup = new QWidget(this);
  auto* afcRow = new QHBoxLayout(afcGroup);
  afcRow->setContentsMargins(0, 0, 0, 0);
  afcRow->setSpacing(6);
  auto* afcName = new QLabel(QStringLiteral("AFC"), afcGroup);
  afcName->setObjectName(QStringLiteral("readoutLabel"));
  {
    QFont small("Inter");
    small.setPixelSize(10);
    small.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
    afcName->setFont(small);
  }
  auto* afc = new QPushButton(QStringLiteral("Off"), afcGroup);
  afc->setObjectName(QStringLiteral("afcToggle"));
  afc->setCheckable(true);
  afc->setChecked(false);
  afc->setEnabled(false);
  afc->setFocusPolicy(Qt::NoFocus);
  afc->setFixedHeight(22);
  afc->setFont(monoFont(12));
  afcRow->addWidget(afcName);
  afcRow->addWidget(afc);
  layout->addWidget(afcGroup);
  layout->addWidget(separator());

  auto* sqlGroup = new QWidget(this);
  auto* sqlRow = new QHBoxLayout(sqlGroup);
  sqlRow->setContentsMargins(0, 0, 0, 0);
  sqlRow->setSpacing(6);
  auto* sqlName = new QLabel(QStringLiteral("SQL"), sqlGroup);
  sqlName->setObjectName(QStringLiteral("readoutLabel"));
  {
    QFont small("Inter");
    small.setPixelSize(10);
    small.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
    sqlName->setFont(small);
  }
  auto* sql = new QPushButton(QStringLiteral("Off"), sqlGroup);
  sql->setObjectName(QStringLiteral("squelchToggle"));
  sql->setCheckable(true);
  sql->setFocusPolicy(Qt::NoFocus);
  sql->setFixedHeight(22);
  sql->setFont(monoFont(12));
  auto* level = new QSpinBox(sqlGroup);
  level->setObjectName(QStringLiteral("squelchLevel"));
  level->setRange(0, 100);
  level->setFixedWidth(58);
  level->setFixedHeight(22);
  level->setFont(monoFont(12));
  level->setButtonSymbols(QAbstractSpinBox::PlusMinus);
  {
    QSettings settings;
    const bool on = settings.value(QStringLiteral("dx/squelchOn"), false).toBool();
    const int saved = std::clamp(settings.value(QStringLiteral("dx/squelchLevel"), 10).toInt(), 0, 100);
    sql->setChecked(on);
    level->setValue(saved);
  }
  sqlRow->addWidget(sqlName);
  sqlRow->addWidget(sql);
  sqlRow->addWidget(level);
  layout->addWidget(sqlGroup);
  applyDecoderControls(QStringLiteral("FT8"));
  layout->addWidget(separator());
  layout->addWidget(readout("DIAL", "dialValue", QString::fromUtf8("—")));
  layout->addWidget(separator());
  layout->addWidget(readout("RX", "rxLevel", QString::fromUtf8("—")));
  layout->addWidget(new RxLevelMeter(this));
  layout->addWidget(gainSpin(this, "rxGain", QStringLiteral("RX "), "audio/rxGainDb",
                              QStringLiteral("Local receive gain for the waterfall and the decoder. "
                                             "It does not change the radio and it does not key.")));
  layout->addWidget(gainSpin(this, "txGain", QStringLiteral("TX "), "audio/txGainDb",
                              QStringLiteral(
                                  "Local transmit gain. While Tune or TX is on the air, that "
                                  "audio is mixed onto the waterfall. It does not key.")));
  auto* txHot = new QLabel(QStringLiteral("TX gain is too hot"), this);
  txHot->setObjectName(QStringLiteral("txGainHot"));
  txHot->setFont(monoFont(12));
  txHot->setToolTip(QStringLiteral(
      "Shown while the transmit audio is past full scale, before the waterfall "
      "draws it near the receive level. Receive levels do not raise this. This does not key."));
  txHot->hide();
  layout->addWidget(txHot);
  layout->addWidget(separator());
  layout->addWidget(new PttLamp(this));
  auto* levelTick = new QTimer(this);
  connect(levelTick, &QTimer::timeout, this, [this] { refreshRxLevel(); });
  levelTick->start(100);
  layout->addStretch(1);

  auto* dot = new QLabel(QString::fromUtf8("●"));
  dot->setObjectName("radioDot");
  dot->setFont(monoFont(10));

  auto* radio = new QLabel("Radio not connected");
  radio->setObjectName("radioState");
  radio->setFont(monoFont(12));

  layout->addWidget(dot);
  layout->addWidget(radio);
}

namespace {

QLabel* labeled(StatusStrip* strip, const char* name) {
  return strip->findChild<QLabel*>(name);
}

}  // namespace

void StatusStrip::setMode(const QString& text) {
  if (QLabel* label = labeled(this, "modeValue"))
    label->setText(text);
}

void StatusStrip::setDigitalMode(const QString& mode) {
  if (auto* chip = findChild<QPushButton*>(QStringLiteral("digitalModeChip")))
    showDigitalMode(chip, mode);
  applyDecoderControls(mode);
}

void StatusStrip::applyDecoderControls(const QString& mode) {
  const bool hell = hellFamily(mode);
  const bool ftFamily = mode == QLatin1String("FT8") || mode == QLatin1String("FT4") ||
                        mode == QLatin1String("FT2");
  if (auto* afc = findChild<QPushButton*>(QStringLiteral("afcToggle"))) {
    afc->setEnabled(false);
    afc->setChecked(false);
    afc->setText(QStringLiteral("Off"));
    if (hell)
      afc->setToolTip(QStringLiteral(
          "Feld Hell has no AFC switch. Fldigi leaves AFC off for this mode. "
          "This does not key and does not send START."));
    else if (ftFamily)
      afc->setToolTip(QStringLiteral(
          "FT8, FT4, and FT2 have no AFC switch. MSHV searches the audio window. "
          "This does not key and does not send START."));
    else
      afc->setToolTip(QStringLiteral(
          "This mode has no AFC switch yet. This does not key and does not send START."));
  }
  auto* sql = findChild<QPushButton*>(QStringLiteral("squelchToggle"));
  auto* level = findChild<QSpinBox*>(QStringLiteral("squelchLevel"));
  if (!sql)
    return;
  if (hell) {
    sql->setEnabled(true);
    sql->setText(sql->isChecked() ? QStringLiteral("On") : QStringLiteral("Off"));
    sql->setToolTip(QStringLiteral(
        "Fldigi squelch for Feld Hell. On drops a column at or below the level. "
        "Off prints the tape. This does not key and does not send START."));
    if (level) {
      level->setVisible(true);
      level->setEnabled(true);
      level->setToolTip(QStringLiteral(
          "Hell squelch level, 0 to 100, same scale as Fldigi. "
          "This does not key and does not send START."));
    }
    return;
  }
  sql->setEnabled(false);
  sql->setText(QString::fromUtf8("—"));
  if (ftFamily)
    sql->setToolTip(QStringLiteral(
        "FT8, FT4, and FT2 have no squelch slider. The decoder drops weak candidates. "
        "This does not key and does not send START."));
  else
    sql->setToolTip(QStringLiteral(
        "Squelch is the Feld Hell gate. This mode does not use it. "
        "This does not key and does not send START."));
  if (level)
    level->setVisible(false);
}

void StatusStrip::setDial(const QString& text) {
  if (QLabel* label = labeled(this, "dialValue"))
    label->setText(text);
}

void StatusStrip::setPtt(bool onAir, bool connected) {
  auto* lamp = findChild<QPushButton*>("ptt");
  if (!lamp)
    return;
  lamp->setChecked(onAir);
  lamp->setEnabled(false);
  if (!connected)
    lamp->setToolTip(QStringLiteral("Off. The radio is not connected."));
  else if (onAir)
    lamp->setToolTip(QStringLiteral("The radio is reporting PTT. STD does not key from this lamp."));
  else
    lamp->setToolTip(QStringLiteral("PTT is off."));
  lamp->update();
}

void StatusStrip::setRxLevel(const float* samples, int count) {
  if (!samples || count <= 0) {
    rxMeanSq_ = 0.0;
    rxHeardMs_ = 0;
    refreshRxLevel();
    return;
  }
  double sum = 0.0;
  for (int i = 0; i < count; ++i) {
    const double sample = samples[i];
    sum += sample * sample;
  }
  const double block = sum / static_cast<double>(count);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (rxHeardMs_ <= 0) {
    rxMeanSq_ = block;
  } else {
    const double dt = std::clamp((now - rxHeardMs_) / 1000.0, 0.0, 1.0);
    // About 0.8 s to follow a new level, so a single spike does not jump the readout.
    const double alpha = 1.0 - std::exp(-dt / 0.8);
    rxMeanSq_ += (block - rxMeanSq_) * alpha;
  }
  rxHeardMs_ = now;
}

void StatusStrip::refreshRxLevel() {
  auto* meter = findChild<QWidget*>("rxLevelMeter");
  auto* label = labeled(this, "rxLevel");
  if (!meter || !label)
    return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (rxHeardMs_ <= 0 || now - rxHeardMs_ > 1200) {
    meter->setProperty("zone", 0);
    meter->setProperty("fraction", 0.0);
    label->setText(QString::fromUtf8("—"));
    label->setStyleSheet(QString());
    meter->update();
    return;
  }
  double db = -120.0;
  if (rxMeanSq_ > 1e-12)
    db = 10.0 * std::log10(rxMeanSq_);
  int zone = 1;
  if (db > 0.05)
    zone = 3;
  else if (db >= -1.0)
    zone = 2;
  const double fraction = std::clamp((db + 6.0) / 6.0, 0.0, 1.0);
  meter->setProperty("zone", zone);
  meter->setProperty("fraction", fraction);
  label->setText(QString::number(db, 'f', 1) + QStringLiteral(" dB"));
  const char* color = "#e0b15a";
  if (zone == 2)
    color = "#7dffb3";
  else if (zone == 3)
    color = "#ff8d8d";
  label->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(color)));
  label->setToolTip(QStringLiteral("Averaged receive level. Green is about -1 to 0 dB. This does not key."));
  meter->update();
}

void StatusStrip::setTxHot(bool hot) {
  if (QLabel* label = labeled(this, "txGainHot"))
    label->setVisible(hot);
}

void StatusStrip::setRadio(bool connected, const QString& line) {
  QLabel* radio = labeled(this, "radioState");
  QLabel* dot = labeled(this, "radioDot");
  if (!radio)
    return;
  const QFontMetrics metrics(radio->font());
  radio->setText(metrics.elidedText(line, Qt::ElideRight, 520));
  radio->setToolTip(line);
  const QString style = QStringLiteral("color: %1;").arg(connected ? QStringLiteral("#00e5ff")
                                                                     : QStringLiteral("#e08a3d"));
  radio->setStyleSheet(style);
  if (dot)
    dot->setStyleSheet(style);
}
