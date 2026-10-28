// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindow.h"

#include "WaterfallWidget.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QMenu>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QToolButton>
#include <QStyleFactory>
#include <QTimer>

namespace {

QPalette darkPalette() {
  QPalette palette;
  const QColor window("#06090e");
  const QColor panel("#0f1720");
  const QColor text("#cdd9e5");
  const QColor muted("#6d8496");
  const QColor cyan("#00e5ff");
  palette.setColor(QPalette::Window, window);
  palette.setColor(QPalette::WindowText, text);
  palette.setColor(QPalette::Base, QColor("#080d13"));
  palette.setColor(QPalette::AlternateBase, panel);
  palette.setColor(QPalette::Text, text);
  palette.setColor(QPalette::Button, QColor("#121a24"));
  palette.setColor(QPalette::ButtonText, text);
  palette.setColor(QPalette::BrightText, cyan);
  palette.setColor(QPalette::Highlight, cyan);
  palette.setColor(QPalette::HighlightedText, QColor("#061018"));
  palette.setColor(QPalette::PlaceholderText, muted);
  palette.setColor(QPalette::ToolTipBase, panel);
  palette.setColor(QPalette::ToolTipText, text);
  palette.setColor(QPalette::Disabled, QPalette::Text, muted);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
  return palette;
}

int shellReady(const MainWindow& window) {
  auto fail = [](const char* reason) {
    qCritical("%s", reason);
    return 1;
  };

  if (window.windowTitle() != QLatin1String("STD"))
    return fail("window title is not STD");
  if (window.windowIcon().isNull())
    return fail("window icon is missing");
  if (!window.findChild<WaterfallWidget*>())
    return fail("waterfall widget is missing");

  const auto* contrast = window.findChild<QSlider*>("waterfallContrast");
  const auto* brightness = window.findChild<QSlider*>("waterfallBrightness");
  if (!contrast || !brightness || !contrast->isEnabled() || !brightness->isEnabled() ||
      contrast->maximum() != 100 || brightness->maximum() != 100)
    return fail("waterfall contrast or brightness is missing");

  const auto* palette = window.findChild<QToolButton*>("waterfallPalette");
  const char* paletteNames[] = {"Classic", "Inferno", "Viridis", "Plasma", "Rainbow",
                                 "Ocean",   "Night",   "Grayscale", "Amber"};
  if (!palette || !palette->menu() || palette->menu()->actions().size() != 9)
    return fail("waterfall palette menu is missing");
  for (int i = 0; i < 9; ++i) {
    if (palette->menu()->actions().at(i)->text() != QLatin1String(paletteNames[i]))
      return fail("waterfall palette name is wrong");
  }

  const auto* ticks = window.findChild<QToolButton*>("waterfallTicks");
  const char* tickNames[] = {"100 Hz", "250 Hz", "500 Hz"};
  if (!ticks || !ticks->menu() || ticks->menu()->actions().size() != 3)
    return fail("waterfall tick menu is missing");
  for (int i = 0; i < 3; ++i) {
    if (ticks->menu()->actions().at(i)->text() != QLatin1String(tickNames[i]))
      return fail("waterfall tick step is wrong");
  }

  const auto* subtitle = window.findChild<QLabel*>("brandSubtitle");
  if (!subtitle || subtitle->text() != QLatin1String("Shift, Tone, Decode"))
    return fail("subtitle is missing");

  const auto* receive = window.findChild<QPlainTextEdit*>("receiveText");
  if (!receive || !receive->isReadOnly() || !receive->toPlainText().isEmpty() ||
      !receive->placeholderText().contains(QLatin1String("Received")))
    return fail("receive pane is not empty");

  const auto* transmit = window.findChild<QPlainTextEdit*>("transmitText");
  if (!transmit || transmit->isReadOnly() ||
      !transmit->placeholderText().contains(QLatin1String("Transmit")))
    return fail("transmit pane is missing");

  const auto macros = window.findChildren<QPushButton*>("macro");
  if (macros.size() != 8)
    return fail("macro row does not have 8 slots");
  for (const QPushButton* button : macros) {
    if (button->isEnabled() && !button->toolTip().contains(QLatin1String("Does not key")))
      return fail("a macro slot can key");
  }

  const char* dxNames[] = {"decodeThreeStage", "decodeAp",     "decodeDepthFast", "decodeDepthNormal",
                           "decodeDepthDeep",  "timeSync",     "macrosEdit",      "networkConfig",
                           "listShowTime",     "listShowFreq", "listClearPeriod", "logAuto",
                           "logPrompt",        "aseqMostDistant", "txConfirm73",  "autoResetQso"};
  for (const char* name : dxNames) {
    if (!window.findChild<QAction*>(QLatin1String(name)))
      return fail("a DX option is missing");
  }

  const auto* mode = window.findChild<QLabel*>("modeValue");
  const auto* afc = window.findChild<QLabel*>("afcValue");
  const auto* squelch = window.findChild<QLabel*>("squelchValue");
  if (!mode || !afc || !squelch)
    return fail("status readouts are missing");
  if (afc->text() != QLatin1String("Off"))
    return fail("AFC is not off");

  const auto* radio = window.findChild<QLabel*>("radioState");
  if (!radio || !radio->text().contains(QLatin1String("not connected"), Qt::CaseInsensitive))
    return fail("radio state is not disconnected");

  const auto* ptt = window.findChild<QPushButton*>("ptt");
  if (!ptt || ptt->isChecked() || ptt->isEnabled())
    return fail("PTT is not the idle on-air control");

  const auto* dial = window.findChild<QLabel*>("dialValue");
  if (!dial || dial->text() != QString::fromUtf8("—"))
    return fail("dial is not blank");

  const auto* rxLevel = window.findChild<QLabel*>("rxLevel");
  const auto* rxMeter = window.findChild<QWidget*>("rxLevelMeter");
  if (!rxLevel || rxLevel->text() != QString::fromUtf8("—") || !rxMeter)
    return fail("RX level meter is missing");
  const auto* rxGain = window.findChild<QSpinBox*>("rxGain");
  const auto* txGain = window.findChild<QSpinBox*>("txGain");
  if (!rxGain || !txGain || rxGain->minimum() > -20 || rxGain->maximum() < 40 ||
      txGain->minimum() > -20 || txGain->maximum() < 40)
    return fail("RX or TX gain is missing");
  if (!rxGain->toolTip().contains(QLatin1String("does not key")) ||
      !txGain->toolTip().contains(QLatin1String("does not key")))
    return fail("a gain control can key");

  if (!window.findChild<QWidget*>("radioConnect"))
    return fail("connect control is missing");

  const auto* autoConnect = window.findChild<QAction*>("tciAutoConnect");
  const auto* autoMonitor = window.findChild<QAction*>("tciAutoMonitor");
  if (!autoConnect || !autoConnect->isCheckable() || !autoMonitor || !autoMonitor->isCheckable())
    return fail("auto-connect or auto monitor is missing");

  const auto* profile = window.findChild<QWidget*>("mshvProfile");
  if (!profile || profile->isHidden())
    return fail("FT8 profile is hidden");
  const auto* speed = window.findChild<QSpinBox*>("profileSpeed");
  const auto* start = window.findChild<QSpinBox*>("profileStart");
  const auto* bandwidth = window.findChild<QSpinBox*>("profileBandwidth");
  const auto* autoDec = window.findChild<QCheckBox*>("profileAutoDec");
  if (!speed || speed->minimum() != 1 || speed->maximum() != 9)
    return fail("speed control is missing");
  if (!start || start->singleStep() != 100)
    return fail("start control is missing");
  if (!bandwidth || bandwidth->minimum() != 2000 || bandwidth->maximum() != 5000)
    return fail("bandwidth control is missing");
  if (!autoDec)
    return fail("auto decode is missing");
  const auto bands = window.findChildren<QPushButton*>("profileBand");
  if (bands.size() != 10)
    return fail("band buttons are missing");
  int checked = 0;
  for (const QPushButton* button : bands) {
    if (button->isChecked())
      ++checked;
  }
  if (checked != 1)
    return fail("no band is highlighted");
  bool guide = false;
  if (window.menuBar()) {
    for (QAction* menuAction : window.menuBar()->actions()) {
      if (!menuAction->menu())
        continue;
      for (QAction* item : menuAction->menu()->actions()) {
        if (item->objectName() == QLatin1String("helpGuide"))
          guide = true;
      }
    }
  }
  if (!guide)
    return fail("guide is missing");
  if (!window.findChild<QAction*>("saveLayout"))
    return fail("save layout is missing");

  qInfo("STD self-test ok");
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setOrganizationName("N8SDR");
  QApplication::setApplicationName("STD");
  QApplication::setApplicationDisplayName("STD");
  QApplication::setApplicationVersion(STD_VERSION);
  QApplication::setWindowIcon(stdIcon());

  if (QStyle* fusion = QStyleFactory::create("Fusion"))
    QApplication::setStyle(fusion);
  app.setPalette(darkPalette());

  QFile style(":/std/style.qss");
  if (style.open(QIODevice::ReadOnly))
    app.setStyleSheet(QString::fromUtf8(style.readAll()));
  else
    qWarning("STD stylesheet is missing from the binary resources");

  MainWindow window;
  window.show();

  if (qEnvironmentVariableIsSet("STD_SELFTEST")) {
    QTimer::singleShot(0, &app, [&app, &window] { app.exit(shellReady(window)); });
  }

  return app.exec();
}
