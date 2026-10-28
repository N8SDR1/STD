// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindow.h"

#include "Ft8Decode.h"
#include "hell/HellEngine.h"
#include "hell/HellRaster.h"
#include "ModeProfile.h"
#include "StatusStrip.h"
#include "TciClient.h"
#include "TxWave.h"
#include "WaterfallWidget.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDataStream>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QFormLayout>
#include <QIcon>
#include <QDockWidget>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTextCursor>
#include <QTextFrame>
#include <QUrl>
#include <QSizePolicy>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QFont interfaceFont(int pixelSize, QFont::Weight weight = QFont::Normal) {
  QFont font("Inter");
  font.setPixelSize(pixelSize);
  font.setWeight(weight);
  return font;
}

QFont monoFont(int pixelSize) {
  QFont font("JetBrains Mono");
  font.setStyleHint(QFont::Monospace);
  font.setPixelSize(pixelSize);
  return font;
}

bool gridCenter(const QString& token, double* lat, double* lon) {
  const QString grid = token.toUpper();
  if (grid.size() != 4 && grid.size() != 6)
    return false;
  if (grid.at(0) < QLatin1Char('A') || grid.at(0) > QLatin1Char('R'))
    return false;
  if (grid.at(1) < QLatin1Char('A') || grid.at(1) > QLatin1Char('R'))
    return false;
  if (!grid.at(2).isDigit() || !grid.at(3).isDigit())
    return false;
  if (grid.size() == 6 && (grid.at(4) < QLatin1Char('A') || grid.at(4) > QLatin1Char('X') ||
                           grid.at(5) < QLatin1Char('A') || grid.at(5) > QLatin1Char('X')))
    return false;
  double lonv = (grid.at(0).unicode() - u'A') * 20.0 - 180.0;
  double latv = (grid.at(1).unicode() - u'A') * 10.0 - 90.0;
  lonv += (grid.at(2).unicode() - u'0') * 2.0;
  latv += (grid.at(3).unicode() - u'0') * 1.0;
  if (grid.size() == 6) {
    lonv += (grid.at(4).unicode() - u'A') * (5.0 / 60.0);
    latv += (grid.at(5).unicode() - u'A') * (2.5 / 60.0);
    lonv += 2.5 / 60.0;
    latv += 1.25 / 60.0;
  } else {
    lonv += 1.0;
    latv += 0.5;
  }
  *lat = latv;
  *lon = lonv;
  return true;
}

QString gridInMessage(const QString& message) {
  const QStringList tokens = message.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (int i = tokens.size() - 1; i >= 0; --i) {
    double lat = 0.0;
    double lon = 0.0;
    QString token = tokens.at(i);
    token.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
    if (gridCenter(token, &lat, &lon))
      return token.toUpper();
  }
  return {};
}

double kilometers(double lat1, double lon1, double lat2, double lon2) {
  constexpr double kRadius = 6371.0;
  constexpr double kDeg = 0.017453292519943295;
  const double dLat = (lat2 - lat1) * kDeg;
  const double dLon = (lon2 - lon1) * kDeg;
  const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1 * kDeg) * std::cos(lat2 * kDeg) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return kRadius * 2 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
}

int continentOf(const QString& call) {
  const QString token = call.toUpper().section(QLatin1Char('/'), 0, 0);
  const struct {
    const char* prefix;
    int continent;
  } rows[] = {
      {"KH6", 4}, {"KL", 5},  {"VE", 5},  {"VO", 5},  {"VY", 5},  {"XE", 5},  {"CM", 5},  {"CO", 5},
      {"TG", 5},  {"TI", 5},  {"HP", 5},  {"HR", 5},  {"YN", 5},  {"YS", 5},  {"HI", 5},  {"HH", 5},
      {"HK", 6},  {"HC", 6},  {"CP", 6},  {"PY", 6},  {"LU", 6},  {"CE", 6},  {"CX", 6},  {"ZP", 6},
      {"OA", 6},  {"YV", 6},  {"VK", 4},  {"ZL", 4},  {"JA", 2},  {"BY", 2},  {"BV", 2},  {"HL", 2},
      {"HS", 2},  {"VU", 2},  {"9M", 2},  {"YB", 2},  {"4X", 2},  {"HZ", 2},  {"TA", 2},  {"ZS", 0},
      {"5Z", 0},  {"5N", 0},  {"CN", 0},  {"SU", 0},  {"G", 3},   {"M", 3},   {"EI", 3},  {"F", 3},
      {"EA", 3},  {"CT", 3},  {"DL", 3},  {"PA", 3},  {"ON", 3},  {"HB", 3},  {"I", 3},   {"LA", 3},
      {"SM", 3},  {"OH", 3},  {"OZ", 3},  {"SP", 3},  {"OK", 3},  {"HA", 3},  {"YO", 3},  {"LZ", 3},
      {"SV", 3},  {"K", 5},   {"N", 5},   {"W", 5},   {"A", 5},
  };
  int best = -1;
  int length = 0;
  for (const auto& row : rows) {
    const QString prefix = QString::fromLatin1(row.prefix);
    if (token.startsWith(prefix) && prefix.size() > length) {
      best = row.continent;
      length = prefix.size();
    }
  }
  return best;
}

QString otherCall(const QString& message, const QString& mine) {
  const QStringList tokens = message.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (const QString& token : tokens) {
    if (token.compare(QLatin1String("CQ"), Qt::CaseInsensitive) == 0)
      continue;
    if (token.compare(QLatin1String("DE"), Qt::CaseInsensitive) == 0)
      continue;
    bool digit = false;
    bool letter = false;
    for (const QChar ch : token) {
      digit = digit || ch.isDigit();
      letter = letter || ch.isLetter();
    }
    if (!digit || !letter || token.size() < 3)
      continue;
    if (!mine.isEmpty() && token.compare(mine, Qt::CaseInsensitive) == 0)
      continue;
    return token.toUpper();
  }
  return {};
}

QString columnCell(const QString& text, int width) {
  const QString cut = text.size() > width ? text.left(width) : text;
  return cut.leftJustified(width, QLatin1Char(' '));
}

QString decodeColumns(const QString& time, const QString& db, const QString& dt, const QString& dist,
                      const QString& message, const QString& freq, bool showTime, bool showFreq,
                      bool showDist) {
  QStringList fields;
  if (showTime)
    fields << columnCell(time, 8);
  fields << columnCell(db, 4);
  fields << columnCell(dt, 6);
  if (showDist)
    fields << columnCell(dist, 6);
  fields << columnCell(message, 28);
  if (showFreq)
    fields << columnCell(freq, 6);
  return fields.join(QStringLiteral(" | "));
}

// Raised capsule lettering: bruise-purple face, amber tube glow.
class RaisedBrandTitle : public QWidget {
public:
  explicit RaisedBrandTitle(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName("brandName");
    setAccessibleName("STD");
    QFont face("Inter");
    face.setPixelSize(32);
    face.setWeight(QFont::Black);
    face.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
    setFont(face);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
  }

  QSize sizeHint() const override {
    const QFontMetrics metrics(font());
    return {metrics.horizontalAdvance(QStringLiteral("STD")) + 28, metrics.height() + 24};
  }

  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QFontMetrics metrics(font());
    const qreal padX = 12.0;
    const qreal padY = 12.0;
    QPainterPath letters;
    letters.addText(padX, padY + metrics.ascent(), font(), QStringLiteral("STD"));

    painter.setBrush(Qt::NoBrush);
    const int glowWidths[] = {18, 12, 7};
    const int glowAlpha[] = {18, 32, 54};
    for (int i = 0; i < 3; ++i) {
      painter.setPen(QPen(QColor(255, 146, 38, glowAlpha[i]), glowWidths[i], Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(letters);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(22, 4, 16, 220));
    painter.drawPath(letters.translated(2.6, 3.4));
    painter.setBrush(QColor(96, 28, 78, 235));
    painter.drawPath(letters.translated(1.3, 1.8));

    const QRectF bounds = letters.boundingRect();
    QLinearGradient face(bounds.topLeft(), bounds.bottomLeft());
    face.setColorAt(0.0, QColor(232, 206, 246));
    face.setColorAt(0.38, QColor(154, 72, 196));
    face.setColorAt(0.72, QColor(108, 36, 158));
    face.setColorAt(1.0, QColor(62, 16, 96));
    painter.setBrush(face);
    painter.drawPath(letters);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(244, 226, 255, 190), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(letters.translated(-0.7, -0.85));
    painter.setPen(QPen(QColor(255, 176, 62, 220), 1.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(letters.translated(0.65, 0.8));
  }
};

QString formatDial(qint64 hz) {
  if (hz < 0)
    return QString::fromUtf8("—");
  QString digits = QString::number(hz);
  for (int i = digits.size() - 3; i > 0; i -= 3)
    digits.insert(i, QLatin1Char('.'));
  return digits;
}

}  // namespace

QIcon stdIcon() {
  QIcon icon;
  const QPixmap base(":/std/std-icon.png");
  if (base.isNull())
    return icon;
  const int sizes[] = {16, 22, 32, 48, 64, 128, 256};
  for (int size : sizes)
    icon.addPixmap(base.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  return icon;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("STD");
  setWindowIcon(stdIcon());
  resize(1240, 860);
  setMinimumSize(880, 620);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                 QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
  setDockNestingEnabled(true);

  buildBrandBar();
  buildDocks();
  buildMenus();
  arrangeDefault();

  tci_ = new TciClient(this);
  if (profile_ && waterfall_) {
    waterfall_->setTraceSpeed(profile_->speed());
    connect(profile_, &ModeProfile::displayChanged, waterfall_, &WaterfallWidget::setFrequencyWindow);
    connect(profile_, &ModeProfile::speedChanged, waterfall_, &WaterfallWidget::setTraceSpeed);
    connect(waterfall_, &WaterfallWidget::toneChanged, this, [this] { rememberModeTone(); });
    connect(waterfall_, &WaterfallWidget::txHotChanged, this, [this](bool hot) {
      if (statusStrip_)
        statusStrip_->setTxHot(hot);
    });
    applyModeTone();
    connect(profile_, &ModeProfile::tuneRequested, this,
            [this](qint64 hz, const QString&) {
              if (!tci_ || !profile_->mshvFamily())
                return;
              // FT8 and FT4 stay DIGU on every band, including 160, 80, and 40.
              tci_->setSideband(QStringLiteral("DIGU"));
              tci_->setDialHz(hz);
            });
  }

  connect(tci_, &TciClient::receiveBandChanged, this, [this](int lowHz, int highHz) {
    if (waterfall_)
      waterfall_->setReceiveBand(lowHz, highHz);
  });
  connect(tci_, &TciClient::sessionUp, this, [this](const QString& host, quint16 port) {
    showRadioUp(host, port);
  });
  connect(tci_, &TciClient::sessionDown, this, [this](const QString& error) {
    if (txTune_)
      stopTune(true);
    else
      stopUserTx(true);
    showRadioDown(error);
  });
  connect(tci_, &TciClient::dialChanged, this, [this](qint64 hz) { showDial(hz); });
  connect(tci_, &TciClient::modeChanged, this, [this](const QString& mode) {
    if (statusStrip_)
      statusStrip_->setMode(mode);
  });
  connect(tci_, &TciClient::pttChanged, this, [this](bool on) {
    if (statusStrip_)
      statusStrip_->setPtt(on, tci_->isOpen());
    // The lamp follows the radio. STD is not transmitting, and RX
    // energy must not raise the TX-gain notice.
    if (waterfall_)
      waterfall_->setTransmitting(false);
  });
  auto rememberGain = [this](const char* name, const char* key, bool transmit) {
    auto* spin = statusStrip_ ? statusStrip_->findChild<QSpinBox*>(QLatin1String(name)) : nullptr;
    if (!spin)
      return;
    auto apply = [this, key, transmit](int db) {
      const float linear = std::pow(10.0f, db / 20.0f);
      if (transmit) {
        if (tci_)
          tci_->setTxGain(linear);
      } else {
        rxGain_ = linear;
      }
      QSettings settings;
      settings.setValue(QLatin1String(key), db);
    };
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, apply);
    apply(spin->value());
  };
  rememberGain("rxGain", "audio/rxGainDb", false);
  rememberGain("txGain", "audio/txGainDb", true);

  connect(tci_, &TciClient::rxAudio, this, [this](int rate, const QByteArray& mono) {
    if (mono.size() < int(sizeof(float)))
      return;
    const auto* samples = reinterpret_cast<const float*>(mono.constData());
    const int count = mono.size() / int(sizeof(float));
    rxScaled_.resize(static_cast<size_t>(count));
    const float gain = rxGain_;
    for (int i = 0; i < count; ++i)
      rxScaled_[static_cast<size_t>(i)] = samples[i] * gain;
    const float* gained = rxScaled_.data();
    if (waterfall_)
      waterfall_->pushRealAudio(gained, count, rate);
    if (statusStrip_)
      statusStrip_->setRxLevel(gained, count);
    if (ft8_)
      ft8_->pushAudio(gained, count, rate);
    if (hell_)
      hell_->pushAudio(gained, count, rate);
  });

  hell_ = new HellEngine(this);
  connect(hell_, &HellEngine::columnReady, this, [this](const QByteArray& column) {
    if (auto* raster = findChild<HellRaster*>(QStringLiteral("hellReceive")))
      raster->addColumn(column);
  });
  ft8_ = new Ft8Decode(this);
  connect(ft8_, &Ft8Decode::decoded, this,
          [this](const QString& time, int db, const QString& dt, const QString& message, int freqHz) {
            if (clearEachPeriod_ && !time.isEmpty() && time != lastPeriod_)
              decodes_.clear();
            if (!time.isEmpty()) {
              lastPeriod_ = time;
              for (const DecodeRow& row : decodes_) {
                const int df = row.freqHz - freqHz;
                if (row.time == time && row.message == message && df > -6 && df < 6)
                  return;
              }
            }
            if (maxSignals_ > 0 && !time.isEmpty()) {
              int count = 0;
              int weakest = -1;
              int weakestDb = 999;
              for (int i = 0; i < decodes_.size(); ++i) {
                if (decodes_.at(i).time != time)
                  continue;
                ++count;
                if (decodes_.at(i).db < weakestDb) {
                  weakestDb = decodes_.at(i).db;
                  weakest = i;
                }
              }
              if (count >= maxSignals_) {
                if (weakest < 0 || db <= weakestDb)
                  return;
                decodes_.removeAt(weakest);
              }
            }
            if (decodes_.size() >= 500)
              decodes_.remove(0, decodes_.size() - 499);
            decodes_.push_back(DecodeRow{time, db, dt, message, freqHz});
            refreshDecodes();
          },
          Qt::QueuedConnection);
  if (waterfall_) {
    ft8_->setSpot(waterfall_->rxHz(), waterfall_->txHz());
    connect(waterfall_, &WaterfallWidget::frequenciesChanged, ft8_, &Ft8Decode::setSpot);
  }
  if (profile_) {
    ft8_->setMultiAnswer(profile_->multiAnswer());
    connect(profile_, &ModeProfile::multiAnswerChanged, ft8_, &Ft8Decode::setMultiAnswer);
    connect(profile_, &ModeProfile::houndChanged, this, [this](bool on) {
      if (on && waterfall_)
        waterfall_->setRxHz(750);
      applyChannelWidth();
    });
    connect(profile_, &ModeProfile::filtersChanged, this, [this](bool on) {
      filtersOn_ = on;
      refreshDecodes();
    });
    connect(profile_, &ModeProfile::filterEditRequested, this, [this] { openDecodeFilters(); });
    filtersOn_ = profile_->findChild<QPushButton*>(QStringLiteral("decodeFilters")) &&
                 profile_->findChild<QPushButton*>(QStringLiteral("decodeFilters"))->isChecked();
  }
  auto* decodeClock = new QTimer(this);
  connect(decodeClock, &QTimer::timeout, this, [this] {
    if (!ft8_ || !profile_)
      return;
    const QString mode = profile_->digitalMode();
    const bool ft4 = mode == QLatin1String("FT4");
    const bool ft2 = mode == QLatin1String("FT2");
    const bool hell = mode == QLatin1String("Hell");
    const bool armed = autoMonitor_ && profile_->mshvFamily() &&
                       (ft4 || ft2 || mode == QLatin1String("FT8")) && profile_->autoDecode();
    if (hell_) {
      hell_->setArmed(autoMonitor_ && hell);
      if (hell && waterfall_)
        hell_->setToneHz(waterfall_->rxHz());
    }
    int low = profile_->startHz();
    int high = low + profile_->bandwidthHz();
    if (!ft4 && !ft2 && !hell && profile_->superHound()) {
      low = 750;
      high = 750 + 1488;
    }
    ft8_->setMode(ft2 ? Ft8Decode::Period::Ft2 : ft4 ? Ft8Decode::Period::Ft4 : Ft8Decode::Period::Ft8);
    ft8_->setArmed(armed, low, high);
  });
  decodeClock->start(200);

  txTimer_ = new QTimer(this);
  txTimer_->setTimerType(Qt::PreciseTimer);
  connect(txTimer_, &QTimer::timeout, this, [this] { pumpUserTx(); });

  applyDxOptions();
  QTimer::singleShot(0, this, [this] { maybeAutoConnect(); });
}

void MainWindow::buildBrandBar() {
  brandBar_ = addToolBar("STD");
  brandBar_->setObjectName("brandBar");
  brandBar_->setMovable(false);
  brandBar_->setFloatable(false);
  brandBar_->setAllowedAreas(Qt::TopToolBarArea);

  auto* host = new QWidget(brandBar_);
  host->setFixedHeight(110);
  auto* layout = new QHBoxLayout(host);
  layout->setContentsMargins(10, 8, 16, 8);
  layout->setSpacing(14);

  auto* mark = new QLabel(host);
  const QPixmap icon(":/std/std-icon.png");
  mark->setPixmap(icon.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  mark->setFixedSize(72, 72);

  auto* words = new QWidget(host);
  auto* wordLayout = new QVBoxLayout(words);
  wordLayout->setContentsMargins(0, 0, 0, 0);
  wordLayout->setSpacing(4);

  auto* name = new RaisedBrandTitle(words);

  auto* subtitle = new QLabel("Shift, Tone, Decode", words);
  subtitle->setObjectName("brandSubtitle");
  subtitle->setFont(interfaceFont(18));
  subtitle->setContentsMargins(12, 0, 0, 0);

  wordLayout->addWidget(name);
  wordLayout->addWidget(subtitle);

  layout->addWidget(mark);
  layout->addWidget(words);
  layout->addStretch(1);

  brandBar_->addWidget(host);
}

void MainWindow::buildDocks() {
  waterfall_ = new WaterfallWidget(this);
  profile_ = new ModeProfile(this);
  auto* column = new QWidget(this);
  column->setObjectName("waterfallColumn");
  auto* columnLayout = new QVBoxLayout(column);
  columnLayout->setContentsMargins(0, 0, 0, 0);
  columnLayout->setSpacing(0);
  columnLayout->addWidget(waterfall_, 1);
  columnLayout->addWidget(profile_);
  if (auto* tone = waterfall_->findChild<QWidget*>(QStringLiteral("waterfallTone"))) {
    if (auto* row = qobject_cast<QHBoxLayout*>(tone->layout())) {
      auto* lock = waterfall_->findChild<QPushButton*>(QStringLiteral("freqLock"));
      int at = row->indexOf(lock);
      if (at < 0)
        at = row->count();
      profile_->speedBox()->setParent(tone);
      profile_->startBox()->setParent(tone);
      profile_->bandwidthBox()->setParent(tone);
      row->insertWidget(at, profile_->speedBox());
      row->insertWidget(at + 1, profile_->startBox());
      row->insertWidget(at + 2, profile_->bandwidthBox());
    }
  }
  applyChannelWidth();

  waterfallDock_ = new QDockWidget("Waterfall", this);
  waterfallDock_->setObjectName("waterfallDock");
  waterfallDock_->setWidget(column);
  waterfallDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  waterfallDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

  receiveDock_ = new QDockWidget("Receive", this);
  receiveDock_->setObjectName("receiveDock");
  receiveDock_->setWidget(makeReceivePanel());
  receiveDock_->setAllowedAreas(Qt::AllDockWidgetAreas);

  transmitDock_ = new QDockWidget("Transmit", this);
  transmitDock_->setObjectName("transmitDock");
  transmitDock_->setWidget(makeTextPanel(
      "transmitText",
      "Transmit text.\nCharacters stay in this window until you press TX or Tune.",
      false,
      "Click a decode to load a reply, or press Tune for a tone. START is never sent."));
  transmitDock_->setAllowedAreas(Qt::AllDockWidgetAreas);

  macroDock_ = new QDockWidget("Macros", this);
  macroDock_->setObjectName("macroDock");
  macroDock_->setWidget(makeMacroRow());
  macroDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  macroDock_->setMinimumHeight(68);
  macroDock_->setMaximumHeight(96);

  statusDock_ = new QDockWidget("Status", this);
  statusDock_->setObjectName("statusDock");
  statusStrip_ = new StatusStrip(statusDock_);
  statusDock_->setWidget(statusStrip_);
  statusDock_->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
  statusDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
  statusDock_->setMinimumHeight(40);
  statusDock_->setMaximumHeight(48);
  auto* hiddenTitle = new QWidget(statusDock_);
  hiddenTitle->setFixedSize(0, 0);
  statusDock_->setTitleBarWidget(hiddenTitle);
}

QWidget* MainWindow::makeTextPanel(const QString& editName, const QString& placeholder,
                                   bool readOnly, const QString& note) {
  auto* panel = new QWidget(this);
  panel->setObjectName("textPanel");
  panel->setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  if (editName == QLatin1String("transmitText"))
    addStationFields(panel, layout, true);

  auto* edit = new QPlainTextEdit(panel);
  edit->setObjectName(editName);
  edit->setPlaceholderText(placeholder);
  edit->setReadOnly(readOnly);
  edit->setFont(monoFont(13));
  edit->setTabChangesFocus(true);
  edit->setMinimumHeight(72);
  layout->addWidget(edit);

  if (editName == QLatin1String("transmitText")) {
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    auto* tune = new QPushButton(QStringLiteral("Tune"), panel);
    tune->setObjectName(QStringLiteral("txTune"));
    tune->setCheckable(true);
    tune->setChecked(false);
    tune->setFont(interfaceFont(12));
    tune->setFocusPolicy(Qt::NoFocus);
    tune->setToolTip(QStringLiteral(
        "Send a tone at the TX marker so you can set TX gain and hear RF. "
        "TRX goes out only while this is pressed. START is never sent."));
    connect(tune, &QPushButton::toggled, this, [this](bool on) { setTune(on); });
    auto* tx = new QPushButton(QStringLiteral("TX"), panel);
    tx->setObjectName(QStringLiteral("txSend"));
    tx->setCheckable(true);
    tx->setChecked(false);
    tx->setFont(interfaceFont(12));
    tx->setFocusPolicy(Qt::NoFocus);
    tx->setToolTip(QStringLiteral(
        "Send one FT8, FT4, or Hell transmission of the text in this box. "
        "Click a decode to load a reply. TRX goes out only after this is pressed. START is never sent."));
    connect(tx, &QPushButton::toggled, this, [this](bool on) { setUserTx(on); });
    buttons->addWidget(tune);
    buttons->addWidget(tx);
    buttons->addStretch(1);
    layout->addLayout(buttons);
  }

  if (!note.isEmpty()) {
    auto* caption = new QLabel(note, panel);
    caption->setObjectName(editName == QLatin1String("transmitText") ? QStringLiteral("txStatus")
                                                                      : QStringLiteral("panelNote"));
    caption->setFont(interfaceFont(11));
    caption->setWordWrap(true);
    layout->addWidget(caption);
  }
  return panel;
}

void MainWindow::addStationFields(QWidget* panel, QVBoxLayout* layout, bool transmit) {
  QSettings settings;
  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  auto* callLabel = new QLabel(QStringLiteral("Callsign"), panel);
  callLabel->setFont(interfaceFont(12));
  auto* call = new QLineEdit(panel);
  call->setObjectName(transmit ? QStringLiteral("txStationCall") : QStringLiteral("stationCall"));
  call->setPlaceholderText(QStringLiteral("N8SDR"));
  call->setMaxLength(20);
  call->setClearButtonEnabled(true);
  call->setFont(interfaceFont(12));
  call->setToolTip(QStringLiteral(
      "Your callsign. TX replies use it. A portable suffix such as N8SDR/VE sets the region. "
      "This does not key."));
  call->setText(settings.value(QStringLiteral("dx/reportCall")).toString());
  auto* gridLabel = new QLabel(QStringLiteral("Locator"), panel);
  gridLabel->setFont(interfaceFont(12));
  auto* grid = new QLineEdit(panel);
  grid->setObjectName(transmit ? QStringLiteral("txStationGrid") : QStringLiteral("stationGrid"));
  grid->setPlaceholderText(QStringLiteral("EM79"));
  grid->setMaxLength(6);
  grid->setFixedWidth(76);
  grid->setClearButtonEnabled(true);
  grid->setFont(interfaceFont(12));
  grid->setToolTip(QStringLiteral(
      "Your grid. DIST is from here to the grid in a decode. TX replies include it. This does not key."));
  grid->setText(settings.value(QStringLiteral("dx/reportGrid")).toString());
  row->addWidget(callLabel);
  row->addWidget(call, 1);
  row->addWidget(gridLabel);
  row->addWidget(grid);
  layout->addLayout(row);
  connect(call, &QLineEdit::textChanged, this, [this, call] { rememberStation(call); });
  connect(grid, &QLineEdit::textChanged, this, [this, grid] { rememberStation(grid); });
}

void MainWindow::rememberStation(QLineEdit* source) {
  if (!source)
    return;
  const int cursor = source->cursorPosition();
  const QString upper = source->text().toUpper();
  if (upper != source->text()) {
    const QSignalBlocker block(source);
    source->setText(upper);
    source->setCursorPosition(cursor);
  }
  const bool call = source->objectName().contains(QStringLiteral("Call"));
  const QString text = source->text().trimmed();
  QSettings store;
  store.setValue(call ? QStringLiteral("dx/reportCall") : QStringLiteral("dx/reportGrid"), text);
  const QString name = source->objectName();
  QString otherName = QStringLiteral("stationGrid");
  if (name == QLatin1String("stationCall"))
    otherName = QStringLiteral("txStationCall");
  else if (name == QLatin1String("txStationCall"))
    otherName = QStringLiteral("stationCall");
  else if (name == QLatin1String("stationGrid"))
    otherName = QStringLiteral("txStationGrid");
  if (auto* other = findChild<QLineEdit*>(otherName)) {
    if (other->text() != source->text()) {
      const QSignalBlocker block(other);
      other->setText(source->text());
    }
  }
  if (call) {
    if (profile_)
      profile_->refreshStation();
  } else
    refreshDecodes();
}

QString MainWindow::stationCall() const {
  if (auto* edit = findChild<QLineEdit*>(QStringLiteral("stationCall")))
    return edit->text().trimmed();
  return QSettings().value(QStringLiteral("dx/reportCall")).toString().trimmed();
}

QString MainWindow::stationGrid() const {
  if (auto* edit = findChild<QLineEdit*>(QStringLiteral("stationGrid")))
    return edit->text().trimmed();
  return QSettings().value(QStringLiteral("dx/reportGrid")).toString().trimmed();
}

QWidget* MainWindow::makeReceivePanel() {
  auto* panel = new QWidget(this);
  panel->setObjectName("textPanel");
  panel->setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  addStationFields(panel, layout, false);

  auto* tools = new QHBoxLayout();
  tools->setSpacing(6);
  auto* filter = new QLineEdit(panel);
  filter->setObjectName("decodeFilter");
  filter->setPlaceholderText(QStringLiteral("Filter"));
  filter->setClearButtonEnabled(true);
  filter->setFont(interfaceFont(12));
  filter->setToolTip(QStringLiteral("Show decode lines that contain this text."));
  auto* clear = new QPushButton(QStringLiteral("CLR MSG"), panel);
  clear->setObjectName("decodeClear");
  clear->setFont(interfaceFont(12));
  clear->setFocusPolicy(Qt::NoFocus);
  clear->setToolTip(QStringLiteral("Clear the decode list. This does not key."));
  tools->addWidget(filter, 1);
  tools->addWidget(clear);
  layout->addLayout(tools);

  auto* header = new QLabel(decodeColumns(QStringLiteral("Time"), QStringLiteral("dB"),
                                           QStringLiteral("DT"), QStringLiteral("Dist"),
                                           QStringLiteral("Message"), QStringLiteral("Freq"), true, true,
                                           true),
                             panel);
  header->setObjectName("decodeHeader");
  header->setFont(monoFont(13));
  header->setTextInteractionFlags(Qt::NoTextInteraction);
  layout->addWidget(header);

  auto* raster = new HellRaster(panel);
  raster->hide();
  layout->addWidget(raster, 1);

  auto* edit = new QPlainTextEdit(panel);
  edit->setObjectName("receiveText");
  edit->setPlaceholderText(
      QStringLiteral("Received text will appear here.\nNo radio is connected, so nothing is being decoded."));
  edit->setReadOnly(true);
  edit->setFont(monoFont(13));
  edit->setTabChangesFocus(true);
  edit->setMinimumHeight(72);
  edit->setLineWrapMode(QPlainTextEdit::NoWrap);
  edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  edit->document()->setDocumentMargin(0);
  edit->viewport()->installEventFilter(this);
  layout->addWidget(edit, 1);

  connect(filter, &QLineEdit::textChanged, this, [this](const QString&) { refreshDecodes(); });
  connect(clear, &QPushButton::clicked, this, [this] {
    decodes_.clear();
    refreshDecodes();
    if (auto* tape = findChild<HellRaster*>(QStringLiteral("hellReceive")))
      tape->clear();
  });
  return panel;
}

void MainWindow::refreshDecodes() {
  auto* edit = findChild<QPlainTextEdit*>("receiveText");
  auto* filter = findChild<QLineEdit*>("decodeFilter");
  if (!edit)
    return;
  const QString needle = filter ? filter->text().trimmed() : QString();
  QString text;
  shownDecodes_.clear();
  for (const DecodeRow& row : decodes_) {
    QString line;
    if (row.time.isEmpty())
      line = row.message;
    else
      line = decodeColumns(row.time, QString::number(row.db), row.dt, distanceText(row.message),
                           row.message, QString::number(row.freqHz), showTime_, showFreq_, showDist_);
    if (!needle.isEmpty() && !line.contains(needle, Qt::CaseInsensitive))
      continue;
    if (!keepDecode(row.message))
      continue;
    if (!text.isEmpty())
      text += QLatin1Char('\n');
    text += line;
    shownDecodes_.push_back(row);
  }
  edit->setPlainText(text);
  edit->moveCursor(QTextCursor::End);
}

QWidget* MainWindow::makeMacroRow() {
  auto* row = new QWidget(this);
  row->setObjectName("macroRow");
  row->setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(6);

  for (int i = 1; i <= 8; ++i) {
    auto* button = new QPushButton(QStringLiteral("M%1").arg(i), row);
    button->setObjectName("macro");
    button->setFont(monoFont(12));
    button->setProperty("slot", i);
    button->setEnabled(false);
    button->setToolTip("Empty macro slot. Does not key.");
    button->setAccessibleName(QStringLiteral("Macro %1, empty").arg(i));
    connect(button, &QPushButton::clicked, this, [this, i] {
      QSettings settings;
      const QString text = settings.value(QStringLiteral("dx/macro/%1").arg(i)).toString();
      auto* transmit = findChild<QPlainTextEdit*>("transmitText");
      if (transmit && !text.trimmed().isEmpty())
        transmit->setPlainText(text);
    });
    layout->addWidget(button);
  }

  auto* empty = new QLabel("No macros loaded", row);
  empty->setObjectName("macroEmpty");
  empty->setFont(interfaceFont(12));
  layout->addWidget(empty);
  layout->addStretch(1);
  reloadMacros();
  return row;
}

void MainWindow::buildMenus() {
  auto* fileMenu = menuBar()->addMenu("File");
  auto* quit = fileMenu->addAction("Quit");
  quit->setShortcut(QKeySequence::Quit);
  connect(quit, &QAction::triggered, this, &QWidget::close);

  auto* radioMenu = menuBar()->addMenu("Radio");
  auto* connectRadio = radioMenu->addAction("Connect…");
  connectRadio->setObjectName(QStringLiteral("radioConnect"));
  connectRadio->setToolTip(QStringLiteral(
      "Open the TCI host and port. Connect sends AUDIO_START only. Does not send START and does not key."));
  connect(connectRadio, &QAction::triggered, this, [this] { openRadioDialog(); });
  radioMenu->addSeparator();

  QSettings settings;
  autoMonitor_ = settings.value(QStringLiteral("tci/autoMonitor"), true).toBool();
  autoConnectAction_ = radioMenu->addAction(QStringLiteral("Auto-connect"));
  autoConnectAction_->setObjectName(QStringLiteral("tciAutoConnect"));
  autoConnectAction_->setCheckable(true);
  autoConnectAction_->setChecked(settings.value(QStringLiteral("tci/autoConnect"), true).toBool());
  autoConnectAction_->setToolTip(QStringLiteral(
      "On launch, open the last TCI host and port. Sends AUDIO_START only. Does not send START and does not key."));
  connect(autoConnectAction_, &QAction::toggled, this, [](bool on) {
    QSettings store;
    store.setValue(QStringLiteral("tci/autoConnect"), on);
  });

  autoMonitorAction_ = radioMenu->addAction(QStringLiteral("Auto monitor"));
  autoMonitorAction_->setObjectName(QStringLiteral("tciAutoMonitor"));
  autoMonitorAction_->setCheckable(true);
  autoMonitorAction_->setChecked(autoMonitor_);
  autoMonitorAction_->setToolTip(QStringLiteral(
      "Stay on receive and decode. Does not transmit."));
  connect(autoMonitorAction_, &QAction::toggled, this, [this](bool on) {
    autoMonitor_ = on;
    QSettings store;
    store.setValue(QStringLiteral("tci/autoMonitor"), on);
    applyMonitorLabel();
  });

  QSettings dxSettings;
  auto* optionsMenu = menuBar()->addMenu("Options");
  auto* decodeMenu = optionsMenu->addMenu("Decode");
  auto saveBool = [this](const char* key, bool on) {
    QSettings store;
    store.setValue(QLatin1String(key), on);
    applyDxOptions();
  };
  auto addCheck = [&](QMenu* menu, const char* name, const QString& label, const char* key, bool fallback,
                      const QString& tip) {
    auto* action = menu->addAction(label);
    action->setObjectName(QLatin1String(name));
    action->setCheckable(true);
    action->setChecked(dxSettings.value(QLatin1String(key), fallback).toBool());
    action->setToolTip(tip);
    connect(action, &QAction::toggled, this, [saveBool, key](bool on) { saveBool(key, on); });
    return action;
  };

  addCheck(decodeMenu, "decodeThreeStage", QStringLiteral("Use Three-stage Decoding FT8"), "dx/threeStage",
           true,
           QStringLiteral(
               "Decode at three points in the FT8 period. The first pass is before the next period. "
               "FT4 and FT2 stay one pass. Does not key."));
  auto* depthGroup = new QActionGroup(this);
  depthGroup->setExclusive(true);
  const int savedDepth = dxSettings.value(QStringLiteral("dx/depth"), 1).toInt();
  const struct {
    const char* name;
    const char* label;
    int depth;
  } depths[] = {{"decodeDepthFast", "Fast", 1},
                {"decodeDepthNormal", "Normal", 2},
                {"decodeDepthDeep", "Deep", 3}};
  for (const auto& row : depths) {
    auto* action = decodeMenu->addAction(QString::fromLatin1(row.label));
    action->setObjectName(QLatin1String(row.name));
    action->setCheckable(true);
    action->setChecked(savedDepth == row.depth);
    action->setData(row.depth);
    action->setToolTip(QStringLiteral("FT8 decoder depth. Deep is slower. Does not key."));
    depthGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, depth = row.depth] {
      QSettings store;
      store.setValue(QStringLiteral("dx/depth"), depth);
      applyDxOptions();
    });
  }
  addCheck(decodeMenu, "decodeAp", QStringLiteral("Enable AP"), "dx/ap", true,
           QStringLiteral("A-priori FT8 decode. Does not key."));
  auto* maxMenu = decodeMenu->addMenu(QStringLiteral("Max decoded signals"));
  auto* maxGroup = new QActionGroup(this);
  maxGroup->setExclusive(true);
  const int savedMax = dxSettings.value(QStringLiteral("dx/maxSignals"), 0).toInt();
  const struct {
    const char* label;
    int count;
  } maxima[] = {{"Unlimited", 0}, {"4", 4}, {"8", 8}, {"16", 16}, {"32", 32}};
  for (const auto& row : maxima) {
    auto* action = maxMenu->addAction(QString::fromLatin1(row.label));
    action->setCheckable(true);
    action->setChecked(savedMax == row.count);
    action->setData(row.count);
    action->setToolTip(QStringLiteral("Keep the strongest lines in a period. Does not key."));
    maxGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, count = row.count] {
      QSettings store;
      store.setValue(QStringLiteral("dx/maxSignals"), count);
      applyDxOptions();
    });
  }

  auto* timeSync = optionsMenu->addAction(QStringLiteral("Time Synchronization"));
  timeSync->setObjectName(QStringLiteral("timeSync"));
  timeSync->setToolTip(QStringLiteral("Calibrate the period clock. Does not key."));
  connect(timeSync, &QAction::triggered, this, [this] { openTimeDialog(); });

  auto* macrosEdit = optionsMenu->addAction(QStringLiteral("Macros"));
  macrosEdit->setObjectName(QStringLiteral("macrosEdit"));
  macrosEdit->setToolTip(QStringLiteral("Edit the eight local macros. They do not key."));
  connect(macrosEdit, &QAction::triggered, this, [this] { openMacrosDialog(); });

  auto* networkConfig = optionsMenu->addAction(QStringLiteral("Network Configuration"));
  networkConfig->setObjectName(QStringLiteral("networkConfig"));
  networkConfig->setToolTip(QStringLiteral("PSK Reporter style settings. Spots are not sent. Does not key."));
  connect(networkConfig, &QAction::triggered, this, [this] { openNetworkDialog(); });

  auto* textMenu = optionsMenu->addMenu(QStringLiteral("RX / TX Text"));
  textMenu->setObjectName(QStringLiteral("textStyleMenu"));
  auto* fontMenu = textMenu->addMenu(QStringLiteral("Font"));
  auto* sizeMenu = textMenu->addMenu(QStringLiteral("Size"));
  auto* fontGroup = new QActionGroup(this);
  fontGroup->setExclusive(true);
  auto* sizeGroup = new QActionGroup(this);
  sizeGroup->setExclusive(true);
  const QString savedFont =
      dxSettings.value(QStringLiteral("ui/textFont"), QStringLiteral("Consolas")).toString();
  const QString savedSize =
      dxSettings.value(QStringLiteral("ui/textSize"), QStringLiteral("medium")).toString();
  const struct {
    const char* name;
    const char* label;
  } fonts[] = {{"textFontConsolas", "Consolas"},
               {"textFontCascadiaMono", "Cascadia Mono"},
               {"textFontLucidaConsole", "Lucida Console"},
               {"textFontCourierNew", "Courier New"},
               {"textFontSegoeUI", "Segoe UI"}};
  bool fontChecked = false;
  for (const auto& row : fonts) {
    auto* action = fontMenu->addAction(QString::fromLatin1(row.label));
    action->setObjectName(QLatin1String(row.name));
    action->setCheckable(true);
    const bool on = savedFont == QLatin1String(row.label);
    action->setChecked(on);
    fontChecked = fontChecked || on;
    action->setToolTip(QStringLiteral(
        "Font for the receive list, its titles, and the transmit box. This does not key."));
    fontGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, family = QString::fromLatin1(row.label)] {
      QSettings store;
      store.setValue(QStringLiteral("ui/textFont"), family);
      applyTextStyle();
    });
  }
  if (!fontChecked && !fontMenu->actions().isEmpty())
    fontMenu->actions().at(0)->setChecked(true);
  const struct {
    const char* name;
    const char* label;
    const char* key;
  } textSizes[] = {{"textSizeSmall", "Small", "small"},
                   {"textSizeMedium", "Medium", "medium"},
                   {"textSizeLarge", "Large", "large"}};
  bool sizeChecked = false;
  for (const auto& row : textSizes) {
    auto* action = sizeMenu->addAction(QString::fromLatin1(row.label));
    action->setObjectName(QLatin1String(row.name));
    action->setCheckable(true);
    const bool on = savedSize == QLatin1String(row.key);
    action->setChecked(on);
    sizeChecked = sizeChecked || on;
    action->setToolTip(QStringLiteral(
        "Size for the receive list, its titles, and the transmit box. This does not key."));
    sizeGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, key = QString::fromLatin1(row.key)] {
      QSettings store;
      store.setValue(QStringLiteral("ui/textSize"), key);
      applyTextStyle();
    });
  }
  if (!sizeChecked) {
    for (QAction* action : sizeMenu->actions()) {
      if (action->objectName() == QLatin1String("textSizeMedium"))
        action->setChecked(true);
    }
  }

  auto* listMenu = optionsMenu->addMenu(QStringLiteral("Decode List Options"));
  addCheck(listMenu, "listShowTime", QStringLiteral("Show Time Column"), "dx/showTime", true,
           QStringLiteral("Show or hide the Time column."));
  addCheck(listMenu, "listShowDist", QStringLiteral("Show Distance Column"), "dx/showDist", true,
           QStringLiteral("Distance from your grid to the grid in the message. This does not key."));
  auto* miles = listMenu->addAction(QStringLiteral("Distance in miles"));
  miles->setObjectName(QStringLiteral("distUnitMiles"));
  miles->setCheckable(true);
  auto* km = listMenu->addAction(QStringLiteral("Distance in kilometers"));
  km->setObjectName(QStringLiteral("distUnitKm"));
  km->setCheckable(true);
  auto* units = new QActionGroup(this);
  units->addAction(miles);
  units->addAction(km);
  const bool savedMiles = dxSettings.value(QStringLiteral("dx/distMiles"), true).toBool();
  miles->setChecked(savedMiles);
  km->setChecked(!savedMiles);
  miles->setToolTip(QStringLiteral("DIST is miles. This does not key."));
  km->setToolTip(QStringLiteral("DIST is kilometers. This does not key."));
  connect(miles, &QAction::toggled, this, [this](bool on) {
    if (!on)
      return;
    QSettings store;
    store.setValue(QStringLiteral("dx/distMiles"), true);
    applyDxOptions();
  });
  connect(km, &QAction::toggled, this, [this](bool on) {
    if (!on)
      return;
    QSettings store;
    store.setValue(QStringLiteral("dx/distMiles"), false);
    applyDxOptions();
  });
  auto* filters = optionsMenu->addAction(QStringLiteral("Decode List Filters…"));
  filters->setObjectName(QStringLiteral("decodeFilterDialog"));
  filters->setToolTip(QStringLiteral("Choose which decode lines stay on the list. This does not key."));
  connect(filters, &QAction::triggered, this, [this] { openDecodeFilters(); });
  addCheck(listMenu, "listShowFreq", QStringLiteral("Show Frequency Column"), "dx/showFreq", true,
           QStringLiteral("Show or hide the Freq column."));
  addCheck(listMenu, "listClearPeriod", QStringLiteral("New period clears the message list"),
           "dx/clearEachPeriod", false,
           QStringLiteral("Drop the previous period when the next one decodes."));

  auto* logMenu = optionsMenu->addMenu(QStringLiteral("Log Options"));
  const QString logTip = QStringLiteral("Saved. Transmit is off, so nothing is logged and the radio is not keyed.");
  addCheck(logMenu, "logAuto", QStringLiteral("Log Automatically QSO"), "dx/logAuto", false, logTip);
  addCheck(logMenu, "logPrompt", QStringLiteral("Prompt Me To Log QSO"), "dx/logPrompt", false, logTip);
  addCheck(logMenu, "logWarn", QStringLiteral("Warn Me If QSO Before"), "dx/logWarn", false, logTip);
  addCheck(logMenu, "logStartEqualsEnd", QStringLiteral("Log QSO Start Time = End Time"), "dx/logStartEqualsEnd",
           false, logTip);
  addCheck(logMenu, "logNoComment", QStringLiteral("Turn Auto Comments Off"), "dx/logNoComment", false, logTip);

  optionsMenu->addSeparator();
  const QString txTip =
      QStringLiteral("Saved. Transmit is off, so this does not reply and does not key.");
  addCheck(optionsMenu, "aseqMostDistant", QStringLiteral("ASeq: Reply to the Most Distant"), "dx/aseq", false,
           txTip);
  addCheck(optionsMenu, "txConfirm73", QStringLiteral("TX Confirmation If 73 Or RR73"), "dx/txConfirm73", true,
           txTip);
  addCheck(optionsMenu, "autoResetQso", QStringLiteral("Auto RESET QSO at end"), "dx/autoReset", true, txTip);

  auto* modeMenu = menuBar()->addMenu("Mode");
  modeMenu->setObjectName("modeMenu");
  auto* modes = new QActionGroup(this);
  // Exclusive groups swallow a second click on the checked mode. None still
  // shows one check, and every click can cycle that mode's dials.
  modes->setExclusionPolicy(QActionGroup::ExclusionPolicy::None);
  const struct {
    const char* label;
    const char* objectName;
    bool mshv;
    bool checked;
  } modeRows[] = {{"FT8", "modeFt8", true, true},
                  {"FT4", "modeFt4", true, false},
                  {"FT2", "modeFt2", true, false},
                  {"Hell", "modeHell", true, false},
                  {"RTTY", nullptr, false, false},
                  {"PSK31", nullptr, false, false}};
  for (const auto& row : modeRows) {
    auto* action = modeMenu->addAction(row.label);
    action->setCheckable(true);
    action->setChecked(row.checked);
    if (row.objectName)
      action->setObjectName(QString::fromLatin1(row.objectName));
    modes->addAction(action);
    const QString modeName = QString::fromLatin1(row.label);
    connect(action, &QAction::triggered, this, [this, modeName] { activateDigitalMode(modeName); });
  }
  if (statusStrip_) {
    if (auto* chip = statusStrip_->findChild<QPushButton*>(QStringLiteral("digitalModeChip")))
      connect(chip, &QPushButton::clicked, this, [this] { cycleDigitalMode(); });
  }

  auto* viewMenu = menuBar()->addMenu("View");
  const struct {
    QDockWidget* dock;
    const char* label;
  } panels[] = {
      {waterfallDock_, "Waterfall"},
      {receiveDock_, "Receive"},
      {transmitDock_, "Transmit"},
      {macroDock_, "Macros"},
      {statusDock_, "Status strip"},
  };
  for (const auto& panel : panels) {
    QAction* toggle = panel.dock->toggleViewAction();
    toggle->setText(panel.label);
    viewMenu->addAction(toggle);
  }
  viewMenu->addSeparator();
  auto* saveLayout = viewMenu->addAction(QStringLiteral("Save layout"));
  saveLayout->setObjectName(QStringLiteral("saveLayout"));
  saveLayout->setToolTip(QStringLiteral(
      "Save this dock arrangement, contrast, and brightness for the current mode. "
      "They are restored the next time that mode opens. Does not key."));
  connect(saveLayout, &QAction::triggered, this, [this] { saveModeLayout(true); });
  auto* reset = viewMenu->addAction("Reset layout");
  connect(reset, &QAction::triggered, this, [this] {
    QSettings store;
    store.remove(QStringLiteral("layout/%1/state").arg(layoutMode_));
    store.remove(QStringLiteral("layout/%1/geometry").arg(layoutMode_));
    layoutGuard_ = true;
    arrangeDefault();
    resizeToHero();
    layoutGuard_ = false;
    captureLayoutBaseline();
  });

  auto* helpMenu = menuBar()->addMenu("Help");
  auto* guide = helpMenu->addAction("Guide");
  guide->setObjectName("helpGuide");
  connect(guide, &QAction::triggered, this, [this] { showGuide(); });
  auto* about = helpMenu->addAction("About STD");
  connect(about, &QAction::triggered, this, [this] { showAbout(); });
  applyTextStyle();
}

void MainWindow::activateDigitalMode(const QString& modeName) {
  if (auto* menu = findChild<QMenu*>(QStringLiteral("modeMenu"))) {
    for (QAction* action : menu->actions()) {
      if (!action->isCheckable())
        continue;
      const bool on = action->text() == modeName;
      if (action->isChecked() == on)
        continue;
      const QSignalBlocker block(action);
      action->setChecked(on);
    }
  }
  const bool hell = modeName == QLatin1String("Hell");
  const bool mshv = modeName == QLatin1String("FT8") || modeName == QLatin1String("FT4") ||
                    modeName == QLatin1String("FT2") || hell;
  if (modeName != layoutMode_) {
    offerLayoutSave();
    layoutMode_ = modeName;
    if (!restoreModeLayout()) {
      layoutGuard_ = true;
      arrangeDefault();
      resizeToHero();
      layoutGuard_ = false;
    }
    applyModeTone();
    captureLayoutBaseline();
  }
  if (profile_) {
    if (mshv)
      profile_->setDigitalMode(modeName);
    profile_->setMshvFamily(mshv);
  }
  if (statusStrip_)
    statusStrip_->setDigitalMode(modeName);
  showHellReceive(hell);
  if (hell && waterfall_)
    waterfall_->setRxHz(1500);
  if (hell_)
    hell_->setArmed(autoMonitor_ && hell);
  applyChannelWidth();
  if (mshv)
    tuneActiveBand();
}

void MainWindow::showHellReceive(bool on) {
  if (auto* raster = findChild<HellRaster*>(QStringLiteral("hellReceive"))) {
    if (on)
      raster->clear();
    raster->setVisible(on);
  }
  if (auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("receiveText")))
    edit->setVisible(!on);
  if (auto* header = findChild<QLabel*>(QStringLiteral("decodeHeader")))
    header->setVisible(!on);
  if (auto* filter = findChild<QLineEdit*>(QStringLiteral("decodeFilter")))
    filter->setVisible(!on);
  if (auto* clear = findChild<QPushButton*>(QStringLiteral("decodeClear")))
    clear->setVisible(!on);
}

void MainWindow::cycleDigitalMode() {
  QStringList names;
  if (auto* menu = findChild<QMenu*>(QStringLiteral("modeMenu"))) {
    for (QAction* action : menu->actions()) {
      if (action->isCheckable())
        names.append(action->text());
    }
  }
  if (names.isEmpty())
    return;
  int index = names.indexOf(layoutMode_);
  if (index < 0)
    index = 0;
  else
    index = (index + 1) % names.size();
  activateDigitalMode(names.at(index));
}

void MainWindow::tuneActiveBand() {
  if (!tci_ || !profile_ || !profile_->mshvFamily())
    return;
  // FT8 and FT4. DIGU on every band. This is not START and it does not key.
  tci_->setSideband(QStringLiteral("DIGU"));
  tci_->setDialHz(profile_->dialHz());
}

void MainWindow::showGuide() {
  const struct {
    const char* title;
    const char* body;
  } sections[] = {
      {"Connect",
       "Radio → Connect opens one TCI socket. It sends AUDIO_START for receiver 0 and nothing else. "
       "It does not send START, and it does not key. Auto-connect opens the last host and port when "
       "STD starts. Auto monitor stays on receive and decode and does not transmit."},
      {"Waterfall zoom",
       "The wheel zooms around the cursor. Drag pans. Both stay inside the receive band the radio "
       "reports. A double-click or a right-click opens that whole band again. Start and BW narrow "
       "the same way, and they cannot cross the reported edges. Ticks on the bottom bar sets the "
       "ruler marks to 100, 250, or 500 Hz."},
      {"Contrast and brightness",
       "The two sliders on the bottom bar recolor the trace. Each mode keeps its own contrast and "
       "brightness with that mode's layout. Switching back to the mode restores them. They do not "
       "change the frequency span."},
      {"Palettes",
       "Palette on that same bar lists Classic, Inferno, Viridis, Plasma, Rainbow, Ocean, Night, "
       "Grayscale, and Amber."},
      {"Mode profile",
       "Mode → FT8, FT4, or FT2 shows Speed, Start, BW, Auto Dec, Region, and the band buttons. RTTY and PSK31 "
       "hide that bar. Region Auto reads the callsign. N8SDR/VE uses VE, and the call is left as typed. "
       "Region 1, 2, or 3 overrides that. The dials are the WSJT-X list. 40 m FT4 is 7.0475 MHz in every "
       "region. 80 m FT4 is 3.568 MHz in Region 3 and 3.575 MHz otherwise. FT2 uses the MSHV dials. Type a dial in Extra and press "
       "Enter to insert it for this mode and band. Right-click Extra clears that list. Clicking the mode "
       "again, or the same band button, cycles the WSJT-X default and those extras. Choosing FT8, FT4, or a band "
       "button sends vfo:0,0,<Hz> and modulation:0,digu. "
       "FT8, FT4, FT2, and Hell are DIGU on every band. Neither "
       "command keys the radio, and neither is START."},
      {"Feld Hell",
       "Mode → Hell receives Feld Hell from the Fldigi 4.2.06 column detector. The Receive pane "
       "paints the tape. It does not turn the picture into text, and it does not transmit. The tone "
       "starts at 1500 Hz. Click the waterfall to move it. The dial is the Feld Hell Club signal "
       "minus 1500 Hz, so 20 m is 14.0615 MHz for the 14.063 MHz calling frequency. 30 m Region 1 "
       "uses 10.144 MHz. The band button sends vfo:0,0,<Hz> and modulation:0,digu. That is not START "
       "and it does not key."},
      {"Fox/Hound",
       "Super Hound or Super Fox, on the mode bar, sets 40 m to 7.056 MHz DIGU and moves the RX marker "
       "to 750 Hz. That is the fox. It is still 15 second FT8. TX FIRST and TX SECOND only remember a "
       "slot. Multi turns on MSHV multi-answer so the decoder keeps a-priori state for more than one "
       "caller. Transmit stays off. None of these key the radio, and none send START."},
      {"FT8 and FT4 decode",
       "Auto Dec is on by default. The MSHV FT8 decoder starts on the period that is still running, "
       "about 11.8 seconds in, before the tones end, then again at 13.6 and 14.6 seconds when three-stage "
       "is on. FT4 is one pass at 6.1 seconds of its 7.5 second period. FT2 is one pass at 3.4 seconds of "
       "its 3.75 second period. MSHV does not run that three-stage "
       "timer for FT4. Each line is painted as soon as the decoder finds it. The Receive pane has column "
       "headers Time, dB, DT, Dist, Message, and Freq. Dist is miles or kilometers from your grid. "
       "Filter keeps lines that contain the text. CLR MSG clears "
       "the list. Freq is the audio frequency in Hz. The band buttons use the WSJT-X "
       "dials and send modulation:0,digu on every band. Decoding does not send START and it does not key."},
      {"DX options",
       "Options → Decode sets three-stage FT8, Fast/Normal/Deep, AP, and a cap on lines. Time "
       "Synchronization calibrates the period clock. Macros edit eight local texts. Network "
       "Configuration stores a PSK Reporter style address and does not send spots. Decode List "
       "Options show or hide Time, Dist, and Freq, and can clear the list each period. Log options, ASeq, "
       "TX confirmation, and Auto RESET QSO are saved. Those options do not key."},
      {"Callsign and locator",
       "Callsign and Locator are on the receive list and on the transmit box. They are saved. "
       "DIST is the distance from your locator to the grid in the message. A click on a decode "
       "builds the reply from your callsign and locator. Typing them does not key."},
      {"Text",
       "Options → RX / TX Text sets the font and the size of the receive list, its column titles, "
       "and the transmit box. The faces are Consolas, Cascadia Mono, Lucida Console, Courier New, "
       "and Segoe UI. The sizes are small, medium, and large. The choice is remembered. It does not key."},
      {"TX",
       "Click or double-click a decode to load an MSHV-style reply and move the TX marker to that "
       "station. That click does not key. Tune sends a tone at the TX marker and holds TRX until you "
       "release it, so you can set TX gain. TX sends the text in the box. FT8 and FT4 wait for the "
       "next slot. Hell and Tune start at once. TRX goes out only after Tune or TX is pressed. "
       "START is never sent. An empty box does not key. FT2, RTTY, and PSK31 do not key."},
      {"RX and TX markers",
       "A green band is the RX channel and a red band is the TX channel. Each starts at the marker "
       "and runs up by the protocol width: 50 Hz on FT8, 90 Hz on FT4, 125 Hz on FT2. Super Hound "
       "widens the green band to the fox waveform, and Super Fox widens the red band. The TX-gain "
       "notice stays off on receive. It can show only while STD is transmitting. The gain spin "
       "does not key. "
       "Click the waterfall to set RX. Shift+click sets TX. Ctrl+click sets both. Lock moves them "
       "together. RX=TX copies RX onto TX. None of these key the radio."},
      {"Layout",
       "View → Save layout stores the dock arrangement for the mode that is selected. The next time "
       "that mode opens, including after a restart, those panels come back. Leaving a mode, or "
       "closing STD, asks to save only when the docks differ from that mode's saved layout. "
       "View → Reset layout stacks the current mode again."},
      {"Gain",
       "RX and TX on the status strip are local gains, from -20 dB to +40 dB. RX gain is applied to "
       "the waterfall and the FT8 decoder so the peak can be brought toward -1 to 0 dB. TX gain is "
       "kept for a later transmit path. Neither value is sent to the radio, and neither keys."},
  };

  QString html;
  for (const auto& section : sections) {
    html += QStringLiteral("<h3>%1</h3><p>%2</p>")
                .arg(QString::fromLatin1(section.title), QString::fromLatin1(section.body));
  }

  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("STD guide"));
  dialog.setObjectName(QStringLiteral("helpGuideDialog"));
  dialog.resize(520, 420);
  auto* text = new QTextBrowser(&dialog);
  text->setOpenExternalLinks(false);
  text->setHtml(html);
  auto* close = new QPushButton(QStringLiteral("Close"), &dialog);
  connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(text);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  buttons->addWidget(close);
  layout->addLayout(buttons);
  dialog.exec();
}

void MainWindow::arrangeDefault() {
  const QList<QDockWidget*> docks = {waterfallDock_, receiveDock_, transmitDock_, macroDock_,
                                     statusDock_};
  for (QDockWidget* dock : docks) {
    dock->setFloating(false);
    dock->show();
  }

  addDockWidget(Qt::TopDockWidgetArea, waterfallDock_);
  splitDockWidget(waterfallDock_, receiveDock_, Qt::Vertical);
  splitDockWidget(receiveDock_, transmitDock_, Qt::Vertical);
  splitDockWidget(transmitDock_, macroDock_, Qt::Vertical);
  splitDockWidget(macroDock_, statusDock_, Qt::Vertical);
}

void MainWindow::resizeToHero() {
  const int chrome = menuBar()->height() + (brandBar_ ? brandBar_->height() : 0);
  const int available = std::max(480, height() - chrome);
  const int status = 44;
  const int macros = 80;
  const int transmit = std::max(108, available * 14 / 100);
  const int receive = std::max(128, available * 18 / 100);
  const int waterfall = std::max(240, available - status - macros - transmit - receive);
  resizeDocks({waterfallDock_, receiveDock_, transmitDock_, macroDock_, statusDock_},
              {waterfall, receive, transmit, macros, status}, Qt::Vertical);
}

namespace {
struct DockSignature {
  QByteArray bytes;
  bool parsed = false;
};
DockSignature dockSignature(const QByteArray& state);
}  // namespace

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (!heroSized_) {
    heroSized_ = true;
    // restoreState during this event is ignored for dock sizes. Wait until
    // the window is on screen, then put the saved mode layout back.
    QTimer::singleShot(0, this, [this] {
      if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
        resizeToHero();
        const DockSignature once = dockSignature(saveState(1));
        const DockSignature twice = dockSignature(saveState(1));
        layoutReady_ = true;
        alignDecodeHeader();
        if (!once.parsed || once.bytes.isEmpty() || once.bytes != twice.bytes)
          qFatal("layout signature failed");
        return;
      }
      const bool restored = restoreModeLayout();
      layoutReady_ = true;
      if (!restored)
        resizeToHero();
      captureLayoutBaseline();
      alignDecodeHeader();
    });
  }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  alignDecodeHeader();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (txTune_)
    stopTune(false);
  stopUserTx(false);
  offerLayoutSave();
  QMainWindow::closeEvent(event);
}

namespace {

constexpr quint32 kStateVersionMarker = 0xff;
constexpr uchar kDockWidgetStateMarker = 0xfd;
constexpr uchar kFloatingDockWidgetTabMarker = 0xf9;
constexpr uchar kSequenceMarker = 0xfc;
constexpr uchar kTabMarker = 0xfa;
constexpr uchar kWidgetMarker = 0xfb;
constexpr uchar kToolBarStateMarker = 0xfe;
constexpr uchar kToolBarStateMarkerEx = 0xfc;
constexpr uchar kDockFloating = 0x02;

bool writeDockInfo(QDataStream& in, QDataStream& out) {
  uchar marker = 0;
  in >> marker;
  if (in.status() != QDataStream::Ok)
    return false;
  out << marker;
  if (marker == kTabMarker) {
    qint32 index = 0;
    in >> index;
    out << index;
  } else if (marker != kSequenceMarker) {
    return false;
  }
  uchar orientation = 0;
  qint32 count = 0;
  in >> orientation >> count;
  if (in.status() != QDataStream::Ok || count < 0 || count > 64)
    return false;
  out << orientation << count;
  for (qint32 i = 0; i < count; ++i) {
    uchar next = 0;
    in >> next;
    out << next;
    if (next == kWidgetMarker) {
      QString name;
      uchar flags = 0;
      in >> name >> flags;
      qint32 a = 0;
      qint32 b = 0;
      qint32 c = 0;
      qint32 d = 0;
      in >> a >> b >> c >> d;
      out << name << flags;
      if (flags & kDockFloating) {
        out << a << b << c << d;
      } else {
        // pos and size are the split. Minimum and maximum are live widget
        // constraints and change when a mode panel lays out.
        out << a << b << qint32(0) << qint32(0);
      }
    } else if (next == kSequenceMarker) {
      qint32 pos = 0;
      qint32 size = 0;
      qint32 minimum = 0;
      qint32 maximum = 0;
      in >> pos >> size >> minimum >> maximum;
      out << pos << size << qint32(0) << qint32(0);
      if (!writeDockInfo(in, out))
        return false;
    } else {
      return false;
    }
    if (in.status() != QDataStream::Ok)
      return false;
  }
  return true;
}

bool writeToolBars(QDataStream& in, QDataStream& out, uchar marker) {
  qint32 lines = 0;
  in >> lines;
  if (in.status() != QDataStream::Ok || lines < 0 || lines > 64)
    return false;
  out << lines;
  for (qint32 line = 0; line < lines; ++line) {
    qint32 pos = 0;
    qint32 count = 0;
    in >> pos >> count;
    if (count < 0 || count > 64)
      return false;
    out << pos << count;
    for (qint32 item = 0; item < count; ++item) {
      QString name;
      uchar shown = 0;
      qint32 itemPos = 0;
      qint32 preferred = 0;
      in >> name >> shown >> itemPos >> preferred;
      // The brand bar is not a dock. Its placement is rewritten on layout.
      out << name << shown << qint32(0) << qint32(0);
      if (marker == kToolBarStateMarkerEx) {
        quint32 geom0 = 0;
        quint32 geom1 = 0;
        in >> geom0 >> geom1;
        out << quint32(0) << quint32(0);
      }
      if (in.status() != QDataStream::Ok)
        return false;
    }
  }
  return in.status() == QDataStream::Ok;
}

// saveState() after restoreState() is not the saved blob. Qt rewrites the
// dock area size, the live minimum and maximum, and the toolbar placement.
// Compare the dock split after that rewrite, and drop those fields.
DockSignature dockSignature(const QByteArray& state) {
  DockSignature signature;
  if (state.isEmpty())
    return signature;
  QByteArray copy = state;
  QDataStream in(&copy, QIODevice::ReadOnly);
  in.setVersion(QDataStream::Qt_5_0);
  QDataStream out(&signature.bytes, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_0);

  quint32 versionMarker = 0;
  qint32 version = 0;
  in >> versionMarker >> version;
  if (versionMarker != kStateVersionMarker || in.status() != QDataStream::Ok)
    return signature;
  out << versionMarker << version;

  uchar dockMarker = 0;
  in >> dockMarker;
  if (dockMarker != kDockWidgetStateMarker)
    return signature;
  out << dockMarker;
  qint32 areas = 0;
  in >> areas;
  if (areas < 0 || areas > 4)
    return signature;
  out << areas;
  for (qint32 area = 0; area < areas; ++area) {
    qint32 pos = 0;
    QSize areaSize;
    in >> pos >> areaSize;
    out << pos << QSize(0, 0);
    if (!writeDockInfo(in, out))
      return {};
  }
  QSize central;
  in >> central;
  out << QSize(0, 0);
  for (int corner = 0; corner < 4; ++corner) {
    qint32 value = 0;
    in >> value;
    out << value;
  }
  if (in.status() != QDataStream::Ok)
    return {};

  while (!in.atEnd()) {
    uchar marker = 0;
    in >> marker;
    if (in.status() != QDataStream::Ok)
      return {};
    if (marker == kToolBarStateMarker || marker == kToolBarStateMarkerEx) {
      out << marker;
      if (!writeToolBars(in, out, marker) || !in.atEnd())
        return {};
      signature.parsed = true;
      return signature;
    }
    if (marker != kFloatingDockWidgetTabMarker)
      return {};
    QRect geometry;
    in >> geometry;
    out << marker << geometry;
    if (!writeDockInfo(in, out))
      return {};
  }
  return {};
}

}  // namespace

void MainWindow::offerLayoutSave() {
  if (layoutMode_.isEmpty() || !layoutReady_ || layoutBaselinePending_ ||
      qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  const DockSignature now = dockSignature(saveState(1));
  const QByteArray current = now.parsed ? now.bytes : saveState(1);
  if (current == layoutBaseline_)
    return;
  const auto answer = QMessageBox::question(this, QStringLiteral("STD"),
                                            QStringLiteral("Save layout for this mode?"),
                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (answer == QMessageBox::Yes)
    saveModeLayout(true);
}

void MainWindow::captureLayoutBaseline() {
  // restoreState() applies dock sizes on the following layout pass. Snapshot
  // after that pass so an unchanged mode matches its saved arrangement.
  const int epoch = ++layoutEpoch_;
  layoutBaselinePending_ = true;
  QTimer::singleShot(0, this, [this, epoch] {
    if (epoch != layoutEpoch_)
      return;
    const DockSignature saved = dockSignature(saveState(1));
    layoutBaseline_ = saved.parsed ? saved.bytes : saveState(1);
    layoutBaselinePending_ = false;
  });
}

void MainWindow::rememberModeTone() {
  if (!waterfall_ || layoutMode_.isEmpty() || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  QSettings settings;
  settings.setValue(QStringLiteral("layout/%1/contrast").arg(layoutMode_), waterfall_->contrast());
  settings.setValue(QStringLiteral("layout/%1/brightness").arg(layoutMode_), waterfall_->brightness());
}

void MainWindow::applyModeTone() {
  if (!waterfall_ || layoutMode_.isEmpty())
    return;
  QSettings settings;
  const QString mode = layoutMode_;
  const int contrast = std::clamp(
      settings.value(QStringLiteral("layout/%1/contrast").arg(mode),
                     settings.value(QStringLiteral("waterfall/contrast"), 50))
          .toInt(),
      0, 100);
  const int brightness = std::clamp(
      settings.value(QStringLiteral("layout/%1/brightness").arg(mode),
                     settings.value(QStringLiteral("waterfall/brightness"), 50))
          .toInt(),
      0, 100);
  waterfall_->setTone(contrast, brightness);
}

void MainWindow::saveModeLayout(bool force) {
  if (layoutMode_.isEmpty() || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (!force && (layoutGuard_ || !layoutReady_))
    return;
  QSettings settings;
  settings.setValue(QStringLiteral("layout/%1/state").arg(layoutMode_), saveState(1));
  settings.setValue(QStringLiteral("layout/%1/geometry").arg(layoutMode_), saveGeometry());
  rememberModeTone();
  settings.sync();
  captureLayoutBaseline();
}

bool MainWindow::restoreModeLayout() {
  if (layoutMode_.isEmpty() || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return false;
  QSettings settings;
  const QByteArray state = settings.value(QStringLiteral("layout/%1/state").arg(layoutMode_)).toByteArray();
  if (state.isEmpty())
    return false;
  layoutGuard_ = true;
  const QByteArray geometry =
      settings.value(QStringLiteral("layout/%1/geometry").arg(layoutMode_)).toByteArray();
  if (!geometry.isEmpty())
    restoreGeometry(geometry);
  const bool restored = restoreState(state, 1);
  layoutGuard_ = false;
  return restored;
}

void MainWindow::openRadioDialog() {
  if (tci_)
    openTciConnectDialog(this, tci_);
  syncConnectOptions();
}

void MainWindow::syncConnectOptions() {
  QSettings settings;
  autoMonitor_ = settings.value(QStringLiteral("tci/autoMonitor"), true).toBool();
  const bool connectOn = settings.value(QStringLiteral("tci/autoConnect"), true).toBool();
  if (autoConnectAction_) {
    const QSignalBlocker block(autoConnectAction_);
    autoConnectAction_->setChecked(connectOn);
  }
  if (autoMonitorAction_) {
    const QSignalBlocker block(autoMonitorAction_);
    autoMonitorAction_->setChecked(autoMonitor_);
  }
  applyMonitorLabel();
}

void MainWindow::maybeAutoConnect() {
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (!tci_ || tci_->isOpen())
    return;
  QSettings settings;
  if (!settings.value(QStringLiteral("tci/autoConnect"), true).toBool())
    return;
  if (!settings.contains(QStringLiteral("tci/host")))
    return;
  const QString host = settings.value(QStringLiteral("tci/host")).toString().trimmed();
  const int port = settings.value(QStringLiteral("tci/port"), TciClient::kDefaultPort).toInt();
  if (host.isEmpty() || port < 1 || port > 65535)
    return;
  tci_->connectToHost(host, static_cast<quint16>(port));
}

void MainWindow::applyMonitorLabel() {
  if (!tci_ || !tci_->isOpen() || !statusStrip_)
    return;
  const QString line = autoMonitor_
                           ? QStringLiteral("Monitor %1:%2").arg(tci_->host()).arg(tci_->port())
                           : QStringLiteral("Connected %1:%2").arg(tci_->host()).arg(tci_->port());
  statusStrip_->setRadio(true, line);
}

void MainWindow::applyChannelWidth() {
  if (!waterfall_ || !profile_)
    return;
  const QString mode = profile_->digitalMode();
  int width = 50;
  if (mode == QLatin1String("FT4"))
    width = 90;
  else if (mode == QLatin1String("FT2"))
    width = 125;
  else if (mode == QLatin1String("Hell"))
    width = 245;
  int rx = width;
  int tx = width;
  if (profile_->superHound())
    rx = 1488;
  if (profile_->superFox())
    tx = 1488;
  waterfall_->setChannelWidth(rx, tx);
}

QString MainWindow::distanceText(const QString& message) const {
  const QString home = stationGrid();
  const QString theirs = gridInMessage(message);
  double lat1 = 0.0;
  double lon1 = 0.0;
  double lat2 = 0.0;
  double lon2 = 0.0;
  if (!gridCenter(home, &lat1, &lon1) || !gridCenter(theirs, &lat2, &lon2))
    return QStringLiteral("-");
  const double km = kilometers(lat1, lon1, lat2, lon2);
  const double shown = distMiles_ ? km * 0.621371 : km;
  return QString::number(int(std::lround(shown)));
}

void MainWindow::alignDecodeHeader() {
  if (headerAlignQueued_)
    return;
  auto* header = findChild<QLabel*>(QStringLiteral("decodeHeader"));
  auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("receiveText"));
  if (!header || !edit || !edit->viewport())
    return;
  headerAlignQueued_ = true;
  header->setContentsMargins(0, 0, 0, 0);
  QTextFrameFormat cleared = edit->document()->rootFrame()->frameFormat();
  cleared.setLeftMargin(0);
  cleared.setTopMargin(0);
  cleared.setRightMargin(0);
  cleared.setBottomMargin(0);
  edit->document()->rootFrame()->setFrameFormat(cleared);
  QTimer::singleShot(0, this, [this] {
    headerAlignQueued_ = false;
    auto* header = findChild<QLabel*>(QStringLiteral("decodeHeader"));
    auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("receiveText"));
    if (!header || !edit || !edit->viewport() || !header->parentWidget())
      return;
    const QWidget* parent = header->parentWidget();
    const int textX = edit->viewport()->mapTo(parent, QPoint(0, 0)).x();
    const int headerX = header->mapTo(parent, QPoint(0, 0)).x();
    const int delta = textX - headerX;
    if (delta > 0)
      header->setContentsMargins(delta, 0, 0, 0);
    else if (delta < 0) {
      QTextFrameFormat format = edit->document()->rootFrame()->frameFormat();
      format.setLeftMargin(-delta);
      format.setTopMargin(0);
      format.setRightMargin(0);
      format.setBottomMargin(0);
      edit->document()->rootFrame()->setFrameFormat(format);
    }
  });
}

namespace {

bool isGrid(const QString& word) {
  static const QRegularExpression grid(QStringLiteral("^[A-R]{2}[0-9]{2}$"),
                                        QRegularExpression::CaseInsensitiveOption);
  return grid.match(word).hasMatch();
}

bool isCall(const QString& word) {
  if (isGrid(word))
    return false;
  if (word.compare(QLatin1String("CQ"), Qt::CaseInsensitive) == 0 ||
      word.compare(QLatin1String("DX"), Qt::CaseInsensitive) == 0 ||
      word.compare(QLatin1String("RR73"), Qt::CaseInsensitive) == 0 ||
      word.compare(QLatin1String("RRR"), Qt::CaseInsensitive) == 0 ||
      word.compare(QLatin1String("73"), Qt::CaseInsensitive) == 0)
    return false;
  static const QRegularExpression call(
      QStringLiteral("^[A-Z0-9]{1,3}[0-9][A-Z0-9]{1,4}(?:/[A-Z0-9]{1,4})?$"),
      QRegularExpression::CaseInsensitiveOption);
  return call.match(word).hasMatch();
}

QString snrText(int db) {
  if (db > 49)
    db = 49;
  if (db < -50)
    db = -50;
  if (db >= 0)
    return QStringLiteral("+%1").arg(db, 2, 10, QLatin1Char('0'));
  return QString::number(db);
}

QString replyFor(const QString& message, int db, const QString& mine, const QString& grid) {
  const QStringList words = message.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  if (words.isEmpty())
    return {};
  QString sender;
  const bool cq = words.at(0).compare(QLatin1String("CQ"), Qt::CaseInsensitive) == 0;
  QStringList calls;
  for (const QString& word : words) {
    if (isCall(word))
      calls << word.toUpper();
  }
  if (cq) {
    for (const QString& word : words) {
      if (isCall(word)) {
        sender = word.toUpper();
        break;
      }
    }
  } else if (calls.size() >= 2)
    sender = calls.at(1);
  else if (!calls.isEmpty())
    sender = calls.at(0);
  if (sender.isEmpty())
    return {};

  const QString me = mine.trimmed().toUpper();
  bool toMe = false;
  QString report;
  bool roger = false;
  bool rrr = false;
  bool rr73 = false;
  bool seventy = false;
  for (const QString& word : words) {
    if (!me.isEmpty() && word.compare(me, Qt::CaseInsensitive) == 0)
      toMe = true;
    if (word.compare(QLatin1String("RR73"), Qt::CaseInsensitive) == 0)
      rr73 = true;
    else if (word.compare(QLatin1String("RRR"), Qt::CaseInsensitive) == 0)
      rrr = true;
    else if (word.compare(QLatin1String("73"), Qt::CaseInsensitive) == 0)
      seventy = true;
    else if (word.size() > 1 && word.at(0).toUpper() == QLatin1Char('R') &&
             (word.at(1) == QLatin1Char('+') || word.at(1) == QLatin1Char('-') || word.at(1).isDigit()))
      roger = true;
    else if (word.size() >= 2 && (word.at(0) == QLatin1Char('+') || word.at(0) == QLatin1Char('-')) &&
             word.at(1).isDigit())
      report = word;
  }
  if (me.isEmpty())
    return sender;
  QString tail;
  if (!cq && toMe && (seventy || rr73 || rrr))
    tail = QStringLiteral("73");
  else if (!cq && toMe && roger)
    tail = QStringLiteral("RRR");
  else if (!cq && toMe && !report.isEmpty())
    tail = QStringLiteral("R%1").arg(report);
  else if (!cq && toMe)
    tail = snrText(db);
  else
    tail = grid.trimmed().left(4).toUpper();
  if (tail.isEmpty())
    return QStringLiteral("%1 %2").arg(sender, me);
  return QStringLiteral("%1 %2 %3").arg(sender, me, tail);
}

}  // namespace

void MainWindow::setTxStatus(const QString& text) {
  if (auto* label = findChild<QLabel*>(QStringLiteral("txStatus")))
    label->setText(text);
}

QString MainWindow::transmitMessage() const {
  auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("transmitText"));
  if (!edit)
    return {};
  const QStringList lines = edit->toPlainText().split(QLatin1Char('\n'));
  for (const QString& line : lines) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty())
      return trimmed;
  }
  return {};
}

bool MainWindow::periodReady(const QString& mode) {
  int period = 0;
  if (mode == QLatin1String("FT8"))
    period = 15000;
  else if (mode == QLatin1String("FT4"))
    period = 7500;
  else if (mode == QLatin1String("Hell"))
    return true;
  else
    return false;
  const QTime now = QDateTime::currentDateTimeUtc().time();
  const int ms = ((now.hour() * 60 + now.minute()) * 60 + now.second()) * 1000 + now.msec();
  const int into = ms % period;
  const bool crossed = txPeriodMark_ >= 0 && into < txPeriodMark_;
  txPeriodMark_ = into;
  return crossed || into < 300;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("receiveText"));
  if (!edit || watched != edit->viewport())
    return QMainWindow::eventFilter(watched, event);
  if (event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseButtonDblClick)
    return QMainWindow::eventFilter(watched, event);
  const auto* mouse = static_cast<const QMouseEvent*>(event);
  if (mouse->button() != Qt::LeftButton)
    return QMainWindow::eventFilter(watched, event);
  const QTextCursor cursor = edit->cursorForPosition(mouse->position().toPoint());
  loadReplyFromLine(cursor.blockNumber());
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::loadReplyFromLine(int line) {
  if (line < 0 || line >= shownDecodes_.size())
    return;
  const DecodeRow row = shownDecodes_.at(line);
  const QString mine = stationCall();
  const QString grid = stationGrid();
  const QString reply = replyFor(row.message, row.db, mine, grid);
  auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("transmitText"));
  if (!edit || reply.isEmpty()) {
    setTxStatus(QStringLiteral("That decode has no call to answer. Nothing was keyed."));
    return;
  }
  edit->setPlainText(reply);
  if (waterfall_ && row.freqHz > 0)
    waterfall_->setTxHz(row.freqHz);
  if (mine.trimmed().isEmpty())
    setTxStatus(QStringLiteral(
        "Loaded their call. Type yours in Callsign, then press TX. Nothing was keyed."));
  else
    setTxStatus(QStringLiteral("Loaded %1. Press TX to send it. Nothing was keyed.").arg(reply));
}

void MainWindow::setTune(bool on) {
  if (!on) {
    stopTune(false);
    return;
  }
  auto* button = findChild<QPushButton*>(QStringLiteral("txTune"));
  auto release = [button] {
    if (!button)
      return;
    const QSignalBlocker block(button);
    button->setChecked(false);
    button->setText(QStringLiteral("Tune"));
  };
  if (qEnvironmentVariableIsSet("STD_SELFTEST") || !tci_ || !tci_->isOpen() || !txTimer_) {
    release();
    setTxStatus(QStringLiteral("Radio is not connected. Tune did not key. START was not sent."));
    return;
  }
  stopUserTx(true);
  txTuneHz_ = std::clamp(waterfall_ ? waterfall_->txHz() : 1500, 200, 3000);
  txTunePhase_ = 0.0;
  txHold_ = 2;
  txTune_ = true;
  if (!tci_->setTransmit(true)) {
    txTune_ = false;
    release();
    setTxStatus(QStringLiteral("Radio is not connected. Tune did not key. START was not sent."));
    return;
  }
  txKeySent_ = true;
  if (button)
    button->setText(QStringLiteral("TUNE ON"));
  setTxStatus(QStringLiteral("Tune tone at %1 Hz. TRX is held until Tune is released. START was not sent.")
                  .arg(txTuneHz_));
  txTimer_->start(20);
}

void MainWindow::stopTune(bool uncheck) {
  const bool keyed = txKeySent_;
  txTune_ = false;
  if (txTimer_)
    txTimer_->stop();
  if (keyed && tci_)
    tci_->setTransmit(false);
  txKeySent_ = false;
  if (auto* button = findChild<QPushButton*>(QStringLiteral("txTune"))) {
    if (uncheck) {
      const QSignalBlocker block(button);
      button->setChecked(false);
    }
    button->setText(QStringLiteral("Tune"));
  }
}

void MainWindow::setUserTx(bool on) {
  if (!on) {
    stopUserTx(false);
    return;
  }
  auto* button = findChild<QPushButton*>(QStringLiteral("txSend"));
  auto release = [button] {
    if (!button)
      return;
    const QSignalBlocker block(button);
    button->setChecked(false);
    button->setText(QStringLiteral("TX"));
  };
  if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
    release();
    return;
  }
  if (txTune_)
    stopTune(true);
  if (!tci_ || !tci_->isOpen() || !profile_ || !txTimer_) {
    release();
    setTxStatus(QStringLiteral("Radio is not connected. TX did not key. START was not sent."));
    return;
  }
  const QString mode = profile_->digitalMode();
  const QString text = transmitMessage();
  if (text.isEmpty()) {
    release();
    setTxStatus(QStringLiteral(
        "The box is empty. Click a decode to load a reply, or type one. Nothing was keyed."));
    return;
  }
  const int tone = waterfall_ ? waterfall_->txHz() : 1500;
  if (!buildTxWave(mode, text, tone, &txSamples_) || txSamples_.empty()) {
    release();
    setTxStatus(QStringLiteral(
        "%1 did not build a transmission. FT2, RTTY, and PSK31 do not key. START was not sent.")
                    .arg(mode));
    return;
  }
  txCursor_ = 0;
  txHold_ = 0;
  txKeySent_ = false;
  txPeriodMark_ = -1;
  if (button)
    button->setText(mode == QLatin1String("Hell") ? QStringLiteral("TX") : QStringLiteral("WAIT"));
  if (mode == QLatin1String("Hell"))
    setTxStatus(QStringLiteral("Sending Hell. TRX goes out with the audio. START was not sent."));
  else
    setTxStatus(QStringLiteral("Waiting for the next %1 slot. TRX has not been sent.").arg(mode));
  txTimer_->start(20);
}

void MainWindow::pumpUserTx() {
  if (txTune_) {
    if (!tci_ || !tci_->isTransmitting()) {
      stopTune(true);
      setTxStatus(QStringLiteral("Tune stopped. TRX is off. START was not sent."));
      return;
    }
    if (txHold_ > 0) {
      --txHold_;
      return;
    }
    constexpr int kChunk = 2400;
    std::vector<float> chunk(kChunk);
    const double step = 2.0 * 3.141592653589793 * double(txTuneHz_) / 48000.0;
    for (int i = 0; i < kChunk; ++i) {
      chunk[static_cast<size_t>(i)] = float(0.7 * std::sin(txTunePhase_));
      txTunePhase_ += step;
      if (txTunePhase_ > 6.283185307179586)
        txTunePhase_ -= 6.283185307179586;
    }
    tci_->pushTxAudio(chunk.data(), kChunk, 48000);
    return;
  }
  if (!tci_ || !profile_ || txSamples_.empty()) {
    stopUserTx(true);
    return;
  }
  const QString mode = profile_->digitalMode();
  if (!txKeySent_) {
    if (!periodReady(mode))
      return;
    if (!tci_->setTransmit(true)) {
      setTxStatus(QStringLiteral("Radio is not connected. TX did not key. START was not sent."));
      stopUserTx(true);
      return;
    }
    txKeySent_ = true;
    txHold_ = 2;
    if (auto* button = findChild<QPushButton*>(QStringLiteral("txSend")))
      button->setText(QStringLiteral("ON AIR"));
    setTxStatus(QStringLiteral("On the air. TRX is held until this transmission ends. START was not sent."));
    return;
  }
  if (txHold_ > 0) {
    --txHold_;
    return;
  }
  constexpr int kChunk = 2400;
  if (txCursor_ >= int(txSamples_.size())) {
    setTxStatus(QStringLiteral("That transmission ended. TRX is off. START was not sent."));
    stopUserTx(true);
    return;
  }
  const int count = std::min(kChunk, int(txSamples_.size()) - txCursor_);
  tci_->pushTxAudio(txSamples_.data() + txCursor_, count, 48000);
  txCursor_ += count;
}

void MainWindow::stopUserTx(bool uncheck) {
  if (txTune_)
    return;
  if (txTimer_)
    txTimer_->stop();
  if (txKeySent_ && tci_)
    tci_->setTransmit(false);
  txKeySent_ = false;
  txHold_ = 0;
  txSamples_.clear();
  txCursor_ = 0;
  txPeriodMark_ = -1;
  if (auto* button = findChild<QPushButton*>(QStringLiteral("txSend"))) {
    if (uncheck) {
      const QSignalBlocker block(button);
      button->setChecked(false);
    }
    button->setText(QStringLiteral("TX"));
  }
}

bool MainWindow::keepDecode(const QString& message) const {
  if (!filtersOn_)
    return true;
  const QString mine = stationCall();
  if (!mine.isEmpty() && message.contains(mine, Qt::CaseInsensitive))
    return true;
  const auto token = [&](const char* word) {
    return message.contains(QRegularExpression(QStringLiteral("\\b%1\\b").arg(QLatin1String(word)),
                                               QRegularExpression::CaseInsensitiveOption));
  };
  if (filterCq_ && !token("CQ"))
    return false;
  if (filterCq73_ && !(token("CQ") || token("73") || token("RR73") || token("RRR")))
    return false;
  if (!filterText_.trimmed().isEmpty() && !message.contains(filterText_.trimmed(), Qt::CaseInsensitive))
    return false;
  bool hiding = false;
  for (bool on : hideContinent_)
    hiding = hiding || on;
  if (hiding) {
    const int continent = continentOf(otherCall(message, mine));
    if (continent >= 0 && continent < 7 && hideContinent_[continent])
      return false;
  }
  return true;
}

void MainWindow::openDecodeFilters() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Decode filters"));
  dialog.setObjectName(QStringLiteral("decodeFilterDialogBox"));
  auto* form = new QVBoxLayout(&dialog);
  auto* note = new QLabel(QStringLiteral(
      "Lines for your own call stay. Nothing here transmits. This does not key."),
                           &dialog);
  note->setWordWrap(true);
  form->addWidget(note);
  auto* cq = new QCheckBox(QStringLiteral("Show CQ only"), &dialog);
  auto* cq73 = new QCheckBox(QStringLiteral("Show CQ, RR73, 73, and RRR only"), &dialog);
  auto* text = new QLineEdit(filterText_, &dialog);
  text->setPlaceholderText(QStringLiteral("Show lines containing"));
  cq->setChecked(filterCq_);
  cq73->setChecked(filterCq73_);
  form->addWidget(cq);
  form->addWidget(cq73);
  form->addWidget(text);
  const char* names[] = {"Africa", "Antarctica", "Asia", "Europe", "Oceania", "North America", "South America"};
  QCheckBox* boxes[7];
  for (int i = 0; i < 7; ++i) {
    boxes[i] = new QCheckBox(QStringLiteral("Hide %1").arg(QLatin1String(names[i])), &dialog);
    boxes[i]->setChecked(hideContinent_[i]);
    form->addWidget(boxes[i]);
  }
  auto* buttons = new QHBoxLayout;
  auto* cancel = new QPushButton(QStringLiteral("Cancel"), &dialog);
  auto* save = new QPushButton(QStringLiteral("Apply"), &dialog);
  save->setDefault(true);
  buttons->addStretch(1);
  buttons->addWidget(cancel);
  buttons->addWidget(save);
  form->addLayout(buttons);
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
  if (dialog.exec() != QDialog::Accepted)
    return;
  filterCq_ = cq->isChecked();
  filterCq73_ = cq73->isChecked();
  filterText_ = text->text();
  QString hide;
  for (int i = 0; i < 7; ++i) {
    hideContinent_[i] = boxes[i]->isChecked();
    hide += hideContinent_[i] ? QLatin1Char('1') : QLatin1Char('0');
  }
  QSettings settings;
  settings.setValue(QStringLiteral("dx/filterCq"), filterCq_);
  settings.setValue(QStringLiteral("dx/filterCq73"), filterCq73_);
  settings.setValue(QStringLiteral("dx/filterText"), filterText_);
  settings.setValue(QStringLiteral("dx/hideContinent"), hide);
  refreshDecodes();
}

void MainWindow::applyTextStyle() {
  QSettings settings;
  QString family = settings.value(QStringLiteral("ui/textFont"), QStringLiteral("Consolas")).toString();
  const QString sizeName = settings.value(QStringLiteral("ui/textSize"), QStringLiteral("medium")).toString();
  const QStringList faces = {QStringLiteral("Consolas"), QStringLiteral("Cascadia Mono"),
                             QStringLiteral("Lucida Console"), QStringLiteral("Courier New"),
                             QStringLiteral("Segoe UI")};
  if (!faces.contains(family))
    family = QStringLiteral("Consolas");
  int pixelSize = 13;
  if (sizeName == QLatin1String("small"))
    pixelSize = 11;
  else if (sizeName == QLatin1String("large"))
    pixelSize = 16;
  QFont font(family);
  font.setStyleHint(family == QLatin1String("Segoe UI") ? QFont::SansSerif : QFont::Monospace);
  font.setPixelSize(pixelSize);
  if (auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("receiveText")))
    edit->setFont(font);
  if (auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("transmitText")))
    edit->setFont(font);
  if (auto* header = findChild<QLabel*>(QStringLiteral("decodeHeader")))
    header->setFont(font);
  alignDecodeHeader();
}

void MainWindow::applyDxOptions() {
  QSettings settings;
  const bool threeStage = settings.value(QStringLiteral("dx/threeStage"), true).toBool();
  int depth = settings.value(QStringLiteral("dx/depth"), 1).toInt();
  if (depth < 1 || depth > 3)
    depth = 1;
  const bool ap = settings.value(QStringLiteral("dx/ap"), true).toBool();
  const int offset = std::clamp(settings.value(QStringLiteral("dx/clockOffsetMs"), 0).toInt(), -5000, 5000);
  showTime_ = settings.value(QStringLiteral("dx/showTime"), true).toBool();
  showFreq_ = settings.value(QStringLiteral("dx/showFreq"), true).toBool();
  showDist_ = settings.value(QStringLiteral("dx/showDist"), true).toBool();
  distMiles_ = settings.value(QStringLiteral("dx/distMiles"), true).toBool();
  filterCq_ = settings.value(QStringLiteral("dx/filterCq"), false).toBool();
  filterCq73_ = settings.value(QStringLiteral("dx/filterCq73"), false).toBool();
  filterText_ = settings.value(QStringLiteral("dx/filterText")).toString();
  const QString hide = settings.value(QStringLiteral("dx/hideContinent")).toString();
  for (int i = 0; i < 7; ++i)
    hideContinent_[i] = hide.size() > i && hide.at(i) == QLatin1Char('1');
  if (auto* button = findChild<QPushButton*>(QStringLiteral("decodeFilters")))
    filtersOn_ = button->isChecked();
  clearEachPeriod_ = settings.value(QStringLiteral("dx/clearEachPeriod"), false).toBool();
  maxSignals_ = settings.value(QStringLiteral("dx/maxSignals"), 0).toInt();
  if (maxSignals_ < 0)
    maxSignals_ = 0;
  if (ft8_)
    ft8_->setOptions(threeStage, depth, ap, offset);
  refreshDecodeHeader();
  refreshDecodes();
}

void MainWindow::refreshDecodeHeader() {
  auto* header = findChild<QLabel*>("decodeHeader");
  if (!header)
    return;
  header->setText(decodeColumns(QStringLiteral("Time"), QStringLiteral("dB"), QStringLiteral("DT"),
                                 QStringLiteral("Dist"), QStringLiteral("Message"), QStringLiteral("Freq"),
                                 showTime_, showFreq_, showDist_));
}

void MainWindow::reloadMacros() {
  bool any = false;
  const auto buttons = findChildren<QPushButton*>("macro");
  for (QPushButton* button : buttons) {
    const int slot = button->property("slot").toInt();
    QSettings settings;
    const QString text = settings.value(QStringLiteral("dx/macro/%1").arg(slot)).toString();
    const bool filled = !text.trimmed().isEmpty();
    any = any || filled;
    button->setEnabled(filled);
    button->setToolTip(filled ? text + QStringLiteral("\nInserts into the transmit box. Does not key.")
                              : QStringLiteral("Empty macro slot. Does not key."));
  }
  if (auto* empty = findChild<QLabel*>("macroEmpty")) {
    empty->setText(any ? QStringLiteral("Macros stay in this window. They do not key.")
                       : QStringLiteral("No macros loaded"));
  }
}

void MainWindow::openTimeDialog() {
  QSettings settings;
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Time synchronization"));
  dialog.setMinimumWidth(420);
  auto* offset = new QSpinBox(&dialog);
  offset->setObjectName(QStringLiteral("clockOffset"));
  offset->setRange(-2000, 2000);
  offset->setSingleStep(10);
  offset->setSuffix(QStringLiteral(" ms"));
  offset->setValue(std::clamp(settings.value(QStringLiteral("dx/clockOffsetMs"), 0).toInt(), -2000, 2000));
  offset->setToolTip(QStringLiteral("Added to the FT8 period clock. Does not key."));
  auto* form = new QFormLayout;
  form->addRow(QStringLiteral("Clock offset"), offset);
  auto* note = new QLabel(
      QStringLiteral("The offset moves the decode period. Opening the Windows clock or time.is does not key."),
      &dialog);
  note->setWordWrap(true);
  auto* windows = new QPushButton(QStringLiteral("Open Windows time settings"), &dialog);
  auto* online = new QPushButton(QStringLiteral("Check time online"), &dialog);
  connect(windows, &QPushButton::clicked, &dialog, [] {
    QProcess::startDetached(QStringLiteral("control.exe"), {QStringLiteral("timedate.cpl,,0")});
  });
  connect(online, &QPushButton::clicked, &dialog, [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://time.is/")));
  });
  auto* ok = new QPushButton(QStringLiteral("OK"), &dialog);
  auto* cancel = new QPushButton(QStringLiteral("Cancel"), &dialog);
  auto* buttons = new QHBoxLayout;
  buttons->addWidget(windows);
  buttons->addWidget(online);
  buttons->addStretch(1);
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(note);
  layout->addLayout(buttons);
  connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;
  QSettings store;
  store.setValue(QStringLiteral("dx/clockOffsetMs"), offset->value());
  applyDxOptions();
}

void MainWindow::openMacrosDialog() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Macros"));
  dialog.setMinimumWidth(480);
  QLineEdit* edits[8] = {};
  auto* form = new QFormLayout;
  QSettings settings;
  for (int i = 0; i < 8; ++i) {
    edits[i] = new QLineEdit(settings.value(QStringLiteral("dx/macro/%1").arg(i + 1)).toString(), &dialog);
    edits[i]->setPlaceholderText(QStringLiteral("Local text. Not transmitted."));
    form->addRow(QStringLiteral("M%1").arg(i + 1), edits[i]);
  }
  auto* note = new QLabel(
      QStringLiteral("A filled button copies its text into the transmit box. Nothing is sent, and the radio is not keyed."),
      &dialog);
  note->setWordWrap(true);
  auto* ok = new QPushButton(QStringLiteral("OK"), &dialog);
  auto* cancel = new QPushButton(QStringLiteral("Cancel"), &dialog);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(note);
  layout->addLayout(buttons);
  connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;
  QSettings store;
  for (int i = 0; i < 8; ++i)
    store.setValue(QStringLiteral("dx/macro/%1").arg(i + 1), edits[i]->text());
  reloadMacros();
}

void MainWindow::openNetworkDialog() {
  QSettings settings;
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Network configuration"));
  dialog.setMinimumWidth(460);
  auto* enable = new QCheckBox(QStringLiteral("Enable PSK Reporter style spots"), &dialog);
  enable->setChecked(settings.value(QStringLiteral("dx/reportEnabled"), false).toBool());
  auto* call = new QLineEdit(settings.value(QStringLiteral("dx/reportCall")).toString(), &dialog);
  call->setPlaceholderText(QStringLiteral("Callsign"));
  auto* grid = new QLineEdit(settings.value(QStringLiteral("dx/reportGrid")).toString(), &dialog);
  grid->setPlaceholderText(QStringLiteral("Grid"));
  auto* region = new QComboBox(&dialog);
  region->setObjectName(QStringLiteral("profileRegionSetting"));
  region->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
  region->addItem(QStringLiteral("Region 1"), QStringLiteral("1"));
  region->addItem(QStringLiteral("Region 2"), QStringLiteral("2"));
  region->addItem(QStringLiteral("Region 3"), QStringLiteral("3"));
  const QString savedRegion = settings.value(QStringLiteral("profile/region"), QStringLiteral("auto")).toString();
  const int regionIndex = region->findData(savedRegion);
  region->setCurrentIndex(regionIndex < 0 ? 0 : regionIndex);
  region->setToolTip(QStringLiteral(
      "Auto reads the callsign. N8SDR/VE uses VE. The call is stored as typed. DIGU on every band. Does not key."));
  auto* host = new QLineEdit(
      settings.value(QStringLiteral("dx/reportHost"), QStringLiteral("report.pskreporter.info")).toString(),
      &dialog);
  auto* port = new QSpinBox(&dialog);
  port->setRange(1, 65535);
  port->setValue(settings.value(QStringLiteral("dx/reportPort"), 4739).toInt());
  auto* form = new QFormLayout;
  form->addRow(QStringLiteral("Call"), call);
  form->addRow(QStringLiteral("Grid"), grid);
  form->addRow(QStringLiteral("Region"), region);
  form->addRow(QStringLiteral("Server"), host);
  form->addRow(QStringLiteral("UDP port"), port);
  auto* note = new QLabel(
      QStringLiteral("The call is saved as typed. A portable suffix such as N8SDR/VE sets Auto to that "
                     "country; Region 1, 2, or 3 overrides it. The address is saved. This build does not "
                     "send spots, does not send START, and does not key. FT8 and FT4 stay DIGU."),
      &dialog);
  note->setWordWrap(true);
  auto* ok = new QPushButton(QStringLiteral("OK"), &dialog);
  auto* cancel = new QPushButton(QStringLiteral("Cancel"), &dialog);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(enable);
  layout->addLayout(form);
  layout->addWidget(note);
  layout->addLayout(buttons);
  connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;
  QSettings store;
  store.setValue(QStringLiteral("dx/reportEnabled"), enable->isChecked());
  store.setValue(QStringLiteral("dx/reportCall"), call->text().trimmed().toUpper());
  store.setValue(QStringLiteral("dx/reportGrid"), grid->text().trimmed().toUpper());
  if (auto* callEdit = findChild<QLineEdit*>(QStringLiteral("stationCall")))
    callEdit->setText(call->text().trimmed().toUpper());
  if (auto* gridEdit = findChild<QLineEdit*>(QStringLiteral("stationGrid")))
    gridEdit->setText(grid->text().trimmed().toUpper());
  store.setValue(QStringLiteral("dx/reportHost"), host->text().trimmed());
  store.setValue(QStringLiteral("dx/reportPort"), port->value());
  if (profile_) {
    profile_->setRegionOverride(region->currentData().toString());
    profile_->refreshStation();
  }
}

void MainWindow::setReceivePlaceholder(bool connected) {
  auto* receive = findChild<QPlainTextEdit*>("receiveText");
  if (!receive)
    return;
  receive->setPlaceholderText(
      connected ? QStringLiteral("Audio is arriving.\nFT8 lines appear here as the decoder finds them.")
                : QStringLiteral(
                      "Received text will appear here.\nNo radio is connected, so nothing is being decoded."));
}

void MainWindow::showRadioDown(const QString& error) {
  const QString dash = QString::fromUtf8("—");
  dialText_ = dash;
  setReceivePlaceholder(false);
  const QString line = error.isEmpty() ? QStringLiteral("Radio not connected")
                                        : QStringLiteral("Radio not connected: %1").arg(error);
  if (statusStrip_) {
    statusStrip_->setMode(dash);
    statusStrip_->setDial(dash);
    statusStrip_->setRxLevel(nullptr, 0);
    statusStrip_->setPtt(false, false);
    statusStrip_->setRadio(false, line);
  }
  if (waterfall_) {
    waterfall_->clearReceiveBand();
    waterfall_->clearAudio();
    waterfall_->setRadio(false, dash);
  }
  if (ft8_)
    ft8_->clear();
}

void MainWindow::showRadioUp(const QString& host, quint16 port) {
  dialText_ = QString::fromUtf8("—");
  setReceivePlaceholder(true);
  if (statusStrip_) {
    statusStrip_->setRadio(true, autoMonitor_ ? QStringLiteral("Monitor %1:%2").arg(host).arg(port)
                                               : QStringLiteral("Connected %1:%2").arg(host).arg(port));
    statusStrip_->setDial(dialText_);
    statusStrip_->setPtt(false, true);
  }
  if (waterfall_) {
    waterfall_->clearAudio();
    waterfall_->setRadio(true, dialText_);
  }
}

void MainWindow::showDial(qint64 hz) {
  if (!tci_ || !tci_->isOpen())
    return;
  dialText_ = formatDial(hz);
  if (statusStrip_)
    statusStrip_->setDial(dialText_);
  if (waterfall_)
    waterfall_->setRadio(true, dialText_);
}

void MainWindow::showAbout() {
  QMessageBox about(this);
  about.setWindowTitle("About STD");
  about.setIconPixmap(windowIcon().pixmap(64, 64));
  about.setText("STD\nShift, Tone, Decode");
  about.setInformativeText(
      "Version " STD_VERSION
      "\n\n"
      "Copyright (C) 2026 Rick Langford (N8SDR)\n\n"
      "Licensed under the GNU General Public License, version 3 "
      "or any later version. See the file COPYING.\n\n"
      "This build speaks TCI. It does not decode or transmit.");
  about.exec();
}
