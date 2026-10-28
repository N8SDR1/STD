// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindow.h"

#include "Ft8Decode.h"
#include "ModeProfile.h"
#include "StatusStrip.h"
#include "TciClient.h"
#include "WaterfallWidget.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
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
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTextCursor>
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

QString decodeColumns(const QString& time, const QString& db, const QString& dt,
                      const QString& message, const QString& freq, bool showTime, bool showFreq) {
  const QString msg = message.size() < 28 ? message.leftJustified(28) : message;
  QStringList fields;
  if (showTime)
    fields << QStringLiteral("%1").arg(time, -6);
  fields << QStringLiteral("%1").arg(db, 4);
  fields << QStringLiteral("%1").arg(dt, 5);
  fields << msg;
  if (showFreq)
    fields << QStringLiteral("%1").arg(freq, 5);
  return fields.join(QStringLiteral("  "));
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

class GearButton : public QToolButton {
public:
  explicit GearButton(QWidget* parent = nullptr) : QToolButton(parent) {
    setObjectName("radioConnect");
    setFixedSize(40, 40);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setToolTip("Connect to a TCI radio");
    setAccessibleName("Connect radio");
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(underMouse() ? QColor("#00e5ff") : QColor("#274150"), 1));
    painter.setBrush(underMouse() ? QColor(0, 229, 255, 28) : QColor("#101820"));
    painter.drawRoundedRect(box, 8, 8);

    painter.save();
    painter.translate(rect().center());
    painter.setPen(QPen(QColor("#00e5ff"), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(0, 0), 5.2, 5.2);
    painter.setBrush(QColor("#0b1218"));
    painter.drawEllipse(QPointF(0, 0), 2.1, 2.1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#00e5ff"));
    for (int tooth = 0; tooth < 8; ++tooth) {
      painter.save();
      painter.rotate(tooth * 45.0);
      painter.drawRoundedRect(QRectF(-1.25, -9.0, 2.5, 3.2), 0.6, 0.6);
      painter.restore();
    }
    painter.restore();
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
  connect(tci_, &TciClient::sessionDown, this, [this](const QString& error) { showRadioDown(error); });
  connect(tci_, &TciClient::dialChanged, this, [this](qint64 hz) { showDial(hz); });
  connect(tci_, &TciClient::modeChanged, this, [this](const QString& mode) {
    if (statusStrip_)
      statusStrip_->setMode(mode);
  });
  connect(tci_, &TciClient::pttChanged, this, [this](bool on) {
    if (statusStrip_)
      statusStrip_->setPtt(on, tci_->isOpen());
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
  auto* decodeClock = new QTimer(this);
  connect(decodeClock, &QTimer::timeout, this, [this] {
    if (!ft8_ || !profile_)
      return;
    const QString mode = profile_->digitalMode();
    const bool ft4 = mode == QLatin1String("FT4");
    const bool ft2 = mode == QLatin1String("FT2");
    const bool armed = autoMonitor_ && profile_->mshvFamily() &&
                       (ft4 || ft2 || mode == QLatin1String("FT8")) && profile_->autoDecode();
    const int low = profile_->startHz();
    ft8_->setMode(ft2 ? Ft8Decode::Period::Ft2 : ft4 ? Ft8Decode::Period::Ft4 : Ft8Decode::Period::Ft8);
    ft8_->setArmed(armed, low, low + profile_->bandwidthHz());
  });
  decodeClock->start(200);

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

  auto* gear = new GearButton(host);
  connect(gear, &QToolButton::clicked, this, [this] { openRadioDialog(); });
  layout->addWidget(gear);

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
      "Transmit text.\nCharacters stay in this window. Nothing is sent to the radio.",
      false, "Held locally. This build does not transmit."));
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

  auto* edit = new QPlainTextEdit(panel);
  edit->setObjectName(editName);
  edit->setPlaceholderText(placeholder);
  edit->setReadOnly(readOnly);
  edit->setFont(monoFont(13));
  edit->setTabChangesFocus(true);
  edit->setMinimumHeight(72);
  layout->addWidget(edit);

  if (!note.isEmpty()) {
    auto* caption = new QLabel(note, panel);
    caption->setObjectName("panelNote");
    caption->setFont(interfaceFont(11));
    caption->setWordWrap(true);
    layout->addWidget(caption);
  }
  return panel;
}

QWidget* MainWindow::makeReceivePanel() {
  auto* panel = new QWidget(this);
  panel->setObjectName("textPanel");
  panel->setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

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
                                           QStringLiteral("DT"), QStringLiteral("Message"),
                                           QStringLiteral("Freq"), true, true),
                             panel);
  header->setObjectName("decodeHeader");
  header->setFont(monoFont(13));
  header->setTextInteractionFlags(Qt::NoTextInteraction);
  layout->addWidget(header);

  auto* edit = new QPlainTextEdit(panel);
  edit->setObjectName("receiveText");
  edit->setPlaceholderText(
      QStringLiteral("Received text will appear here.\nNo radio is connected, so nothing is being decoded."));
  edit->setReadOnly(true);
  edit->setFont(monoFont(13));
  edit->setTabChangesFocus(true);
  edit->setMinimumHeight(72);
  edit->document()->setDocumentMargin(0);
  layout->addWidget(edit, 1);

  connect(filter, &QLineEdit::textChanged, this, [this](const QString&) { refreshDecodes(); });
  connect(clear, &QPushButton::clicked, this, [this] {
    decodes_.clear();
    refreshDecodes();
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
  for (const DecodeRow& row : decodes_) {
    QString line;
    if (row.time.isEmpty())
      line = row.message;
    else
      line = decodeColumns(row.time, QString::number(row.db), row.dt, row.message,
                           QString::number(row.freqHz), showTime_, showFreq_);
    if (!needle.isEmpty() && !line.contains(needle, Qt::CaseInsensitive))
      continue;
    if (!text.isEmpty())
      text += QLatin1Char('\n');
    text += line;
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

  auto* listMenu = optionsMenu->addMenu(QStringLiteral("Decode List Options"));
  addCheck(listMenu, "listShowTime", QStringLiteral("Show Time Column"), "dx/showTime", true,
           QStringLiteral("Show or hide the Time column."));
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
      "Save this dock arrangement for the current mode. It is restored the next time that mode opens. Does not key."));
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
  const bool mshv = modeName == QLatin1String("FT8") || modeName == QLatin1String("FT4") ||
                    modeName == QLatin1String("FT2");
  if (modeName != layoutMode_) {
    offerLayoutSave();
    layoutMode_ = modeName;
    if (!restoreModeLayout()) {
      layoutGuard_ = true;
      arrangeDefault();
      resizeToHero();
      layoutGuard_ = false;
    }
    captureLayoutBaseline();
  }
  if (profile_) {
    if (mshv)
      profile_->setDigitalMode(modeName);
    profile_->setMshvFamily(mshv);
  }
  if (statusStrip_)
    statusStrip_->setDigitalMode(modeName);
  if (mshv)
    tuneActiveBand();
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
       "The two sliders on the bottom bar recolor the trace. They do not change the frequency span."},
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
       "FT8 and FT4 are DIGU on every band. Neither "
       "command keys the radio, and neither is START."},
      {"FT8 and FT4 decode",
       "Auto Dec is on by default. The MSHV FT8 decoder starts on the period that is still running, "
       "about 11.8 seconds in, before the tones end, then again at 13.6 and 14.6 seconds when three-stage "
       "is on. FT4 is one pass at 6.1 seconds of its 7.5 second period. FT2 is one pass at 3.4 seconds of "
       "its 3.75 second period. MSHV does not run that three-stage "
       "timer for FT4. Each line is painted as soon as the decoder finds it. The Receive pane has column "
       "headers Time, dB, DT, Message, and Freq. Filter keeps lines that contain the text. CLR MSG clears "
       "the list. Freq is the audio frequency in Hz. Dist is not computed. The band buttons use the WSJT-X "
       "dials and send modulation:0,digu on every band. Decoding does not send START and it does not key."},
      {"DX options",
       "Options → Decode sets three-stage FT8, Fast/Normal/Deep, AP, and a cap on lines. Time "
       "Synchronization calibrates the period clock. Macros edit eight local texts. Network "
       "Configuration stores a PSK Reporter style address and does not send spots. Decode List "
       "Options show or hide Time and Freq, and can clear the list each period. Log options, ASeq, "
       "TX confirmation, and Auto RESET QSO are saved. Transmit is off, so they do not key."},
      {"RX and TX markers",
       "A green line is the RX frequency and a red line is the TX frequency. Click the waterfall to "
       "set RX. Shift+click sets TX. Ctrl+click sets both. Lock moves them together. RX=TX copies RX "
       "onto TX. None of these key the radio."},
      {"Layout",
       "View → Save layout stores the dock arrangement for the mode that is selected. The next time "
       "that mode opens, including after a restart, those panels come back. View → Reset layout "
       "stacks the current mode again."},
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

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (!heroSized_) {
    heroSized_ = true;
    // restoreState during this event is ignored for dock sizes. Wait until
    // the window is on screen, then put the saved mode layout back.
    QTimer::singleShot(0, this, [this] {
      if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
        resizeToHero();
        layoutReady_ = true;
        return;
      }
      const bool restored = restoreModeLayout();
      layoutReady_ = true;
      if (!restored)
        resizeToHero();
      captureLayoutBaseline();
    });
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  offerLayoutSave();
  QMainWindow::closeEvent(event);
}

void MainWindow::offerLayoutSave() {
  if (layoutMode_.isEmpty() || !layoutReady_ || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (saveState(1) == layoutBaseline_)
    return;
  const auto answer = QMessageBox::question(this, QStringLiteral("STD"),
                                            QStringLiteral("Save layout for this mode?"),
                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (answer == QMessageBox::Yes)
    saveModeLayout(true);
}

void MainWindow::captureLayoutBaseline() {
  layoutBaseline_ = saveState(1);
}

void MainWindow::saveModeLayout(bool force) {
  if (layoutMode_.isEmpty() || qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (!force && (layoutGuard_ || !layoutReady_))
    return;
  QSettings settings;
  settings.setValue(QStringLiteral("layout/%1/state").arg(layoutMode_), saveState(1));
  settings.setValue(QStringLiteral("layout/%1/geometry").arg(layoutMode_), saveGeometry());
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
                                 QStringLiteral("Message"), QStringLiteral("Freq"), showTime_, showFreq_));
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
  store.setValue(QStringLiteral("dx/reportCall"), call->text().trimmed());
  store.setValue(QStringLiteral("dx/reportGrid"), grid->text().trimmed());
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
