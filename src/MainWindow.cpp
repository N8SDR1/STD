// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindow.h"

#include "StatusStrip.h"
#include "TciClient.h"
#include "WaterfallWidget.h"

#include <algorithm>

#include <QAction>
#include <QIcon>
#include <QDockWidget>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
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
  connect(tci_, &TciClient::rxAudio, this, [this](int rate, const QByteArray& mono) {
    if (!waterfall_ || mono.size() < int(sizeof(float)))
      return;
    const auto* samples = reinterpret_cast<const float*>(mono.constData());
    waterfall_->pushRealAudio(samples, mono.size() / int(sizeof(float)), rate);
  });
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
  waterfallDock_ = new QDockWidget("Waterfall", this);
  waterfallDock_->setObjectName("waterfallDock");
  waterfallDock_->setWidget(waterfall_);
  waterfallDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  waterfallDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

  receiveDock_ = new QDockWidget("Receive", this);
  receiveDock_->setObjectName("receiveDock");
  receiveDock_->setWidget(makeTextPanel(
      "receiveText",
      "Received text will appear here.\nNo radio is connected, so nothing is being decoded.",
      true, QString()));
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
    button->setEnabled(false);
    button->setToolTip("Empty macro slot");
    button->setAccessibleName(QStringLiteral("Macro %1, empty").arg(i));
    layout->addWidget(button);
  }

  auto* empty = new QLabel("No macros loaded", row);
  empty->setObjectName("macroEmpty");
  empty->setFont(interfaceFont(12));
  layout->addWidget(empty);
  layout->addStretch(1);
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
  auto* reset = viewMenu->addAction("Reset layout");
  connect(reset, &QAction::triggered, this, [this] {
    arrangeDefault();
    resizeToHero();
  });

  auto* helpMenu = menuBar()->addMenu("Help");
  auto* about = helpMenu->addAction("About STD");
  connect(about, &QAction::triggered, this, [this] { showAbout(); });
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
    QTimer::singleShot(0, this, [this] { resizeToHero(); });
  }
}

void MainWindow::openRadioDialog() {
  if (tci_)
    openTciConnectDialog(this, tci_);
}

void MainWindow::setReceivePlaceholder(bool connected) {
  auto* receive = findChild<QPlainTextEdit*>("receiveText");
  if (!receive)
    return;
  receive->setPlaceholderText(
      connected ? QStringLiteral("Audio is arriving.\nNothing is decoded yet.")
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
    statusStrip_->setPtt(false, false);
    statusStrip_->setRadio(false, line);
  }
  if (waterfall_) {
    waterfall_->clearAudio();
    waterfall_->setRadio(false, dash);
  }
}

void MainWindow::showRadioUp(const QString& host, quint16 port) {
  dialText_ = QString::fromUtf8("—");
  setReceivePlaceholder(true);
  if (statusStrip_) {
    statusStrip_->setRadio(true, QStringLiteral("Connected %1:%2").arg(host).arg(port));
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
