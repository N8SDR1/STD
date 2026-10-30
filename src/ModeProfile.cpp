// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DigitalMode.h"
#include "ModeProfile.h"
#include "dominoex/DominoModem.h"
#include "thor/ThorModem.h"
#include "mfsk/MfskModem.h"
#include "olivia/OliviaModem.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaType>
#include <QPushButton>
#include <QVariant>
#include <QRadioButton>
#include <QSettings>
#include <QStringList>
#include <QTimer>
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

// WSJT-X FrequencyList.cpp working frequencies. 40 m FT4 is
// 7.0475 MHz (7047500 Hz) in every region, marked provisional.
// 80 m FT4 is 3.575 MHz, and 3.568 MHz in Region 3. FT8 and FT4
// send DIGU on every band. FT2 dials are the MSHV table. MSHV stays the decoder.
// Hell dials are the Feld Hell Club signal frequency minus 1500 Hz, so DIGU
// puts the tone at 1500 Hz. 20 m is 14.063 MHz, the club calling frequency.
// 30 m Region 1 uses 10.144 MHz. The others use the club list.
struct Ft8Band {
  const char* label;
  qint64 ft8Hz;
  qint64 ft4Hz;
  qint64 ft4Region3Hz;
  qint64 ft2Hz;
  qint64 hellHz;
  qint64 hellRegion1Hz;
  qint64 jt65Hz;
  qint64 q65Hz;
};

constexpr Ft8Band kBands[] = {
    {"1.8", 1840000, 1840000, 0, 1843000, 1841500, 0, 1838000, 1838000},
    {"3.5", 3573000, 3575000, 3568000, 3578000, 3576500, 0, 3570000, 3570000},
    {"7", 7074000, 7047500, 0, 7052000, 7078500, 0, 7076000, 7076000},
    {"10", 10136000, 10140000, 0, 10144000, 10135500, 10142500, 10138000, 10138000},
    {"14", 14074000, 14080000, 0, 14084000, 14061500, 0, 14076000, 14076000},
    {"18", 18100000, 18104000, 0, 18108000, 18103500, 0, 18102000, 18102000},
    {"21", 21074000, 21140000, 0, 21144000, 21061500, 0, 21076000, 21076000},
    {"24", 24915000, 24919000, 0, 24923000, 24922500, 0, 24917000, 24917000},
    {"28", 28074000, 28180000, 0, 28184000, 28061500, 0, 28076000, 28076000},
    {"50", 50313000, 50318000, 0, 50320000, 50284500, 0, 50276000, 50305000},
};

constexpr int kBandCount = int(sizeof(kBands) / sizeof(kBands[0]));
constexpr int kDefaultBand = 4;  // 14 MHz

bool ignoredPortable(const QString& token) {
  return token == QLatin1String("P") || token == QLatin1String("M") || token == QLatin1String("MM") ||
         token == QLatin1String("AM") || token == QLatin1String("QRP") || token == QLatin1String("A") ||
         (token.size() == 1 && token.at(0).isDigit());
}

bool looksLikeCall(const QString& token) {
  bool digit = false;
  bool letter = false;
  for (const QChar ch : token) {
    digit = digit || ch.isDigit();
    letter = letter || ch.isLetter();
  }
  return digit && letter && token.size() >= 3;
}

// Longest prefix wins. KH6 is Hawaii (region 2) and is listed before KH.
int regionForToken(const QString& token) {
  const struct {
    const char* prefix;
    int region;
  } rows[] = {
      {"KH0", 3}, {"KH2", 3}, {"KH8", 3}, {"KH6", 2}, {"KL", 2},  {"AH6", 2}, {"NH6", 2},
      {"WH6", 2}, {"AL7", 2}, {"NL7", 2}, {"WL7", 2}, {"VE", 2},  {"VA", 2},  {"VY", 2},
      {"VO", 2},  {"XE", 2},  {"XF", 2},  {"CM", 2},  {"CO", 2},  {"HI", 2},  {"HH", 2},
      {"HK", 2},  {"HP", 2},  {"HO", 2},  {"HR", 2},  {"HC", 2},  {"HD", 2},  {"YS", 2},
      {"YN", 2},  {"YV", 2},  {"YW", 2},  {"TI", 2},  {"TE", 2},  {"TG", 2},  {"TD", 2},
      {"PY", 2},  {"PP", 2},  {"PQ", 2},  {"PR", 2},  {"PU", 2},  {"PV", 2},  {"PW", 2},
      {"ZX", 2},  {"LU", 2},  {"CE", 2},  {"CX", 2},  {"CV", 2},  {"CP", 2},  {"OA", 2},
      {"OB", 2},  {"ZP", 2},  {"8P", 2},  {"6Y", 2},  {"9Y", 2},  {"9Z", 2},  {"J3", 2},
      {"J6", 2},  {"J7", 2},  {"J8", 2},  {"V2", 2},  {"V3", 2},  {"V4", 2},  {"C6", 2},
      {"FG", 2},  {"FM", 2},  {"FS", 2},  {"FY", 2},  {"PJ", 2},  {"ZF", 2},  {"P4", 2},
      {"OX", 2},  {"KP", 2},  {"JA", 3},  {"JE", 3},  {"JF", 3},  {"JG", 3},  {"JH", 3},
      {"JI", 3},  {"JK", 3},  {"JL", 3},  {"JM", 3},  {"JN", 3},  {"JO", 3},  {"JR", 3},
      {"JS", 3},  {"JD", 3},  {"VK", 3},  {"AX", 3},  {"ZL", 3},  {"ZM", 3},  {"DU", 3},
      {"DV", 3},  {"HL", 3},  {"DS", 3},  {"BV", 3},  {"BY", 3},  {"BA", 3},  {"BD", 3},
      {"BG", 3},  {"BH", 3},  {"VR", 3},  {"VU", 3},  {"9M", 3},  {"9W", 3},  {"9V", 3},
      {"YB", 3},  {"YC", 3},  {"YD", 3},  {"HS", 3},  {"E2", 3},  {"XU", 3},  {"XW", 3},
      {"XZ", 3},  {"4S", 3},  {"8Q", 3},  {"P2", 3},  {"H4", 3},  {"3D2", 3}, {"T30", 3},
      {"T31", 3}, {"T32", 3}, {"T33", 3}, {"T2", 3},  {"V6", 3},  {"V7", 3},  {"V8", 3},
      {"E5", 3},  {"5W", 3},  {"A3", 3},  {"JT", 3},  {"AP", 3},  {"9N", 3},  {"S2", 3},
      {"XV", 3},  {"3W", 3},  {"4W", 3},  {"KH", 3},  {"2E", 1},  {"2I", 1},  {"2M", 1},
      {"2W", 1},  {"IK", 1},  {"IW", 1},  {"DL", 1},  {"EA", 1},  {"EB", 1},  {"EC", 1},
      {"CT", 1},  {"LZ", 1},  {"OK", 1},  {"OM", 1},  {"SP", 1},  {"YO", 1},  {"HA", 1},
      {"HG", 1},  {"OE", 1},  {"HB", 1},  {"PA", 1},  {"PD", 1},  {"ON", 1},  {"OZ", 1},
      {"SM", 1},  {"LA", 1},  {"OH", 1},  {"ES", 1},  {"YL", 1},  {"LY", 1},  {"UA", 1},
      {"RA", 1},  {"ZS", 1},  {"4X", 1},  {"4Z", 1},  {"TA", 1},  {"A4", 1},  {"A6", 1},
      {"A7", 1},  {"A9", 1},  {"HZ", 1},  {"9K", 1},  {"JY", 1},  {"YI", 1},  {"SU", 1},
      {"5A", 1},  {"9A", 1},  {"G", 1},   {"M", 1},   {"F", 1},   {"I", 1},   {"R", 1},
      {"K", 2},   {"N", 2},   {"W", 2},
  };
  int best = 0;
  int region = 0;
  for (const auto& row : rows) {
    const auto prefix = QLatin1String(row.prefix);
    const int length = prefix.size();
    if (length > best && token.startsWith(prefix)) {
      best = length;
      region = row.region;
    }
  }
  if (region == 0 && token.size() >= 2 && token.at(0) == QLatin1Char('A') && token.at(1) >= QLatin1Char('A') &&
      token.at(1) <= QLatin1Char('L'))
    region = 2;
  return region;
}

struct CallRegion {
  QString home;
  QString place;
  int region = 0;
};

CallRegion regionFromCall(const QString& call) {
  CallRegion out;
  const QStringList parts = call.toUpper().split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QStringList kept;
  for (const QString& part : parts) {
    if (!ignoredPortable(part))
      kept.append(part);
  }
  for (const QString& part : kept) {
    if (looksLikeCall(part) && part.size() > out.home.size())
      out.home = part;
  }
  for (const QString& part : kept) {
    if (part != out.home)
      out.place = part;
  }
  if (out.home.isEmpty() && !kept.isEmpty())
    out.home = kept.first();
  const QString key = out.place.isEmpty() ? out.home : out.place;
  out.region = regionForToken(key);
  return out;
}

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
  regionChoice_ = settings.value(QStringLiteral("profile/region"), QStringLiteral("auto")).toString();
  if (regionChoice_ != QLatin1String("1") && regionChoice_ != QLatin1String("2") &&
      regionChoice_ != QLatin1String("3"))
    regionChoice_ = QStringLiteral("auto");

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
  autoDec->setToolTip(QStringLiteral(
      "When on, each FT8 period is written to the Receive pane. This does not key."));

  auto* hound = new QCheckBox(QStringLiteral("Super Hound"), bar_);
  hound->setObjectName(QStringLiteral("profileSuperHound"));
  hound->setFont(barFont());
  hound->setChecked(settings.value(QStringLiteral("profile/superHound"),
                                   settings.value(QStringLiteral("profile/hound"), false)).toBool());
  hound->setToolTip(QStringLiteral(
      "Super Hound. 40 m is 7.056 MHz DIGU. The green RX band is the fox channel at 750 Hz. "
      "This is still 15 second FT8. This does not key."));
  hound_ = hound->isChecked();

  auto* fox = new QCheckBox(QStringLiteral("Super Fox"), bar_);
  fox->setObjectName(QStringLiteral("profileSuperFox"));
  fox->setFont(barFont());
  fox->setChecked(settings.value(QStringLiteral("profile/superFox"), false).toBool());
  fox->setToolTip(QStringLiteral(
      "Super Fox. The red TX band shows the fox channel. Transmit stays off. This does not key."));
  fox_ = fox->isChecked();
  if (hound_ && fox_) {
    fox->setChecked(false);
    fox_ = false;
  }

  auto* half = new QLabel(QStringLiteral("1st"), bar_);
  half->setObjectName(QStringLiteral("periodHalf"));
  half->setFont(barFont());
  half->setToolTip(QStringLiteral(
      "The FT8 half that is on the air. 1st is seconds 0–14. 2nd is 15–29. This does not key."));

  txFirst_ = settings.value(QStringLiteral("profile/txFirst"), false).toBool();
  auto* txFirst = new QRadioButton(QStringLiteral("TX FIRST"), bar_);
  txFirst->setObjectName(QStringLiteral("txFirst"));
  txFirst->setFont(barFont());
  txFirst->setToolTip(QStringLiteral(
      "Remember the first 15 second slot. Nothing is transmitted. This does not key."));
  auto* txSecond = new QRadioButton(QStringLiteral("TX SECOND"), bar_);
  txSecond->setObjectName(QStringLiteral("txSecond"));
  txSecond->setFont(barFont());
  txSecond->setToolTip(QStringLiteral(
      "Remember the second 15 second slot. Nothing is transmitted. This does not key."));
  txFirst->setChecked(txFirst_);
  txSecond->setChecked(!txFirst_);
  auto* txSlots = new QButtonGroup(bar_);
  txSlots->addButton(txFirst);
  txSlots->addButton(txSecond);

  auto* filters = new QPushButton(QStringLiteral("FILTERS OFF"), bar_);
  filters->setObjectName(QStringLiteral("decodeFilters"));
  filters->setFont(barFont());
  filters->setCheckable(true);
  filters->setFocusPolicy(Qt::NoFocus);
  filters->setChecked(settings.value(QStringLiteral("profile/filters"), false).toBool());
  filters->setToolTip(QStringLiteral(
      "Filter the receive list. Right-click edits the rules. This does not key."));
  filters->setText(filters->isChecked() ? QStringLiteral("FILTERS ON") : QStringLiteral("FILTERS OFF"));
  filters->setContextMenuPolicy(Qt::CustomContextMenu);

  auto* multi = new QComboBox(bar_);
  multi->setObjectName(QStringLiteral("profileMultiAnswer"));
  multi->setFont(barFont());
  multi->addItem(QStringLiteral("MA Off"), 0);
  multi->addItem(QStringLiteral("MA Std"), 1);
  multi->addItem(QStringLiteral("MA DX"), 2);
  multi->setToolTip(QStringLiteral(
      "MSHV Multi Answering Auto Seq Protocol. "
      "MA Std (MSHV Ctrl+Q): stations answering your CQ wait in the Queue and are worked from it, "
      "one TX slot (two off the standard FT8/FT4 frequencies). "
      "MA DX (MSHV Ctrl+`): DXpedition, several QSOs in one transmission, one message per slot, "
      "60 Hz apart. Standard activity type only. This does not key."));
  {
    // Earlier builds stored a single on/off. On was the queue, MA Std.
    int stored = settings.value(QStringLiteral("profile/multiAnswerMode"), -1).toInt();
    if (stored < 0)
      stored = settings.value(QStringLiteral("profile/multiAnswer"), false).toBool() ? 1 : 0;
    multiMode_ = std::clamp(stored, 0, 2);
    const QSignalBlocker block(multi);
    multi->setCurrentIndex(multiMode_);
  }

  region_ = new QComboBox(bar_);
  region_->setObjectName(QStringLiteral("profileRegion"));
  region_->setFont(barFont());
  region_->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
  region_->addItem(QStringLiteral("Region 1"), QStringLiteral("1"));
  region_->addItem(QStringLiteral("Region 2"), QStringLiteral("2"));
  region_->addItem(QStringLiteral("Region 3"), QStringLiteral("3"));
  {
    const int index = region_->findData(regionChoice_);
    const QSignalBlocker block(region_);
    region_->setCurrentIndex(index < 0 ? 0 : index);
  }
  region_->setSizeAdjustPolicy(QComboBox::AdjustToContents);

  extra_ = new QSpinBox(bar_);
  extra_->setObjectName(QStringLiteral("profileExtra"));
  extra_->setFont(barFont());
  extra_->setRange(0, 250000000);
  extra_->setSingleStep(500);
  extra_->setSpecialValueText(QStringLiteral("default"));
  extra_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  extra_->setFixedWidth(96);
  extra_->setToolTip(QStringLiteral(
      "Type a dial in Hz and press Enter to insert it for this mode and band. "
      "Click the mode or the band button to cycle the WSJT-X default and those extras. "
      "Right-click clears them. DIGU. Does not key."));
  extra_->setContextMenuPolicy(Qt::CustomContextMenu);

  row->addWidget(autoDec);
  row->addWidget(hound);
  row->addWidget(fox);
  row->addWidget(multi);
  row->addWidget(half);
  row->addWidget(txFirst);
  row->addWidget(txSecond);
  row->addWidget(filters);
  row->addWidget(region_);
  row->addWidget(extra_);

  auto* bands = new QButtonGroup(bar_);
  bands->setExclusive(true);
  for (int i = 0; i < kBandCount; ++i) {
    auto* button = new QPushButton(QString::fromLatin1(kBands[i].label), bar_);
    button->setObjectName(QStringLiteral("profileBand"));
    button->setFont(barFont());
    button->setCheckable(true);
    button->setChecked(i == band_);
    bands->addButton(button, i);
    row->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, i] {
      if (band_ == i)
        advanceCycle();
      else {
        band_ = i;
        cycle_ = 0;
        reloadExtras();
      }
      remember();
      refreshBands();
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
  connect(hound, &QCheckBox::toggled, this, [this, fox](bool on) {
    hound_ = on;
    if (on && fox_) {
      const QSignalBlocker block(fox);
      fox->setChecked(false);
      fox_ = false;
    }
    remember();
    refreshBands();
    emit houndChanged(hound_ || fox_);
    emit tuneRequested(dialHz(), QStringLiteral("DIGU"));
  });
  connect(fox, &QCheckBox::toggled, this, [this, hound](bool on) {
    fox_ = on;
    if (on && hound_) {
      const QSignalBlocker block(hound);
      hound->setChecked(false);
      hound_ = false;
    }
    remember();
    refreshBands();
    emit houndChanged(hound_ || fox_);
    emit tuneRequested(dialHz(), QStringLiteral("DIGU"));
  });
  connect(txFirst, &QRadioButton::toggled, this, [this](bool on) {
    txFirst_ = on;
    remember();
  });
  connect(filters, &QPushButton::toggled, this, [filters](bool on) {
    filters->setText(on ? QStringLiteral("FILTERS ON") : QStringLiteral("FILTERS OFF"));
  });
  connect(filters, &QPushButton::toggled, this, [this](bool on) {
    QSettings settings;
    settings.setValue(QStringLiteral("profile/filters"), on);
    emit filtersChanged(on);
  });
  connect(filters, &QWidget::customContextMenuRequested, this, [this] { emit filterEditRequested(); });
  auto* halfClock = new QTimer(this);
  connect(halfClock, &QTimer::timeout, this, [half] {
    const int second = QDateTime::currentDateTimeUtc().time().second();
    half->setText((second % 30) < 15 ? QStringLiteral("1st") : QStringLiteral("2nd"));
  });
  halfClock->start(250);
  half->setText((QDateTime::currentDateTimeUtc().time().second() % 30) < 15 ? QStringLiteral("1st")
                                                                              : QStringLiteral("2nd"));
  connect(multi, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, multi](int) {
    multiMode_ = std::clamp(multi->currentData().toInt(), 0, 2);
    remember();
    emit multiAnswerChanged(multiMode_ != 0);
  });
  connect(region_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    regionChoice_ = region_->currentData().toString();
    remember();
    refreshBands();
    emit tuneRequested(dialHz(), QStringLiteral("DIGU"));
  });
  connect(extra_, &QSpinBox::editingFinished, this, [this] { insertExtra(extra_->value()); });
  connect(extra_, &QWidget::customContextMenuRequested, this, [this] {
    extras_.clear();
    cycle_ = 0;
    saveExtras();
    refreshBands();
    emit tuneRequested(dialHz(), QStringLiteral("DIGU"));
  });
  refreshBands();
}

void ModeProfile::setMshvFamily(bool mshv) {
  family_ = mshv;
  bar_->setVisible(mshv);
  keyboard_->setVisible(!mshv && !wsprMode(mode_));
}

void ModeProfile::setDigitalMode(const QString& mode) {
  QString next = QStringLiteral("FT8");
  if (mode == QLatin1String("FT4"))
    next = QStringLiteral("FT4");
  else if (mode == QLatin1String("FT2"))
    next = QStringLiteral("FT2");
  else if (jt65Mode(mode) || q65Mode(mode))
    next = mode;
  else if (keyboardTextMode(mode))
    next = mode;
  else if (wsprMode(mode))
    next = QStringLiteral("WSPR");
  const bool same = next == mode_;
  mode_ = next;
  if (same)
    advanceCycle();
  else {
    cycle_ = 0;
    reloadExtras();
  }
  refreshBands();
  const bool hell = hellFamily(mode_);
  const bool keyboard = keyboardTextMode(mode_);
  const bool rtty = rttyMode(mode_);
  const bool js8 = mode_.compare(QLatin1String("JS8"), Qt::CaseInsensitive) == 0;
  const bool keys = rttyMode(mode_) || keyedPsk(mode_) ||
                    oliviaBandwidthHz(mode_) > 0 || mfskBandwidthHz(mode_) > 0 ||
                    dominoBandwidthHz(mode_) > 0 || thorBandwidthHz(mode_) > 0 || js8;
  if (auto* label = qobject_cast<QLabel*>(keyboard_)) {
    const QString js8Hint = QStringLiteral(
        "JS8 uses the transmit box and the receive echo. Band buttons move the radio to the "
        "JS8Call-improved calling dial. CLR RX and CLR TX clear those. TX sends the box. "
        "START is never sent.");
    label->setText(!keyboard || hell ? QStringLiteral(
                                           "Keyboard modes keep their own controls. None are loaded yet.")
                    : js8       ? js8Hint
                    : rtty      ? QStringLiteral(
                                      "RTTY uses the transmit box and the receive echo. "
                                      "CLR RX and CLR TX clear those. TX sends it.")
                    : keys      ? QStringLiteral("%1 uses the transmit box and the receive echo. "
                                                 "CLR RX and CLR TX clear those. TX sends it. "
                                                 "START is never sent.")
                                      .arg(mode_)
                                : QStringLiteral("%1 uses the transmit box and the receive echo. "
                                                 "CLR RX and CLR TX clear those. This mode does not key yet. "
                                                 "START is never sent.")
                                      .arg(mode_));
  }
  if (start_)
    start_->setVisible(!keyboard);
  if (bandwidth_)
    bandwidth_->setVisible(!keyboard);
  if (auto* box = findChild<QCheckBox*>(QStringLiteral("profileAutoDec")))
    box->setVisible(!keyboard);
  const bool ft8 = mode_ == QLatin1String("FT8");
  const bool sequenced = sequencedFamily(mode_);
  if (auto* hound = findChild<QCheckBox*>(QStringLiteral("profileSuperHound")))
    hound->setVisible(ft8);
  if (auto* fox = findChild<QCheckBox*>(QStringLiteral("profileSuperFox")))
    fox->setVisible(ft8);
  if (auto* half = findChild<QLabel*>(QStringLiteral("periodHalf")))
    half->setVisible(sequenced);
  if (auto* first = findChild<QRadioButton*>(QStringLiteral("txFirst")))
    first->setVisible(sequenced);
  if (auto* second = findChild<QRadioButton*>(QStringLiteral("txSecond")))
    second->setVisible(sequenced);
  if (auto* filters = findChild<QPushButton*>(QStringLiteral("decodeFilters")))
    filters->setVisible(sequenced);
  if (auto* multi = findChild<QComboBox*>(QStringLiteral("profileMultiAnswer")))
    multi->setVisible(sequenced);
}

int ModeProfile::effectiveRegion() const {
  if (regionChoice_ == QLatin1String("1") || regionChoice_ == QLatin1String("2") ||
      regionChoice_ == QLatin1String("3"))
    return regionChoice_.toInt();
  QSettings settings;
  return regionFromCall(settings.value(QStringLiteral("dx/reportCall")).toString()).region;
}

void ModeProfile::refreshBands() {
  reloadExtras();
  QSettings settings;
  const CallRegion station = regionFromCall(settings.value(QStringLiteral("dx/reportCall")).toString());
  const int chosen = effectiveRegion();
  QString where = station.place.isEmpty() ? station.home : station.place;
  if (where.isEmpty())
    where = QStringLiteral("the callsign");
  QString tip = QStringLiteral(
      "IARU region for these dials. Auto reads the callsign. A portable suffix such as N8SDR/VE uses "
      "that country, and the home call is left as typed. Pick Region 1, 2, or 3 to override. "
      "Dials are the WSJT-X list. 40 m FT4 is 7.0475 MHz in every region. 80 m FT4 is 3.568 MHz "
      "in Region 3. DIGU on every band. Does not key.");
  if (regionChoice_ == QLatin1String("auto")) {
    if (station.region >= 1)
      tip.prepend(QStringLiteral("Auto · Region %1 from %2. ").arg(station.region).arg(where));
    else
      tip.prepend(QStringLiteral("Auto · no region in the call yet. "));
  } else {
    tip.prepend(QStringLiteral("Region %1, set by hand. ").arg(regionChoice_));
  }
  if (region_)
    region_->setToolTip(tip);
  const auto buttons = bar_->findChildren<QPushButton*>(QStringLiteral("profileBand"),
                                                        Qt::FindDirectChildrenOnly);
  for (int i = 0; i < buttons.size() && i < kBandCount; ++i) {
    const bool here = i == band_;
    const qint64 hz = here && cycle_ > 0 && cycle_ <= extras_.size() ? extras_.at(cycle_ - 1) : tableHz(i);
    const QList<qint64> extras = here ? extras_ : storedExtras(i);
    QString cycleNote;
    if (!extras.isEmpty()) {
      QStringList dials;
      dials << QString::number(tableHz(i));
      for (qint64 extraHz : extras)
        dials << QString::number(extraHz);
      const int step = here ? std::min(cycle_, static_cast<int>(dials.size()) - 1) : 0;
      cycleNote = QStringLiteral(" · %1/%2 %3")
                      .arg(step + 1)
                      .arg(dials.size())
                      .arg(dials.join(QStringLiteral(", ")));
    }
    const QString region = chosen >= 1 ? QStringLiteral("Region %1").arg(chosen) : QStringLiteral("Auto");
    buttons[i]->setToolTip(QStringLiteral("%1 MHz  ·  %2 %3 Hz  ·  %4  ·  DIGU%5")
                               .arg(QString::fromLatin1(kBands[i].label), mode_, QString::number(hz), region,
                                    cycleNote));
    buttons[i]->setAccessibleName(buttons[i]->toolTip());
  }
}

void ModeProfile::setRegionOverride(const QString& choice) {
  const QString key = (choice == QLatin1String("1") || choice == QLatin1String("2") ||
                       choice == QLatin1String("3"))
                          ? choice
                          : QStringLiteral("auto");
  const bool changed = key != regionChoice_;
  regionChoice_ = key;
  if (region_) {
    const QSignalBlocker block(region_);
    const int index = region_->findData(key);
    if (index >= 0)
      region_->setCurrentIndex(index);
  }
  remember();
  refreshBands();
  if (changed)
    emit tuneRequested(dialHz(), QStringLiteral("DIGU"));
}

void ModeProfile::refreshStation() {
  refreshBands();
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

bool ModeProfile::hound() const {
  return hound_;
}

void ModeProfile::setTxFirst(bool first) {
  if (auto* button = findChild<QRadioButton*>(QStringLiteral("txFirst"))) {
    if (!button->isEnabled())
      first = false;
    const QSignalBlocker block(button);
    button->setChecked(first);
  }
  if (auto* button = findChild<QRadioButton*>(QStringLiteral("txSecond"))) {
    const QSignalBlocker block(button);
    button->setChecked(!first);
  }
  txFirst_ = first;
  remember();
}

void ModeProfile::setTxFirstLocked(bool locked) {
  auto* first = findChild<QRadioButton*>(QStringLiteral("txFirst"));
  if (!first)
    return;
  first->setEnabled(!locked);
  if (locked)
    setTxFirst(false);
}

bool ModeProfile::multiAnswer() const {
  return multiMode_ != 0;
}

qint64 ModeProfile::tableHz(int index) const {
  const Ft8Band& band = kBands[index];
  if (mode_ == QLatin1String("FT8") && (hound_ || fox_) && QLatin1String(band.label) == QLatin1String("7"))
    return 7056000;
  if (hellFamily(mode_)) {
    if (effectiveRegion() == 1 && band.hellRegion1Hz > 0)
      return band.hellRegion1Hz;
    return band.hellHz;
  }
  if (jt65Mode(mode_))
    return band.jt65Hz;
  if (q65Mode(mode_))
    return band.q65Hz;
  if (mode_ == QLatin1String("FT2"))
    return band.ft2Hz;
  if (mode_ == QLatin1String("FT4")) {
    if (effectiveRegion() == 3 && band.ft4Region3Hz > 0)
      return band.ft4Region3Hz;
    return band.ft4Hz;
  }
  return band.ft8Hz;
}

qint64 ModeProfile::dialHz() const {
  if (cycle_ > 0 && cycle_ <= extras_.size())
    return extras_.at(cycle_ - 1);
  return tableHz(band_);
}

QList<qint64> ModeProfile::storedExtras(int index) const {
  QList<qint64> extras;
  QSettings settings;
  const QString key =
      QStringLiteral("profile/extra/%1/%2").arg(mode_, QString::fromLatin1(kBands[index].label));
  const QVariant stored = settings.value(key);
  const auto accept = [&extras, this, index](qint64 hz) {
    if (hz > 0 && hz != tableHz(index) && !extras.contains(hz))
      extras.append(hz);
  };
  if (stored.userType() == QMetaType::QStringList) {
    for (const QString& text : stored.toStringList())
      accept(text.toLongLong());
  } else {
    accept(stored.toLongLong());
  }
  return extras;
}

void ModeProfile::reloadExtras() {
  extras_ = storedExtras(band_);
  if (cycle_ > extras_.size())
    cycle_ = 0;
  if (!extra_)
    return;
  const QSignalBlocker block(extra_);
  extra_->setValue(0);
}

void ModeProfile::saveExtras() const {
  QSettings settings;
  const QString key =
      QStringLiteral("profile/extra/%1/%2").arg(mode_, QString::fromLatin1(kBands[band_].label));
  if (extras_.isEmpty()) {
    settings.remove(key);
    return;
  }
  QStringList texts;
  for (qint64 hz : extras_)
    texts.append(QString::number(hz));
  settings.setValue(key, texts);
}

void ModeProfile::advanceCycle() {
  reloadExtras();
  const int count = extras_.size() + 1;
  if (count <= 1) {
    cycle_ = 0;
    return;
  }
  cycle_ = (cycle_ + 1) % count;
}

void ModeProfile::insertExtra(qint64 hz) {
  if (!extra_)
    return;
  if (hz > 0 && hz != tableHz(band_) && !extras_.contains(hz)) {
    extras_.append(hz);
    saveExtras();
  }
  const QSignalBlocker block(extra_);
  extra_->setValue(0);
  refreshBands();
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
  settings.setValue(QStringLiteral("profile/superHound"), hound_);
  settings.setValue(QStringLiteral("profile/superFox"), fox_);
  settings.setValue(QStringLiteral("profile/txFirst"), txFirst_);
  settings.setValue(QStringLiteral("profile/multiAnswerMode"), multiMode_);
  settings.setValue(QStringLiteral("profile/multiAnswer"), multiMode_ != 0);
  settings.setValue(QStringLiteral("profile/band"), band_);
  settings.setValue(QStringLiteral("profile/region"), regionChoice_);
}
