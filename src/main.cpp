// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DigitalMode.h"
#include "hell/HellRaster.h"
#include "MacroFile.h"
#include "MaEngine.h"
#include "MainWindow.h"
#include "StatusStrip.h"

#include "QsoLog.h"
#include "StationFiles.h"
#include "TxWave.h"
#include "hell/HellEngine.h"
#include "dominoex/DominoModem.h"
#include "thor/ThorModem.h"
#include "mfsk/MfskModem.h"
#include "mt63/Mt63Modem.h"
#include "olivia/OliviaModem.h"
#include "psk/PskModem.h"
#include "rtty/RttyModem.h"
#include "wefax/WefaxModem.h"
#include "js8/Js8Modem.h"
#include "wspr/WsprModem.h"
#include "spot/PskReporter.h"
#include "spot/WsprNet.h"
#include "UpdateChecker.h"
#include "WaterfallGpu.h"
#include "WaterfallWidget.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QTableWidget>
#include <QTime>
#include <QTimeZone>
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

int shellReady(MainWindow& window) {
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

  const auto* standard = window.findChild<QLabel*>("txStandard");
  const auto* txExtra = window.findChild<QLineEdit*>("txExtra");
  if (!standard || standard->text() != QLatin1String("Standard") || !txExtra)
    return fail("TX standard or extra field is missing");
  for (int i = 1; i <= 6; ++i) {
    if (!window.findChild<QLineEdit*>(QStringLiteral("tx%1").arg(i)))
      return fail("a standard TX field is missing");
  }
  const auto* tune = window.findChild<QPushButton*>("txTune");
  if (!tune || tune->isChecked())
    return fail("Tune is not idle");
  const auto* halt = window.findChild<QPushButton*>("txHalt");
  if (!halt || halt->isCheckable() || halt->isChecked() ||
      halt->toolTip().contains(QLatin1String("MSHV")) ||
      halt->toolTip().contains(QLatin1String("START")) ||
      !halt->toolTip().contains(QLatin1String("Does not key")))
    return fail("Halt is missing");
  const auto* rxTx = window.findChild<QPushButton*>("freqRxTx");
  if (!rxTx || rxTx->isCheckable() ||
      (rxTx->text() != QLatin1String("RX=TX") && rxTx->text() != QLatin1String("TX=RX")) ||
      !rxTx->toolTip().contains(QLatin1String("TX=RX")) ||
      !rxTx->toolTip().contains(QLatin1String("does not key")))
    return fail("RX=TX chip is missing");
  const auto* qsy = window.findChild<QAction*>("qsyOnDecode");
  if (!qsy || !qsy->isCheckable() || !qsy->toolTip().contains(QLatin1String("does not key")))
    return fail("QSY option is missing");
  auto* clrNow = window.findChild<QPushButton*>("txClrNow");
  if (!clrNow || !clrNow->toolTip().contains(QLatin1String("QSO")))
    return fail("CLR Now does not clear the QSO pane");
  window.placeAnswer(QStringLiteral("ZZ9ZZ"));
  const auto* planted = window.findChild<QLineEdit*>(QStringLiteral("tx1"));
  if (!planted || !planted->text().contains(QLatin1String("ZZ9ZZ")))
    return fail("answer was not placed on Tx1");
  clrNow->click();
  if (tune->isChecked())
    return fail("CLR Now keyed");
  const auto* send = window.findChild<QPushButton*>(QStringLiteral("txSend"));
  if (send && send->isChecked())
    return fail("CLR Now armed TX");
  const auto* tx6 = window.findChild<QLineEdit*>(QStringLiteral("tx6"));
  const auto* activity = window.findChild<QComboBox*>("txActivity");
  const int activityId = activity ? activity->currentData().toInt() : 0;
  if (!tx6)
    return fail("Tx6 is missing");
  for (int i = 1; i <= 6; ++i) {
    const auto* field = window.findChild<QLineEdit*>(QStringLiteral("tx%1").arg(i));
    if (!field)
      return fail("a standard TX field is missing");
    if (field->text().contains(QLatin1String("ZZ9ZZ")))
      return fail("CLR Now left a call on a TX line");
  }
  if (activityId == 0) {
    const auto* tx1 = window.findChild<QLineEdit*>(QStringLiteral("tx1"));
    const auto* tx2 = window.findChild<QLineEdit*>(QStringLiteral("tx2"));
    const auto* tx3 = window.findChild<QLineEdit*>(QStringLiteral("tx3"));
    const auto* tx4 = window.findChild<QLineEdit*>(QStringLiteral("tx4"));
    const auto* tx5 = window.findChild<QLineEdit*>(QStringLiteral("tx5"));
    const auto* extra = window.findChild<QLineEdit*>(QStringLiteral("txExtra"));
    if (!tx1 || tx1->text().isEmpty() || tx1->text().startsWith(QLatin1String("CQ")) ||
        tx1->text() == tx6->text())
      return fail("standard Tx1 was left as the CQ");
    if (!tx2 || (!tx2->text().contains(QLatin1Char('+')) && !tx2->text().contains(QLatin1Char('-'))))
      return fail("standard report is missing from Tx2");
    if (!tx3 || !tx3->text().contains(QLatin1Char('R')))
      return fail("standard roger is missing from Tx3");
    if (!tx4 || !tx4->text().contains(QLatin1String("RR73")))
      return fail("standard RR73 is missing from Tx4");
    if (!tx5 || tx5->text().contains(QLatin1String("RR73")) || !tx5->text().contains(QLatin1String("73")))
      return fail("standard 73 is missing from Tx5");
    if (!tx6->text().startsWith(QLatin1String("CQ")))
      return fail("CLR Now did not restore CQ");
    if (extra && extra->text().contains(QLatin1String("QSOB4")))
      return fail("standard extra is a contest line");
  } else if (!tx6->text().startsWith(QLatin1String("CQ"))) {
    return fail("contest CQ is missing from Tx6");
  } else {
    bool tagged = false;
    for (int i = 1; i <= 3; ++i) {
      if (window.findChild<QLineEdit*>(QStringLiteral("tx%1").arg(i))->text() != tx6->text())
        tagged = true;
    }
    if (!tagged)
      return fail("contest exchange was not written to Tx1 through Tx3");
  }
  if (!activity || activity->count() < 18 || activity->itemText(0) != QLatin1String("Standard") ||
      activity->findText(QStringLiteral("ARRL Field Day")) < 0)
    return fail("contest activity list is missing");
  const auto* cqType = window.findChild<QComboBox*>("txCqType");
  const char* cqNames[] = {"CQ", "CQ MDX", "CQ DX", "CQ UP", "CQ IOTA", "CQ POTA", "CQ SOTA",
                           "CQ BOTA", "CQ WWFF", "CQ AF"};
  if (!cqType)
    return fail("CQ type list is missing");
  for (const char* name : cqNames) {
    if (cqType->findText(QLatin1String(name)) < 0)
      return fail("a CQ type is missing");
  }
  const auto* foxCq = window.findChild<QComboBox*>("txFoxCq");
  if (!foxCq || foxCq->findText(QStringLiteral("QSY To")) < 0 ||
      foxCq->findText(QStringLiteral("Free Msg")) < 0)
    return fail("Super Fox CQ list is missing");
  const auto* dupes = window.findChild<QComboBox*>("txDupes");
  const char* dupeNames[] = {"No Limit", "1 QSO", "2 QSOs", "3 QSOs"};
  if (!dupes || dupes->count() != 4)
    return fail("dupe list is missing");
  for (int i = 0; i < 4; ++i) {
    if (dupes->itemText(i) != QLatin1String(dupeNames[i]))
      return fail("dupe choice is wrong");
  }
  if (!window.findChild<QSpinBox*>("txQueueLimit") || !window.findChild<QSpinBox*>("txSlots") ||
      !window.findChild<QSpinBox*>("txMaxPeriods") || !window.findChild<QCheckBox*>("txCns") ||
      !window.findChild<QCheckBox*>("txCqOnFree") || !window.findChild<QPushButton*>("txClrQueue") ||
      !window.findChild<QPushButton*>("txClrNow") || !window.findChild<QWidget*>("txMultiOptions"))
    return fail("multi TX options are missing");
  const auto* qso = window.findChild<QPlainTextEdit*>("txQso");
  if (!qso || !qso->isReadOnly() || !qso->toPlainText().isEmpty())
    return fail("QSO pane is not empty");

  const auto macros = window.findChildren<QPushButton*>("macro");
  if (macros.size() != 12)
    return fail("macro row does not have 12 slots");
  for (const QPushButton* button : macros) {
    if (!button->toolTip().contains(QLatin1String("does not key"), Qt::CaseInsensitive) ||
        !button->toolTip().contains(QLatin1String("START")))
      return fail("a macro slot can key");
  }
  const auto* bank = window.findChild<QPushButton*>(QStringLiteral("macroBank"));
  const auto* active = window.findChild<QLabel*>(QStringLiteral("macroActive"));
  const auto* macroOpen = window.findChild<QAction*>(QStringLiteral("macroOpen"));
  const auto* macroSave = window.findChild<QAction*>(QStringLiteral("macroSave"));
  if (!bank || bank->text() != QLatin1String("1") ||
      !bank->toolTip().contains(QLatin1String("does not key"), Qt::CaseInsensitive) ||
      !bank->toolTip().contains(QLatin1String("START")))
    return fail("macro bank is missing");
  if (!active || !macroOpen || !macroSave ||
      !macroOpen->toolTip().contains(QLatin1String("START")) ||
      !macroSave->toolTip().contains(QLatin1String("does not key"), Qt::CaseInsensitive))
    return fail("macro file menu is missing");
  QVector<MacroSlot> parsed;
  QString parseError;
  const QString sample = QStringLiteral(
      "//fldigi macro definition file extended\n"
      "\n// Macro # 1\n/$ 0 CQ  @>|\n<TX>CQ de <MYCALL><RX>\n");
  if (!loadMacroDefinition(sample, &parsed, &parseError) || parsed.at(0).name != QLatin1String("CQ  @>|"))
    return fail("mdf import failed");
  const QString inserted = macroInsertText(parsed.at(0).text, QStringLiteral("N8SDR"), QStringLiteral("EM89"));
  if (inserted.contains(QStringLiteral("<TX>")) || inserted.contains(QStringLiteral("<RX>")) ||
      !inserted.contains(QStringLiteral("N8SDR")))
    return fail("macro text did not convert");
  const QVector<MacroSlot> blank = blankMacroSet();
  if (blank.size() != 48 || blank.at(0).name != QLatin1String("F-1") ||
      blank.at(47).name != QLatin1String("F-48") || !blank.at(0).text.isEmpty())
    return fail("new macro set is wrong");
  QVector<MacroSlot> roundTrip;
  if (!loadMacroDefinition(saveMacroDefinition(blank), &roundTrip, &parseError) ||
      roundTrip.at(11).name != QLatin1String("F-12") || !roundTrip.at(11).text.isEmpty())
    return fail("new macro set did not save as mdf");
  bool sawFreq = false;
  bool sawCall = false;
  for (const MacroTagRow& tag : macroTagList()) {
    if (tag.tag == QLatin1String("<FREQ>"))
      sawFreq = true;
    if (tag.tag == QLatin1String("<MYCALL>"))
      sawCall = true;
  }
  if (!sawFreq || !sawCall)
    return fail("macro tag list is missing");
  QFile n8file(QStringLiteral("C:/Users/N8SDR/fldigi.files/macros/macrosN8SDR.mdf"));
  if (!n8file.open(QIODevice::ReadOnly | QIODevice::Text))
    return fail("macrosN8SDR.mdf did not open");
  QVector<MacroSlot> n8;
  if (!loadMacroDefinition(QString::fromUtf8(n8file.readAll()), &n8, &parseError) || n8.size() != 48)
    return fail("macrosN8SDR.mdf did not parse");
  if (n8.at(0).name != QLatin1String("CQ  @>|") || !n8.at(0).text.contains(QStringLiteral("<MYCALL>")) ||
      !n8.at(0).text.contains(QStringLiteral("<TX>")))
    return fail("macrosN8SDR CQ slot is wrong");
  if (!n8.at(2).text.contains(QLatin1Char('\n')) || n8.at(2).name != QLatin1String("RST/ LOG"))
    return fail("macrosN8SDR newline slot is wrong");
  if (n8.at(36).name != QLatin1String("3x call") || !n8.at(37).text.contains(QStringLiteral("FH#5065")) ||
      !n8.at(22).text.isEmpty())
    return fail("macrosN8SDR later slots are wrong");

  const char* dxNames[] = {"decodeThreeStage", "decodeAp",     "decodeDepthFast", "decodeDepthNormal",
                           "decodeDepthDeep",  "timeSync",     "macrosEdit",      "userSettings",
                           "networkConfig", "contestTags",
                           "listShowTime",     "listShowFreq", "listClearPeriod", "logAuto",
                           "logPrompt",        "logFileDialog",   "aseqMostDistant", "txConfirm73",
                           "autoResetQso"};
  for (const char* name : dxNames) {
    if (!window.findChild<QAction*>(QLatin1String(name)))
      return fail("a DX option is missing");
  }
  for (const char* name : {"backupSettings", "importSettings", "backupLogbook", "exportAdif"}) {
    const auto* action = window.findChild<QAction*>(QLatin1String(name));
    if (!action || !action->toolTip().contains(QLatin1String("START")) ||
        !action->toolTip().contains(QLatin1String("not key")))
      return fail("a backup action is missing");
  }
  if (window.findChild<QLineEdit*>(QStringLiteral("backupCall")) ||
      window.findChild<QLineEdit*>(QStringLiteral("backupGrid")))
    return fail("backup added a callsign field");
  {
    QTemporaryDir dir;
    if (!dir.isValid())
      return fail("settings temp dir is missing");
    const QString sourcePath = dir.filePath(QStringLiteral("source.ini"));
    const QString destPath = dir.filePath(QStringLiteral("dest.ini"));
    const QString layoutOnlyPath = dir.filePath(QStringLiteral("layout.ini"));
    QSettings source(sourcePath, QSettings::IniFormat);
    source.setValue(QStringLiteral("layout/FT8/state"), QByteArray("ABC"));
    source.setValue(QStringLiteral("dx/reportCall"), QStringLiteral("W1AW"));
    source.setValue(QStringLiteral("dx/reportGrid"), QStringLiteral("FN31"));
    source.setValue(QStringLiteral("tci/host"), QStringLiteral("192.0.2.1"));
    source.setValue(QStringLiteral("tci/port"), 9);
    source.sync();
    QSettings dest(destPath, QSettings::IniFormat);
    dest.setValue(QStringLiteral("layout/FT8/state"), QByteArray("OLD"));
    dest.setValue(QStringLiteral("layout/WSPR/state"), QByteArray("GONE"));
    dest.setValue(QStringLiteral("dx/reportCall"), QStringLiteral("KEEP"));
    dest.setValue(QStringLiteral("tci/host"), QStringLiteral("127.0.0.1"));
    dest.sync();
    if (copyStationSettings(source, dest, true) < 3)
      return fail("settings copy wrote nothing");
    dest.sync();
    QSettings check(destPath, QSettings::IniFormat);
    if (check.value(QStringLiteral("layout/FT8/state")).toByteArray() != QByteArray("ABC") ||
        check.value(QStringLiteral("dx/reportCall")).toString() != QLatin1String("W1AW") ||
        check.value(QStringLiteral("dx/reportGrid")).toString() != QLatin1String("FN31") ||
        check.contains(QStringLiteral("layout/WSPR/state")) ||
        check.value(QStringLiteral("tci/host")).toString() != QLatin1String("127.0.0.1") ||
        check.value(QStringLiteral("tci/port")).toInt() != 0)
      return fail("settings copy touched TCI or dropped a layout");
    QSettings layoutOnly(layoutOnlyPath, QSettings::IniFormat);
    layoutOnly.setValue(QStringLiteral("layout/JS8/state"), QByteArray("JS"));
    layoutOnly.sync();
    if (copyStationSettings(layoutOnly, check, true) < 1)
      return fail("layout-only import wrote nothing");
    check.sync();
    QSettings afterLayout(destPath, QSettings::IniFormat);
    if (afterLayout.value(QStringLiteral("dx/reportCall")).toString() != QLatin1String("W1AW") ||
        afterLayout.value(QStringLiteral("layout/JS8/state")).toByteArray() != QByteArray("JS") ||
        afterLayout.contains(QStringLiteral("layout/FT8/state")) ||
        afterLayout.value(QStringLiteral("tci/host")).toString() != QLatin1String("127.0.0.1"))
      return fail("layout-only import changed the callsign or TCI");
    const QString logPath = dir.filePath(QStringLiteral("std.adi"));
    const QByteArray logBody =
        QByteArrayLiteral("STD ADIF\n<ADIF_VER:5>3.1.0\n<PROGRAMID:3>STD\n<EOH>\n"
                          "<CALL:4>W1AW<GRIDSQUARE:4>FN31<MODE:3>FT8<EOR>\n");
    QFile logFile(logPath);
    if (!logFile.open(QIODevice::WriteOnly) || logFile.write(logBody) != logBody.size())
      return fail("logbook sample did not write");
    logFile.close();
    const QString backupPath = dir.filePath(QStringLiteral("std-logbook-backup.adi"));
    QString fileError;
    if (!backupLogbookFile(logPath, backupPath, &fileError) ||
        QFile(backupPath).size() != logBody.size())
      return fail("logbook backup did not copy");
    if (backupLogbookFile(logPath, logPath, &fileError))
      return fail("logbook backup replaced itself");
    if (backupLogbookFile(dir.filePath(QStringLiteral("missing.adi")), backupPath, &fileError))
      return fail("logbook backup invented a file");
    const QString exportPath = dir.filePath(QStringLiteral("std-logbook-export.adi"));
    int records = -1;
    if (!exportAdifForLogger(logPath, exportPath, &records, &fileError) || records != 1)
      return fail("ADIF export missed the QSO");
    QFile exported(exportPath);
    if (!exported.open(QIODevice::ReadOnly))
      return fail("ADIF export did not write");
    const QByteArray exportedBody = exported.readAll();
    if (!exportedBody.contains("<EOH>") || !exportedBody.contains("<CALL:4>W1AW") ||
        !exportedBody.contains("<EOR>") || !exportedBody.contains("<PROGRAMID:3>STD"))
      return fail("ADIF export is not a logger file");
    if (QFile(logPath).size() != logBody.size())
      return fail("ADIF export changed the logbook");
  }

  const auto* chip = window.findChild<QPushButton*>("digitalModeChip");
  if (!chip || !chip->isEnabled() || chip->text() != QLatin1String("FT8") ||
      !chip->toolTip().contains(QLatin1String("does not key"), Qt::CaseInsensitive))
    return fail("digital mode chip is missing");
  const auto* hellMenu = window.findChild<QMenu*>(QStringLiteral("modeHellMenu"));
  const char* hellNames[] = {"Feld Hell", "Slow Hell", "Feld Hell X5", "Feld Hell X9",
                             "FSK Hell", "FSK Hell-105", "Hell 80"};
  if (!hellMenu || hellMenu->actions().size() != 7)
    return fail("Hell submenu is missing");
  for (const char* name : hellNames) {
    bool found = false;
    for (const QAction* action : hellMenu->actions()) {
      if (action->text() == QLatin1String(name) && action->isCheckable() &&
          action->toolTip().contains(QLatin1String("does not key")) &&
          action->toolTip().contains(QLatin1String("START")))
        found = true;
    }
    if (!found)
      return fail("a Hell mode is missing");
  }
  const auto* hellText = window.findChild<QPlainTextEdit*>(QStringLiteral("txHell"));
  const auto* wfAfc = window.findChild<QPushButton*>(QStringLiteral("wfAfc"));
  const auto* wfSql = window.findChild<QPushButton*>(QStringLiteral("wfSql"));
  if (!hellText || !hellText->isHidden() || !wfAfc || wfAfc->isEnabled() || !wfSql || wfSql->isEnabled() ||
      !wfAfc->toolTip().contains(QLatin1String("does not key")) ||
      !wfSql->toolTip().contains(QLatin1String("START")))
    return fail("Hell transmit pane or waterfall AFC SQL is missing");
  const auto* rxEcho = window.findChild<QTextEdit*>(QStringLiteral("rxEcho"));
  const auto* clrRx = window.findChild<QPushButton*>(QStringLiteral("clrRx"));
  const auto* clrTx = window.findChild<QPushButton*>(QStringLiteral("clrTx"));
  const auto* macroTx = window.findChild<QPushButton*>(QStringLiteral("macroTx"));
  if (!rxEcho || !rxEcho->isReadOnly() || !rxEcho->isHidden() || !clrRx || !clrRx->isHidden() || !clrTx ||
      !clrTx->isHidden())
    return fail("text echo is showing on FT8");
  if (!rxEcho->toolTip().contains(QLatin1String("does not key")) ||
      !rxEcho->toolTip().contains(QLatin1String("START")) ||
      !clrRx->toolTip().contains(QLatin1String("does not key")) ||
      !clrRx->toolTip().contains(QLatin1String("START")) ||
      !clrTx->toolTip().contains(QLatin1String("does not key")) ||
      !clrTx->toolTip().contains(QLatin1String("START")))
    return fail("text echo tooltip is wrong");
  if (!macroTx || macroTx->isChecked() || !macroTx->toolTip().contains(QLatin1String("does not key")) ||
      !macroTx->toolTip().contains(QLatin1String("START")))
    return fail("TX macro chip is wrong");
  const auto* rtty = window.findChild<QAction*>(QStringLiteral("modeRtty"));
  const auto* psk = window.findChild<QAction*>(QStringLiteral("modePsk31"));
  if (!rtty || !psk || !rtty->isCheckable() || !psk->isCheckable() ||
      !rtty->toolTip().contains(QLatin1String("START")) ||
      !rtty->toolTip().contains(QLatin1String("Mark")) ||
      rtty->toolTip().contains(QLatin1String("does not key yet")) ||
      !psk->toolTip().contains(QLatin1String("START")) ||
      !psk->toolTip().contains(QLatin1String("31.25")) ||
      psk->toolTip().contains(QLatin1String("does not key yet")))
    return fail("RTTY or PSK31 is missing");
  const auto* rttyRow = window.findChild<QWidget*>(QStringLiteral("rttyOptions"));
  const auto* rttyShift = window.findChild<QComboBox*>(QStringLiteral("rttyShift"));
  const auto* rttyBaud = window.findChild<QComboBox*>(QStringLiteral("rttyBaud"));
  const auto* rttyShape = window.findChild<QCheckBox*>(QStringLiteral("rttyShape"));
  const auto* rttyReverse = window.findChild<QCheckBox*>(QStringLiteral("rttyReverse"));
  const auto* rttyIta2 = window.findChild<QCheckBox*>(QStringLiteral("rttyIta2"));
  if (!rttyRow || !rttyRow->isHidden() || !rttyShift || !rttyBaud || !rttyShape || !rttyReverse || !rttyIta2)
    return fail("RTTY row is missing");
  if (rttyShift->findText(QStringLiteral("170"), Qt::MatchExactly) < 0 ||
      rttyShift->findText(QStringLiteral("23"), Qt::MatchExactly) < 0 ||
      rttyShift->findText(QStringLiteral("850"), Qt::MatchExactly) < 0 ||
      rttyBaud->findText(QStringLiteral("45.45"), Qt::MatchExactly) < 0 ||
      rttyBaud->findText(QStringLiteral("45"), Qt::MatchExactly) < 0 ||
      rttyBaud->findText(QStringLiteral("300"), Qt::MatchExactly) < 0)
    return fail("RTTY baud or shift list is wrong");
  if (!rttyShape->toolTip().contains(QLatin1String("does not key")) ||
      !rttyShape->toolTip().contains(QLatin1String("START")) ||
      !rttyReverse->toolTip().contains(QLatin1String("does not key")) ||
      !rttyIta2->toolTip().contains(QLatin1String("START")) ||
      !rttyShift->toolTip().contains(QLatin1String("does not key")) ||
      !rttyBaud->toolTip().contains(QLatin1String("45.45")))
    return fail("RTTY row tooltip is wrong");
  if (!keyboardTextMode(QStringLiteral("Feld Hell")) || !keyboardTextMode(QStringLiteral("RTTY")) ||
      !keyboardTextMode(QStringLiteral("PSK31")) || !keyboardTextMode(QStringLiteral("BPSK63")) ||
      !keyboardTextMode(QStringLiteral("QPSK31")) ||       !keyboardTextMode(QStringLiteral("Olivia 8/250")) ||
      !keyboardTextMode(QStringLiteral("Contestia 8/250")) ||
      !keyboardTextMode(QStringLiteral("MFSK16")) ||
      !keyboardTextMode(QStringLiteral("DominoEX 16")) ||
      !keyboardTextMode(QStringLiteral("Thor 16")) || !keyboardTextMode(QStringLiteral("JS8")) ||
      keyboardTextMode(QStringLiteral("WSPR")) ||
      keyboardTextMode(QStringLiteral("FT8")) ||
      keyboardTextMode(QStringLiteral("FT4")) || keyboardTextMode(QStringLiteral("FT2")) ||
      keyboardTextMode(QStringLiteral("JT65A")) || keyboardTextMode(QStringLiteral("Q65A")) ||
      keyboardTextMode(QStringLiteral("CW")))
    return fail("keyboard text mode list is wrong");
  if (textModeSamplesPerChar(QStringLiteral("Feld Hell")) != 0 ||
      textModeSamplesPerChar(QStringLiteral("Hell 80")) != 0 ||
      textModeSamplesPerChar(QStringLiteral("PSK31")) != 0 ||
      textModeSamplesPerChar(QStringLiteral("RTTY")) != 0 ||
      textModeSamplesPerChar(QStringLiteral("Olivia 8/250")) != 0 ||
      textModeSamplesPerChar(QStringLiteral("FT8")) != 0)
    return fail("character length is wrong");
  if (hellFirstInkRow(QLatin1Char('L'), 1) != 3)
    return fail("Hell transmit polarity is inverted");
  if (hellColumnRate(QStringLiteral("Feld Hell")) != 17.5 ||
      hellColumnRate(QStringLiteral("Slow Hell")) != 2.1875 ||
      hellColumnRate(QStringLiteral("Feld Hell X5")) != 87.5 ||
      hellColumnRate(QStringLiteral("Hell 80")) != 35.0)
    return fail("Hell column rate is wrong");
  std::vector<float> feldWave;
  std::vector<float> slowWave;
  std::vector<float> fastWave;
  std::vector<float> fskWave;
  std::vector<int> hellEcho;
  if (!buildTxWave(QStringLiteral("Feld Hell"), QStringLiteral("E"), 1500, &feldWave, &hellEcho) ||
      hellEcho.size() != 1 ||
      !buildTxWave(QStringLiteral("Slow Hell"), QStringLiteral("E"), 1500, &slowWave, nullptr) ||
      !buildTxWave(QStringLiteral("Feld Hell X5"), QStringLiteral("E"), 1500, &fastWave, nullptr) ||
      !buildTxWave(QStringLiteral("FSK Hell"), QStringLiteral("E"), 1500, &fskWave, nullptr))
    return fail("Hell modulator is missing");
  int hellInk = 0;
  for (; hellInk < int(feldWave.size()); ++hellInk) {
    if (std::fabs(feldWave[static_cast<size_t>(hellInk)]) > 0.05f)
      break;
  }
  if (hellInk < 2000)
    return fail("Feld Hell has no blank column ahead of the dots");
  float early = 0.0f;
  float later = 0.0f;
  for (int i = hellInk; i < hellInk + 48 && i < int(feldWave.size()); ++i)
    early = std::max(early, std::fabs(feldWave[static_cast<size_t>(i)]));
  for (int i = hellInk + 192; i < hellInk + 288 && i < int(feldWave.size()); ++i)
    later = std::max(later, std::fabs(feldWave[static_cast<size_t>(i)]));
  if (early > 0.45f || later < 0.5f)
    return fail("Feld Hell rise is not the 2 ms raised cosine");
  const double slowRatio = double(slowWave.size()) / double(feldWave.size());
  const double fastRatio = double(feldWave.size()) / double(fastWave.size());
  if (slowRatio < 7.9 || slowRatio > 8.1 || fastRatio < 4.9 || fastRatio > 5.1)
    return fail("Hell family rates do not match Fldigi");
  {
    // Slow Hell is Feld Hell at one eighth the speed. With full trailing
    // pixels the same text carries about 10 times the keyed energy (the
    // 2 ms edges cost Feld Hell more). Fldigi's 4 ms trailing pixel gives 6.9.
    double feldEnergy = 0.0;
    double slowEnergy = 0.0;
    for (float v : feldWave)
      feldEnergy += double(v) * v;
    for (float v : slowWave)
      slowEnergy += double(v) * v;
    const double energyRatio = feldEnergy > 0.0 ? slowEnergy / feldEnergy : 0.0;
    if (energyRatio < 9.5 || energyRatio > 10.8) {
      qCritical("Slow Hell energy ratio %.2f", energyRatio);
      return fail("Slow Hell trailing pixels are cut short");
    }
  }
  // Loopback: every Hell mode, transmitted at 48 kHz, prints with clear ink
  // and paper through the receive path at that mode's Fldigi filter width.
  for (const char* name : hellNames) {
    const QString mode = QString::fromLatin1(name);
    std::vector<float> wave;
    if (!buildTxWave(mode, QStringLiteral("HI HI"), 1500, &wave, nullptr))
      return fail("Hell loopback did not build");
    HellEngine engine;
    engine.setColumnRate(hellColumnRate(mode));
    engine.setFskShiftHz(hellFskShiftHz(mode));
    engine.setToneHz(1500);
    engine.setArmed(true);
    int crisp = 0;
    long gray = 0;
    long total = 0;
    QObject::connect(&engine, &HellEngine::columnReady, [&crisp, &gray, &total](const QByteArray& column) {
      int lo = 255;
      int hi = 0;
      for (int i = column.size() / 2; i < column.size(); ++i) {
        const int v = static_cast<unsigned char>(column.at(i));
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        if (v > 64 && v < 192) ++gray;
        ++total;
      }
      if (lo < 64 && hi > 192)
        ++crisp;
    });
    for (size_t at = 0; at < wave.size(); at += 4800) {
      const int n = int(std::min<size_t>(4800, wave.size() - at));
      engine.pushAudio(wave.data() + at, n, 48000);
    }
    // X5 and X9 smear to about 23% gray through a 245 Hz filter.
    if (crisp < 25 || gray * 100 > total * 12) {
      qCritical("Hell loopback %s crisp columns %d gray %ld of %ld", name, crisp, gray, total);
      return fail("Hell loopback does not print");
    }
  }
  for (int i = 0; i + 64 < int(fskWave.size()); i += 64) {
    float peak = 0.0f;
    for (int j = 0; j < 64; ++j)
      peak = std::max(peak, std::fabs(fskWave[static_cast<size_t>(i + j)]));
    if (peak < 0.2f)
      return fail("FSK Hell is on-off instead of two tones");
  }
  if (hellFskShiftHz(QStringLiteral("FSK Hell")) != 122.5 ||
      hellFskShiftHz(QStringLiteral("FSK Hell-105")) != 55.0 ||
      hellFskShiftHz(QStringLiteral("Hell 80")) != 300.0 ||
      hellFskShiftHz(QStringLiteral("Feld Hell")) != 0.0)
    return fail("FSK Hell shift is wrong");
  auto countInk = [](const std::vector<float>& wave, double shift) {
    HellEngine tape;
    tape.setColumnRate(17.5);
    tape.setFskShiftHz(shift);
    tape.setToneHz(1500);
    int dark = 0;
    int light = 0;
    QObject::connect(&tape, &HellEngine::columnReady, [&](const QByteArray& column) {
      for (unsigned char pixel : column) {
        if (pixel < 40)
          ++dark;
        if (pixel > 200)
          ++light;
      }
    });
    tape.setArmed(true);
    tape.pushAudio(wave.data(), static_cast<int>(wave.size()), 48000);
    return std::pair<int, int>(dark, light);
  };
  const auto fskInk = countInk(fskWave, 122.5);
  const auto ampInk = countInk(fskWave, 0.0);
  if (fskInk.first < 20 || fskInk.second < 20)
    return fail("FSK Hell tape did not print both tones");
  if (ampInk.first > 5 && ampInk.second > 5)
    return fail("amplitude detector printed an FSK Hell tape");
  if (hellTapeRowFromTop(0, 40) != 39 || hellTapeRowFromTop(39, 40) != 0)
    return fail("Hell tape polarity is inverted");
  const auto* mode = window.findChild<QLabel*>("modeValue");
  const auto* afc = window.findChild<QPushButton*>("afcToggle");
  const auto* squelch = window.findChild<QPushButton*>("squelchToggle");
  const auto* squelchLevel = window.findChild<QSpinBox*>("squelchLevel");
  if (!mode || !afc || !squelch || !squelchLevel)
    return fail("status readouts are missing");
  if (afc->isEnabled() || afc->isChecked() || afc->text() != QLatin1String("Off") ||
      !afc->toolTip().contains(QLatin1String("does not key")) ||
      !afc->toolTip().contains(QLatin1String("START")))
    return fail("AFC is not off");
  if (squelch->isEnabled() || !squelchLevel->isHidden() ||
      !squelch->toolTip().contains(QLatin1String("does not key")) ||
      !squelch->toolTip().contains(QLatin1String("START")))
    return fail("FT8 squelch is not idle");
  if (auto* strip = window.findChild<StatusStrip*>(QStringLiteral("statusStrip"))) {
    strip->setDigitalMode(QStringLiteral("Hell"));
    if (!squelch->isEnabled() || squelchLevel->isHidden() || afc->isEnabled() ||
        !afc->toolTip().contains(QLatin1String("Feld Hell")) ||
        !squelch->toolTip().contains(QLatin1String("does not key")))
      return fail("Hell squelch is not available");
    strip->setDigitalMode(QStringLiteral("RTTY"));
    if (!afc->isEnabled() || !squelch->isEnabled() || squelchLevel->isHidden() ||
        !afc->toolTip().contains(QLatin1String("does not key")) ||
        !afc->toolTip().contains(QLatin1String("START")) ||
        !squelch->toolTip().contains(QLatin1String("RTTY")))
      return fail("RTTY AFC or squelch is not available");
    strip->setDigitalMode(QStringLiteral("PSK31"));
    if (!afc->isEnabled() || !squelch->isEnabled() ||
        !squelch->toolTip().contains(QLatin1String("PSK")))
      return fail("PSK AFC or squelch is not available");
    strip->setDigitalMode(QStringLiteral("FT8"));
    if (squelch->isEnabled() || !squelchLevel->isHidden() ||
        !window.findChild<QPushButton*>(QStringLiteral("digitalModeChip")) ||
        window.findChild<QPushButton*>(QStringLiteral("digitalModeChip"))->text() !=
            QLatin1String("FT8"))
      return fail("FT8 squelch stayed on");
  }

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

  if (!window.findChild<QAction*>("radioConnect"))
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
  for (const QPushButton* button : bands) {
    const QString tip = button->toolTip();
    if (!tip.contains(QStringLiteral("DIGU")) || tip.contains(QStringLiteral("DIGL")))
      return fail("band sideband is not DIGU");
  }
  const auto* region = window.findChild<QComboBox*>("profileRegion");
  if (!region || region->count() < 4 || !region->itemText(0).contains(QStringLiteral("Auto")))
    return fail("region control is missing");
  const auto* extra = window.findChild<QSpinBox*>("profileExtra");
  if (!extra || extra->minimum() != 0 || !extra->toolTip().contains(QStringLiteral("DIGU")) ||
      !extra->toolTip().contains(QStringLiteral("does not key"), Qt::CaseInsensitive))
    return fail("extra frequency is missing");
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
  const auto* namedLayouts = window.findChild<QAction*>("namedLayouts");
  if (!namedLayouts || !namedLayouts->toolTip().contains(QStringLiteral("does not key"), Qt::CaseInsensitive) ||
      !namedLayouts->toolTip().contains(QStringLiteral("START")))
    return fail("named layouts is missing");

  const auto* queueEntry = window.findChild<QLineEdit*>("txQueueEntry");
  const auto* queueList = window.findChild<QWidget*>("txQueueList");
  if (!queueEntry || !queueList)
    return fail("queue entry is missing");

  const QByteArray beat = QsoLog::heartbeatDatagram(QStringLiteral("0"));
  if (beat.size() < 12 || static_cast<unsigned char>(beat[0]) != 0xad ||
      static_cast<unsigned char>(beat[1]) != 0xbc ||
      static_cast<unsigned char>(beat[2]) != 0xcb ||
      static_cast<unsigned char>(beat[3]) != 0xda)
    return fail("logger datagram is not WSJT-X");

  std::vector<float> ft8;
  if (!buildTxWave(QStringLiteral("FT8"), QStringLiteral("CQ N8SDR EM79"), 1500, &ft8) ||
      !ft8TxMatchesMshv(ft8, 1500))
    return fail("FT8 encoder is not MSHV 8-FSK");

  auto tonal = [](const std::vector<float>& wave) {
    int n = int(wave.size());
    while (n > 0 && wave[static_cast<size_t>(n - 1)] == 0.0f)
      --n;
    return n;
  };
  std::vector<float> ft4;
  if (!buildTxWave(QStringLiteral("FT4"), QStringLiteral("CQ N8SDR EM79"), 1500, &ft4))
    return fail("FT4 encoder is missing");
  const int ft4Tone = tonal(ft4);
  // genft4 writes 241920 tone samples (5.04 s) and then silence. The 7.5 s
  // slot unkeys 1.17 s early, so the tone has to finish before that.
  if (ft4Tone < 230000 || ft4Tone > 250000 || ft4Tone * 1000 / 48000 > 6330)
    return fail("FT4 waveform does not fit 7.5 s");
  std::vector<float> ft2;
  if (!buildTxWave(QStringLiteral("FT2"), QStringLiteral("CQ N8SDR EM79"), 1500, &ft2))
    return fail("FT2 encoder is missing");
  const int ft2Tone = tonal(ft2);
  // genft2 writes 120960 tone samples (2.52 s). The 3.75 s slot unkeys 0.77 s early.
  if (ft2Tone < 110000 || ft2Tone > 130000 || ft2Tone * 1000 / 48000 > 2980)
    return fail("FT2 waveform does not fit 3.75 s");
  const auto* jt65a = window.findChild<QAction*>(QStringLiteral("modeJt65A"));
  const auto* q65a = window.findChild<QAction*>(QStringLiteral("modeQ65A"));
  if (!jt65a || !jt65a->toolTip().contains(QLatin1String("1270")) ||
      !jt65a->toolTip().contains(QLatin1String("60")) ||
      !jt65a->toolTip().contains(QLatin1String("START")) ||
      !jt65a->toolTip().contains(QLatin1String("does not key")) || !q65a ||
      !q65a->toolTip().contains(QLatin1String("30")) ||
      !q65a->toolTip().contains(QLatin1String("START")) ||
      !q65a->toolTip().contains(QLatin1String("does not key")))
    return fail("JT65 or Q65 is missing from the menu");
  for (const char* name : {"JT65A", "JT65B", "JT65C"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("CQ N8SDR EM79"), 1500, &wave) ||
        wave.size() < 48000)
      return fail("JT65 encoder is missing");
  }
  for (const char* name : {"Q65A", "Q65B", "Q65C", "Q65D"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("CQ N8SDR EM79"), 1500, &wave) ||
        wave.size() < 48000)
      return fail("Q65 encoder is missing");
  }

  auto tonePower = [](const std::vector<float>& wave, int begin, int end, double hz) {
    double si = 0.0;
    double sq = 0.0;
    double phase = 0.0;
    const double step = 2.0 * 3.141592653589793 * hz / 48000.0;
    const int n0 = begin < 0 ? 0 : begin;
    const int n1 = end > int(wave.size()) ? int(wave.size()) : end;
    for (int i = n0; i < n1; ++i) {
      const double sample = wave[static_cast<size_t>(i)];
      si += sample * std::sin(phase);
      sq += sample * std::cos(phase);
      phase += step;
    }
    return si * si + sq * sq;
  };
  std::vector<float> rttyWave;
  std::vector<int> rttyEcho;
  RttyOptions hardRtty;
  hardRtty.shaped = false;
  if (!buildTxWave(QStringLiteral("RTTY"), QStringLiteral("E"), 1500, &rttyWave, &rttyEcho, &hardRtty) ||
      rttyWave.size() < 3200 || rttyEcho.empty())
    return fail("RTTY modulator is missing");
  // Two mark bits of idle, then the LTRS start bit, which is space.
  const double markHi = tonePower(rttyWave, 0, 2000, 1585.0);
  const double markLo = tonePower(rttyWave, 0, 2000, 1415.0);
  if (!(markHi > markLo * 4.0))
    return fail("RTTY mark is not the higher tone");
  const double spaceHi = tonePower(rttyWave, 2112, 3100, 1585.0);
  const double spaceLo = tonePower(rttyWave, 2112, 3100, 1415.0);
  if (!(spaceLo > spaceHi * 4.0))
    return fail("RTTY space is not the lower tone");
  const QString rttyBack = rttyDecode(rttyWave.data(), int(rttyWave.size()), 48000, 1500);
  if (!rttyBack.contains(QLatin1Char('E'))) {
    qCritical("RTTY decode [%s]", qPrintable(rttyBack));
    return fail("RTTY did not decode");
  }
  {
    // Item 4: squelch on, noise only, must not print. The real wave still must.
    std::vector<float> noise(48000 * 10);
    unsigned noiseState = 1;
    for (float& noiseSample : noise) {
      noiseState = noiseState * 1664525u + 1013904223u;
      noiseSample = (float((noiseState >> 16) & 65535u) / 32768.0f - 1.0f) * 0.25f;
    }
    const QString junk = rttyDecode(noise.data(), int(noise.size()), 48000, 1500, true, 10);
    if (!junk.isEmpty()) {
      qCritical("RTTY noise [%s]", qPrintable(junk));
      return fail("RTTY squelch printed noise");
    }
    const QString heard =
        rttyDecode(rttyWave.data(), int(rttyWave.size()), 48000, 1500, true, 10);
    if (!heard.contains(QLatin1Char('E')))
      return fail("RTTY squelch blocked a signal");
    const QString tracked =
        rttyDecode(rttyWave.data(), int(rttyWave.size()), 48000, 1500, false, 10, true);
    if (!tracked.contains(QLatin1Char('E'))) {
      qCritical("RTTY AFC [%s]", qPrintable(tracked));
      return fail("RTTY AFC walked off the signal");
    }
  }
  {
    std::vector<float> shaped;
    if (!buildTxWave(QStringLiteral("RTTY"), QStringLiteral("E"), 1500, &shaped, nullptr) ||
        shaped.size() < 3200)
      return fail("shaped RTTY is missing");
    const QString shapedBack = rttyDecode(shaped.data(), int(shaped.size()), 48000, 1500);
    if (!shapedBack.contains(QLatin1Char('E'))) {
      qCritical("shaped RTTY [%s]", qPrintable(shapedBack));
      return fail("shaped RTTY did not decode");
    }
    RttyOptions reversed;
    reversed.shaped = false;
    reversed.reverse = true;
    std::vector<float> revWave;
    if (!buildTxWave(QStringLiteral("RTTY"), QStringLiteral("E"), 1500, &revWave, nullptr, &reversed) ||
        revWave.size() < 3200)
      return fail("reversed RTTY is missing");
    const double revHi = tonePower(revWave, 0, 2000, 1585.0);
    const double revLo = tonePower(revWave, 0, 2000, 1415.0);
    if (!(revLo > revHi * 4.0))
      return fail("reversed RTTY mark is not the lower tone");
    const double revSpaceHi = tonePower(revWave, 2112, 3100, 1585.0);
    const double revSpaceLo = tonePower(revWave, 2112, 3100, 1415.0);
    if (!(revSpaceHi > revSpaceLo * 4.0))
      return fail("reversed RTTY space is not the higher tone");
    const QString revBack =
        rttyDecode(revWave.data(), int(revWave.size()), 48000, 1500, false, 10, false, reversed);
    if (!revBack.contains(QLatin1Char('E'))) {
      qCritical("reversed RTTY [%s]", qPrintable(revBack));
      return fail("reversed RTTY did not decode");
    }
    RttyOptions ita2;
    ita2.shaped = false;
    ita2.ita2 = true;
    std::vector<float> plusWave;
    if (!buildTxWave(QStringLiteral("RTTY"), QStringLiteral("+"), 1500, &plusWave, nullptr, &ita2) ||
        plusWave.size() < 3200)
      return fail("ITA2 RTTY is missing");
    const QString itaBack =
        rttyDecode(plusWave.data(), int(plusWave.size()), 48000, 1500, false, 10, false, ita2);
    if (!itaBack.contains(QLatin1Char('+'))) {
      qCritical("ITA2 [%s]", qPrintable(itaBack));
      return fail("ITA2 plus did not decode");
    }
    const QString usBack = rttyDecode(plusWave.data(), int(plusWave.size()), 48000, 1500);
    if (!usBack.contains(QLatin1Char('"')) || usBack.contains(QLatin1Char('+'))) {
      qCritical("US figures [%s]", qPrintable(usBack));
      return fail("ITA2 plus decoded as US figures");
    }
    RttyOptions other;
    other.shaped = false;
    other.baud = 75;
    other.shiftHz = 200;
    std::vector<float> otherWave;
    if (!buildTxWave(QStringLiteral("RTTY"), QStringLiteral("E"), 1500, &otherWave, nullptr, &other) ||
        otherWave.size() < 2000)
      return fail("75 baud RTTY is missing");
    const QString otherBack =
        rttyDecode(otherWave.data(), int(otherWave.size()), 48000, 1500, false, 10, false, other);
    if (!otherBack.contains(QLatin1Char('E'))) {
      qCritical("75 baud [%s]", qPrintable(otherBack));
      return fail("75 baud 200 Hz RTTY did not decode");
    }
  }
  std::vector<float> pskWave;
  if (!buildTxWave(QStringLiteral("PSK31"), QStringLiteral("E"), 1500, &pskWave) || pskWave.size() < 1000)
    return fail("PSK31 modulator is missing");
  const double pskOn = tonePower(pskWave, 0, int(pskWave.size()), 1500.0);
  const double pskOff = tonePower(pskWave, 0, int(pskWave.size()), 1000.0);
  if (!(pskOn > pskOff * 4.0))
    return fail("PSK31 carrier is not on the cursor");
  const QString pskBack =
      pskDecode(QStringLiteral("PSK31"), pskWave.data(), int(pskWave.size()), 48000, 1500);
  if (!pskBack.contains(QLatin1Char('E')))
    return fail("PSK31 did not decode");
  {
    // Item 11: squelch on, noise only, must not print. The real wave still must.
    std::vector<float> noise(48000 * 10);
    unsigned noiseState = 1;
    for (float& noiseSample : noise) {
      noiseState = noiseState * 1664525u + 1013904223u;
      noiseSample = (float((noiseState >> 16) & 65535u) / 32768.0f - 1.0f) * 0.25f;
    }
    const QString junk =
        pskDecode(QStringLiteral("PSK31"), noise.data(), int(noise.size()), 48000, 1500, true, 10);
    if (!junk.isEmpty()) {
      qCritical("PSK noise [%s]", qPrintable(junk));
      return fail("PSK squelch printed noise");
    }
    const QString heard =
        pskDecode(QStringLiteral("PSK31"), pskWave.data(), int(pskWave.size()), 48000, 1500, true, 10);
    if (!heard.contains(QLatin1Char('E')))
      return fail("PSK squelch blocked a signal");
    const QString tracked = pskDecode(QStringLiteral("PSK31"), pskWave.data(), int(pskWave.size()),
                                      48000, 1500, false, 10, true);
    if (!tracked.contains(QLatin1Char('E'))) {
      qCritical("PSK AFC [%s]", qPrintable(tracked));
      return fail("PSK AFC walked off the signal");
    }
    const QString accent = QString(QChar(0x00E9));
    std::vector<float> accentWave;
    if (!buildTxWave(QStringLiteral("PSK31"), accent, 1500, &accentWave) || accentWave.size() < 1000)
      return fail("PSK extended character was dropped");
    const QString accentBack =
        pskDecode(QStringLiteral("PSK31"), accentWave.data(), int(accentWave.size()), 48000, 1500);
    if (!accentBack.contains(QChar(0x00E9))) {
      qCritical("PSK latin1 [%s]", qPrintable(accentBack));
      return fail("PSK extended character did not decode");
    }
  }
  for (const char* baudName : {"PSK63", "PSK125", "PSK250", "PSK500", "PSK1000"}) {
    std::vector<float> faster;
    if (!buildTxWave(QString::fromLatin1(baudName), QStringLiteral("E"), 1500, &faster) ||
        faster.size() < 1000)
      return fail("BPSK modulator is missing");
    const QString back =
        pskDecode(QString::fromLatin1(baudName), faster.data(), int(faster.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E')))
      return fail("BPSK did not decode");
  }
  const auto* psk63 = window.findChild<QAction*>(QStringLiteral("modePsk63"));
  const auto* fax = window.findChild<QAction*>(QStringLiteral("modeWefax576"));
  if (!psk63 || !psk63->toolTip().contains(QLatin1String("62.5")) ||
      !psk63->toolTip().contains(QLatin1String("START")) ||
      psk63->toolTip().contains(QLatin1String("does not key yet")) || !fax ||
      !fax->toolTip().contains(QLatin1String("does not key")) ||
      !fax->toolTip().contains(QLatin1String("START")) ||
      !fax->toolTip().contains(QLatin1String("576")))
    return fail("PSK63 or WEFAX is missing");
  const auto* olivia = window.findChild<QAction*>(QStringLiteral("modeOlivia8250"));
  const auto* contestia = window.findChild<QAction*>(QStringLiteral("modeContestia8250"));
  if (!olivia || !contestia || !olivia->toolTip().contains(QLatin1String("31.25")) ||
      !olivia->toolTip().contains(QLatin1String("START")) ||
      olivia->toolTip().contains(QLatin1String("does not key yet")) ||
      !contestia->toolTip().contains(QLatin1String("31.25")) ||
      !contestia->toolTip().contains(QLatin1String("START")) ||
      contestia->toolTip().contains(QLatin1String("does not key yet")))
    return fail("Olivia or Contestia is missing");
  for (const char* name : {"Olivia 8/250", "Contestia 8/250", "Olivia 16/500", "Contestia 4/250"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("Olivia modulator is missing");
    const QString back =
        oliviaDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("Olivia decode %s [%s]", name, qPrintable(back));
      return fail("Olivia did not decode");
    }
  }
  std::vector<float> oliviaCq;
  if (!buildTxWave(QStringLiteral("Olivia 8/250"), QStringLiteral("CQ"), 1500, &oliviaCq))
    return fail("Olivia modulator is missing");
  const QString cqBack = oliviaDecode(QStringLiteral("Olivia 8/250"), oliviaCq.data(),
                                      int(oliviaCq.size()), 48000, 1500);
  if (!cqBack.contains(QLatin1String("CQ"))) {
    qCritical("Olivia CQ decode [%s]", qPrintable(cqBack));
    return fail("Olivia did not decode");
  }
  for (const char* name : {"MFSK16", "MFSK31"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("MFSK modulator is missing");
    const QString back =
        mfskDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("MFSK decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("MFSK did not decode");
    }
  }
  const auto* mfsk16 = window.findChild<QAction*>(QStringLiteral("modeMfsk16"));
  if (!mfsk16 || !mfsk16->toolTip().contains(QLatin1String("15.625")) ||
      !mfsk16->toolTip().contains(QLatin1String("START")) ||
      mfsk16->toolTip().contains(QLatin1String("does not key yet")))
    return fail("MFSK16 is missing");
  for (const char* name : {"DominoEX 16", "DominoEX 8"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("DominoEX modulator is missing");
    const QString back =
        dominoDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("DominoEX decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("DominoEX did not decode");
    }
  }
  std::vector<float> dominoCq;
  if (!buildTxWave(QStringLiteral("DominoEX 16"), QStringLiteral("CQ"), 1500, &dominoCq))
    return fail("DominoEX modulator is missing");
  const QString dominoCqBack =
      dominoDecode(QStringLiteral("DominoEX 16"), dominoCq.data(), int(dominoCq.size()), 48000, 1500);
  if (!dominoCqBack.contains(QLatin1String("CQ"))) {
    qCritical("DominoEX CQ decode [%s]", qPrintable(dominoCqBack));
    return fail("DominoEX did not decode");
  }
  const auto* domino16 = window.findChild<QAction*>(QStringLiteral("modeDominoex16"));
  if (!domino16 || !domino16->toolTip().contains(QLatin1String("15.625")) ||
      !domino16->toolTip().contains(QLatin1String("START")) ||
      domino16->toolTip().contains(QLatin1String("does not key yet")))
    return fail("DominoEX 16 is missing");
  for (const char* name : {"Thor 16", "Thor 8"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("Thor modulator is missing");
    const QString back =
        thorDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("Thor decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("Thor did not decode");
    }
  }
  std::vector<float> thorCq;
  if (!buildTxWave(QStringLiteral("Thor 16"), QStringLiteral("CQ"), 1500, &thorCq))
    return fail("Thor modulator is missing");
  const QString thorCqBack =
      thorDecode(QStringLiteral("Thor 16"), thorCq.data(), int(thorCq.size()), 48000, 1500);
  if (!thorCqBack.contains(QLatin1String("CQ"))) {
    qCritical("Thor CQ decode [%s]", qPrintable(thorCqBack));
    return fail("Thor did not decode");
  }
  const auto* thor16 = window.findChild<QAction*>(QStringLiteral("modeThor16"));
  if (!thor16 || !thor16->toolTip().contains(QLatin1String("15.625")) ||
      !thor16->toolTip().contains(QLatin1String("START")) ||
      thor16->toolTip().contains(QLatin1String("does not key yet")))
    return fail("Thor 16 is missing");
  for (const char* name : {"QPSK31", "QPSK63", "QPSK125", "QPSK250", "QPSK500"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("QPSK modulator is missing");
    const QString back =
        pskDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("QPSK decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("QPSK did not decode");
    }
  }
  const auto* qpsk31 = window.findChild<QAction*>(QStringLiteral("modeQpsk31"));
  if (!qpsk31 || !qpsk31->toolTip().contains(QLatin1String("31.25")) ||
      !qpsk31->toolTip().contains(QLatin1String("START")) ||
      qpsk31->toolTip().contains(QLatin1String("does not key yet")))
    return fail("QPSK31 is missing");
  for (const char* name : {"MFSK64L", "MFSK128L"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("MFSK long modulator is missing");
    const QString back =
        mfskDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("MFSK decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("MFSK long did not decode");
    }
  }
  for (const char* name : {"DominoEX Micro"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("DominoEX Micro modulator is missing");
    const QString back =
        dominoDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("DominoEX decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("DominoEX Micro did not decode");
    }
  }
  for (const char* name : {"Thor 25x4", "Thor 50x1", "Thor 50x2", "Thor 100"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("Thor K=15 modulator is missing");
    const QString back =
        thorDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("Thor decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("Thor K=15 did not decode");
    }
  }
  {
    std::vector<float> wave;
    if (!buildTxWave(QStringLiteral("JS8"), QStringLiteral("E"), 1500, &wave) || wave.size() < 1000)
      return fail("JS8 modulator is missing");
    int js8Snr = -999;
    const QString back = js8Decode(wave.data(), int(wave.size()), 48000, 1500, &js8Snr);
    if (!back.contains(QLatin1Char('E'))) {
      qCritical("JS8 decode [%s] samples %d", qPrintable(back), int(wave.size()));
      return fail("JS8 did not decode");
    }
    if (js8Snr < 10)
      return fail("JS8 SNR is not measured");
    if (int(wave.size()) != js8FrameSamples())
      return fail("JS8 frame length is wrong");
    double lead = 0.0;
    for (int i = 0; i < 24000 && i < int(wave.size()); ++i)
      lead += std::fabs(wave[static_cast<std::size_t>(i)]);
    if (lead > 1.0)
      return fail("JS8 frame does not start with half a second of silence");
  }
  {
    const QString many(40, QLatin1Char('E'));
    int used = 0;
    const Js8Plan first = planJs8(many, 1500, 1, &used, true);
    if (int(first.samples.size()) != js8FrameSamples() || used < 8 || used >= many.size())
      return fail("JS8 is not one 15 second frame");
    const QString part = js8Decode(first.samples.data(), int(first.samples.size()), 48000, 1500);
    if (part != many.left(used))
      return fail("JS8 first part did not decode");
    int used2 = 0;
    const Js8Plan second = planJs8(many.mid(used), 1500, 1, &used2, false);
    if (second.samples.size() != first.samples.size() || used2 < 1)
      return fail("JS8 continuation frame is missing");
    const QString part2 = js8Decode(second.samples.data(), int(second.samples.size()), 48000, 1500);
    if (part2 != many.mid(used, used2))
      return fail("JS8 continuation did not decode");
    const Js8Plan whole = planJs8(many, 1500);
    if (int(whole.samples.size()) <= js8FrameSamples())
      return fail("JS8 dropped the rest of a long message");
    const QString all = js8Decode(whole.samples.data(), int(whole.samples.size()), 48000, 1500);
    if (all != many.left(used + used2) && all != many)
      return fail("JS8 long message did not decode");
  }
  {
    QSettings keep;
    const QVariant hellSaved = keep.value(QStringLiteral("tx/hellText"));
    auto* echo = window.findChild<QTextEdit*>(QStringLiteral("rxEcho"));
    auto* box = window.findChild<QPlainTextEdit*>(QStringLiteral("txHell"));
    auto* list = window.findChild<QPlainTextEdit*>(QStringLiteral("receiveText"));
    if (!echo || !box || !list)
      return fail("panes are missing");
    echo->setPlainText(QStringLiteral("OLD-RX"));
    {
      const QSignalBlocker block(box);
      box->setPlainText(QStringLiteral("OLD-TX"));
    }
    list->setPlainText(QStringLiteral("OLD-LIST"));
    auto* ft8Mode = window.findChild<QAction*>(QStringLiteral("modeFt8"));
    auto* rttyMode = window.findChild<QAction*>(QStringLiteral("modeRtty"));
    if (!ft8Mode || !rttyMode)
      return fail("mode actions are missing");
    ft8Mode->trigger();
    const bool sameKept = echo->toPlainText() == QLatin1String("OLD-RX") &&
                          box->toPlainText() == QLatin1String("OLD-TX") &&
                          list->toPlainText() == QLatin1String("OLD-LIST");
    rttyMode->trigger();
    const bool cleared = echo->toPlainText().isEmpty() && box->toPlainText().isEmpty() &&
                         list->toPlainText().isEmpty();
    ft8Mode->trigger();
    if (hellSaved.isValid())
      keep.setValue(QStringLiteral("tx/hellText"), hellSaved);
    else
      keep.remove(QStringLiteral("tx/hellText"));
    keep.sync();
    if (!sameKept)
      return fail("the same mode cleared the panes");
    if (!cleared)
      return fail("mode change kept the previous text");
  }
  const auto* js8 = window.findChild<QAction*>(QStringLiteral("modeJs8"));
  if (!js8 || !js8->toolTip().contains(QLatin1String("15")) ||
      !js8->toolTip().contains(QLatin1String("START")) ||
      js8->toolTip().contains(QLatin1String("does not key yet")))
    return fail("JS8 is missing");
  {
    std::vector<float> wave;
    if (!buildTxWave(QStringLiteral("WSPR"), QStringLiteral("N8SDR EM79 37"), 1500, &wave) ||
        wave.size() < 1000000)
      return fail("WSPR modulator is missing");
    const QString back = wsprDecode(wave.data(), int(wave.size()), 48000);
    if (back != QLatin1String("N8SDR EM79 37")) {
      qCritical("WSPR decode [%s] samples %d", qPrintable(back), int(wave.size()));
      return fail("WSPR did not decode");
    }
  }
  {
    // MSHV Multi Answering engine, both modes.
    MaEngine stdMa;
    stdMa.setStation(QStringLiteral("N8SDR"), QStringLiteral("EM79"));
    stdMa.setMode(MaEngine::Mode::Standard);
    stdMa.setSlotCount(1);
    stdMa.setQueueLimit(5);
    if (stdMa.message() != QLatin1String("CQ N8SDR EM79"))
      return fail("MA Std does not call CQ with an empty Now");
    stdMa.decoded(QStringLiteral("N8SDR K1ABC FN42"), -5, 1200, false);
    stdMa.decoded(QStringLiteral("N8SDR W9XYZ EN52"), -7, 1400, false);
    if (stdMa.now().size() != 1 || stdMa.queue().size() != 1 ||
        stdMa.message() != QLatin1String("K1ABC N8SDR -05"))
      return fail("MA Std did not work the first caller and queue the second");
    stdMa.decoded(QStringLiteral("N8SDR K1ABC R-10"), -5, 1200, false);
    if (stdMa.message() != QLatin1String("K1ABC N8SDR RR73"))
      return fail("MA Std did not answer R-report with RR73");
    stdMa.txStarted();
    const QVector<MaEngine::Logged> done = stdMa.txEnded();
    if (done.size() != 1 || done.front().call != QLatin1String("K1ABC") || done.front().rxRpt != QLatin1String("-10") ||
        stdMa.message() != QLatin1String("W9XYZ N8SDR -07") || !stdMa.queue().isEmpty())
      return fail("MA Std did not log the QSO and take the next caller from the queue");
    stdMa.decoded(QStringLiteral("N8SDR W9XYZ RR73"), -7, 1400, false);
    stdMa.txStarted();
    stdMa.txEnded();
    if (!stdMa.takeStop())
      return fail("MA Std with CNS off did not stop when Now emptied");

    MaEngine dx;
    dx.setStation(QStringLiteral("N8SDR"), QStringLiteral("EM79"));
    dx.setDigitalMode(QStringLiteral("FT8"));
    dx.setMode(MaEngine::Mode::Dxpedition);
    if (dx.maxSlots() != 6)
      return fail("MA DX FT8 is not six slots");
    dx.setRestricted(true);
    if (dx.maxSlots() != 1)
      return fail("MA DX is not one slot on a standard FT8 frequency");
    dx.setRestricted(false);
    dx.setSlotCount(3);
    dx.setQueueLimit(5);
    dx.setCqOnFreeSlot(true);
    dx.decoded(QStringLiteral("N8SDR K1ABC FN42"), -5, 1200, false);
    if (dx.message() != QLatin1String("K1ABC N8SDR -05#CQ N8SDR EM79"))
      return fail("MA DX did not put CQ on the free slot");
    dx.decoded(QStringLiteral("N8SDR W9XYZ EN52"), -7, 1400, false);
    if (dx.message() != QLatin1String("K1ABC N8SDR -05#W9XYZ N8SDR -07"))
      return fail("MA DX did not send one message per slot");
    std::vector<float> both;
    if (!buildTxWave(QStringLiteral("FT8"), dx.message(), 1500, &both) || both.size() < 600000)
      return fail("MA DX two-slot FT8 did not generate");

    MaEngine sm;
    sm.setStation(QStringLiteral("N8SDR"), QStringLiteral("EM79"));
    sm.setDigitalMode(QStringLiteral("FT8"));
    sm.setMode(MaEngine::Mode::Dxpedition);
    sm.setSlotCount(1);
    sm.setQueueLimit(5);
    sm.setTxSm(true);
    sm.decoded(QStringLiteral("N8SDR K1ABC FN42"), -5, 1200, false);
    sm.decoded(QStringLiteral("N8SDR W9XYZ EN52"), -7, 1400, false);
    sm.decoded(QStringLiteral("N8SDR K1ABC R-10"), -5, 1200, false);
    if (sm.message() != QLatin1String("K1ABC RR73; W9XYZ <N8SDR> -07"))
      return fail("MA DX TX RR73; next did not fold the next caller");
  }
  {
    // Hash values from the WSJT-X nhash.c, initval 146.
    if (wsprCallHash(QStringLiteral("K1ABC")) != 6521 || wsprCallHash(QStringLiteral("N8SDR")) != 7430 ||
        wsprCallHash(QStringLiteral("PJ4/K1ABC")) != 19735 || wsprCallHash(QStringLiteral("G4ABC/P")) != 587 ||
        wsprCallHash(QStringLiteral("ABCDEFGHIJKLM")) != 19339)
      return fail("WSPR callsign hash does not match WSJT-X");
    if (!wsprOsdSelfTest())
      return fail("WSPR OSD decoder did not repair the codeword");
    // WSPR Tx stays inside 1400 to 1600 Hz, as in WSJT-X and JTDX.
    if (wsprClampTxHz(1000) != 1400 || wsprClampTxHz(2500) != 1600 || wsprClampTxHz(1453) != 1453)
      return fail("WSPR Tx frequency is not held to 1400-1600 Hz");
    std::vector<float> wave;
    if (!buildTxWave(QStringLiteral("WSPR"), QStringLiteral("K1JT FN20 30"), 1453, &wave) ||
        wave.size() < 1000000)
      return fail("WSPR modulator is missing");
    // About -22 dB in 2500 Hz, the way wsprd reports SNR.
    std::mt19937 rng(20260502u);
    std::normal_distribution<float> gauss(0.0f, 22.0f);
    std::vector<float> noisy(size_t(48000) * 114, 0.0f);
    for (size_t i = 0; i < noisy.size(); ++i)
      noisy[i] = (i < wave.size() ? wave[i] : 0.0f) + gauss(rng);
    const std::vector<WsprSpot> spots = wsprDecodePeriod(noisy.data(), int(noisy.size()), 48000);
    if (spots.empty() || spots.front().message != QLatin1String("K1JT FN20 30") ||
        std::fabs(spots.front().audioHz - 1453.0) > 1.0 || spots.front().snrDb > -16 ||
        spots.front().snrDb < -28) {
      qCritical("WSPR noisy decode count %d [%s] %.1f Hz %d dB", int(spots.size()),
                spots.empty() ? "" : qPrintable(spots.front().message),
                spots.empty() ? 0.0 : spots.front().audioHz, spots.empty() ? 0 : spots.front().snrDb);
      return fail("WSPR did not decode a weak signal");
    }
  }
  const auto* wspr = window.findChild<QAction*>(QStringLiteral("modeWspr"));
  if (!wspr || !wspr->toolTip().contains(QLatin1String("Tx Pct")) ||
      !wspr->toolTip().contains(QLatin1String("START")) ||
      wspr->toolTip().contains(QLatin1String("does not key yet")))
    return fail("WSPR is missing");
  const auto* wsprPower = window.findChild<QComboBox*>(QStringLiteral("wsprPower"));
  if (!wsprPower || wsprPower->count() != 19 || wsprPower->findData(37) < 0 ||
      wsprPower->findData(0) < 0 || wsprPower->findData(60) < 0 ||
      !wsprPower->itemText(wsprPower->findData(37)).contains(QLatin1String("5 W")) ||
      !wsprPowerLabel(37).contains(QLatin1String("5 W")) ||
      !wsprPowerLabel(0).contains(QLatin1String("1 mW")))
    return fail("WSPR power is missing");
  const QString wsprHead = wsprActivityHeader();
  if (!wsprHead.contains(QLatin1String("Time")) || !wsprHead.contains(QLatin1String("dB")) ||
      !wsprHead.contains(QLatin1String("Frequency")) || !wsprHead.contains(QLatin1String("Drift")) ||
      !wsprHead.contains(QLatin1String("Call")) || !wsprHead.contains(QLatin1String("Locator")) ||
      !wsprHead.contains(QLatin1String("Report")))
    return fail("WSPR column titles are missing");
  const auto* wsprTitle = window.findChild<QLabel*>(QStringLiteral("wsprRxHeader"));
  const auto* wsprTitleHost = window.findChild<QWidget*>(QStringLiteral("wsprRxHeaderHost"));
  if (!wsprTitle || !wsprTitleHost || wsprTitle->text() != wsprHead ||
      wsprTitle->parentWidget() != wsprTitleHost || wsprTitleHost->isVisible())
    return fail("WSPR column header is not held");
  const QString wsprColumns = wsprActivityLine(QStringLiteral("1928"), 2, 0.9, QStringLiteral("7.040045"), 0,
                                              QStringLiteral("AB8EF EN63 37"));
  if (!wsprColumns.contains(QLatin1String("AB8EF")) || !wsprColumns.contains(QLatin1String("EN63")) ||
      !wsprColumns.contains(QLatin1String("1928")) || !wsprColumns.contains(QLatin1String("7.040045")))
    return fail("WSPR column line is wrong");
  const auto* wsprPct = window.findChild<QSpinBox*>(QStringLiteral("wsprTxPct"));
  if (!wsprPct || wsprPct->minimum() != 0 || wsprPct->maximum() != 100)
    return fail("WSPR Tx Pct is missing");
  if (!window.findChild<QLabel*>(QStringLiteral("wsprLocator")) ||
      !window.findChild<QCheckBox*>(QStringLiteral("wsprBandHop")) ||
      !window.findChild<QPushButton*>(QStringLiteral("wsprSchedule")))
    return fail("WSPR locator or band hop is missing");
  if (window.findChild<QLineEdit*>(QStringLiteral("wsprCall")) ||
      window.findChild<QLineEdit*>(QStringLiteral("wsprGrid")))
    return fail("WSPR added a callsign field");
  if (wsprBandIndex(0) != 0 || wsprBandIndex(10) != 5 || wsprDialHz(5) != 14095600 ||
      wsprDialHz(4) != 10138700 || wsprDialHz(3) != 7038600 || wsprDialHz(0) != 136000)
    return fail("WSPR dials are wrong");
  for (int i = 0; i < 10; ++i) {
    const auto* button = window.findChild<QPushButton*>(QStringLiteral("wsprBand%1").arg(i));
    if (!button || button->text() != wsprBandName(i) ||
        !button->toolTip().contains(QString::number(wsprDialHz(i))) ||
        !button->toolTip().contains(QLatin1String("START")))
      return fail("WSPR band button is missing");
  }
  const auto* wsprRow = window.findChild<QWidget*>(QStringLiteral("wsprBands"));
  if (!wsprRow || !wsprRow->toolTip().contains(QLatin1String("7038600")))
    return fail("WSPR band row is missing");
  if (js8SuggestedBandCount() != 10 || js8SuggestedDialHz(2) != 7078000 ||
      js8SuggestedDialHz(4) != 14078000 || js8SuggestedDialHz(9) != 50318000 ||
      js8SuggestedDialHz(0) != 1842000)
    return fail("JS8 dials are wrong");
  for (int i = 0; i < js8SuggestedBandCount(); ++i) {
    const auto* button = window.findChild<QPushButton*>(QStringLiteral("js8Band%1").arg(i));
    if (!button || button->text() != js8SuggestedBandName(i) ||
        !button->toolTip().contains(QString::number(js8SuggestedDialHz(i))) ||
        !button->toolTip().contains(QLatin1String("START")))
      return fail("JS8 band button is missing");
  }
  const auto* js8Row = window.findChild<QWidget*>(QStringLiteral("js8Bands"));
  if (!js8Row || !js8Row->toolTip().contains(QLatin1String("144.178")) ||
      !js8Row->toolTip().contains(QLatin1String("START")))
    return fail("JS8 band row is missing");
  if (window.findChild<QLineEdit*>(QStringLiteral("js8Call")) ||
      window.findChild<QLineEdit*>(QStringLiteral("js8Grid")))
    return fail("JS8 added a callsign field");
  if (wsprTxThisSlot(0))
    return fail("WSPR Tx Pct 0 transmitted");
  const QDateTime wsprDay(QDate(2026, 10, 29), QTime(16, 0), QTimeZone(QTimeZone::UTC));
  const QDateTime wsprNight(QDate(2026, 10, 29), QTime(4, 0), QTimeZone(QTimeZone::UTC));
  const int wsprDayPeriod = wsprGrayPeriod(QStringLiteral("EM79"), 60, wsprDay);
  const int wsprNightPeriod = wsprGrayPeriod(QStringLiteral("EM79"), 60, wsprNight);
  if (wsprDayPeriod != 1 || wsprNightPeriod != 3) {
    qCritical("WSPR grayline day %d night %d", wsprDayPeriod, wsprNightPeriod);
    return fail("WSPR grayline is wrong");
  }
  bool hopBits[60] = {};
  hopBits[1 * 10 + 5] = true;
  if (wsprPickBand(1, 5, true, hopBits) != 5 || wsprPickBand(1, 0, true, hopBits) != 5)
    return fail("WSPR band hop missed 20 m");
  hopBits[1 * 10 + 5] = false;
  if (wsprPickBand(1, 0, true, hopBits) != -1)
    return fail("WSPR band hop invented a band");
  showWsprBandHopping(&window);
  auto* schedule = window.findChild<QDialog*>(QStringLiteral("wsprSchedule"));
  const auto* hopTable = window.findChild<QTableWidget*>(QStringLiteral("wsprHopTable"));
  const auto* gray = window.findChild<QSpinBox*>(QStringLiteral("wsprGrayTime"));
  if (!schedule || !schedule->windowTitle().contains(QLatin1String("WSPR Band Hopping")) ||
      !hopTable || hopTable->columnCount() != 10 || hopTable->rowCount() != 6 || !gray ||
      gray->suffix() != QLatin1String("min"))
    return fail("WSPR schedule is missing");
  if (!hopTable->horizontalHeaderItem(0) ||
      hopTable->horizontalHeaderItem(0)->text() != QLatin1String("160m") ||
      !hopTable->horizontalHeaderItem(9) ||
      hopTable->horizontalHeaderItem(9)->text() != QLatin1String("10m") ||
      !hopTable->verticalHeaderItem(0) ||
      hopTable->verticalHeaderItem(0)->text() != QLatin1String("Sunrise grayline") ||
      !hopTable->verticalHeaderItem(4) ||
      hopTable->verticalHeaderItem(4)->text() != QLatin1String("Tune") ||
      !hopTable->verticalHeaderItem(5) ||
      hopTable->verticalHeaderItem(5)->text() != QLatin1String("Rx only"))
    return fail("WSPR schedule labels are wrong");
  schedule->close();
  const auto* mt63Menu = window.findChild<QMenu*>(QStringLiteral("modeMt63Menu"));
  if (!mt63Menu || mt63Menu->actions().size() != 6)
    return fail("MT63 submenu is missing");
  for (const char* name : {"MT63-500S", "MT63-500L", "MT63-1000S", "MT63-1000L", "MT63-2000S",
                           "MT63-2000L"}) {
    std::vector<float> wave;
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("CQE"), 1500, &wave) || wave.size() < 1000)
      return fail("MT63 modulator is missing");
    const QString back =
        mt63Decode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1String("CQE"))) {
      qCritical("MT63 decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("MT63 did not decode");
    }
  }
  const auto* mt63 = window.findChild<QAction*>(QStringLiteral("modeMt631000S"));
  if (!mt63 || !mt63->toolTip().contains(QLatin1String("1000 Hz")) ||
      !mt63->toolTip().contains(QLatin1String("START")) ||
      mt63->toolTip().contains(QLatin1String("does not key yet")))
    return fail("MT63-1000S is missing");
  if (keyboardTextMode(QStringLiteral("WEFAX-576")))
    return fail("WEFAX is a keyboard mode");
  std::vector<float> faxWave;
  if (buildTxWave(QStringLiteral("WEFAX-576"), QStringLiteral("E"), 1900, &faxWave))
    return fail("WEFAX keyed");
  WefaxSpec faxSpec;
  const int faxPixels = wefaxPixelsPerLine(faxSpec.ioc);
  const int faxRows = 8;
  std::vector<unsigned char> faxGray(static_cast<size_t>(faxPixels * faxRows), 255);
  for (int y = 0; y < faxRows; ++y) {
    for (int x = 0; x < faxPixels; ++x) {
      const double pos = (double(x) + 0.5) / double(faxPixels);
      const bool phasing = y < 2;
      if (pos < 0.045 || (!phasing && pos >= 0.12 && pos < 0.28))
        faxGray[static_cast<size_t>(y * faxPixels + x)] = 0;
    }
  }
  const std::vector<float> faxAudio = wefaxModulateTest(faxSpec, 48000, faxGray, faxPixels, faxRows);
  const WefaxImage faxBack = wefaxDecode(faxSpec, faxAudio.data(), int(faxAudio.size()), 48000);
  if (faxBack.width != faxPixels || faxBack.height < 4)
    return fail("WEFAX did not decode");
  const auto faxAt = [&](int row, double pos) {
    int x = int(pos * double(faxBack.width));
    if (x < 0)
      x = 0;
    if (x >= faxBack.width)
      x = faxBack.width - 1;
    return int(faxBack.gray[static_cast<size_t>(row * faxBack.width + x)]);
  };
  const int faxRow = faxBack.height - 1;
  if (faxAt(faxRow, 0.02) > 80 || faxAt(faxRow, 0.20) > 80 || faxAt(faxRow, 0.60) < 180)
    return fail("WEFAX picture is inverted");
  const struct {
    const char* mode;
    int minute;
    int second;
    bool first;
  } halves[] = {
      {"FT8", 0, 0, true},  {"FT8", 0, 15, false}, {"FT8", 0, 30, true},  {"FT8", 0, 45, false},
      {"FT4", 0, 0, true},  {"FT4", 0, 7, false},  {"FT4", 0, 15, true},   {"FT4", 0, 22, false},
      {"FT4", 1, 37, false}, {"FT4", 1, 45, true}, {"FT2", 0, 0, true},    {"FT2", 0, 3, false},
      {"FT2", 0, 7, true},  {"FT2", 0, 11, false}, {"FT2", 0, 18, false}, {"FT2", 0, 22, true},
      {"FT2", 0, 56, false}, {"FT2", 1, 0, true},
      {"JT65A", 0, 0, true}, {"JT65A", 1, 0, false},
      {"Q65A", 0, 0, true}, {"Q65A", 0, 30, false},
  };
  std::vector<float> jt65;
  if (!buildTxWave(QStringLiteral("JT65A"), QStringLiteral("CQ N8SDR EM79"), 1500, &jt65) ||
      jt65.size() < 48000)
    return fail("JT65 encoder is missing");
  const QString jt65Back = decodeSequencedMessage(QStringLiteral("JT65A"), jt65);
  qInfo("JT65A %s", qPrintable(jt65Back));
  if (!jt65Back.contains(QLatin1String("N8SDR")))
    return fail("JT65A did not decode");
  std::vector<float> q65;
  if (!buildTxWave(QStringLiteral("Q65A"), QStringLiteral("CQ N8SDR EM79"), 1500, &q65) ||
      q65.size() < 48000)
    return fail("Q65 encoder is missing");
  const QString q65Back = decodeSequencedMessage(QStringLiteral("Q65A"), q65);
  qInfo("Q65A %s", qPrintable(q65Back));
  if (!q65Back.contains(QLatin1String("N8SDR")))
    return fail("Q65A did not decode");
  for (const auto& row : halves) {
    if (txStampIsFirstHalf(QString::fromLatin1(row.mode), row.minute, row.second) != row.first)
      return fail("period half is wrong");
  }
  QString spotCall;
  QString spotGrid;
  if (!pskSpotFromMessage(QStringLiteral("CQ N8SDR EM79"), &spotCall, &spotGrid) ||
      spotCall != QLatin1String("N8SDR") || spotGrid != QLatin1String("EM79"))
    return fail("PSK Reporter missed a CQ");
  if (!pskSpotFromMessage(QStringLiteral("W1AW N8SDR FN31"), &spotCall, &spotGrid) ||
      spotCall != QLatin1String("N8SDR") || spotGrid != QLatin1String("FN31"))
    return fail("PSK Reporter picked the wrong call");
  if (!pskSpotFromMessage(QStringLiteral("N8SDR EM79 37"), &spotCall, &spotGrid) ||
      spotCall != QLatin1String("N8SDR") || spotGrid != QLatin1String("EM79"))
    return fail("PSK Reporter missed WSPR");
  if (!pskSpotFromMessage(QStringLiteral("<N8SDR> EM79 37"), &spotCall, &spotGrid) ||
      spotCall != QLatin1String("N8SDR") || spotGrid != QLatin1String("EM79"))
    return fail("PSK Reporter missed a hashed call");
  if (!pskSpotFromMessage(QStringLiteral("W1AW <N8SDR> -12"), &spotCall, &spotGrid) ||
      spotCall != QLatin1String("N8SDR") || !spotGrid.isEmpty())
    return fail("PSK Reporter missed a hashed sender");
  if (pskSpotFromMessage(QStringLiteral("<...>"), &spotCall, &spotGrid))
    return fail("PSK Reporter invented a hashed call");
  if (pskSpotFromMessage(QStringLiteral("E"), &spotCall, &spotGrid))
    return fail("PSK Reporter invented a call");
  if (pskReporterModeName(QStringLiteral("JT65A")) != QLatin1String("JT65") ||
      pskReporterModeName(QStringLiteral("Q65A")) != QLatin1String("Q65"))
    return fail("PSK Reporter mode name is wrong");
  const QString mapUrl = pskReporterMapUrl(QStringLiteral("n8sdr"));
  if (!mapUrl.startsWith(QLatin1String("https://pskreporter.info/pskmap.html")) ||
      !mapUrl.contains(QLatin1String("callsign=N8SDR")))
    return fail("PSK Reporter link is wrong");
  if (pskReporterMapUrl(QString()).contains(QLatin1String("callsign=")))
    return fail("PSK Reporter link invented a callsign");
  PskSpot spot;
  spot.call = QStringLiteral("W1AW");
  spot.grid = QStringLiteral("FN31");
  spot.freqHz = 14074000;
  spot.mode = QStringLiteral("FT8");
  spot.snr = -15;
  spot.unixTime = 1700000000;
  const QByteArray packet = pskReporterDatagram(QStringLiteral("N8SDR"), QStringLiteral("EM79"),
                                                QStringLiteral("STD 0.1.0"), QString(), {spot},
                                                1700000000, 1, 0x12345678);
  if (packet.size() < 16 || quint8(packet.at(0)) != 0x00 || quint8(packet.at(1)) != 0x0A)
    return fail("PSK Reporter header is wrong");
  const int declared = (quint8(packet.at(2)) << 8) | quint8(packet.at(3));
  if (declared != packet.size() || packet.at(12) != char(0x12) || packet.at(15) != char(0x78))
    return fail("PSK Reporter length is wrong");
  if (!packet.contains("N8SDR") || !packet.contains("EM79") || !packet.contains("W1AW") ||
      !packet.contains("FT8"))
    return fail("PSK Reporter spot is wrong");
  const int callAt = packet.indexOf("W1AW");
  if (callAt < 1 || quint8(packet.at(callAt - 1)) != 4 || quint8(packet.at(callAt + 4)) != 0x00 ||
      quint8(packet.at(callAt + 5)) != 0xD6 || quint8(packet.at(callAt + 6)) != 0xC0 ||
      quint8(packet.at(callAt + 7)) != 0x90 || quint8(packet.at(callAt + 8)) != 0xF1)
    return fail("PSK Reporter frequency is wrong");
  if (pskReporterDatagram(QString(), QStringLiteral("EM79"), QStringLiteral("STD"), QString(), {spot},
                          1, 1, 1)
          .size() != 0)
    return fail("PSK Reporter sent without a callsign");
  if (!pskReporterDatagram(QStringLiteral("N8SDR"), QStringLiteral("EM79"), QStringLiteral("STD 0.1.0"),
                           QStringLiteral("dipole"), {spot}, 1700000000, 1, 1)
           .contains("dipole"))
    return fail("PSK Reporter dropped the antenna");
  const QByteArray live = QsoLog::decodeDatagram(QStringLiteral("FT8"), -15, 0.2f, 1200,
                                                 QStringLiteral("CQ N8SDR EM79"), QTime(19, 30));
  if (live.size() < 12 || quint8(live.at(0)) != 0xad || quint8(live.at(8)) != 0 || quint8(live.at(11)) != 2 ||
      !live.contains("CQ N8SDR EM79") || !live.contains("FT8"))
    return fail("live decode UDP is wrong");
  const QByteArray wsprUdp = QsoLog::wsprDecodeDatagram(-12, 0.4f, 7040045, 0, QStringLiteral("<AB8EF>"),
                                                       QStringLiteral("EN63"), 37, QTime(19, 28));
  if (wsprUdp.size() < 12 || quint8(wsprUdp.at(11)) != 10 || !wsprUdp.contains("AB8EF") ||
      wsprUdp.contains("<AB8EF>") || !wsprUdp.contains("EN63"))
    return fail("WSPR decode UDP is wrong");
  const auto* pskLink = window.findChild<QAction*>(QStringLiteral("pskReporterLink"));
  const auto* network = window.findChild<QAction*>(QStringLiteral("networkConfig"));
  if (!pskLink || !pskLink->toolTip().contains(QLatin1String("pskreporter.info")) ||
      !pskLink->toolTip().contains(QLatin1String("does not send START")) ||
      pskLink->toolTip().contains(QLatin1String("does not key yet")))
    return fail("PSK Reporter link is missing");
  if (!network || network->toolTip().contains(QLatin1String("Spots are not sent")))
    return fail("PSK Reporter still says spots are not sent");
  if (window.findChild<QLineEdit*>(QStringLiteral("pskCall")) ||
      window.findChild<QLineEdit*>(QStringLiteral("reportCall")) ||
      window.findChild<QLineEdit*>(QStringLiteral("wsprCall")) ||
      window.findChild<QLineEdit*>(QStringLiteral("wsprnetCall")) ||
      window.findChild<QLineEdit*>(QStringLiteral("wsprnetGrid")))
    return fail("a second callsign field was added");
  QString postedCall;
  const QByteArray wsprBody = wsprnetFormBody(
      QStringLiteral("n8sdr"), QStringLiteral("em79rj"), 7038600, 1490.0, QStringLiteral("261030"),
      QStringLiteral("1928"), -12, 0.4, 1, QStringLiteral("<AB8EF> EN63 37"), QStringLiteral("STD 0.1.0"),
      &postedCall);
  if (postedCall != QLatin1String("AB8EF") || !wsprBody.contains("function=wspr") ||
      !wsprBody.contains("rcall=N8SDR") || !wsprBody.contains("rgrid=EM79RJ") ||
      !wsprBody.contains("rqrg=7.040100") || !wsprBody.contains("tqrg=7.040090") ||
      !wsprBody.contains("tcall=AB8EF") || wsprBody.contains("<AB8EF>") ||
      !wsprBody.contains("tgrid=EN63") || !wsprBody.contains("sig=-12") ||
      !wsprBody.contains("dt=0.4") || !wsprBody.contains("drift=1") || !wsprBody.contains("dbm=37") ||
      !wsprBody.contains("date=261030") || !wsprBody.contains("time=1928") ||
      !wsprBody.contains("mode=2") || !wsprBody.contains("version=STD"))
    return fail("WSPRnet spot is wrong");
  if (!wsprnetFormBody(QStringLiteral("N8SDR"), QStringLiteral("EM79"), 7038600, 1500.0,
                       QStringLiteral("261030"), QStringLiteral("1928"), -10, 0.1, 0,
                       QStringLiteral("N8SDR EM79 37"), QStringLiteral("STD 0.1.0"), nullptr)
           .isEmpty())
    return fail("WSPRnet posted the operator");
  if (!wsprnetFormBody(QStringLiteral("N8SDR"), QStringLiteral("EM79"), 7038600, 1500.0,
                       QStringLiteral("261030"), QStringLiteral("1928"), -10, 0.1, 0,
                       QStringLiteral("<...> EM79 37"), QStringLiteral("STD 0.1.0"), nullptr)
           .isEmpty())
    return fail("WSPRnet invented a hashed call");
  if (!wsprnetFormBody(QString(), QStringLiteral("EM79"), 7038600, 1500.0, QStringLiteral("261030"),
                       QStringLiteral("1928"), -10, 0.1, 0, QStringLiteral("W1AW FN31 37"),
                       QStringLiteral("STD 0.1.0"), nullptr)
           .isEmpty() ||
      !wsprnetFormBody(QStringLiteral("N8SDR"), QString(), 7038600, 1500.0, QStringLiteral("261030"),
                       QStringLiteral("1928"), -10, 0.1, 0, QStringLiteral("W1AW FN31 37"),
                       QStringLiteral("STD 0.1.0"), nullptr)
           .isEmpty() ||
      !wsprnetFormBody(QStringLiteral("N8SDR"), QStringLiteral("EM79"), 50000, 1500.0,
                       QStringLiteral("261030"), QStringLiteral("1928"), -10, 0.1, 0,
                       QStringLiteral("W1AW FN31 37"), QStringLiteral("STD 0.1.0"), nullptr)
           .isEmpty())
    return fail("WSPRnet sent without a callsign");
  const QString reports = wsprnetReportsUrl(QStringLiteral("n8sdr"));
  if (!reports.startsWith(QLatin1String("https://www.wsprnet.org/olddb")) ||
      !reports.contains(QLatin1String("findreporter=N8SDR")))
    return fail("WSPRnet reports link is wrong");
  if (wsprnetReportsUrl(QString()).contains(QLatin1String("findreporter=")) ||
      !wsprnetReportsUrl(QString()).contains(QLatin1String("wsprnet.org")))
    return fail("WSPRnet reports link invented a callsign");
  const auto* wsprLink = window.findChild<QAction*>(QStringLiteral("wsprnetReports"));
  if (!wsprLink || !wsprLink->toolTip().contains(QLatin1String("wsprnet.org")) ||
      !wsprLink->toolTip().contains(QLatin1String("password")) ||
      !wsprLink->toolTip().contains(QLatin1String("does not send START")) ||
      wsprLink->toolTip().contains(QLatin1String("does not key yet")))
    return fail("WSPRnet reports link is missing");
  if (!network || !network->toolTip().contains(QLatin1String("WSPRnet")) ||
      !network->toolTip().contains(QLatin1String("password")))
    return fail("WSPRnet is missing from Network");
  if (stdVersionName() != QStringLiteral("testing phase \u00B7 highly transmissible"))
    return fail("version subtitle is wrong");
  if (!UpdateChecker::isNewer(QStringLiteral("v0.1.1"), QStringLiteral("0.1.0")) ||
      UpdateChecker::isNewer(QStringLiteral("v0.1.0"), QStringLiteral("0.1.0")) ||
      UpdateChecker::isNewer(QStringLiteral("v0.0.9"), QStringLiteral("0.1.0")))
    return fail("update compare is wrong");
  const auto* checkUpdates = window.findChild<QAction*>(QStringLiteral("checkForUpdates"));
  const auto* updateChip = window.findChild<QAction*>(QStringLiteral("updateAvailable"));
  if (!checkUpdates || !checkUpdates->toolTip().contains(QLatin1String("does not send START")) ||
      !updateChip || updateChip->isVisible())
    return fail("update notice is missing");

  qInfo("STD self-test ok");
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  const bool selftest = qEnvironmentVariableIsSet("STD_SELFTEST");
  WaterfallGpu::prepareProcessBackend(!selftest);

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
  if (!selftest)
    window.primeGraphics();
  qInfo("STD graphics backend %s", qPrintable(WaterfallGpu::processBackend()));
  window.show();

  if (selftest) {
    QTimer::singleShot(0, &app, [&app, &window] { app.exit(shellReady(window)); });
  }

  return app.exec();
}
