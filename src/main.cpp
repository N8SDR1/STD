// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindow.h"

#include "WaterfallWidget.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
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
    if (button->isEnabled())
      return fail("a macro slot is enabled");
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

  if (!window.findChild<QWidget*>("radioConnect"))
    return fail("connect control is missing");

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
