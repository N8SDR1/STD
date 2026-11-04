// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DigitalMode.h"
#include "ModeProfile.h"
#include "rade/RadeMeters.h"
#include "speech/SpeechInput.h"
#include "speech/SpeechReader.h"
#include "dominoex/DominoModem.h"
#include "thor/ThorModem.h"
#include "mfsk/MfskModem.h"
#include "olivia/OliviaModem.h"

#include <algorithm>

#include <QButtonGroup>

#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QListWidget>
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
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
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
    // 60 m is channelized. FT8 is the WSJT-X 5.357 MHz dial; RADE has the
    // two FreeDV Reporter dials. The other modes have none, so the chip is
    // hidden for them. It is last in this table so saved band numbers keep
    // their meaning, and sits between 3.5 and 7 on screen.
    {"5.3", 5357000, 0, 0, 0, 0, 0, 0, 0},
};

constexpr int kBandCount = int(sizeof(kBands) / sizeof(kBands[0]));
constexpr int kDefaultBand = 4;  // 14 MHz
constexpr int k60mBand = 10;

// FreeDV Reporter calling dials (qso.freedv.org) and the regional nets
// named on freedv.org. 20 m stays 14.236 then 14.240, both USB. Those are
// the reporter's digital-voice dials. SSTV calling is 14.230 and digital
// SSTV is 14.233 (ARRL band plan and the considerate-operator guide).
// 160, 80, and 40 are the reporter's LSB dials, so the radio uses DIGL
// there. Every other band is DIGU. 30 m and 6 m are not on that list.
// Hz, first entry is the one a band click lands on.
void radePublished(int index, const qint64*& rows, int& count) {
  static constexpr qint64 k160[] = {1870000};
  static constexpr qint64 k80[] = {3643000, 3625000, 3693000, 3697000, 3803000, 3640000};
  static constexpr qint64 k40[] = {7177000, 7197000, 7045000, 7181500};
  static constexpr qint64 k20[] = {14236000, 14240000};
  static constexpr qint64 k17[] = {18118000};
  static constexpr qint64 k15[] = {21313000};
  static constexpr qint64 k12[] = {24933000};
  static constexpr qint64 k10[] = {28330000, 28720000};
  static constexpr qint64 k60[] = {5368500, 5403500};
  rows = nullptr;
  count = 0;
  switch (index) {
  case 0: rows = k160; count = 1; break;
  case 1: rows = k80; count = 6; break;
  case 2: rows = k40; count = 4; break;
  case 4: rows = k20; count = 2; break;
  case 5: rows = k17; count = 1; break;
  case 6: rows = k15; count = 1; break;
  case 7: rows = k12; count = 1; break;
  case 8: rows = k10; count = 2; break;
  case k60mBand: rows = k60; count = 2; break;
  default: break;
  }
}

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
  // The stylesheet adds 2px padding each side and a 1px frame, and the
  // line edit inside keeps its own 2px margins. Without room for those the
  // last character was cut off ("Speed 9" showed as "Speed (").
  const int width = text + std::max(button, 1) + 16;
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
      "Right-click lists them to edit or delete. DIGU. Does not key."));
  extra_->setContextMenuPolicy(Qt::CustomContextMenu);

  row->addWidget(autoDec);
  row->addWidget(hound);
  row->addWidget(fox);
  row->addWidget(multi);
  row->addWidget(half);
  row->addWidget(txFirst);
  row->addWidget(txSecond);
  row->addWidget(filters);
  auto* extraLabel = new QLabel(QStringLiteral("Extra"), bar_);
  extraLabel->setObjectName(QStringLiteral("profileExtraLabel"));
  extraLabel->setFont(barFont());
  extraLabel->setToolTip(extra_->toolTip());
  row->addWidget(region_);
  row->addWidget(extraLabel);
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
    if (i == k60mBand) {
      // On screen 60 m goes after 3.5.
      QWidget* before = nullptr;
      for (QAbstractButton* other : bands->buttons()) {
        if (other->text() == QLatin1String("3.5"))
          before = other;
      }
      const int at = before ? row->indexOf(before) : -1;
      if (at >= 0)
        row->insertWidget(at + 1, button);
      else
        row->addWidget(button);
    } else {
      row->addWidget(button);
    }
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

  radeAudio_ = new QWidget(this);
  radeAudio_->setObjectName(QStringLiteral("radeAudioBar"));
  auto* audio = new QHBoxLayout(radeAudio_);
  audio->setContentsMargins(8, 0, 8, 6);
  audio->setSpacing(8);
  auto* micLabel = new QLabel(QStringLiteral("Mic"), radeAudio_);
  micLabel->setFont(barFont());
  radeMicBox_ = new QComboBox(radeAudio_);
  radeMicBox_->setObjectName(QStringLiteral("radeMicDevice"));
  radeMicBox_->setFont(barFont());
  radeMicBox_->setMinimumWidth(160);
  radeMicBox_->setMaximumWidth(280);
  radeMicBox_->setToolTip(QStringLiteral(
      "Speech microphone. Same list as Options, Speech. TX uses this device. "
      "This does not key. START is never sent."));
  auto* driveLabel = new QLabel(QStringLiteral("Drive"), radeAudio_);
  driveLabel->setFont(barFont());
  auto* drive = new QSlider(Qt::Horizontal, radeAudio_);
  drive->setObjectName(QStringLiteral("radeDrive"));
  drive->setRange(0, 100);
  drive->setFixedWidth(160);
  drive->setFocusPolicy(Qt::NoFocus);
  drive->setValue(std::clamp(QSettings().value(QStringLiteral("rade/drive"), 70).toInt(), 0, 100));
  drive->setToolTip(QStringLiteral(
      "Modem level sent to the radio. 0 is silent. 100 is the FreeDV app's level. "
      "Set it so ALC barely moves; RADE peaks run well above its average. "
      "Changing it does not key. START is never sent."));
  auto* driveValue = new QLabel(QString::number(drive->value()), radeAudio_);
  driveValue->setObjectName(QStringLiteral("radeDriveValue"));
  driveValue->setFont(barFont());
  driveValue->setMinimumWidth(28);
  connect(drive, &QSlider::valueChanged, this, [driveValue](int value) {
    driveValue->setText(QString::number(value));
    QSettings().setValue(QStringLiteral("rade/drive"), value);
  });
  auto* speakerLabel = new QLabel(QStringLiteral("Speaker"), radeAudio_);
  speakerLabel->setFont(barFont());
  radeSpeakerBox_ = new QComboBox(radeAudio_);
  radeSpeakerBox_->setObjectName(QStringLiteral("radeSpeakerDevice"));
  radeSpeakerBox_->setFont(barFont());
  radeSpeakerBox_->setMinimumWidth(160);
  radeSpeakerBox_->setMaximumWidth(280);
  radeSpeakerBox_->setToolTip(QStringLiteral(
      "Speech Play on device. Decoded speech and analog SSB play here, not on the radio. "
      "This does not key. START is never sent."));
  auto* volumeLabel = new QLabel(QStringLiteral("Volume"), radeAudio_);
  volumeLabel->setFont(barFont());
  radeVolume_ = new QSlider(Qt::Horizontal, radeAudio_);
  radeVolume_->setObjectName(QStringLiteral("radeVolume"));
  radeVolume_->setRange(0, 100);
  radeVolume_->setFixedWidth(140);
  radeVolume_->setToolTip(QStringLiteral(
      "Playback level for decoded speech and analog SSB. This is the Speech volume. "
      "0 is silent and 100 is full. This does not key. START is never sent."));
  radeVolumeValue_ = new QLabel(radeAudio_);
  radeVolumeValue_->setObjectName(QStringLiteral("radeVolumeValue"));
  radeVolumeValue_->setFont(barFont());
  radeVolumeValue_->setMinimumWidth(28);
  auto* analog = new QCheckBox(QStringLiteral("Analog passthrough"), radeAudio_);
  analog->setObjectName(QStringLiteral("radeAnalog"));
  analog->setFont(barFont());
  analog->setFocusPolicy(Qt::NoFocus);
  analog->setChecked(QSettings().value(QStringLiteral("rade/analog"), false).toBool());
  analog->setToolTip(QStringLiteral(
      "Analog voice mix only. Off plays decoded RADE speech and stays silent until sync. "
      "On also plays the radio SSB while sync is idle. "
      "This does not change DIGU or DIGL, and it does not stop TCI audio. "
      "This does not key. START is never sent."));
  connect(analog, &QCheckBox::toggled, this, [](bool on) {
    QSettings().setValue(QStringLiteral("rade/analog"), on);
  });
  auto* denoise = new QCheckBox(QStringLiteral("Mic NR"), radeAudio_);
  denoise->setObjectName(QStringLiteral("radeDenoise"));
  denoise->setFont(barFont());
  denoise->setFocusPolicy(Qt::NoFocus);
  denoise->setChecked(QSettings().value(QStringLiteral("rade/denoise"), true).toBool());
  denoise->setToolTip(QStringLiteral(
      "Mic noise suppression (RNNoise) before the encoder, as in the FreeDV app, where it "
      "is on by default. It takes fans, hum and room noise out of what you send. "
      "This does not key. START is never sent."));
  connect(denoise, &QCheckBox::toggled, this, [](bool on) {
    QSettings().setValue(QStringLiteral("rade/denoise"), on);
  });
  auto* wide = new QCheckBox(QStringLiteral("BW expand"), radeAudio_);
  wide->setObjectName(QStringLiteral("radeWide"));
  wide->setFont(barFont());
  wide->setFocusPolicy(Qt::NoFocus);
  wide->setChecked(QSettings().value(QStringLiteral("rade/wide"), true).toBool());
  wide->setToolTip(QStringLiteral(
      "Bandwidth expander for decoded speech, as in the FreeDV app, where it is on by "
      "default. RADE speech is 16 kHz; this fills in the band above 8 kHz so it plays at "
      "48 kHz and sounds fuller. Off plays the same speech without the top band. "
      "This does not key. START is never sent."));
  connect(wide, &QCheckBox::toggled, this, [](bool on) {
    QSettings().setValue(QStringLiteral("rade/wide"), on);
  });
  auto* gainLabel = new QLabel(QStringLiteral("Gain"), radeAudio_);
  gainLabel->setFont(barFont());
  auto* micGain = new QSlider(Qt::Horizontal, radeAudio_);
  micGain->setObjectName(QStringLiteral("radeMicGain"));
  micGain->setRange(0, 30);
  micGain->setFixedWidth(96);
  micGain->setFocusPolicy(Qt::NoFocus);
  micGain->setValue(std::clamp(QSettings().value(QStringLiteral("rade/micGain"), 0).toInt(), 0, 30));
  micGain->setToolTip(QStringLiteral(
      "Microphone gain before the leveler, in dB. 0 leaves the microphone as Windows set it. "
      "The bar after it shows the speech peaks after the leveler. "
      "Options, Speech has no microphone gain; its Volume is playback. "
      "Drive is the modem level and does not move the bar. "
      "This does not key. START is never sent."));
  auto* micGainValue = new QLabel(QString::number(micGain->value()), radeAudio_);
  micGainValue->setObjectName(QStringLiteral("radeMicGainValue"));
  micGainValue->setFont(barFont());
  micGainValue->setMinimumWidth(22);
  connect(micGain, &QSlider::valueChanged, this, [micGainValue](int value) {
    micGainValue->setText(QString::number(value));
    QSettings().setValue(QStringLiteral("rade/micGain"), value);
  });
  audio->addWidget(micLabel);
  audio->addWidget(radeMicBox_);
  audio->addWidget(gainLabel);
  audio->addWidget(micGain);
  audio->addWidget(micGainValue);
  audio->addWidget(new RadeMicBar(radeAudio_));
  audio->addWidget(driveLabel);
  audio->addWidget(drive);
  audio->addWidget(driveValue);
  audio->addWidget(denoise);
  audio->addWidget(speakerLabel);
  audio->addWidget(radeSpeakerBox_);
  audio->addWidget(volumeLabel);
  audio->addWidget(radeVolume_);
  audio->addWidget(radeVolumeValue_);
  audio->addWidget(wide);
  audio->addWidget(analog);
  audio->addStretch(1);
  radeAudio_->hide();

  keyboard_ = new QLabel(
      QStringLiteral("Keyboard modes keep their own controls. None are loaded yet."), this);
  keyboard_->setObjectName(QStringLiteral("keyboardProfile"));
  keyboard_->setFont(barFont());
  keyboard_->hide();

  auto* stack = new QVBoxLayout(this);
  stack->setContentsMargins(0, 0, 0, 0);
  stack->setSpacing(0);
  stack->addWidget(bar_);
  stack->addWidget(radeAudio_);
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
  connect(radeMicBox_, &QComboBox::currentIndexChanged, this, [this](int) {
    if (!radeMicBox_)
      return;
    QSettings().setValue(QStringLiteral("speech/mic"), radeMicBox_->currentData().toString());
    emit radeCaptureChanged(radeMicBox_->currentData().toString());
  });
  connect(radeSpeakerBox_, &QComboBox::currentIndexChanged, this, [this](int) {
    if (!radeSpeakerBox_)
      return;
    QSettings().setValue(QStringLiteral("speech/output"), radeSpeakerBox_->currentData().toString());
    emit radeOutputChanged(radeSpeakerBox_->currentData().toString());
  });
  connect(radeVolume_, &QSlider::valueChanged, this, [this](int value) {
    if (radeVolumeValue_)
      radeVolumeValue_->setText(QString::number(value));
    QSettings().setValue(QStringLiteral("speech/volume"), value);
    emit radeVolumeChanged(value);
  });
  refreshRadeDevices();
  connect(extra_, &QWidget::customContextMenuRequested, this, [this] { editExtras(); });
  refreshBands();
}

void ModeProfile::setMshvFamily(bool mshv) {
  family_ = mshv;
  bar_->setVisible(mshv);
  keyboard_->setVisible(!mshv && !wsprMode(mode_));
  if (radeAudio_)
    radeAudio_->setVisible(mshv && radeMode(mode_));
}

namespace {

void fillDeviceBox(QComboBox* box, const QList<SpeechReader::Choice>& choices, const QString& current) {
  if (!box)
    return;
  const QSignalBlocker block(box);
  box->clear();
  int pick = 0;
  bool found = false;
  for (int i = 0; i < choices.size(); ++i) {
    box->addItem(choices.at(i).name, choices.at(i).id);
    if (choices.at(i).id == current) {
      pick = i;
      found = true;
    }
  }
  if (!found && !current.isEmpty()) {
    box->addItem(current, current);
    pick = box->count() - 1;
  }
  if (box->count() > 0)
    box->setCurrentIndex(pick);
}

}  // namespace

void ModeProfile::refreshRadeDevices() {
  QSettings store;
  fillDeviceBox(radeMicBox_, SpeechInput::inputs(), store.value(QStringLiteral("speech/mic")).toString());
  fillDeviceBox(radeSpeakerBox_, SpeechReader::outputs(), store.value(QStringLiteral("speech/output")).toString());
  if (!radeVolume_)
    return;
  const QSignalBlocker block(radeVolume_);
  const int volume = std::clamp(store.value(QStringLiteral("speech/volume"), 80).toInt(), 0, 100);
  radeVolume_->setValue(volume);
  if (radeVolumeValue_)
    radeVolumeValue_->setText(QString::number(volume));
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
  else if (wefaxMode(mode))
    next = mode;
  else if (radeMode(mode))
    next = radeV2(mode) ? radeV2Name() : radeV1Name();
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
        "JS8: band activity, conversation and call activity above, the message buttons and the "
        "transmit box below. Click a call to pick it, double-click to move your offset to it. "
        "Band buttons move the radio to the JS8Call-improved calling dial. CLR RX and CLR TX clear "
        "those. TX waits for the next "
        "15 second UTC period and sends one frame, starting half a second in. A longer message "
        "continues on the following periods. Changing mode clears both panes. "
        "START is never sent.");
    label->setText(radeMode(mode_) ? QStringLiteral(
                                         "RADE uses its own screen. V1 is the on-air waveform. "
                                         "V2 is experimental and does not talk to V1. "
                                         "Mic and speaker are the Speech devices. "
                                         "This does not key. START is never sent.")
                    : wefaxMode(mode_) ? QStringLiteral(
                                          "WEFAX-576 receives weather charts. Pick a region and a station "
                                          "below: the dial goes 1.9 kHz under the carrier and the marker to "
                                          "1900 Hz. A chart starts on its start tone; Start begins one now if "
                                          "you tuned in mid-chart. Receive only. START is never sent.")
                    : !keyboard || hell ? QStringLiteral(
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
  const bool rade = radeMode(mode_);
  if (radeAudio_) {
    const bool show = rade && family_;
    const bool was = radeAudio_->isVisible();
    radeAudio_->setVisible(show);
    if (show && !was)
      refreshRadeDevices();
  }
  if (start_)
    start_->setVisible(!keyboard && !rade);
  if (bandwidth_)
    bandwidth_->setVisible(!keyboard && !rade);
  if (region_)
    region_->setVisible(!rade);
  if (extra_) {
    extra_->setToolTip(rade ? QStringLiteral(
                                  "Type a dial in Hz and press Enter to insert it for this mode and band. "
                                  "Click the band button to cycle the FreeDV Reporter dials and those extras. "
                                  "Right-click lists the extras to edit or delete. 160, 80, and 40 use DIGL. The other bands use DIGU. "
                                  "Does not key. START is never sent.")
                            : QStringLiteral(
                                  "Type a dial in Hz and press Enter to insert it for this mode and band. "
                                  "Click the mode or the band button to cycle the WSJT-X default and those extras. "
                                  "Right-click lists them to edit or delete. DIGU. Does not key."));
    if (auto* extraLabel = findChild<QLabel*>(QStringLiteral("profileExtraLabel")))
      extraLabel->setToolTip(extra_->toolTip());
  }
  if (auto* box = findChild<QCheckBox*>(QStringLiteral("profileAutoDec")))
    box->setVisible(!keyboard && !rade);
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
  if (buttons.size() > k60mBand) {
    // 60 m only shows for modes that have a dial there.
    const bool has60 = tableHz(k60mBand) > 0;
    buttons[k60mBand]->setVisible(has60);
    if (!has60 && band_ == k60mBand && kDefaultBand < buttons.size()) {
      band_ = kDefaultBand;
      cycle_ = 0;
      const QSignalBlocker block(buttons[kDefaultBand]);
      buttons[kDefaultBand]->setChecked(true);
      reloadExtras();
    }
  }
  for (int i = 0; i < buttons.size() && i < kBandCount; ++i) {
    const bool here = i == band_;
    const QList<qint64> dials = cycleDials(i);
    const int step = dials.isEmpty() ? 0 : (here ? std::min(cycle_, static_cast<int>(dials.size()) - 1) : 0);
    const qint64 hz = dials.isEmpty() ? tableHz(i) : dials.at(step);
    QString cycleNote;
    if (dials.size() > 1) {
      QStringList texts;
      for (qint64 dial : dials)
        texts << QString::number(dial);
      cycleNote = QStringLiteral(" · %1/%2 %3")
                      .arg(step + 1)
                      .arg(dials.size())
                      .arg(texts.join(QStringLiteral(", ")));
    }
    if (radeMode(mode_)) {
      const QString side = i <= 2 ? QStringLiteral("DIGL") : QStringLiteral("DIGU");
      const QString sstv =
          QLatin1String(kBands[i].label) == QLatin1String("14")
              ? QStringLiteral(
                    " 14.236 and 14.240 are FreeDV digital voice. SSTV calling is 14.230 and digital SSTV is 14.233.")
              : QString();
      const QString body =
          hz > 0 ? QStringLiteral("%1 MHz  ·  %2 %3 Hz  ·  %4%5. FreeDV Reporter.%6 Click again to cycle. "
                                  "Extra keeps another dial for this mode and band. Does not key. "
                                  "START is never sent.")
                       .arg(QString::fromLatin1(kBands[i].label), mode_, QString::number(hz), side, cycleNote, sstv)
                 : QStringLiteral("%1 MHz  ·  %2 has no FreeDV Reporter dial. Does not key. START is never sent.")
                       .arg(QString::fromLatin1(kBands[i].label), mode_);
      buttons[i]->setToolTip(body);
    } else {
      const QString region = chosen >= 1 ? QStringLiteral("Region %1").arg(chosen) : QStringLiteral("Auto");
      buttons[i]->setToolTip(QStringLiteral("%1 MHz  ·  %2 %3 Hz  ·  %4  ·  DIGU%5")
                                 .arg(QString::fromLatin1(kBands[i].label), mode_, QString::number(hz), region,
                                      cycleNote));
    }
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
  if (radeMode(mode_)) {
    const qint64* rows = nullptr;
    int count = 0;
    radePublished(index, rows, count);
    return count > 0 ? rows[0] : 0;
  }
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

QList<qint64> ModeProfile::cycleDials(int index) const {
  QList<qint64> dials;
  if (radeMode(mode_)) {
    const qint64* rows = nullptr;
    int count = 0;
    radePublished(index, rows, count);
    for (int i = 0; i < count; ++i)
      dials.append(rows[i]);
  } else {
    dials.append(tableHz(index));
  }
  const QList<qint64> extras = index == band_ ? extras_ : storedExtras(index);
  for (qint64 hz : extras) {
    if (hz > 0 && !dials.contains(hz))
      dials.append(hz);
  }
  return dials;
}

bool ModeProfile::stockDial(int index, qint64 hz) const {
  if (radeMode(mode_)) {
    const qint64* rows = nullptr;
    int count = 0;
    radePublished(index, rows, count);
    for (int i = 0; i < count; ++i) {
      if (rows[i] == hz)
        return true;
    }
    return false;
  }
  return hz == tableHz(index);
}

qint64 ModeProfile::dialHz() const {
  const QList<qint64> dials = cycleDials(band_);
  if (dials.isEmpty())
    return 0;
  return dials.at(std::clamp(cycle_, 0, static_cast<int>(dials.size()) - 1));
}

QList<qint64> ModeProfile::storedExtras(int index) const {
  QList<qint64> extras;
  QSettings settings;
  const QString key =
      QStringLiteral("profile/extra/%1/%2").arg(mode_, QString::fromLatin1(kBands[index].label));
  const QVariant stored = settings.value(key);
  const auto accept = [&extras, this, index](qint64 hz) {
    if (hz > 0 && !stockDial(index, hz) && !extras.contains(hz))
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
  if (cycle_ >= cycleDials(band_).size())
    cycle_ = 0;
  if (!extra_)
    return;
  const QSignalBlocker block(extra_);
  extra_->setValue(0);
}

namespace {

// "7.160" is MHz, "7160" is kHz, "7160000" is Hz. 0 when it is not a dial.
qint64 parseDial(const QString& text) {
  QString t = text.trimmed().toLower();
  t.remove(QStringLiteral("mhz"));
  t.remove(QStringLiteral("khz"));
  t.remove(QStringLiteral("hz"));
  t.remove(QLatin1Char(','));
  t = t.trimmed();
  bool ok = false;
  const double v = t.toDouble(&ok);
  if (!ok || v <= 0)
    return 0;
  double hz = v;
  if (v < 1000.0)
    hz = v * 1e6;
  else if (v < 1e6)
    hz = v * 1e3;
  return (hz >= 100000.0 && hz <= 250000000.0) ? qint64(std::llround(hz)) : 0;
}

QString dialText(qint64 hz) {
  return QString::number(double(hz) / 1e6, 'f', 4) + QStringLiteral(" MHz");
}

}  // namespace

void ModeProfile::setExtraDials(const QList<qint64>& dials) {
  const qint64 before = dialHz();
  extras_.clear();
  for (qint64 hz : dials)
    if (hz > 0 && !stockDial(band_, hz) && !extras_.contains(hz))
      extras_.append(hz);
  saveExtras();
  if (cycle_ >= cycleDials(band_).size())
    cycle_ = 0;
  refreshBands();
  // Only move the radio when the dial it was on is gone.
  if (dialHz() != before && !cycleDials(band_).contains(before)) {
    cycle_ = 0;
    refreshBands();
    emit tuneRequested(dialHz(), sideband());
  }
}

void ModeProfile::editExtras() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Extra dials, %1, %2 MHz band").arg(mode_, QString::fromLatin1(kBands[band_].label)));
  dialog.setMinimumWidth(380);
  auto* note = new QLabel(QStringLiteral(
      "Extra dials for this mode and band. They join the band button's cycle, and RADE Scan "
      "listens on them too. Double-click a dial to edit it. Type 7.160 for MHz, 7160 for kHz, "
      "or 7160000 for Hz. This does not key. START is never sent."), &dialog);
  note->setWordWrap(true);
  auto* list = new QListWidget(&dialog);
  list->setObjectName(QStringLiteral("extraDialList"));
  for (qint64 hz : extras_) {
    auto* item = new QListWidgetItem(dialText(hz), list);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
  }
  auto* add = new QPushButton(QStringLiteral("Add"), &dialog);
  auto* remove = new QPushButton(QStringLiteral("Delete"), &dialog);
  auto* clear = new QPushButton(QStringLiteral("Clear all"), &dialog);
  auto* ok = new QPushButton(QStringLiteral("OK"), &dialog);
  auto* cancel = new QPushButton(QStringLiteral("Cancel"), &dialog);
  ok->setDefault(true);
  connect(add, &QPushButton::clicked, &dialog, [list] {
    const qint64 base = list->count() > 0 ? parseDial(list->item(list->count() - 1)->text()) : 0;
    auto* item = new QListWidgetItem(base > 0 ? dialText(base) : QStringLiteral("0.0000 MHz"), list);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    list->setCurrentItem(item);
    list->editItem(item);
  });
  connect(remove, &QPushButton::clicked, &dialog, [list] { delete list->currentItem(); });
  connect(clear, &QPushButton::clicked, &dialog, [list] { list->clear(); });
  connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  auto* side = new QVBoxLayout;
  side->addWidget(add);
  side->addWidget(remove);
  side->addWidget(clear);
  side->addStretch(1);
  auto* middle = new QHBoxLayout;
  middle->addWidget(list, 1);
  middle->addLayout(side);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(note);
  layout->addLayout(middle);
  layout->addLayout(buttons);
  if (dialog.exec() != QDialog::Accepted)
    return;
  QList<qint64> dials;
  for (int i = 0; i < list->count(); ++i) {
    const qint64 hz = parseDial(list->item(i)->text());
    if (hz > 0)
      dials.append(hz);
  }
  setExtraDials(dials);
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

namespace {

// RADE bands with FreeDV Reporter dials, low to high: band index and meters.
struct ScanBand {
  int index;
  const char* meters;
};
constexpr ScanBand kScanBands[] = {{0, "160"}, {1, "80"}, {k60mBand, "60"}, {2, "40"}, {4, "20"},
                                   {5, "17"},  {6, "15"}, {7, "12"},        {8, "10"}};

}  // namespace

QStringList ModeProfile::radeScanBands() {
  QStringList out;
  for (const ScanBand& band : kScanBands)
    out << QString::fromLatin1(band.meters);
  return out;
}

QList<QPair<int, qint64>> ModeProfile::radeScanDials(const QStringList& meters, bool extras) const {
  QList<QPair<int, qint64>> out;
  for (const ScanBand& scanBand : kScanBands) {
    if (!meters.isEmpty() && !meters.contains(QString::fromLatin1(scanBand.meters)))
      continue;
    const int band = scanBand.index;
    if (extras && radeMode(mode_)) {
      // The band's cycle: reporter dials first, then the operator's extras.
      for (qint64 hz : cycleDials(band))
        if (hz > 0)
          out.append(qMakePair(band, hz));
      continue;
    }
    const qint64* rows = nullptr;
    int count = 0;
    radePublished(band, rows, count);
    for (int i = 0; i < count; ++i)
      out.append(qMakePair(band, rows[i]));
  }
  return out;
}

void ModeProfile::selectDial(int band, qint64 hz) {
  if (band < 0 || band >= kBandCount)
    return;
  if (band != band_) {
    band_ = band;
    reloadExtras();
  }
  const QList<qint64> dials = cycleDials(band_);
  cycle_ = std::max(0, static_cast<int>(dials.indexOf(hz)));
  const auto buttons = bar_->findChildren<QPushButton*>(QStringLiteral("profileBand"),
                                                        Qt::FindDirectChildrenOnly);
  if (band_ < buttons.size()) {
    const QSignalBlocker block(buttons[band_]);
    buttons[band_]->setChecked(true);
  }
  remember();
  refreshBands();
  emit tuneRequested(dialHz(), sideband());
}

void ModeProfile::advanceCycle() {
  reloadExtras();
  const int count = cycleDials(band_).size();
  if (count <= 1) {
    cycle_ = 0;
    return;
  }
  cycle_ = (cycle_ + 1) % count;
}

void ModeProfile::insertExtra(qint64 hz) {
  if (!extra_)
    return;
  if (hz > 0 && !stockDial(band_, hz) && !extras_.contains(hz)) {
    extras_.append(hz);
    saveExtras();
  }
  const QSignalBlocker block(extra_);
  extra_->setValue(0);
  refreshBands();
}

QString ModeProfile::sideband() const {
  // FreeDV Reporter uses LSB on 160, 80, and 40. Those chips are the first three.
  if (radeMode(mode_) && band_ >= 0 && band_ <= 2)
    return QStringLiteral("DIGL");
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
