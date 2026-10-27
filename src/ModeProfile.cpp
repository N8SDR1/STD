// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModeProfile.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCheckBox>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>

namespace {

QFont barFont() {
  QFont font("Inter");
  font.setPixelSize(12);
  font.setStyleStrategy(QFont::PreferAntialias);
  return font;
}

// MSHV src/config_band_all.h, all_bands_mods_frq. FT4 is column 2,
// FT8 is column 3. The dots in "7.074.000" are thousands separators.
// FT8 and FT4 are upper sideband on every band, so the modulation
// token is always DIGU.
struct Ft8Band {
  const char* label;
  qint64 ft8Hz;
  qint64 ft4Hz;
};

constexpr Ft8Band kBands[] = {
    {"1.8", 1840000, 1840000},  {"3.5", 3573000, 3575000}, {"7", 7074000, 7047000},
    {"10", 10136000, 10140000}, {"14", 14074000, 14080000}, {"18", 18100000, 18104000},
    {"21", 21074000, 21140000}, {"24", 24915000, 24919000}, {"28", 28074000, 28180000},
    {"50", 50313000, 50318000},
};

constexpr int kBandCount = int(sizeof(kBands) / sizeof(kBands[0]));
constexpr int kDefaultBand = 4;  // 14 MHz

void fitSpin(QSpinBox* spin) {
  spin->ensurePolished();
  const QFontMetrics metrics(spin->font());
  const QString shown = spin->text();
  const int text = metrics.horizontalAdvance(shown);
  QStyleOptionSpinBox option;
  option.initFrom(spin);
  option.rect = QRect(0, 0, 400, 28);
  option.frame = true;
  option.buttonSymbols = spin->buttonSymbols();
  const int button =
      spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin).width();
  // Padding and the 1px frame live in the stylesheet. Anything past the
  // shown string is the empty field the band row cannot use.
  const int width = text + std::max(button, 1) + 2;
  spin->setFixedWidth(width);
}

QSpinBox* makeSpin(QWidget* parent, const QString& name, const QString& prefix,
                   const QString& suffix, int min, int max, int step, int value) {
  auto* spin = new QSpinBox(parent);
  spin->setObjectName(name);
  spin->setFont(barFont());
  spin->setRange(min, max);
  spin->setSingleStep(step);
  spin->setPrefix(prefix);
  spin->setSuffix(suffix);
  spin->setValue(value);
  spin->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
  spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  fitSpin(spin);
  QObject::connect(spin, &QSpinBox::valueChanged, spin, [spin](int) { fitSpin(spin); });
  return spin;
}

}  // namespace

ModeProfile::ModeProfile(QWidget* parent) : QWidget(parent) {
  setObjectName("modeProfile");
  setAttribute(Qt::WA_StyledBackground, true);
  setFont(barFont());

  QSettings settings;
  const int savedSpeed = settings.value(QStringLiteral("profile/speed"), 9).toInt();
  const int savedStart = settings.value(QStringLiteral("profile/startHz"), 100).toInt();
  const int savedBandwidth = settings.value(QStringLiteral("profile/bandwidthHz"), 3400).toInt();
  const bool savedAuto = settings.value(QStringLiteral("profile/autoDec"), true).toBool();
  band_ = settings.value(QStringLiteral("profile/band"), kDefaultBand).toInt();
  if (band_ < 0 || band_ >= kBandCount)
    band_ = kDefaultBand;

  bar_ = new QWidget(this);
  bar_->setObjectName("mshvProfile");
  auto* row = new QHBoxLayout(bar_);
  row->setContentsMargins(8, 4, 8, 4);
  row->setSpacing(6);

  speed_ = makeSpin(bar_, QStringLiteral("profileSpeed"), QStringLiteral("Speed "), QString(), 1, 9,
                    1, savedSpeed);
  speed_->setToolTip(QStringLiteral("Waterfall scroll speed. 9 is the FT8 default."));
  start_ = makeSpin(bar_, QStringLiteral("profileStart"), QStringLiteral("Start "),
                    QStringLiteral(" Hz"), 0, 3000, 100, savedStart);
  start_->setToolTip(QStringLiteral("Left edge of the FT8 window, in Hz."));
  bandwidth_ = makeSpin(bar_, QStringLiteral("profileBandwidth"), QStringLiteral("BW "),
                        QStringLiteral(" Hz"), 2000, 5000, 100, savedBandwidth);
  bandwidth_->setToolTip(QStringLiteral("Width of the FT8 window. 3400 Hz is the usual span."));

  auto* autoDec = new QCheckBox(QStringLiteral("Auto Dec"), bar_);
  autoDec->setObjectName(QStringLiteral("profileAutoDec"));
  autoDec->setFont(barFont());
  autoDec->setChecked(savedAuto);
  autoDec->setToolTip(QStringLiteral("When on, FT8 periods are decoded. This does not key."));

  row->addWidget(speed_);
  row->addWidget(start_);
  row->addWidget(bandwidth_);
  row->addWidget(autoDec);

  auto* bands = new QButtonGroup(bar_);
  bands->setExclusive(true);
  for (int i = 0; i < kBandCount; ++i) {
    auto* button = new QPushButton(QString::fromLatin1(kBands[i].label), bar_);
    button->setObjectName(QStringLiteral("profileBand"));
    button->setFont(barFont());
    button->setCheckable(true);
    button->setChecked(i == band_);
    button->setToolTip(QStringLiteral("%1 MHz  ·  %2 %3 Hz  ·  DIGU")
                           .arg(QString::fromLatin1(kBands[i].label), mode_,
                                QString::number(kBands[i].ft8Hz)));
    button->setAccessibleName(button->toolTip());
    bands->addButton(button, i);
    row->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, i] {
      band_ = i;
      remember();
      emit tuneRequested(dialHz(), QStringLiteral("DIGU"));
    });
  }
  row->addStretch(1);

  keyboard_ = new QLabel(
      QStringLiteral("Keyboard modes keep their own controls. None are loaded yet."), this);
  keyboard_->setObjectName(QStringLiteral("keyboardProfile"));
  keyboard_->setFont(barFont());
  keyboard_->hide();

  auto* stack = new QVBoxLayout(this);
  stack->setContentsMargins(0, 0, 0, 0);
  stack->setSpacing(0);
  stack->addWidget(bar_);
  stack->addWidget(keyboard_);

  connect(speed_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int speed) {
    remember();
    emit speedChanged(speed);
  });
  connect(start_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { applySpan(); });
  connect(bandwidth_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { applySpan(); });
  connect(autoDec, &QCheckBox::toggled, this, [this](bool) { remember(); });
}

void ModeProfile::setMshvFamily(bool mshv) {
  family_ = mshv;
  bar_->setVisible(mshv);
  keyboard_->setVisible(!mshv);
}

void ModeProfile::setDigitalMode(const QString& mode) {
  mode_ = (mode == QLatin1String("FT4")) ? QStringLiteral("FT4") : QStringLiteral("FT8");
  const auto buttons = bar_->findChildren<QPushButton*>(QStringLiteral("profileBand"),
                                                        Qt::FindDirectChildrenOnly);
  for (int i = 0; i < buttons.size() && i < kBandCount; ++i) {
    const qint64 hz = mode_ == QLatin1String("FT4") ? kBands[i].ft4Hz : kBands[i].ft8Hz;
    buttons[i]->setToolTip(QStringLiteral("%1 MHz  ·  %2 %3 Hz  ·  DIGU")
                               .arg(QString::fromLatin1(kBands[i].label), mode_,
                                    QString::number(hz)));
    buttons[i]->setAccessibleName(buttons[i]->toolTip());
  }
}

int ModeProfile::speed() const {
  return speed_->value();
}

int ModeProfile::startHz() const {
  return start_->value();
}

int ModeProfile::bandwidthHz() const {
  return bandwidth_->value();
}

bool ModeProfile::autoDecode() const {
  const auto* box = findChild<QCheckBox*>(QStringLiteral("profileAutoDec"));
  return box && box->isChecked();
}

qint64 ModeProfile::dialHz() const {
  if (mode_ == QLatin1String("FT4"))
    return kBands[band_].ft4Hz;
  return kBands[band_].ft8Hz;
}

QString ModeProfile::sideband() const {
  return QStringLiteral("DIGU");
}

void ModeProfile::applySpan() {
  int start = start_->value();
  const int width = bandwidth_->value();
  if (start + width > 5000) {
    start = std::max(0, 5000 - width);
    const QSignalBlocker block(start_);
    start_->setValue(start);
  }
  remember();
  emit displayChanged(start, width);
}

void ModeProfile::remember() const {
  QSettings settings;
  settings.setValue(QStringLiteral("profile/speed"), speed_->value());
  settings.setValue(QStringLiteral("profile/startHz"), start_->value());
  settings.setValue(QStringLiteral("profile/bandwidthHz"), bandwidth_->value());
  settings.setValue(QStringLiteral("profile/autoDec"), autoDecode());
  settings.setValue(QStringLiteral("profile/band"), band_);
}
