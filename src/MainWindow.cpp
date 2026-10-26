// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindow.h"

#include "StatusStrip.h"
#include "WaterfallWidget.h"

#include <algorithm>

#include <QAction>
#include <QIcon>
#include <QDockWidget>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QToolBar>
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
}

void MainWindow::buildBrandBar() {
  brandBar_ = addToolBar("STD");
  brandBar_->setObjectName("brandBar");
  brandBar_->setMovable(false);
  brandBar_->setFloatable(false);
  brandBar_->setAllowedAreas(Qt::TopToolBarArea);

  auto* host = new QWidget(brandBar_);
  host->setFixedHeight(52);
  auto* layout = new QHBoxLayout(host);
  layout->setContentsMargins(8, 6, 12, 6);
  layout->setSpacing(10);

  auto* mark = new QLabel(host);
  const QPixmap icon(":/std/std-icon.png");
  mark->setPixmap(icon.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  mark->setFixedSize(40, 40);

  auto* words = new QWidget(host);
  auto* wordLayout = new QVBoxLayout(words);
  wordLayout->setContentsMargins(0, 0, 0, 0);
  wordLayout->setSpacing(0);

  auto* name = new QLabel("STD", words);
  name->setObjectName("brandName");
  QFont nameFont = interfaceFont(18, QFont::DemiBold);
  nameFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
  name->setFont(nameFont);

  auto* subtitle = new QLabel("Shift, Tone, Decode", words);
  subtitle->setObjectName("brandSubtitle");
  subtitle->setFont(interfaceFont(11));

  wordLayout->addWidget(name);
  wordLayout->addWidget(subtitle);

  layout->addWidget(mark);
  layout->addWidget(words);
  layout->addStretch(1);

  brandBar_->addWidget(host);
}

void MainWindow::buildDocks() {
  auto* waterfall = new WaterfallWidget(this);
  waterfallDock_ = new QDockWidget("Waterfall", this);
  waterfallDock_->setObjectName("waterfallDock");
  waterfallDock_->setWidget(waterfall);
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
      "Transmit text.\nCharacters stay in this window until a radio is connected.",
      false, "Held locally. Nothing is transmitted while the radio is disconnected."));
  transmitDock_->setAllowedAreas(Qt::AllDockWidgetAreas);

  macroDock_ = new QDockWidget("Macros", this);
  macroDock_->setObjectName("macroDock");
  macroDock_->setWidget(makeMacroRow());
  macroDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  macroDock_->setMinimumHeight(68);
  macroDock_->setMaximumHeight(96);

  statusDock_ = new QDockWidget("Status", this);
  statusDock_->setObjectName("statusDock");
  statusDock_->setWidget(new StatusStrip(statusDock_));
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
      "This build is the dock shell. It does not decode, transmit, "
      "or open a radio connection.");
  about.exec();
}
