// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DigitalMode.h"
#include "EchoLog.h"
#include "hell/HellRaster.h"
#include "ContestMode.h"
#include "MacroFile.h"
#include "MaEngine.h"
#include "MainWindow.h"
#include "ModeProfile.h"
#include "StatusStrip.h"

#include "QsoLog.h"
#include "RstMeter.h"
#include "StationFiles.h"
#include "TxWave.h"
#include "hell/HellEngine.h"
#include "dominoex/DominoModem.h"
#include "thor/ThorModem.h"
#include "mfsk/MfskModem.h"
#include "LiveTx.h"
#include "speech/SpeechReader.h"
#include "speech/SpeechInput.h"
#include "speech/SpeechModel.h"
#include "speech/SpeechRecognizer.h"
#include "mt63/Mt63Modem.h"
#include "olivia/OliviaModem.h"
#include "psk/PskModem.h"
#include "rtty/RttyModem.h"
#include "wefax/WefaxModem.h"
#include "js8/Js8Modem.h"
#include "modem/ToneClock.h"
#include "rade/RadeLink.h"
#include "rade/RadeScreen.h"
#include "wspr/WsprModem.h"
#include "spot/FreeDvReporter.h"
#include "spot/PskReporter.h"
#include "spot/WsprNet.h"
#include "UpdateChecker.h"
#include "WxFeed.h"
#include "QsoCapture.h"
#include "Callbook.h"
#include "WaterfallGpu.h"
#include "WaterfallWidget.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#endif

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QAction>
#include <QApplication>
#include <QtEndian>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QToolBar>
#include <QImage>
#include <QSettings>
#include <QCoreApplication>
#include <QDockWidget>
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QPalette>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextEdit>
#include <QScrollBar>
#include <QHash>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QComboBox>
#include <QDataStream>
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

// Contiguous width, in Hz, where the Welch spectrum stays within 26 dB of
// its peak. The transform is checked against a 1500 Hz tone (bin 256).
double occupiedWidthHz(const std::vector<float>& wave, double* peakOut, int* peakBinOut) {
  constexpr int nfft = 8192;
  if (int(wave.size()) < nfft)
    return 0.0;
  const double binHz = 48000.0 / nfft;
  std::vector<double> re(nfft);
  std::vector<double> im(nfft);
  std::vector<double> power(nfft / 2, 0.0);
  auto reverseBits = [](int value) {
    int out = 0;
    for (int bit = 0; bit < 13; ++bit) {
      out = (out << 1) | (value & 1);
      value >>= 1;
    }
    return out;
  };
  for (size_t off = 0; off + nfft <= wave.size(); off += nfft / 2) {
    for (int i = 0; i < nfft; ++i) {
      const int j = reverseBits(i);
      const double window = 0.5 * (1.0 - std::cos(2.0 * 3.141592653589793 * i / (nfft - 1)));
      re[static_cast<size_t>(j)] = double(wave[off + static_cast<size_t>(i)]) * window;
      im[static_cast<size_t>(j)] = 0.0;
    }
    for (int len = 1; len < nfft; len <<= 1) {
      const double theta = -3.141592653589793 / double(len);
      const double wRe = std::cos(theta);
      const double wIm = std::sin(theta);
      for (int start = 0; start < nfft; start += len << 1) {
        double curRe = 1.0;
        double curIm = 0.0;
        for (int j = 0; j < len; ++j) {
          const int a = start + j;
          const int b = a + len;
          const double tRe = re[static_cast<size_t>(b)] * curRe - im[static_cast<size_t>(b)] * curIm;
          const double tIm = re[static_cast<size_t>(b)] * curIm + im[static_cast<size_t>(b)] * curRe;
          re[static_cast<size_t>(b)] = re[static_cast<size_t>(a)] - tRe;
          im[static_cast<size_t>(b)] = im[static_cast<size_t>(a)] - tIm;
          re[static_cast<size_t>(a)] += tRe;
          im[static_cast<size_t>(a)] += tIm;
          const double nextRe = curRe * wRe - curIm * wIm;
          curIm = curRe * wIm + curIm * wRe;
          curRe = nextRe;
        }
      }
    }
    for (int k = 0; k < nfft / 2; ++k)
      power[static_cast<size_t>(k)] += re[static_cast<size_t>(k)] * re[static_cast<size_t>(k)] +
                                       im[static_cast<size_t>(k)] * im[static_cast<size_t>(k)];
  }
  int peakBin = 1;
  double peakPower = 0.0;
  for (int k = 1; k < nfft / 2; ++k) {
    if (power[static_cast<size_t>(k)] > peakPower) {
      peakPower = power[static_cast<size_t>(k)];
      peakBin = k;
    }
  }
  const double limit = peakPower * std::pow(10.0, -26.0 / 10.0);
  int low = peakBin;
  int high = peakBin;
  while (low > 1 && power[static_cast<size_t>(low - 1)] >= limit)
    --low;
  while (high + 1 < nfft / 2 && power[static_cast<size_t>(high + 1)] >= limit)
    ++high;
  if (peakOut)
    *peakOut = 0.0;
  if (peakBinOut)
    *peakBinOut = peakBin;
  return (high - low) * binHz;
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

  // Floor and Range in dB, with Auto and Flatten, as MSHV / WSJT-X / Lyra.
  const auto* floorSlider = window.findChild<QSlider*>("waterfallFloor");
  const auto* rangeSlider = window.findChild<QSlider*>("waterfallRange");
  const auto* autoButton = window.findChild<QPushButton*>("waterfallAuto");
  const auto* flattenButton = window.findChild<QPushButton*>("waterfallFlatten");
  if (!floorSlider || !rangeSlider || !autoButton || !flattenButton || !autoButton->isCheckable() ||
      !flattenButton->isCheckable() || floorSlider->minimum() != -160 || floorSlider->maximum() != 0 ||
      rangeSlider->minimum() != 10 || rangeSlider->maximum() != 60 ||
      floorSlider->isEnabled() == autoButton->isChecked())
    return fail("waterfall Floor, Range, Auto, or Flatten is missing");
  // The Tx line marker icons load through Qt's SVG image plugin. If the
  // plugin were missing, the style sheet would fall back to no marker at all.
  for (const char* icon : {":/std/biohazard-on.svg", ":/std/biohazard-off.svg", ":/std/biohazard-hover.svg"}) {
    QImage art(QString::fromLatin1(icon));
    if (art.isNull())
      return fail("a Tx line marker icon does not load");
    const QString dump = qEnvironmentVariable("STD_ICON_PNG");
    if (!dump.isEmpty())
      art.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation)
          .save(dump + QLatin1Char('/') + QFileInfo(QString::fromLatin1(icon)).completeBaseName() + ".png");
  }
  for (const char* name : {"waterfallContrast", "waterfallBrightness"}) {
    if (window.findChild<QSlider*>(QLatin1String(name)))
      return fail("the old Contrast / Brightness sliders are still there");
  }
  {
    // Flatten follows a passband slope and ignores a strong narrow signal.
    QVector<float> line(780);
    for (int c = 0; c < line.size(); ++c)
      line[c] = -110.0f + 20.0f * float(c) / float(line.size() - 1);  // 20 dB tilt
    for (int c = 400; c < 412; ++c)
      line[c] += 30.0f;  // one FT8-wide signal
    const QVector<float> base = waterfallBaseline(line);
    float worst = 0.0f;
    for (int c = 0; c < line.size(); ++c) {
      const float slope = -110.0f + 20.0f * float(c) / float(line.size() - 1);
      worst = std::max(worst, std::fabs(base[c] - slope));
    }
    if (base.size() != line.size() || worst > 1.5f)
      return fail("waterfall Flatten does not follow the passband slope");
  }

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
  if (!activity || activity->count() != 17 || activity->itemText(0) != QLatin1String("Standard") ||
      activity->findText(QStringLiteral("ARRL Field Day")) < 0)
    return fail("contest activity list is missing");
  if (activity->findData(1) >= 0)
    return fail("EU RSQ is on the FT activity list; MSHV leaves it off");
  // ARRL International EME and FT Challenge use MSHV's report sequence:
  // Tx2 the report, Tx3 R+report, Tx6 CQ / CQ FTC. Not a grid exchange.
  {
    auto* box = const_cast<QComboBox*>(activity);
    const int was = box->currentIndex();
    const struct {
      int id;
      const char* cq;
    } reportContests[] = {{16, "CQ "}, {17, "CQ FTC "}};
    for (const auto& c : reportContests) {
      box->setCurrentIndex(box->findData(c.id));
      const QString t2 = window.findChild<QLineEdit*>(QStringLiteral("tx2"))->text();
      const QString t3 = window.findChild<QLineEdit*>(QStringLiteral("tx3"))->text();
      const QString t6 = window.findChild<QLineEdit*>(QStringLiteral("tx6"))->text();
      const QString t7 = window.findChild<QLineEdit*>(QStringLiteral("txExtra"))->text();
      const bool report = t2.contains(QRegularExpression(QStringLiteral("[+-]\\d\\d$")));
      const bool roger = t3.contains(QRegularExpression(QStringLiteral(" R[+-]\\d\\d$")));
      if (!report || !roger || !t6.startsWith(QLatin1String(c.cq)) || !t7.endsWith(QLatin1String("QSOB4"))) {
        qCritical("activity %d: [%s] [%s] [%s] [%s]", c.id, qPrintable(t2), qPrintable(t3), qPrintable(t6),
                  qPrintable(t7));
        return fail("EME / FT Challenge lines do not match MSHV");
      }
    }
    box->setCurrentIndex(was);
  }
  // Exchanges that can't pack into a 77-bit message are flagged when the
  // contest is chosen; ones MSHV accepts are not. EU VHF wants six locator
  // characters. Settings are put back afterwards.
  {
    auto* box = const_cast<QComboBox*>(activity);
    auto* status = window.findChild<QLabel*>(QStringLiteral("txStatus"));
    if (!status)
      return fail("TX status line is missing");
    QSettings store;
    const QVariant fdWas = store.value(QStringLiteral("tx/fdExch"));
    const QVariant ruWas = store.value(QStringLiteral("tx/ruExch"));
    const int was = box->currentIndex();
    const QString statusWas = status->text();
    const auto flagged = [&](int id, const char* key, const char* exchange) {
      store.setValue(QLatin1String(key), QLatin1String(exchange));
      box->setCurrentIndex(0);
      status->clear();
      box->setCurrentIndex(box->findData(id));
      return status->text();
    };
    const struct {
      int id;
      const char* key;
      const char* exchange;
      bool bad;
    } exchanges[] = {
        {4, "tx/fdExch", "2A OH", false},   {4, "tx/fdExch", "3F ENY", false}, {4, "tx/fdExch", "32A DX", false},
        {4, "tx/fdExch", "2X OH", true},    {4, "tx/fdExch", "33A OH", true},  {4, "tx/fdExch", "2A OHIO", true},
        {4, "tx/fdExch", "2A", true},       {9, "tx/ruExch", "DX", false},     {9, "tx/ruExch", "ON", false},
        {9, "tx/ruExch", "7999", false},    {10, "tx/ruExch", "X42", false},   {9, "tx/ruExch", "8000", true},
        {11, "tx/ruExch", "OHIO", true},    {12, "tx/ruExch", "X00", true},
    };
    for (const auto& e : exchanges) {
      const QString text = flagged(e.id, e.key, e.exchange);
      if (text.isEmpty() == e.bad) {
        qCritical("contest %d exchange [%s] status [%s]", e.id, e.exchange, qPrintable(text));
        return fail("contest exchange check is wrong");
      }
    }
    status->clear();
    box->setCurrentIndex(0);
    box->setCurrentIndex(box->findData(3));
    const bool euWarned = status->text().contains(QLatin1String("6-character"));
    store.setValue(QStringLiteral("tx/fdExch"), fdWas);
    store.setValue(QStringLiteral("tx/ruExch"), ruWas);
    box->setCurrentIndex(was);
    status->setText(statusWas);
    if (euWarned != (window.findChild<QLabel*>(QStringLiteral("wsprLocator"))->text().size() < 6))
      return fail("EU VHF locator warning is wrong");
  }
  // MSHV's JT65 / Q65 Decode entries, and the WSPR PSK Reporter switch.
  for (const char* name : {"decodeSlowMenu", "decodeAveraging", "decodeAutoClearAvg", "decodeClearAvg",
                           "decodeQ65Single", "decodeQ65Drift", "decodeJt65Vhf", "decodeJt65Max8"}) {
    if (!window.findChild<QObject*>(QLatin1String(name))) {
      qCritical("missing %s", name);
      return fail("JT65/Q65 decode option is missing");
    }
  }
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
  // The bank in use is a saved setting, so any of 1 to 4, with or without a name.
  if (!bank || !QRegularExpression(QStringLiteral("^[1-4]( .+)?$")).match(bank->text()).hasMatch() ||
      !bank->toolTip().contains(QLatin1String("does not key"), Qt::CaseInsensitive) ||
      !bank->toolTip().contains(QLatin1String("START"))) {
    if (bank)
      qCritical("macro bank [%s] [%s]", qPrintable(bank->text()), qPrintable(bank->toolTip()));
    return fail("macro bank is missing");
  }
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
  bool sawPower = false;
  bool sawName = false;
  for (const MacroUserField& field : macroUserFields()) {
    if (field.tag == QLatin1String("<MYCALL>") || field.tag == QLatin1String("<MYLOC>") ||
        field.tag == QLatin1String("<MYGRID>") || field.objectName == QLatin1String("userCall") ||
        field.objectName == QLatin1String("userGrid"))
      return fail("a station tag duplicated the callsign or locator");
    if (field.tag == QLatin1String("<MYNAME>"))
      sawName = true;
    if (field.tag == QLatin1String("<TX_PWR>"))
      sawPower = true;
  }
  if (!sawName || !sawPower)
    return fail("station tag fields are missing");
  QHash<QString, QString> stationTags;
  for (const MacroUserField& field : macroUserFields()) {
    if (field.tag == QLatin1String("<MYNAME>"))
      stationTags.insert(field.tag, QStringLiteral("Rick"));
    else if (field.tag == QLatin1String("<MYQTH>"))
      stationTags.insert(field.tag, QStringLiteral("Cincinnati"));
    else if (field.tag == QLatin1String("<ANTENNA>"))
      stationTags.insert(field.tag, QStringLiteral("dipole"));
    else if (field.tag == QLatin1String("<TX_PWR>"))
      stationTags.insert(field.tag, QStringLiteral("100"));
    else
      stationTags.insert(field.tag, field.label);
  }
  const QString expanded = macroInsertText(
      QStringLiteral("<MYCALL> <MYLOC> <MYGRID> <myname> <MYQTH> <ANTENNA> <TX_PWR> <CALL>"),
      QStringLiteral("N8SDR"), QStringLiteral("EM79"), stationTags);
  if (expanded != QLatin1String("N8SDR EM79 EM79 Rick Cincinnati dipole 100 <CALL>")) {
    qCritical("macro expand [%s]", qPrintable(expanded));
    return fail("station tags did not expand");
  }
  QHash<QString, QString> emptyName;
  emptyName.insert(QStringLiteral("<MYNAME>"), QString());
  if (macroInsertText(QStringLiteral("de <MYNAME>."), QStringLiteral("N8SDR"), QStringLiteral("EM79"),
                      emptyName) != QLatin1String("de ."))
    return fail("an empty station tag stayed in the text");
  {
    // The macro engine: Fldigi tags decide what a press does.
    MacroContext mc;
    mc.myCall = QStringLiteral("n8sdr");
    mc.myLoc = QStringLiteral("EM79");
    mc.tags.insert(QStringLiteral("<CALL>"), QStringLiteral("W1AW"));
    mc.tags.insert(QStringLiteral("<RST>"), QStringLiteral("599"));
    mc.tags.insert(QStringLiteral("<NAME>"), QString());
    mc.tags.insert(QStringLiteral("<MYNAME>"), QStringLiteral("Rick"));
    mc.serial = 7;
    mc.nowUtc = QDateTime(QDate(2026, 11, 3), QTime(14, 5), QTimeZone::utc());
    const MacroPlan reply =
        planMacro(QStringLiteral("<TX>\n<CALL> de <MYCALL> <RST> TU <LOG> <CLRRX><CLRTX> <RX>"), mc);
    if (!reply.keys || !reply.backToRx || !reply.log || !reply.clearRx || !reply.clearTx ||
        reply.text.trimmed() != QLatin1String("W1AW de N8SDR 599 TU") || !reply.missing.isEmpty()) {
      qCritical("macro plan [%s]", qPrintable(reply.text));
      return fail("a keyed Fldigi macro did not plan right");
    }
    const MacroPlan addOnly = planMacro(QStringLiteral("my name ... <MYNAME>"), mc);
    if (addOnly.keys || addOnly.text != QLatin1String("my name ... Rick"))
      return fail("a macro without <TX> was not add-only");
    mc.tags.insert(QStringLiteral("<CALL>"), QString());
    const MacroPlan noCall = planMacro(QStringLiteral("<TX><CALL> de <MYCALL><RX>"), mc);
    if (noCall.missing != QStringList{QStringLiteral("Call")})
      return fail("an empty Call was not reported");
    mc.tags.insert(QStringLiteral("<CALL>"), QStringLiteral("W1AW"));
    if (planMacro(QStringLiteral("tnx <NAME|OM> 73"), mc).text != QLatin1String("tnx OM 73") ||
        !planMacro(QStringLiteral("tnx <NAME|OM>"), mc).missing.isEmpty())
      return fail("a tag fallback did not fill in");
    if (planMacro(QStringLiteral("{CALL} de {MYCALL} {#} {nothing}"), mc).text !=
        QLatin1String("W1AW de N8SDR 007 {nothing}"))
      return fail("brace tags did not fill in");
    const MacroPlan cq = planMacro(QStringLiteral("<TX>CQ <IDLE:5> de <MYCALL> <TIMER:15><RX>"), mc);
    if (cq.text != QLatin1String("CQ de N8SDR") || cq.repeatSeconds != 15 ||
        cq.skipped != QStringList{QStringLiteral("<IDLE:5>")})
      return fail("a Fldigi tag STD does not run was not left out");
    if (planMacro(QStringLiteral("<< <MYCALL>, <MYNAME> >>"), mc).text != QLatin1String("<< N8SDR, Rick >>"))
      return fail("brag text with << >> was changed");
    const QString timed = planMacro(QStringLiteral("<ZDT> <ZT> <COMMENT:note>x"), mc).text;
    if (timed != QLatin1String("2026-11-03 14:05Z 1405Z x")) {
      qCritical("time tags [%s]", qPrintable(timed));
      return fail("time tags did not fill in");
    }
    const MacroPlan counted = planMacro(QStringLiteral("<CNTR><INCR><INCR><DECR>"), mc);
    if (counted.text != QLatin1String("007") || counted.serialStep != 1)
      return fail("serial tags did not count");
    // Weather: each source's reply, the line <WX> sends, and an empty <WX>.
    {
      QString err;
      const WxReading om = WxFeed::parseOpenMeteo(
          R"({"current":{"temperature_2m":12.2,"relative_humidity_2m":72,"weather_code":2,)"
          R"("wind_speed_10m":12.9,"wind_direction_10m":225}})",
          &err);
      if (!om.valid || WxFeed::describe(om, false) != QLatin1String("54F, partly cloudy, wind SW 8 mph, humidity 72%") ||
          WxFeed::describe(om, true) != QLatin1String("12C, partly cloudy, wind SW 13 km/h, humidity 72%")) {
        qCritical("wx [%s]", qPrintable(WxFeed::describe(om, false)));
        return fail("Open-Meteo weather did not read");
      }
      const WxReading nws = WxFeed::parseNws(
          R"({"properties":{"textDescription":"Light Rain","temperature":{"value":10},)"
          R"("relativeHumidity":{"value":91.4},"windSpeed":{"value":0},"windDirection":{"value":null}}})",
          &err);
      if (WxFeed::describe(nws, false) != QLatin1String("50F, light rain, calm, humidity 91%"))
        return fail("NWS weather did not read");
      const WxReading amb = WxFeed::parseAmbient(
          R"([{"lastData":{"tempf":68,"humidity":40,"windspeedmph":5,"winddir":0}}])", &err);
      if (WxFeed::describe(amb, false) != QLatin1String("68F, wind N 5 mph, humidity 40%"))
        return fail("Ambient weather did not read");
      const WxReading eco = WxFeed::parseEcowitt(
          R"({"code":0,"msg":"success","data":{"outdoor":{"temperature":{"value":"20"},"humidity":{"value":"55"}},)"
          R"("wind":{"wind_speed":{"value":"16.1"},"wind_direction":{"value":"90"}}}})",
          &err);
      if (WxFeed::describe(eco, false) != QLatin1String("68F, wind E 10 mph, humidity 55%"))
        return fail("Ecowitt weather did not read");
      if (WxFeed::parseEcowitt(R"({"code":40010,"msg":"illegal api key"})", &err).valid ||
          !err.contains(QLatin1String("illegal")))
        return fail("an Ecowitt error was not reported");
      double lat = 0.0;
      double lon = 0.0;
      if (!WxFeed::gridToLatLon(QStringLiteral("EM79tj"), &lat, &lon) || std::abs(lat - 39.40) > 0.05 ||
          std::abs(lon + 84.38) > 0.05)
        return fail("a locator did not convert to latitude and longitude");
      MacroContext wxc = mc;
      wxc.tags.insert(QStringLiteral("<WX>"), QString());
      const MacroPlan noWx = planMacro(QStringLiteral("<TX>wx here <WX><RX>"), wxc);
      if (noWx.missing != QStringList{QStringLiteral("Weather")} ||
          planMacro(QStringLiteral("wx <WX|fine>"), wxc).text != QLatin1String("wx fine"))
        return fail("an empty <WX> was not reported");
    }
    // Smart capture: what their text tells us, for the QSO bar.
    {
      const QsoHints h = scanQsoText(
          QStringLiteral("N8SDR N8SDR DE K8AB K8AB GM Rick, tnx for the call. Ur RST 579 579. "
                         "Name hr is BOB BOB. QTH is Dayton, OH. Loc EM79tq. HW? N8SDR de K8AB kn"),
          QStringLiteral("N8SDR"));
      if (h.call != QLatin1String("K8AB") || h.name != QLatin1String("Bob") || h.qth != QLatin1String("Dayton") ||
          h.st != QLatin1String("OH") || h.rstIn != QLatin1String("579") || h.loc != QLatin1String("EM79tq")) {
        qCritical("capture [%s|%s|%s|%s|%s|%s]", qPrintable(h.call), qPrintable(h.name), qPrintable(h.qth),
                  qPrintable(h.st), qPrintable(h.rstIn), qPrintable(h.loc));
        return fail("smart capture missed a field");
      }
      const QsoHints op = scanQsoText(QStringLiteral("op: joe qth nr springfield illinois rig ic7300"),
                                      QStringLiteral("N8SDR"));
      if (op.name != QLatin1String("Joe") || op.qth != QLatin1String("Springfield Illinois") || !op.st.isEmpty())
        return fail("smart capture read OP or a long QTH wrong");
      if (!scanQsoText(QStringLiteral("CQ CQ DE N8SDR N8SDR"), QStringLiteral("N8SDR")).call.isEmpty() ||
          !scanQsoText(QStringLiteral("my name is is"), QStringLiteral("N8SDR")).name.isEmpty())
        return fail("smart capture offered our own call or a filler word");
      QsoMemory memory;
      // Two logs; the 2026 QSO is in both and counts once.
      memory.loadText({QByteArray("STD log<EOH>\n"
                                  "<CALL:4>K8AB<NAME:3>Bob<QTH:6>Dayton<STATE:2>OH<QSO_DATE:8>20250412"
                                  "<TIME_ON:6>140000<BAND:3>40M<MODE:4>RTTY<EOR>\n"
                                  "<CALL:4>k8ab<GRIDSQUARE:6>EM79TQ<QSO_DATE:8>20260504<TIME_ON:6>020000"
                                  "<BAND:3>20m<MODE:5>PSK31<EOR>\n"),
                       QByteArray("<CALL:4>K8AB<QSO_DATE:8>20260504<TIME_ON:4>0200<BAND:3>20M"
                                  "<MODE:5>PSK31<EOR>")});
      const QsoHistory k8ab = memory.lookup(QStringLiteral("k8ab"));
      if (k8ab.count != 2 || k8ab.name != QLatin1String("Bob") || k8ab.qth != QLatin1String("Dayton") ||
          k8ab.grid != QLatin1String("EM79TQ") || k8ab.lastDate != QLatin1String("2026-05-04") ||
          k8ab.lastBand != QLatin1String("20m") || k8ab.lastMode != QLatin1String("PSK31") ||
          memory.lookup(QStringLiteral("W1AW")).count != 0)
        return fail("log memory did not read the ADIF");
      // Callbook answers, and portable calls.
      QString key;
      QString err;
      const CallbookRecord qrz = Callbook::parseQrz(
          "<?xml version=\"1.0\" ?><QRZDatabase version=\"1.34\"><Callsign><call>AA7BQ</call>"
          "<fname>FRED L</fname><name>LLOYD</name><addr2>SCOTTSDALE</addr2><state>AZ</state>"
          "<grid>DM32AF</grid></Callsign><Session><Key>abc123</Key><Count>1</Count></Session></QRZDatabase>",
          &key, &err);
      if (!qrz.found || qrz.name != QLatin1String("Fred") || qrz.qth != QLatin1String("Scottsdale") ||
          qrz.state != QLatin1String("AZ") || qrz.grid != QLatin1String("DM32af") || key != QLatin1String("abc123"))
        return fail("a QRZ answer did not read");
      key.clear();
      err.clear();
      if (Callbook::parseQrz("<QRZDatabase><Session><Error>Session Timeout</Error></Session></QRZDatabase>", &key,
                             &err).found ||
          !key.isEmpty() || err != QLatin1String("Session Timeout"))
        return fail("a QRZ session timeout was not seen");
      err.clear();
      const CallbookRecord hq = Callbook::parseHamqth(
          R"({"callsign":"OK2CQR","nick":"Petr","qth":"Neratovice","location":{"grid":"jo70gg"},)"
          R"("address":{"city":"Neratovice"},"region":{"usState":""}})",
          &err);
      if (!hq.found || hq.name != QLatin1String("Petr") || hq.qth != QLatin1String("Neratovice") ||
          hq.grid != QLatin1String("JO70gg"))
        return fail("a HamQTH answer did not read");
      if (Callbook::parseHamqthToken(R"({"token":"t0k","expiresIn":3600})", &err) != QLatin1String("t0k") ||
          !Callbook::parseHamqthToken(R"({"title":"Unauthorized","detail":"Wrong password"})", &err).isEmpty() ||
          err != QLatin1String("Wrong password"))
        return fail("a HamQTH login answer did not read");
      if (!Callbook::portable(QStringLiteral("K8AB/P")) || Callbook::portable(QStringLiteral("K8AB")) ||
          Callbook::homeCall(QStringLiteral("VE3/K8AB/P")) != QLatin1String("K8AB"))
        return fail("a portable call was not recognized");
    }
    // The logger link: a Reply as SDRLogger+ writes it, and Status as it reads it.
    {
      QByteArray reply;
      {
        QDataStream out(&reply, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_5_4);
        out.setByteOrder(QDataStream::BigEndian);
        out << quint32(0xadbccbda) << quint32(3) << quint32(4) << QByteArray("STD") << quint32(52245000)
            << qint32(-12) << double(0.2) << quint32(1523) << QByteArray("~") << QByteArray("CQ K8AB EN81")
            << false << quint8(0);
      }
      QsoLog::UdpRequest req;
      if (!QsoLog::parseRequest(reply, &req) || req.type != 4 || req.message != QLatin1String("CQ K8AB EN81") ||
          req.deltaFrequency != 1523 || req.snr != -12 || req.timeMs != 52245000)
        return fail("a logger Reply did not read");
      QByteArray other;
      {
        QDataStream out(&other, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_5_4);
        out.setByteOrder(QDataStream::BigEndian);
        out << quint32(0xadbccbda) << quint32(3) << quint32(4) << QByteArray("JTDX") << quint32(0) << qint32(0)
            << double(0.0) << quint32(1500) << QByteArray("~") << QByteArray("CQ K8AB EN81") << false;
      }
      QsoLog::UdpRequest notOurs;
      if (QsoLog::parseRequest(other, &notOurs))
        return fail("a Reply for another program was taken");
      QByteArray halt;
      {
        QDataStream out(&halt, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_5_4);
        out.setByteOrder(QDataStream::BigEndian);
        out << quint32(0xadbccbda) << quint32(3) << quint32(8) << QByteArray("STD") << true;
      }
      QsoLog::UdpRequest h;
      if (!QsoLog::parseRequest(halt, &h) || h.type != 8 || !h.autoTxOnly)
        return fail("a logger Halt Tx did not read");
      QsoLog::UdpStatus st;
      st.dialHz = 14074000;
      st.mode = QStringLiteral("FT8");
      st.dxCall = QStringLiteral("k8ab");
      st.deCall = QStringLiteral("N8SDR");
      const QByteArray status = QsoLog::statusDatagram(st);
      QDataStream in(status);
      in.setVersion(QDataStream::Qt_5_4);
      in.setByteOrder(QDataStream::BigEndian);
      quint32 magic = 0, schema = 0, type = 0;
      quint64 dial = 0;
      QByteArray id, smode, dx;
      in >> magic >> schema >> type >> id >> dial >> smode >> dx;
      if (magic != 0xadbccbda || type != 1 || id != "STD" || dial != 14074000 || smode != "FT8" || dx != "K8AB")
        return fail("Status for the logger is wrong");
    }
    if (macroButtonLabel(QStringLiteral("CQ  @>|"), mc, true) != QStringLiteral("▶ CQ") ||
        macroButtonLabel(QStringLiteral("Reply <CALL>"), mc, false) != QLatin1String("Reply W1AW"))
      return fail("macro button labels are wrong");
  }
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
    {
      // Lookup's second ADIF: STD keeps a copy and reads it once the original is gone.
      const QString name = QStringLiteral("std-selftest-memory-%1.adi").arg(QCoreApplication::applicationPid());
      const QString memory = dir.filePath(name);
      QFile::copy(logPath, memory);
      const QString copy = QsoLog::adifCopyPath(memory);
      if (QsoLog::keepAdifCopy(memory) != memory || QFile(copy).size() != logBody.size())
        return fail("Lookup ADIF was not copied into the log folder");
      QFile::remove(memory);
      const bool usesCopy = QsoLog::keepAdifCopy(memory) == copy;
      QFile::remove(copy);
      if (!usesCopy)
        return fail("Lookup ADIF copy was not used once the original was gone");
    }
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
  const auto* hellRow = window.findChild<QWidget*>(QStringLiteral("hellOptions"));
  const auto* hellReverse = window.findChild<QCheckBox*>(QStringLiteral("hellReverseVideo"));
  if (!hellRow || !hellRow->isHidden() || !hellReverse || hellReverse->font().pixelSize() != 12 ||
      !hellReverse->text().contains(QLatin1String("Reverse")) ||
      !hellReverse->toolTip().contains(QLatin1String("does not key")) ||
      !hellReverse->toolTip().contains(QLatin1String("START")) ||
      !hellReverse->toolTip().contains(QLatin1String("white on black")))
    return fail("Hell reverse video is missing");
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
      keyboardTextMode(QStringLiteral("CW")) ||
      keyboardTextMode(QStringLiteral("RADE V1")) ||
      keyboardTextMode(QStringLiteral("RADE V2 (experimental)")))
    return fail("keyboard text mode list is wrong");
  if (!radeMode(QStringLiteral("RADE V1")) || !radeV2(QStringLiteral("RADE V2 (experimental)")) ||
      radeV2(QStringLiteral("RADE V1")) || radeMode(QStringLiteral("FT8")) ||
      sequencedFamily(QStringLiteral("RADE V1")) || hellFamily(QStringLiteral("RADE V1")))
    return fail("RADE mode list is wrong");
  {
    auto* v1 = window.findChild<QAction*>(QStringLiteral("modeRadeV1"));
    auto* v2 = window.findChild<QAction*>(QStringLiteral("modeRadeV2"));
    auto* ft8Back = window.findChild<QAction*>(QStringLiteral("modeFt8"));
    auto* screen = window.findChild<QWidget*>(QStringLiteral("radeScreen"));
    auto* radeProfile = window.findChild<ModeProfile*>();
    if (!v1 || !v2 || !ft8Back || !screen || !radeProfile ||
        !v1->toolTip().contains(QLatin1String("START")) ||
        !v2->toolTip().contains(QLatin1String("experimental")) ||
        [screen] {
          // Text boxes other than those inside a spin box.
          QList<QLineEdit*> edits;
          for (QLineEdit* edit : screen->findChildren<QLineEdit*>())
            if (!qobject_cast<QAbstractSpinBox*>(edit->parentWidget()))
              edits.append(edit);
          QStringList names;
          for (QLineEdit* edit : edits)
            names << edit->objectName();
          names.sort();
          return names != QStringList{QStringLiteral("radeReporterFilter"),
                                      QStringLiteral("radeReporterMessage")};
        }())
      // Identity stays in Options → User. The text boxes here are the
      // reporter message and the station filter.
      return fail("RADE screen is missing");
    {
      const QString loop = window.radeTxLoopback();
      if (!loop.isEmpty()) {
        qWarning("RADE TX loopback: %s", qPrintable(loop));
        return fail("RADE TX through STD's output path did not decode");
      }
    }
    if (radeCodecSelfTest() != 0)
      return fail("RADE codec round trip failed");
    QSettings radeKeep;
    const bool hadBpfV1 = radeKeep.contains(QStringLiteral("rade/bpfV1"));
    const bool hadBpfV2 = radeKeep.contains(QStringLiteral("rade/bpfV2"));
    const QVariant bpfV1 = radeKeep.value(QStringLiteral("rade/bpfV1"));
    const QVariant bpfV2 = radeKeep.value(QStringLiteral("rade/bpfV2"));
    struct BpfRestore {
      bool had1 = false;
      bool had2 = false;
      QVariant v1;
      QVariant v2;
      ~BpfRestore() {
        QSettings store;
        if (had1)
          store.setValue(QStringLiteral("rade/bpfV1"), v1);
        if (had2)
          store.setValue(QStringLiteral("rade/bpfV2"), v2);
      }
    } bpfRestore{hadBpfV1, hadBpfV2, bpfV1, bpfV2};
    radeKeep.remove(QStringLiteral("rade/bpfV1"));
    radeKeep.remove(QStringLiteral("rade/bpfV2"));
    v1->trigger();
    auto* variant = window.findChild<QComboBox*>(QStringLiteral("radeVariant"));
    auto* analog = window.findChild<QCheckBox*>(QStringLiteral("radeAnalog"));
    auto* bpf = window.findChild<QCheckBox*>(QStringLiteral("radeBpf"));
    auto* drive = window.findChild<QSlider*>(QStringLiteral("radeDrive"));
    auto* sync = window.findChild<QLabel*>(QStringLiteral("radeSync"));
    auto* snr = window.findChild<QLabel*>(QStringLiteral("radeSnr"));
    auto* micBox = window.findChild<QComboBox*>(QStringLiteral("radeMicDevice"));
    auto* spkBox = window.findChild<QComboBox*>(QStringLiteral("radeSpeakerDevice"));
    auto* radeVol = window.findChild<QSlider*>(QStringLiteral("radeVolume"));
    auto* audio = window.findChild<QWidget*>(QStringLiteral("radeAudioBar"));
    auto* rx = window.findChild<QPlainTextEdit*>(QStringLiteral("receiveText"));
    auto* radeHellBox = window.findChild<QPlainTextEdit*>(QStringLiteral("txHell"));
    auto* tx1 = window.findChild<QWidget*>(QStringLiteral("tx1"));
    if (window.findChild<QPushButton*>(QStringLiteral("radeDigu")))
      return fail("RADE sideband button is back");
    {
      auto* denoise = window.findChild<QCheckBox*>(QStringLiteral("radeDenoise"));
      auto* wide = window.findChild<QCheckBox*>(QStringLiteral("radeWide"));
      if (!denoise || !wide || denoise->parentWidget() != audio || wide->parentWidget() != audio ||
          !denoise->toolTip().contains(QLatin1String("RNNoise")) ||
          !wide->toolTip().contains(QLatin1String("48 kHz")))
        return fail("RADE Mic NR or BW expand is missing from the audio row");
      if (!window.findChild<QLineEdit*>(QStringLiteral("radeReporterMessage")))
        return fail("RADE screen has no FreeDV Reporter message box");
    }
    {
      // The status strip shows the reporter that the mode feeds, and none otherwise.
      StatusStrip strip;
      auto* group = strip.findChild<QWidget*>(QStringLiteral("spotGroup"));
      auto* name = strip.findChild<QPushButton*>(QStringLiteral("spotName"));
      if (!group || !name)
        return fail("status strip has no spot chip");
      const struct {
        const char* mode;
        const char* label;
      } spotCases[] = {{"FT8", "PSK RPT"}, {"Q65A", "PSK RPT"}, {"JS8", "PSK RPT"},
                       {"WSPR", "WSPRNET"}, {"RADE V1", "FREEDV RPT"}, {"BPSK31", nullptr}};
      for (const auto& spotCase : spotCases) {
        strip.setDigitalMode(QString::fromLatin1(spotCase.mode));
        const bool want = spotCase.label != nullptr;
        if (group->isHidden() == want || (want && name->text() != QLatin1String(spotCase.label)))
          return fail("spot chip shows the wrong reporter for a mode");
      }
    }
    if (!variant || !analog || !bpf || !drive || !sync || !snr || !micBox || !spkBox || !radeVol ||
        !audio || !rx || !radeHellBox || !tx1)
      return fail("RADE V1 screen is missing a control");
    if (!screen->isVisible())
      return fail("RADE screen is hidden");
    if (audio->isHidden())
      return fail("RADE audio row is hidden");
    if (variant->currentText() != QLatin1String("RADE V1") || bpf->isChecked() || bpf->isEnabled())
      return fail("RADE V1 waveform is wrong");
    if (!rx->isHidden() || !radeHellBox->isHidden() || !tx1->isHidden())
      return fail("RADE still shows the text panes");
    if (!analog->toolTip().contains(QLatin1String("silent")) || variant->maximumWidth() > 280 ||
        micBox->count() < 1 || spkBox->count() < 1 || radeVol->maximum() != 100 ||
        !spkBox->toolTip().contains(QLatin1String("Play on")) || drive->maximumWidth() > 180 ||
        analog->parentWidget() != audio || drive->parentWidget() != audio || !screen->isAncestorOf(bpf) ||
        radeProfile->digitalMode() != QLatin1String("RADE V1"))
      return fail("RADE V1 screen is wrong");
    auto* micBar = window.findChild<QWidget*>(QStringLiteral("radeMicLevel"));
    auto* snrTrace = window.findChild<QWidget*>(QStringLiteral("radeSnrTrace"));
    if (!micBar || micBar->parentWidget() != audio || micBar->width() > 140 ||
        !micBar->toolTip().contains(QLatin1String("0.5")) ||
        !micBar->toolTip().contains(QLatin1String("clip")) ||
        !micBar->toolTip().contains(QLatin1String("Drive")))
      return fail("RADE mic bar is missing");
    auto* micGain = window.findChild<QSlider*>(QStringLiteral("radeMicGain"));
    if (!micGain || micGain->parentWidget() != audio || micGain->minimum() != -10 ||
        micGain->maximum() != 30 || micGain->width() > 120 ||
        !micGain->toolTip().contains(QLatin1String("leveler")) ||
        !micGain->toolTip().contains(QLatin1String("Drive")))
      return fail("RADE mic gain is missing");
    auto* rxLevel = window.findChild<QWidget*>(QStringLiteral("radeRxLevel"));
    auto* autoRx = window.findChild<QCheckBox*>(QStringLiteral("radeAutoRx"));
    auto* lamp = window.findChild<QWidget*>(QStringLiteral("radeSyncLamp"));
    auto* snrBar = window.findChild<QWidget*>(QStringLiteral("radeSnrBar"));
    auto* heardClock = window.findChild<QLabel*>(QStringLiteral("radeHeardClock"));
    if (!rxLevel || !screen->isAncestorOf(rxLevel) ||
        !rxLevel->toolTip().contains(QLatin1String("dBFS")) ||
        !rxLevel->toolTip().contains(QLatin1String("CLIP")))
      return fail("RADE RX meter is missing");
    if (!autoRx || !screen->isAncestorOf(autoRx) ||
        !autoRx->toolTip().contains(QLatin1String("clip")))
      return fail("RADE Auto RX is missing");
    if (!lamp || !screen->isAncestorOf(lamp))
      return fail("RADE sync lamp is missing");
    if (!snrBar || !screen->isAncestorOf(snrBar) ||
        !snrBar->toolTip().contains(QLatin1String("-10")) ||
        !snrBar->toolTip().contains(QLatin1String("40")))
      return fail("RADE SNR bar is missing");
    if (!heardClock || !screen->isAncestorOf(heardClock) ||
        !heardClock->toolTip().contains(QLatin1String("how long ago")))
      return fail("RADE heard clock is missing");
    if (auto* radeScreen = window.findChild<RadeScreen*>(QStringLiteral("radeScreen"))) {
      radeScreen->setSync(true, 6.6f, 1.2f);
      if (snr->text() != QLatin1String("SNR  7") || sync->text() != QLatin1String("Sync  yes"))
        return fail("RADE SNR is not a whole number");
      radeScreen->setSync(false, 0.0f, 0.0f);
      if (sync->text() != QLatin1String("Sync  idle") || snr->text() != QStringLiteral("SNR  —"))
        return fail("RADE sync idle text changed");
      radeScreen->setHeard(QStringLiteral("ok"), QStringLiteral("12:00"));
      if (!heardClock->text().contains(QLatin1String("just now")) ||
          !heardClock->text().contains(QLatin1String("UTC")))
        return fail("RADE heard clock did not start");
      radeScreen->setHeard(QString(), QString());
      if (heardClock->text() != QStringLiteral("—"))
        return fail("RADE heard clock did not clear");
    }
    {
      FreeDvReporter reporter;
      QJsonObject hello{{QStringLiteral("sid"), QStringLiteral("s1")},
                        {QStringLiteral("callsign"), QStringLiteral("ONE")},
                        {QStringLiteral("grid_square"), QStringLiteral("AA00")},
                        {QStringLiteral("version"), QStringLiteral("test")},
                        {QStringLiteral("rx_only"), false},
                        {QStringLiteral("last_update"), QStringLiteral("t")},
                        {QStringLiteral("connect_time"), QStringLiteral("t")}};
      reporter.ingestPacket(freedvReporterEvent(
          QStringLiteral("new_connection"), QJsonDocument(hello).toJson(QJsonDocument::Compact)));
      if (reporter.stations().size() != 1 || reporter.stations().at(0).station != QLatin1String("ONE") ||
          reporter.stations().at(0).grid != QLatin1String("AA00"))
        return fail("FreeDV Reporter station list missed a connect");
      QJsonObject freq{{QStringLiteral("sid"), QStringLiteral("s1")},
                       {QStringLiteral("freq"), QJsonValue(qint64(14236000))},
                       {QStringLiteral("callsign"), QStringLiteral("ONE")},
                       {QStringLiteral("grid_square"), QStringLiteral("AA00")},
                       {QStringLiteral("last_update"), QStringLiteral("t")}};
      QJsonArray pair;
      pair.append(QStringLiteral("freq_change"));
      pair.append(QJsonValue(freq));
      QJsonArray bulk;
      bulk.append(QJsonValue(pair));
      reporter.ingestPacket(freedvReporterEvent(
          QStringLiteral("bulk_update"), QJsonDocument(bulk).toJson(QJsonDocument::Compact)));
      if (reporter.stations().size() != 1 || reporter.stations().at(0).freqHz != 14236000)
        return fail("FreeDV Reporter bulk update missed a frequency");
      qint64 askedHz = 0;
      QObject::connect(&reporter, &FreeDvReporter::qsyAsked, &reporter,
                       [&askedHz](const QString&, qint64 hz, const QString&) { askedHz = hz; });
      QJsonObject ask{{QStringLiteral("callsign"), QStringLiteral("TWO")},
                      {QStringLiteral("frequency"), QJsonValue(qint64(7177000))},
                      {QStringLiteral("message"), QStringLiteral("move")}};
      reporter.ingestPacket(freedvReporterEvent(QStringLiteral("qsy_request"),
                                               QJsonDocument(ask).toJson(QJsonDocument::Compact)));
      const QJsonObject payload =
          QJsonDocument::fromJson(freedvReporterQsyPayload(QStringLiteral("s1"), 7177000, QStringLiteral("move")))
              .object();
      if (askedHz != 7177000 || payload.value(QStringLiteral("dest_sid")).toString() != QLatin1String("s1") ||
          qint64(payload.value(QStringLiteral("frequency")).toDouble()) != 7177000)
        return fail("FreeDV Reporter QSY request was not read");
      reporter.ingestPacket(freedvReporterEvent(
          QStringLiteral("remove_connection"),
          QJsonDocument(QJsonObject{{QStringLiteral("sid"), QStringLiteral("s1")}}).toJson(QJsonDocument::Compact)));
      if (!reporter.stations().isEmpty())
        return fail("FreeDV Reporter kept a station that left");
    }
    {
      auto* table = window.findChild<QTableWidget*>(QStringLiteral("radeReporterTable"));
      auto* filter = window.findChild<QLineEdit*>(QStringLiteral("radeReporterFilter"));
      auto* filterLabel = window.findChild<QLabel*>(QStringLiteral("radeReporterFilterLabel"));
      auto* qsyButton = window.findChild<QPushButton*>(QStringLiteral("radeRequestQsy"));
      auto* ignore = window.findChild<QCheckBox*>(QStringLiteral("radeIgnoreQsy"));
      auto* logButton = window.findChild<QPushButton*>(QStringLiteral("radeLogContact"));
      auto* limit = window.findChild<QSpinBox*>(QStringLiteral("radeTxLimit"));
      auto* saved = window.findChild<QComboBox*>(QStringLiteral("radeSavedMessages"));
      auto* page = window.findChild<RadeScreen*>(QStringLiteral("radeScreen"));
      if (!table || !filter || !filterLabel || filterLabel->text() != QLatin1String("Filter") ||
          filter->placeholderText().isEmpty() || !qsyButton || !ignore || !logButton || !limit || !saved ||
          !page ||
          limit->maximum() != 180 || limit->minimum() != 0 || table->columnCount() != 6 ||
          !table->horizontalHeaderItem(0) ||
          table->horizontalHeaderItem(0)->text() != QLatin1String("Station") ||
          !table->horizontalHeaderItem(1) ||
          table->horizontalHeaderItem(1)->text() != QLatin1String("Grid") ||
          !table->horizontalHeaderItem(2) ||
          table->horizontalHeaderItem(2)->text() != QLatin1String("kHz") ||
          !table->horizontalHeaderItem(3) ||
          table->horizontalHeaderItem(3)->text() != QLatin1String("Mode") ||
          !table->horizontalHeaderItem(4) ||
          table->horizontalHeaderItem(4)->text() != QLatin1String("SNR") ||
          !table->horizontalHeaderItem(5) ||
          table->horizontalHeaderItem(5)->text() != QLatin1String("Message") ||
          table->columnWidth(0) < 200 || table->columnWidth(1) < 150 || table->columnWidth(2) < 140 ||
          table->columnWidth(3) < 140 || table->columnWidth(4) < 100 ||
          !table->toolTip().contains(QLatin1String("Double-click")))
        return fail("RADE reporter table is missing");
      QVector<FreeDvReporter::Station> rows(2);
      rows[0].sid = QStringLiteral("a");
      rows[0].station = QStringLiteral("ONE");
      rows[0].grid = QStringLiteral("AA00");
      rows[0].freqHz = 7177000;
      rows[0].mode = QStringLiteral("RADEV1");
      rows[1].sid = QStringLiteral("b");
      rows[1].station = QStringLiteral("TWO");
      rows[1].grid = QStringLiteral("BB11");
      rows[1].freqHz = 14236000;
      rows[1].mode = QStringLiteral("RADEV1");
      rows[1].message = QStringLiteral("zzzz-only");
      page->showReporter(rows);
      if (table->rowCount() != 2)
        return fail("RADE reporter table did not fill");
      if (!table->item(0, 2) ||
          (table->item(0, 2)->textAlignment() & Qt::AlignHorizontal_Mask) != Qt::AlignLeft)
        return fail("RADE reporter columns are not left aligned");
      filter->setText(QStringLiteral("zzzz-only"));
      int shown = 0;
      for (int row = 0; row < table->rowCount(); ++row)
        if (!table->isRowHidden(row))
          ++shown;
      if (shown != 1)
        return fail("RADE reporter filter did not narrow the list");
      filter->clear();
      auto* scope = window.findChild<QComboBox*>(QStringLiteral("radeReporterScope"));
      if (!scope || scope->count() != 2 || scope->itemText(0) != QLatin1String("All") ||
          scope->itemText(1) != QLatin1String("This band"))
        return fail("RADE reporter band choice is missing");
      {
        QVector<FreeDvReporter::Station> colored(3);
        colored[0].sid = QStringLiteral("tx");
        colored[0].station = QStringLiteral("TXROW");
        colored[0].freqHz = 7177000;
        colored[0].transmitting = true;
        colored[1].sid = QStringLiteral("rx");
        colored[1].station = QStringLiteral("RXROW");
        colored[1].freqHz = 7177000;
        colored[1].rxAtMs = QDateTime::currentMSecsSinceEpoch();
        colored[2].sid = QStringLiteral("old");
        colored[2].station = QStringLiteral("OLDROW");
        colored[2].freqHz = 14236000;
        colored[2].rxAtMs = QDateTime::currentMSecsSinceEpoch() - 30000;
        page->showReporter(colored);
        auto rowOf = [table](const QString& name) {
          for (int row = 0; row < table->rowCount(); ++row) {
            const auto* item = table->item(row, 0);
            if (item && item->text() == name)
              return row;
          }
          return -1;
        };
        const int txRow = rowOf(QStringLiteral("TXROW"));
        const int rxRow = rowOf(QStringLiteral("RXROW"));
        const int oldRow = rowOf(QStringLiteral("OLDROW"));
        if (txRow < 0 || rxRow < 0 || oldRow < 0)
          return fail("RADE reporter color rows are missing");
        const QColor txColor = table->item(txRow, 0)->background().color();
        const QColor rxColor = table->item(rxRow, 0)->background().color();
        const QColor oldColor = table->item(oldRow, 0)->background().color();
        if (txColor != QColor(QStringLiteral("#3d2226")) || rxColor != QColor(QStringLiteral("#1e4036")) ||
            oldColor == QColor(QStringLiteral("#1e4036")) || oldColor == QColor(QStringLiteral("#3d2226")))
          return fail("RADE reporter row colors are wrong");
        QSettings scopeStore;
        const bool hadScope = scopeStore.contains(QStringLiteral("rade/reporterScope"));
        const QVariant previousScope = scopeStore.value(QStringLiteral("rade/reporterScope"));
        page->setReporterDial(7177000);
        scope->setCurrentIndex(1);
        int onBand = 0;
        int allRows = 0;
        for (int row = 0; row < table->rowCount(); ++row)
          if (!table->isRowHidden(row))
            ++onBand;
        scope->setCurrentIndex(0);
        for (int row = 0; row < table->rowCount(); ++row)
          if (!table->isRowHidden(row))
            ++allRows;
        if (hadScope)
          scopeStore.setValue(QStringLiteral("rade/reporterScope"), previousScope);
        else
          scopeStore.remove(QStringLiteral("rade/reporterScope"));
        const QSignalBlocker block(scope);
        const int at = scope->findData(hadScope ? previousScope : QVariant(QStringLiteral("all")));
        scope->setCurrentIndex(at < 0 ? 0 : at);
        if (onBand != 2 || allRows != 3)
          return fail("RADE reporter band filter is wrong");
      }
      QSettings messageStore;
      const bool hadMessages = messageStore.contains(QStringLiteral("dx/freedvReporterMessages"));
      const QVariant previousMessages = messageStore.value(QStringLiteral("dx/freedvReporterMessages"));
      struct MessageRestore {
        bool had = false;
        QVariant previous;
        RadeScreen* page = nullptr;
        ~MessageRestore() {
          QSettings again;
          if (had)
            again.setValue(QStringLiteral("dx/freedvReporterMessages"), previous);
          else
            again.remove(QStringLiteral("dx/freedvReporterMessages"));
          if (page)
            page->reloadSavedMessages();
        }
      } messageRestore{hadMessages, previousMessages, page};
      for (int i = 0; i < 20; ++i)
        page->storeReporterMessage(QStringLiteral("note-%1").arg(i));
      if (page->savedMessageCount() != 15)
        return fail("RADE saved messages did not stop at 15");
      if (ModeProfile::radeSidebandForHz(7177000) != QLatin1String("DIGL") ||
          ModeProfile::radeSidebandForHz(14236000) != QLatin1String("DIGU") ||
          ModeProfile::radeSidebandForHz(5368500) != QLatin1String("DIGU") ||
          ModeProfile::radeSidebandForHz(10136000) != QLatin1String("DIGU"))
        return fail("RADE QSY sideband is wrong");
      if (auto* wf = window.findChild<WaterfallWidget*>(QStringLiteral("waterfall"))) {
        const QString previous = wf->radeReadout();
        wf->setRadeReadout(QStringLiteral("RADE   Sync   SNR 7"));
        if (!wf->radeReadout().contains(QLatin1String("SNR 7")))
          return fail("RADE waterfall readout did not stick");
        wf->setRadeReadout(previous);
      } else {
        return fail("RADE waterfall readout is missing");
      }
    }
    auto* offset = window.findChild<QLabel*>(QStringLiteral("radeOffset"));
    if (!snrTrace || !screen->isAncestorOf(snrTrace) || snrTrace->height() < 32 ||
        snrTrace->height() > 48 || snrTrace->width() > 200 ||
        !snrTrace->toolTip().contains(QLatin1String("-10")) ||
        !snrTrace->toolTip().contains(QLatin1String("35")) || !offset || !snr)
      return fail("RADE SNR trace is missing");
    auto* radeTx = window.findChild<QPushButton*>(QStringLiteral("txSend"));
    auto* radeHalt = window.findChild<QPushButton*>(QStringLiteral("txHalt"));
    auto* keySlot = window.findChild<QWidget*>(QStringLiteral("radeKeySlot"));
    if (!radeTx || !radeHalt || !keySlot || !radeTx->parentWidget() ||
        radeTx->parentWidget()->parentWidget() != keySlot ||
        radeHalt->parentWidget() != radeTx->parentWidget())
      return fail("RADE TX is still at the bottom");
    {
      // RX sits before TX, lit green while receiving; Halt is hidden in RADE.
      auto* rx = window.findChild<QPushButton*>(QStringLiteral("radeRx"));
      auto* slotRow = qobject_cast<QHBoxLayout*>(keySlot->layout());
      if (!rx || !slotRow || slotRow->indexOf(rx) != 0 || rx->text() != QLatin1String("RX") ||
          !rx->styleSheet().contains(QLatin1String("#2ecc71")) || !radeHalt->isHidden())
        return fail("RADE RX chip is missing or Halt is still shown");
    }
    {
      // Scan sits after RX and TX: chip, Dwell 5-30 s in 5 s steps, Stay on sync.
      auto* scan = window.findChild<QPushButton*>(QStringLiteral("radeScan"));
      auto* dwell = window.findChild<QSpinBox*>(QStringLiteral("radeScanDwell"));
      auto* stay = window.findChild<QCheckBox*>(QStringLiteral("radeScanStay"));
      auto* operate = window.findChild<QWidget*>(QStringLiteral("radeOperate"));
      auto* meterCol = window.findChild<QWidget*>(QStringLiteral("radeMeterCol"));
      auto* audioHost = window.findChild<QWidget*>(QStringLiteral("radeAudioHost"));
      auto* layoutPick = window.findChild<QPushButton*>(QStringLiteral("radeLayoutPick"));
      if (!scan || !dwell || !stay || !operate || !meterCol || !audioHost || !layoutPick ||
          !operate->isAncestorOf(scan) || !operate->isAncestorOf(keySlot) ||
          !operate->isAncestorOf(dwell) || !operate->isAncestorOf(stay) || dwell->minimum() != 5 ||
          dwell->maximum() != 30 || dwell->singleStep() != 5 || !scan->isCheckable())
        return fail("RADE Scan controls are missing or out of place");
      {
        QSettings store;
        const bool hadLayout = store.contains(QStringLiteral("rade/layout"));
        const QString previous = store.value(QStringLiteral("rade/layout")).toString();
        auto* page = window.findChild<RadeScreen*>(QStringLiteral("radeScreen"));
        if (!page)
          return fail("RADE layout switch is missing");
        page->setColumns(false);
        QApplication::processEvents();
        if (micBox->x() > 0 && micGain->x() > 0 && micBar->x() > 0 && drive->x() > 0 &&
            (micGain->x() < micBox->x() || micGain->x() > micBar->x() || micBar->x() > drive->x()))
          return fail("RADE row order is wrong");
        if (snrTrace->x() > 0 && (snrTrace->x() < snr->x() || snrTrace->x() > offset->x()))
          return fail("RADE SNR trace left the status row");
        if (audioHost->y() > 0 && operate->y() > 0 && audioHost->y() + 4 < operate->y())
          return fail("RADE rows did not stack");
        page->setColumns(true);
        QApplication::processEvents();
        if (operate->x() > 0 && meterCol->x() > 0 && audioHost->x() > 0 &&
            (audioHost->x() >= meterCol->x() || meterCol->x() >= operate->x()))
          return fail("RADE columns did not line up");
        if (keySlot->y() > 0 && scan->y() > 0 && scan->y() + 4 < keySlot->y())
          return fail("RADE RX sits below Scan");
        if (dwell->y() > 0 && stay->y() > 0 && qAbs(dwell->y() - stay->y()) > 8)
          return fail("RADE Dwell left the Stay row");
        if (dwell->width() < 72)
          return fail("RADE Dwell box is clipped");
        if (layoutPick->text() != QLatin1String("Show rows"))
          return fail("RADE layout switch is missing");
        page->setColumns(hadLayout && previous == QLatin1String("rows") ? false : true);
        if (!hadLayout)
          store.remove(QStringLiteral("rade/layout"));
      }
      // No radio in the self-test: Scan must refuse and stay off.
      scan->click();
      if (scan->isChecked())
        return fail("RADE Scan started without a radio");
      // Scan can be limited to chosen bands.
      // Reporter dials only; the operator's own extras would change the counts.
      const auto all = radeProfile->radeScanDials({}, false);
      const auto only40 = radeProfile->radeScanDials({QStringLiteral("40")}, false);
      const auto two = radeProfile->radeScanDials({QStringLiteral("20"), QStringLiteral("40")}, false);
      bool only40ok = only40.size() == 4;
      for (const auto& dial : only40)
        only40ok = only40ok && dial.second >= 7000000 && dial.second < 7300000;
      if (all.size() != 20 || !only40ok || two.size() != 6 || two.first().second != 7177000 ||
          ModeProfile::radeScanBands().size() != 9 || !scan->toolTip().contains(QLatin1String("Right-click")))
        return fail("RADE Scan band choice is wrong");
    }
    auto* radeBar = window.findChild<QWidget*>(QStringLiteral("mshvProfile"));
    if (!radeBar || radeBar->isHidden())
      return fail("RADE band bar is hidden");
    QSettings radeDialSnap;
    struct RadeDialRestore {
      bool hadBand = false;
      QVariant band;
      QList<QPair<QString, QVariant>> extras;
      ~RadeDialRestore() {
        QSettings store;
        const QStringList now = store.allKeys();
        for (const QString& key : now) {
          if (key.startsWith(QLatin1String("profile/extra/RADE")))
            store.remove(key);
        }
        for (const QPair<QString, QVariant>& row : extras)
          store.setValue(row.first, row.second);
        if (hadBand)
          store.setValue(QStringLiteral("profile/band"), band);
        else
          store.remove(QStringLiteral("profile/band"));
      }
    } radeDialRestore;
    radeDialRestore.hadBand = radeDialSnap.contains(QStringLiteral("profile/band"));
    radeDialRestore.band = radeDialSnap.value(QStringLiteral("profile/band"));
    for (const QString& key : radeDialSnap.allKeys()) {
      if (key.startsWith(QLatin1String("profile/extra/RADE")))
        radeDialRestore.extras.append(qMakePair(key, radeDialSnap.value(key)));
    }
    QPushButton* band14 = nullptr;
    QPushButton* band7 = nullptr;
    QPushButton* band80 = nullptr;
    QPushButton* band15 = nullptr;
    QPushButton* band30 = nullptr;
    QPushButton* originalBand = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>(QStringLiteral("profileBand"))) {
      if (button->isChecked())
        originalBand = button;
      if (button->text() == QLatin1String("14"))
        band14 = button;
      else if (button->text() == QLatin1String("7"))
        band7 = button;
      else if (button->text() == QLatin1String("3.5"))
        band80 = button;
      else if (button->text() == QLatin1String("21"))
        band15 = button;
      else if (button->text() == QLatin1String("10"))
        band30 = button;
    }
    if (!band14 || !band7 || !band80 || !band15 || !band30)
      return fail("RADE band buttons are missing");
    if (!band14->toolTip().contains(QLatin1String("14236000")) ||
        !band14->toolTip().contains(QLatin1String("14240000")) ||
        !band14->toolTip().contains(QLatin1String("DIGU")) ||
        !band14->toolTip().contains(QLatin1String("14.230")) ||
        !band14->toolTip().contains(QLatin1String("SSTV")) ||
        band14->toolTip().contains(QLatin1String("DIGL")) ||
        !band7->toolTip().contains(QLatin1String("7177000")) ||
        !band7->toolTip().contains(QLatin1String("7045000")) ||
        !band7->toolTip().contains(QLatin1String("7181500")) ||
        !band7->toolTip().contains(QLatin1String("DIGL")) ||
        !band80->toolTip().contains(QLatin1String("3643000")) ||
        !band80->toolTip().contains(QLatin1String("3640000")) ||
        !band80->toolTip().contains(QLatin1String("DIGL")) ||
        !band15->toolTip().contains(QLatin1String("21313000")) ||
        !band15->toolTip().contains(QLatin1String("DIGU")) ||
        band15->toolTip().contains(QLatin1String("DIGL")) ||
        !band30->toolTip().contains(QLatin1String("no FreeDV Reporter dial")))
      return fail("RADE calling dials are wrong");
    band15->click();
    band7->click();
    if (radeProfile->dialHz() != 7177000 || radeProfile->sideband() != QLatin1String("DIGL"))
      return fail("RADE 40 m dial is wrong");
    band7->click();
    if (radeProfile->dialHz() != 7197000)
      return fail("RADE 40 m did not cycle");
    band80->click();
    if (radeProfile->dialHz() != 3643000 || radeProfile->sideband() != QLatin1String("DIGL"))
      return fail("RADE 80 m dial is wrong");
    band14->click();
    if (radeProfile->dialHz() != 14236000 || radeProfile->sideband() != QLatin1String("DIGU"))
      return fail("RADE 20 m dial is wrong");
    band14->click();
    if (radeProfile->dialHz() != 14240000)
      return fail("RADE 20 m did not cycle");
    {
      // 60 m: the two FreeDV Reporter dials on USB, placed after 3.5 MHz.
      QPushButton* band60 = nullptr;
      for (QPushButton* button : window.findChildren<QPushButton*>(QStringLiteral("profileBand")))
        if (button->text() == QLatin1String("5.3"))
          band60 = button;
      auto* bandRow = band60 ? qobject_cast<QHBoxLayout*>(band60->parentWidget()->layout()) : nullptr;
      if (!band60 || band60->isHidden() || !bandRow ||
          bandRow->indexOf(band60) != bandRow->indexOf(band80) + 1)
        return fail("RADE 60 m button is missing or out of order");
      band60->click();
      if (radeProfile->dialHz() != 5368500 || radeProfile->sideband() != QLatin1String("DIGU"))
        return fail("RADE 60 m dial is wrong");
      band60->click();
      if (radeProfile->dialHz() != 5403500)
        return fail("RADE 60 m did not cycle");
      band14->click();
    }
    auto* radeExtra = window.findChild<QSpinBox*>(QStringLiteral("profileExtra"));
    if (!radeExtra)
      return fail("RADE extra box is missing");
    radeExtra->setValue(14111000);
    emit radeExtra->editingFinished();
    bool sawExtra = false;
    for (int n = 0; n < 8 && !sawExtra; ++n) {
      band14->click();
      sawExtra = radeProfile->dialHz() == 14111000;
    }
    if (!sawExtra)
      return fail("RADE extra dial was not kept");
    {
      // Scan covers that extra too, after the band's reporter dials.
      const auto scan20 = radeProfile->radeScanDials({QStringLiteral("20")});
      QList<qint64> hz;
      for (const auto& dial : scan20)
        hz << dial.second;
      if (hz.size() < 3 || hz.at(0) != 14236000 || hz.at(1) != 14240000 || !hz.contains(14111000))
        return fail("RADE Scan does not cover the Extra dials");
      // Editing: replace, drop a stock dial, then delete all.
      radeProfile->setExtraDials({14112000, 14236000, 14111000});
      const QList<qint64> edited = radeProfile->extraDials();
      radeProfile->setExtraDials({});
      if (edited != QList<qint64>{14112000, 14111000} || !radeProfile->extraDials().isEmpty())
        return fail("RADE Extra dials did not edit or delete");
    }
    if (originalBand)
      originalBand->click();
    v2->trigger();
    if (!screen->isVisible() || variant->currentText() != QLatin1String("RADE V2 (experimental)") ||
        !bpf->isChecked() || radeProfile->digitalMode() != QLatin1String("RADE V2 (experimental)"))
      return fail("RADE V2 screen is wrong");
    ft8Back->trigger();
    auto* buttonRow = window.findChild<QWidget*>(QStringLiteral("txButtonRow"));
    if (radeProfile->digitalMode() != QLatin1String("FT8") || screen->isVisible() || rx->isHidden() ||
        tx1->isHidden() || audio->isVisible() || !buttonRow || !radeTx->parentWidget() ||
        radeTx->parentWidget()->parentWidget() != buttonRow)
      return fail("leaving RADE did not restore FT8");
  }
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
  if (hellBandwidthHz(QStringLiteral("Feld Hell")) != 245 ||
      hellBandwidthHz(QStringLiteral("Slow Hell")) != 30 ||
      hellBandwidthHz(QStringLiteral("Feld Hell X9")) != 2205 ||
      hellBandwidthHz(QStringLiteral("FSK Hell")) != 250 ||
      hellBandwidthHz(QStringLiteral("Hell 80")) != 450)
    return fail("Hell bandwidth is wrong");
  float bandLo = 0.0f;
  float bandHi = 0.0f;
  channelBandHz(1102, 245, true, &bandLo, &bandHi);
  if (std::fabs(bandLo - (1102.0f - 122.5f)) > 0.01f || std::fabs(bandHi - (1102.0f + 122.5f)) > 0.01f)
    return fail("Hell marker is not centered on the carrier");
  channelBandHz(1500, 50, false, &bandLo, &bandHi);
  if (std::fabs(bandLo - 1500.0f) > 0.01f || std::fabs(bandHi - 1550.0f) > 0.01f)
    return fail("FT8 marker does not start at the cursor");
  const char* centeredModes[] = {"Feld Hell", "Slow Hell", "Feld Hell X5", "Feld Hell X9",
                                  "FSK Hell", "FSK Hell-105", "Hell 80", "RTTY", "PSK31",
                                  "QPSK31", "MFSK16", "DominoEX 16", "Thor 16", "Olivia 8/250",
                                  "Contestia 8/250", "MT63-500S", "WSPR", "WEFAX-576"};
  for (const char* name : centeredModes) {
    if (!carrierCenteredMode(QString::fromLatin1(name)))
      return fail("a carrier-centered mode starts at the cursor");
  }
  const char* lowEdgeModes[] = {"FT8", "FT4", "FT2", "JT65A", "Q65A", "JS8"};
  for (const char* name : lowEdgeModes) {
    if (carrierCenteredMode(QString::fromLatin1(name)))
      return fail("an FT8-family marker is centered");
  }
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
  {
    std::vector<float> tone(16384);
    for (int i = 0; i < int(tone.size()); ++i)
      tone[static_cast<size_t>(i)] =
          float(0.5 * std::sin(2.0 * 3.141592653589793 * 1500.0 * i / 48000.0));
    int toneBin = 0;
    const double toneWidth = occupiedWidthHz(tone, nullptr, &toneBin);
    if (toneBin != 256 || toneWidth < 6.0 || toneWidth > 40.0)
      return fail("spectrum bin is wrong");
    std::vector<float> cq;
    if (!buildTxWave(QStringLiteral("Feld Hell"), QStringLiteral("CQ CQ de N8SDR"), 1500, &cq, nullptr) ||
        cq.size() < 16384)
      return fail("Feld Hell CQ did not build");
    float peak = 0.0f;
    for (float value : cq)
      peak = std::max(peak, std::fabs(value));
    int cqBin = 0;
    const double widthHz = occupiedWidthHz(cq, nullptr, &cqBin);
    qInfo("Feld Hell CQ peak %.3f -26 dB width %.0f Hz at bin %d", peak, widthHz, cqBin);
    if (peak > 0.95f)
      return fail("Feld Hell TX samples clip");
    if (std::fabs(cqBin * (48000.0 / 8192.0) - 1500.0) > 30.0)
      return fail("Feld Hell carrier is not 1500 Hz");
    if (widthHz < 200.0 || widthHz > 290.0)
      return fail("Feld Hell occupied width is not about 245 Hz");
  }
  const double slowRatio = double(slowWave.size()) / double(feldWave.size());
  const double fastRatio = double(feldWave.size()) / double(fastWave.size());
  if (slowRatio < 7.9 || slowRatio > 8.1 || fastRatio < 4.9 || fastRatio > 5.1)
    return fail("Hell family rates do not match Fldigi");
  {
    // Slow Hell is Feld Hell at one eighth the speed. Feld's envelope
    // limit softens the short pixels, so the same text carries a bit
    // more than ten times the keyed energy on Slow Hell.
    double feldEnergy = 0.0;
    double slowEnergy = 0.0;
    for (float v : feldWave)
      feldEnergy += double(v) * v;
    for (float v : slowWave)
      slowEnergy += double(v) * v;
    const double energyRatio = feldEnergy > 0.0 ? slowEnergy / feldEnergy : 0.0;
    if (energyRatio < 10.9 || energyRatio > 12.2) {
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
  {
    HellRaster raster;
    // Tape is 640x120. An 8 px margin on each side maps it 1:1, so the
    // ink column is not blended with the dark widget chrome.
    raster.resize(656, 136);
    raster.addColumn(QByteArray(40, char(0)));
    auto lumaAt = [](const QImage& image, int x, int y) {
      const QRgb pixel = image.pixel(x, y);
      return qRed(pixel) + qGreen(pixel) + qBlue(pixel);
    };
    QImage before(raster.size(), QImage::Format_ARGB32);
    raster.render(&before);
    const int y = before.height() / 2;
    const int inkX = 8 + 638;
    const int paperX = 8 + 40;
    const int inkLuma = lumaAt(before, inkX, y);
    const int paperLuma = lumaAt(before, paperX, y);
    raster.setVideoReverse(true);
    QImage after(raster.size(), QImage::Format_ARGB32);
    raster.render(&after);
    const int flipped = lumaAt(after, inkX, y);
    const int paperFlip = lumaAt(after, paperX, y);
    if (!raster.videoReverse() || flipped < inkLuma + 80 || paperFlip > paperLuma - 80) {
      qCritical("hell reverse ink %d -> %d paper %d -> %d", inkLuma, flipped, paperLuma, paperFlip);
      return fail("Hell reverse video did not invert the tape");
    }
    // A long transmission moves up into the history instead of falling off.
    raster.setVideoReverse(false);
    raster.clear();
    for (int i = 0; i < 1000; ++i)
      raster.addColumn(QByteArray(40, char(i % 7 == 0 ? 0 : 255)));
    if (raster.historyRows() < 2 || raster.tapeImage().height() <= 3 * 120)
      return fail("Hell tape lost a long transmission instead of keeping it");
    raster.clear();
    if (raster.historyRows() != 0)
      return fail("CLR RX left the Hell tape history");
  }
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
    strip->setDigitalMode(QStringLiteral("MFSK16"));
    if (!afc->isEnabled() || !squelch->isEnabled() || squelchLevel->isHidden() ||
        !afc->toolTip().contains(QLatin1String("MFSK")) ||
        !squelch->toolTip().contains(QLatin1String("does not key")) ||
        !squelch->toolTip().contains(QLatin1String("START")))
      return fail("MFSK AFC or squelch is not available");
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
  if (bands.size() != 11)
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
  if (!extra || extra->minimum() != 0 || extra->specialValueText() != QLatin1String("default") ||
      !extra->toolTip().contains(QStringLiteral("DIGU")) ||
      !extra->toolTip().contains(QStringLiteral("does not key"), Qt::CaseInsensitive))
    return fail("extra frequency is missing");
  const auto* extraLabel = window.findChild<QLabel*>("profileExtraLabel");
  if (!extraLabel || extraLabel->text() != QLatin1String("Extra") || extraLabel->font().pixelSize() != 12)
    return fail("extra caption is missing");
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
  // Joining a transmission already under way, half a symbol off the symbol
  // grid and without the start tones. Fldigi's block synchronizer looks at
  // two time slices per symbol, so the text after the first few blocks
  // must print.
  {
    std::vector<float> wave;
    if (!buildTxWave(QStringLiteral("Olivia 8/250"),
                     QStringLiteral("CQ CQ CQ DE N8SDR N8SDR N8SDR TEST TEST TEST PSE K"), 1500, &wave))
      return fail("Olivia modulator is missing");
    const int symbol = 1536;  // 32 ms at 48 kHz
    std::mt19937 joinRng(7);
    std::normal_distribution<float> joinNoise(0.0f, 0.25f);
    for (int eighth = 1; eighth < 8; ++eighth) {
      const int cut = 49152 + 10 * symbol + eighth * symbol / 8;  // after 1.024 s of start tones
      if (int(wave.size()) <= cut)
        return fail("Olivia join test wave is too short");
      std::vector<float> noisy(wave.begin() + cut, wave.end());
      for (float& v : noisy)
        v += joinNoise(joinRng);
      const QString joined =
          oliviaDecode(QStringLiteral("Olivia 8/250"), noisy.data(), int(noisy.size()), 48000, 1500);
      if (!joined.contains(QLatin1String("TEST TEST TEST PSE K"))) {
        qCritical("Olivia joined %d/8 symbol off: [%s]", eighth, qPrintable(joined));
        return fail("Olivia did not pull in when joining off the symbol grid");
      }
    }
  }
  {
    // MFSK64L: no lead-in, so the pass is the 2500-bit flush (1250
    // symbols) plus the text framing. The lead-in would add 416 symbols.
    const MfskPlan longPlan = planMfsk(QStringLiteral("MFSK64L"), QStringLiteral("E"), 1500);
    const int symbols = int(longPlan.samples.size()) / 768;
    if (symbols < 1250 || symbols > 1350) {
      qCritical("MFSK64L symbols %d", symbols);
      return fail("MFSK64L sends the lead-in Fldigi leaves out");
    }
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
  {
    std::vector<float> wave;
    if (!buildTxWave(QStringLiteral("MFSK16"), QStringLiteral("CQ CQ CQ"), 1500, &wave) || wave.size() < 1000)
      return fail("MFSK modulator is missing");
    // MFSK16 tone 8 sits 7.8 Hz above the cursor. A louder steady carrier
    // there has to be punctured or the whole block is that one tone.
    const double step = 2.0 * 3.14159265358979323846 * 1507.8125 / 48000.0;
    double phase = 0.0;
    for (float& carrier : wave) {
      carrier += 1.2f * float(std::sin(phase));
      phase += step;
    }
    const QString back = mfskDecode(QStringLiteral("MFSK16"), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1String("CQ"))) {
      qCritical("MFSK carrier [%s]", qPrintable(back));
      return fail("MFSK carrier puncture did not decode");
    }
  }
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
  const QString dominoOff =
      dominoDecode(QStringLiteral("DominoEX 16"), dominoCq.data(), int(dominoCq.size()), 48000, 1508);
  if (!dominoOff.contains(QLatin1String("CQ"))) {
    qCritical("DominoEX offset [%s]", qPrintable(dominoOff));
    return fail("DominoEX half-spacing mistune did not decode");
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
  const QString thorOff =
      thorDecode(QStringLiteral("Thor 16"), thorCq.data(), int(thorCq.size()), 48000, 1508);
  if (!thorOff.contains(QLatin1String("CQ"))) {
    qCritical("Thor offset [%s]", qPrintable(thorOff));
    return fail("Thor half-spacing mistune did not decode");
  }
  const auto* thor16 = window.findChild<QAction*>(QStringLiteral("modeThor16"));
  if (!thor16 || !thor16->toolTip().contains(QLatin1String("15.625")) ||
      !thor16->toolTip().contains(QLatin1String("START")) ||
      thor16->toolTip().contains(QLatin1String("does not key yet")))
    return fail("Thor 16 is missing");
  {
    const QString body = QStringLiteral("A\nB") + QChar(0x00E9);
    const MfskPlan mfskLatin = planMfsk(QStringLiteral("MFSK16"), body, 1500);
    const QString mfskBack = mfskDecode(QStringLiteral("MFSK16"), mfskLatin.samples.data(),
                                        int(mfskLatin.samples.size()), 48000, 1500);
    if (!mfskBack.contains(QStringLiteral("A\nB")) || !mfskBack.contains(QChar(0x00E9)) ||
        mfskBack.contains(QLatin1Char('\r'))) {
      qCritical("MFSK latin [%s]", qPrintable(mfskBack));
      return fail("MFSK newline or Latin-1 did not decode");
    }
    const DominoPlan dominoLatin = planDomino(QStringLiteral("DominoEX 16"), body, 1500);
    const QString dominoBack = dominoDecode(QStringLiteral("DominoEX 16"), dominoLatin.samples.data(),
                                            int(dominoLatin.samples.size()), 48000, 1500);
    if (!dominoBack.contains(QStringLiteral("A\nB")) || !dominoBack.contains(QChar(0x00E9))) {
      qCritical("Domino latin [%s]", qPrintable(dominoBack));
      return fail("DominoEX newline or Latin-1 did not decode");
    }
    const ThorPlan thorLatin = planThor(QStringLiteral("Thor 16"), body, 1500);
    const QString thorBack = thorDecode(QStringLiteral("Thor 16"), thorLatin.samples.data(),
                                        int(thorLatin.samples.size()), 48000, 1500);
    if (!thorBack.contains(QStringLiteral("A\nB")) || !thorBack.contains(QChar(0x00E9))) {
      qCritical("Thor latin [%s]", qPrintable(thorBack));
      return fail("Thor newline or Latin-1 did not decode");
    }
    const OliviaPlan oliviaLatin = planOlivia(QStringLiteral("Olivia 8/250"), body, 1500);
    const QString oliviaBack = oliviaDecode(QStringLiteral("Olivia 8/250"), oliviaLatin.samples.data(),
                                            int(oliviaLatin.samples.size()), 48000, 1500);
    if (!oliviaBack.contains(QStringLiteral("A\nB")) || !oliviaBack.contains(QChar(0x00E9)) ||
        oliviaBack.contains(QLatin1Char('\r'))) {
      qCritical("Olivia latin [%s]", qPrintable(oliviaBack));
      return fail("Olivia newline or Latin-1 did not decode");
    }
    // 17 symbols of silence moves the FEC boundary off phase 0. 1516 Hz
    // is one FFT bin off the 1500 Hz cursor. Both still have to print.
    std::vector<float> slipped(17 * 1536, 0.0f);
    slipped.insert(slipped.end(), oliviaLatin.samples.begin(), oliviaLatin.samples.end());
    const QString slipBack = oliviaDecode(QStringLiteral("Olivia 8/250"), slipped.data(),
                                          int(slipped.size()), 48000, 1500);
    if (!slipBack.contains(QStringLiteral("A\nB")) || !slipBack.contains(QChar(0x00E9))) {
      qCritical("Olivia slip [%s]", qPrintable(slipBack));
      return fail("Olivia block sync did not decode");
    }
    const QString offBack = oliviaDecode(QStringLiteral("Olivia 8/250"), oliviaLatin.samples.data(),
                                         int(oliviaLatin.samples.size()), 48000, 1516);
    if (!offBack.contains(QStringLiteral("A\nB")) || !offBack.contains(QChar(0x00E9))) {
      qCritical("Olivia offset [%s]", qPrintable(offBack));
      return fail("Olivia frequency search did not decode");
    }
    auto feed = [](auto* engine, const std::vector<float>& wave) {
      engine->setCenterHz(1500);
      engine->setSquelch(true, 15);
      engine->setAfc(true);
      engine->setArmed(true);
      const int chunk = 2048;
      for (int i = 0; i < int(wave.size()); i += chunk)
        engine->pushAudio(wave.data() + i, std::min(chunk, int(wave.size()) - i), 48000);
    };
    MfskEngine mfskLive;
    QString mfskGot;
    QObject::connect(&mfskLive, &MfskEngine::textReady, [&](const QString& text) { mfskGot += text; });
    mfskLive.setMode(QStringLiteral("MFSK16"));
    feed(&mfskLive, mfskLatin.samples);
    if (!mfskGot.contains(QStringLiteral("A\nB")) || !mfskGot.contains(QChar(0x00E9))) {
      qCritical("MFSK live [%s]", qPrintable(mfskGot));
      return fail("MFSK live receive did not decode");
    }
    OliviaEngine oliviaLive;
    QString oliviaGot;
    QObject::connect(&oliviaLive, &OliviaEngine::textReady, [&](const QString& text) { oliviaGot += text; });
    oliviaLive.setMode(QStringLiteral("Olivia 8/250"));
    feed(&oliviaLive, slipped);
    if (!oliviaGot.contains(QStringLiteral("A\nB")) || !oliviaGot.contains(QChar(0x00E9))) {
      qCritical("Olivia live [%s]", qPrintable(oliviaGot));
      return fail("Olivia live receive did not decode");
    }
    std::vector<float> noise(48000 * 2);
    std::mt19937 noiseRng(7);
    std::uniform_real_distribution<float> noiseDist(-0.25f, 0.25f);
    for (float& noiseSample : noise)
      noiseSample = noiseDist(noiseRng);
    MfskEngine mfskNoise;
    QString noiseGot;
    QObject::connect(&mfskNoise, &MfskEngine::textReady, [&](const QString& text) { noiseGot += text; });
    mfskNoise.setMode(QStringLiteral("MFSK16"));
    mfskNoise.setCenterHz(1500);
    mfskNoise.setSquelch(true, 70);
    mfskNoise.setArmed(true);
    mfskNoise.pushAudio(noise.data(), int(noise.size()), 48000);
    bool noiseLetter = false;
    for (const QChar ch : noiseGot) {
      if (ch.isLetter())
        noiseLetter = true;
    }
    if (noiseLetter) {
      qCritical("MFSK noise [%s]", qPrintable(noiseGot));
      return fail("MFSK squelch printed noise");
    }
  }
  for (const char* name : {"QPSK31", "QPSK63", "QPSK125", "QPSK250", "QPSK500"}) {
    std::vector<float> wave;
    // A full line, not one letter: a symbol-timing slip shows up as
    // garbled characters before the end of the line.
    if (!buildTxWave(QString::fromLatin1(name), QStringLiteral("CQ CQ DE N8SDR N8SDR PSE K"), 1500, &wave) ||
        wave.size() < 1000)
      return fail("QPSK modulator is missing");
    const QString back =
        pskDecode(QString::fromLatin1(name), wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1String("CQ CQ DE N8SDR N8SDR PSE K"))) {
      qCritical("QPSK decode %s [%s] samples %d", name, qPrintable(back), int(wave.size()));
      return fail("QPSK did not decode");
    }
  }
  // Speech read-back: phrase pacing, spelling, and the controls.
  {
    QString buffer = QStringLiteral("CQ CQ DE N8");
    if (!speechTakeReady(&buffer, false).isEmpty())
      return fail("Speech spoke a half-finished line");
    buffer += QStringLiteral("SDR K\nUR");
    const QString line = speechTakeReady(&buffer, false);
    if (line != QStringLiteral("CQ CQ DE N8SDR K\n") || buffer != QStringLiteral("UR"))
      return fail("Speech did not take the finished line");
    if (speechTakeReady(&buffer, true) != QStringLiteral("UR") || !buffer.isEmpty())
      return fail("Speech did not read the rest after the pause");
    buffer = QStringLiteral("ONE TWO THREE FOUR FIVE SIX SEVEN EIGHT NIN");
    if (speechTakeReady(&buffer, false) != QStringLiteral("ONE TWO THREE FOUR FIVE SIX SEVEN EIGHT ") ||
        buffer != QStringLiteral("NIN"))
      return fail("Speech did not read a long line by finished words");
    if (speechClean(QStringLiteral("CQ DE N8SDR UR 599 #$%& ~~ K"), true) !=
        QStringLiteral("CQ DE N 8 S D R UR 5 9 9 K"))
      return fail("Speech did not spell the call and report or drop the noise");
    if (!speechClean(QStringLiteral("#$%&"), false).contains(QLatin1Char('#')))
      return fail("Speech dropped symbols with Skip garbled off");
    (void)SpeechReader::voices();
    if (SpeechReader::outputs().isEmpty())
      return fail("Speech has no Windows default device entry");
    // Talk gate: 1 s quiet, 0.5 s of voice at about -23 dBFS, 1 s quiet.
    // Kept: the voice, 100 ms before it, and 300 ms after.
    std::vector<float> mic(size_t(SpeechInput::kRate) * 5 / 2, 0.0f);
    for (int i = SpeechInput::kRate; i < SpeechInput::kRate * 3 / 2; ++i)
      mic[size_t(i)] = float(0.1 * std::sin(2.0 * 3.14159265358979 * 300.0 * i / SpeechInput::kRate));
    const std::vector<float> kept = gateSpeech(mic, -45.0);
    if (kept.size() < size_t(SpeechInput::kRate) * 85 / 100 || kept.size() > size_t(SpeechInput::kRate) * 95 / 100) {
      qCritical("Talk gate kept %d samples", int(kept.size()));
      return fail("Talk gate did not keep the voice with its lead-in and hold");
    }
    if (!gateSpeech(std::vector<float>(size_t(SpeechInput::kRate), 0.001f), -45.0).empty())
      return fail("Talk gate let quiet room noise through");
    if (SpeechInput::inputs().isEmpty() || !window.findChild<QPushButton*>(QStringLiteral("txTalk")))
      return fail("Talk is missing");
    {
      const QList<SpeechModelInfo> models = speechModels();
      QStringList ids;
      for (const SpeechModelInfo& m : models) {
        if (m.sha256.size() != 64 || m.bytes < 10000000 || !m.url.startsWith(QStringLiteral("https://")) ||
            !m.url.endsWith(m.file) || ids.contains(m.id))
          return fail("Speech model list is malformed");
        ids << m.id;
      }
      if (models.size() != 3 || !speechModel(QStringLiteral("base.en")).bytes ||
          !speechModel(QStringLiteral("nope")).id.isEmpty() || speechModelInstalled(SpeechModelInfo{}) ||
          speechModelSize(77704715) != QStringLiteral("77.7 MB"))
        return fail("Speech model helpers are wrong");
      SpeechModelDownload probe;
      if (probe.start(SpeechModelInfo{}) || probe.busy())
        return fail("Speech model download started with no model");
    }
    {
      const QList<QPair<QString, QString>> cases = {
          {QStringLiteral("CQ CQ de Kilo Eight Alpha Bravo, Kilo Eight Alpha Bravo."),
           QStringLiteral("CQ CQ DE K8AB K8AB.")},
          {QStringLiteral("Your report is five nine nine."), QStringLiteral("YOUR REPORT IS 599.")},
          {QStringLiteral("I am in Ohio, seventy three."), QStringLiteral("I AM IN OHIO, 73.")},
          {QStringLiteral("This is K-8-A-B slash portable."), QStringLiteral("THIS IS K8AB/P.")},
          {QStringLiteral("um, name is Rick [BLANK_AUDIO]"), QStringLiteral("NAME IS RICK")},
          {QStringLiteral("W 8 X Y Z here"), QStringLiteral("W8XYZ HERE")},
          {QStringLiteral("de Kilo8 Alfa Bravo, Kilo8 Alfa Bravo."), QStringLiteral("DE K8AB K8AB.")},
          {QStringLiteral("I have a rig"), QStringLiteral("I HAVE A RIG")},
      };
      for (const auto& c : cases) {
        if (speechToTxText(c.first) != c.second) {
          qCritical().noquote() << speechToTxText(c.first);
          return fail("Speech clean-up gave the wrong text");
        }
      }
      if (!speechHint(QStringLiteral("n8sdr"), QStringLiteral("W1AW")).contains(QStringLiteral("N8SDR")))
        return fail("Speech hint is missing the call");
      const QStringList fixes = {QStringLiteral("hex beam = hexbeam"), QStringLiteral("DAYTON OHIO = Dayton, OH"),
                                 QStringLiteral("no equals here"), QStringLiteral(" = blank")};
      if (speechApplyCorrections(QStringLiteral("MY HEX BEAM IS UP IN DAYTON OHIO 73"), fixes) !=
              QLatin1String("MY HEXBEAM IS UP IN DAYTON, OH 73") ||
          speechApplyCorrections(QStringLiteral("HEX BEAMS"), fixes) != QLatin1String("HEX BEAMS"))
        return fail("Speech corrections replaced the wrong words");
      if (speechMyWords({QStringLiteral("W8AJT, Kenwood"), QStringLiteral("kenwood"), QStringLiteral(" Hexbeam ")}) !=
          QLatin1String("W8AJT, Kenwood, Hexbeam"))
        return fail("Speech My words did not join");
      SpeechRecognizer::cpuSupported();
      // Optional end-to-end check with an installed model: STD_SPEECH_WAV
      // names a 16 kHz mono 16-bit WAV of spoken text.
      const QString wav = qEnvironmentVariable("STD_SPEECH_WAV");
      SpeechModelInfo installed;
      for (const SpeechModelInfo& m : speechModels()) {
        if (speechModelInstalled(m))
          installed = m;
      }
      if (!wav.isEmpty() && !installed.id.isEmpty()) {
        QFile file(wav);
        if (!file.open(QIODevice::ReadOnly))
          return fail("Speech WAV would not open");
        const QByteArray data = file.readAll().mid(44);
        std::vector<float> pcm(size_t(data.size() / 2));
        for (size_t k = 0; k < pcm.size(); ++k)
          pcm[k] = float(qFromLittleEndian<qint16>(data.constData() + 2 * k)) / 32768.0f;
        SpeechRecognizer recognizer;
        QString heard;
        QString error;
        QEventLoop wait;
        QObject::connect(&recognizer, &SpeechRecognizer::done, &wait, [&](const QString& t, const QString& e) {
          heard = t;
          error = e;
          wait.quit();
        });
        QElapsedTimer took;
        took.start();
        if (!recognizer.start(speechModelPath(installed), gateSpeech(pcm, -45.0), speechHint(QString(), QString())))
          return fail("Recognizer did not start");
        QTimer::singleShot(120000, &wait, &QEventLoop::quit);
        wait.exec();
        qInfo().noquote() << "Speech" << installed.id << took.elapsed() << "ms raw:" << heard << "| tx:"
                          << speechToTxText(heard) << error;
        if (heard.isEmpty())
          return fail("Recognizer heard nothing in the WAV");
      }
    }
    if (!window.findChild<QAction*>(QStringLiteral("speechSettings")) ||
        !window.findChild<QPushButton*>(QStringLiteral("speechChip")) ||
        !window.findChild<QSpinBox*>(QStringLiteral("speechVolume")))
      return fail("Speech controls are missing");
  }
  // Live idle safety toggle. The self-test shares the operator's settings,
  // so only the range is checked here, not the saved on/off and seconds.
  {
    const auto* idleStop = window.findChild<QCheckBox*>(QStringLiteral("liveIdleStop"));
    const auto* idleSeconds = window.findChild<QComboBox*>(QStringLiteral("liveIdleSeconds"));
    if (!idleStop || !idleSeconds || idleSeconds->count() != 4 || idleSeconds->itemData(0).toInt() != 5 ||
        idleSeconds->itemData(3).toInt() != 25)
      return fail("Live TX idle safety choices are not 5, 10, 15, and 25 s");
    if (liveIdleChoice(120) != 25 || liveIdleChoice(12) != 10 || liveIdleChoice(4) != 5 ||
        liveIdleChoice(20) != 15)
      return fail("Live TX idle setting does not snap to a choice");
    if (liveIdleExpired(1000, 1000 + 24999, true, 25) || !liveIdleExpired(1000, 1000 + 25000, true, 25) ||
        liveIdleExpired(1000, 1000 + 999999, false, 25) || liveIdleExpired(0, 999999, true, 25))
      return fail("Live TX idle safety timing is wrong");
  }
  // Live typing: text, a pause of idle, more text, then the tail. The
  // receiver must print both pieces, and idle must not print anything.
  for (const char* name : {"PSK31", "PSK125", "QPSK31", "QPSK250", "RTTY"}) {
    const QString mode = QString::fromLatin1(name);
    auto live = makeLiveTextTx(mode, 1500, nullptr);
    if (!liveTextMode(mode) || !live)
      return fail("Live keyboard TX is missing");
    std::vector<float> wave;
    live->begin(&wave);
    for (const QChar ch : QStringLiteral("CQ CQ"))
      live->put(ch, &wave);
    const size_t before = wave.size();
    for (int i = 0; i < 30; ++i)
      live->idle(&wave);
    if (wave.size() <= before)
      return fail("Live keyboard TX idle added nothing");
    for (const QChar ch : QStringLiteral(" DE N8SDR K"))
      live->put(ch, &wave);
    live->end(&wave);
    const QString back = rttyMode(mode) ? rttyDecode(wave.data(), int(wave.size()), 48000, 1500)
                                        : pskDecode(mode, wave.data(), int(wave.size()), 48000, 1500);
    if (!back.contains(QLatin1String("CQ CQ")) || !back.contains(QLatin1String("DE N8SDR K"))) {
      qCritical("Live %s [%s]", name, qPrintable(back));
      return fail("Live keyboard TX did not decode across idle");
    }
    QString letters;
    for (const QChar ch : back) {
      if (!ch.isSpace())
        letters.append(ch);
    }
    if (letters != QLatin1String("CQCQDEN8SDRK")) {
      qCritical("Live %s idle printed [%s]", name, qPrintable(back));
      return fail("Live keyboard TX idle printed characters");
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
    const Js8Plan clean = planJs8(QStringLiteral("E"), 1500);
    if (int(clean.samples.size()) != js8FrameSamples())
      return fail("JS8 clean frame is missing");
    std::vector<float> js8Slip(2000, 0.0f);
    js8Slip.insert(js8Slip.end(), clean.samples.begin(), clean.samples.end());
    const QString slipped = js8Decode(js8Slip.data(), int(js8Slip.size()), 48000, 1500);
    if (!slipped.contains(QLatin1Char('E')))
      return fail("JS8 time search missed the frame");
    const Js8Plan shifted = planJs8(QStringLiteral("E"), 1504);
    const QString shiftedBack =
        js8Decode(shifted.samples.data(), int(shifted.samples.size()), 48000, 1500);
    if (!shiftedBack.contains(QLatin1Char('E')))
      return fail("JS8 frequency search missed the frame");
    std::vector<float> js8Erased = clean.samples;
    const int eraseAt = 24000 + 43 * 7680;
    if (eraseAt + 7680 > int(js8Erased.size()))
      return fail("JS8 symbol is outside the frame");
    for (int i = 0; i < 7680; ++i)
      js8Erased[static_cast<size_t>(eraseAt + i)] = 0.0f;
    const QString repaired = js8Decode(js8Erased.data(), int(js8Erased.size()), 48000, 1500);
    if (!repaired.contains(QLatin1Char('E')))
      return fail("JS8 LDPC did not repair a symbol");
    // Interop vectors from an independent transcription of JS8Call-improved's
    // encoder (Varicode.cpp packHuffMessage/pack72bits, JS8.cpp encode() with
    // CRC12 = boost::augmented_crc<12,0xc06> ^ 42). STD's tones must match
    // exactly, or JS8Call rejects the frame on its CRC.
    {
      struct Vector {
        const char* text;
        const char* tones;
      };
      const Vector vectors[] = {
          {"HELLO", "4256130644204246400051707535356745444256130416317177377777777777777327204256130"},
          {"CQ DE N8SDR", "4256130630053420512060014063467364474256130542203670677432354037777317664256130"},
      };
      for (const Vector& vec : vectors) {
        const Js8Plan plan = planJs8(QString::fromLatin1(vec.text), 1500);
        if (int(plan.samples.size()) != js8FrameSamples()) {
          qCritical("JS8 vector %s: %d samples, one frame is %d", vec.text, int(plan.samples.size()), js8FrameSamples());
          return fail("JS8 interop vector did not build one frame");
        }
        QString got;
        for (int sym = 0; sym < 79; ++sym) {
          const float* at = plan.samples.data() + 24000 + sym * 7680;
          int best = 0;
          double bestPower = -1.0;
          for (int tone = 0; tone < 8; ++tone) {
            const double w = 2.0 * 3.141592653589793 * (1500.0 + 6.25 * tone) / 48000.0;
            double re = 0.0;
            double im = 0.0;
            for (int n = 0; n < 7680; ++n) {
              re += at[n] * std::cos(w * n);
              im += at[n] * std::sin(w * n);
            }
            if (re * re + im * im > bestPower) {
              bestPower = re * re + im * im;
              best = tone;
            }
          }
          got += QChar(u'0' + best);
        }
        if (got != QLatin1String(vec.tones)) {
          qCritical("JS8 vector %s\n  got  %s\n  want %s", vec.text, qPrintable(got), vec.tones);
          return fail("JS8 frame does not match JS8Call-improved");
        }
      }
    }
    // Every frame type JS8Call sends in Normal mode, built by an independent
    // transcription of JS8Call-improved's packers (Varicode.cpp) and read back
    // with its DecodedText rules. The compressed ones use the JSC word table.
    {
      struct Frame {
        const char* frame;
        int type;
        const char* text;
      };
      const Frame frames[] = {
          {"2tju88QZOkJm", 3, "N8SDR: @HB HEARTBEAT EN80 "},
          {"2tju88QZPkJn", 3, "N8SDR: @ALLCALL CQ DX EN80 "},
          {"2Y-pe-ukv++u", 3, "KN4CRD: @ALLCALL CQ CQ CQ  "},
          {"SN5-lU-2KS00", 3, "KN4CRD: N8SDR SNR? "},
          {"SN5-lU-2KTaJ", 3, "KN4CRD: N8SDR SNR -12 "},
          {"VlWb7HSNwyw0", 3, "N8SDR/P: KN4CRD ACK "},
          {"VlWb7VGGOrO0", 3, "N8SDR: @ALLCALL QSL? "},
          {"VlWb7HSNwzqY", 3, "N8SDR: KN4CRD HEARTBEAT SNR +03 "},
          {"VlWb7HSNwyG0", 3, "N8SDR: KN4CRD GRID? "},
          {"Bu6SibJJqkJm", 1, "VE3/N8SDR: "},
          {"IMgO0wx8O+L0", 2, "J1Y/P ACK "},
          {"IY-pe+BnG+e0", 2, "KN4CRD/P SNR +07 "},
          {"tYuMzoX+++++", 0, "HELLO WORLD"},
          {"tkdKtlbDs9++", 2, "RICKS FOX 73"},
          {"tGnDXsWF++++", 4, "GOOD MORNING EVERYONE"},
      };
      for (const Frame& f : frames) {
        const std::vector<float> wave = js8FrameAudio(QString::fromLatin1(f.frame), f.type, 1500);
        if (int(wave.size()) != js8FrameSamples())
          return fail("JS8 test frame did not build");
        const QString got = js8Decode(wave.data(), int(wave.size()), 48000, 1500);
        if (got != QLatin1String(f.text)) {
          qCritical("JS8 frame %s\n  got  [%s]\n  want [%s]", f.frame, qPrintable(got), f.text);
          return fail("JS8 frame type not read the way JS8Call reads it");
        }
      }
    }
    {
      // JS8Call message structure built by STD: each message goes out and is
      // read back by STD's own decoder the way JS8Call prints it.
      struct Built {
        const char* text;
        const char* want;  // decoded text, or the start of it when partial
        bool exact;
      };
      const Built built[] = {
          {"@HB HEARTBEAT EM79", "N8SDR: @HB HEARTBEAT EM79 ", true},
          {"HB", "N8SDR: @HB HEARTBEAT EM79 ", true},
          {"CQ CQ CQ EM79", "N8SDR: @ALLCALL CQ CQ CQ EM79 ", true},
          {"CQ DX", "N8SDR: @ALLCALL CQ DX EM79 ", true},
          {"W4KUS SNR?", "N8SDR: W4KUS SNR? ", true},
          {"W4KUS SNR -11", "N8SDR: W4KUS SNR -11 ", true},
          {"W4KUS HEARTBEAT SNR -11", "N8SDR: W4KUS HEARTBEAT SNR -11 ", true},
          {"W4KUS/P INFO?", "N8SDR: W4KUS/P INFO? ", true},
          {"@ALLCALL STATUS?", "N8SDR: @ALLCALL STATUS? ", true},
          {"KN4CRD 73", "N8SDR: KN4CRD 73 ", true},
          {"W4KUS HELLO THERE", "N8SDR: W4KUS ", false},
          {"HELLO WORLD", "HELLO WORLD", true},
      };
      for (const Built& b : built) {
        const QString text = QString::fromLatin1(b.text);
        int used = 0;
        const Js8Plan plan = planJs8(text, 1500, 0, &used, true, QStringLiteral("N8SDR"), QStringLiteral("EM79AB"));
        if (used != text.size())
          return fail("JS8 message did not plan all its characters");
        const QString got = js8Decode(plan.samples.data(), int(plan.samples.size()), 48000, 1500);
        const bool ok = b.exact ? got == QLatin1String(b.want)
                                : got.startsWith(QLatin1String(b.want)) && got.endsWith(QLatin1String("HELLO THERE"));
        if (!ok) {
          qCritical("JS8 built [%s]\n  got  [%s]\n  want [%s]", b.text, qPrintable(got), b.want);
          return fail("a JS8Call message STD built did not decode the way JS8Call prints it");
        }
      }
      // A directed header is its own 15 s part; the rest follows as text.
      int headUsed = 0;
      planJs8(QStringLiteral("W4KUS HELLO"), 1500, 1, &headUsed, true, QStringLiteral("N8SDR"), QString());
      if (headUsed != 6)
        return fail("JS8 directed header did not take TO and its space");
      if (!js8Header(QStringLiteral("HELLO WORLD"), QStringLiteral("N8SDR"), QString()).bits.empty())
        return fail("JS8 plain text was mistaken for a directed message");
    }
    const Js8Plan low = planJs8(QStringLiteral("HELLO"), 1200);
    const Js8Plan high = planJs8(QStringLiteral("WORLD"), 1800);
    if (low.samples.size() != high.samples.size() || low.samples.size() < 1000)
      return fail("JS8 passband frames are missing");
    std::vector<float> both(low.samples.size());
    for (size_t i = 0; i < both.size(); ++i)
      both[i] = low.samples[i] + high.samples[i];
    const QString pair = js8Decode(both.data(), int(both.size()), 48000, 1500);
    if (!pair.contains(QLatin1String("HELLO")) || !pair.contains(QLatin1String("WORLD"))) {
      qCritical("JS8 pair [%s]", qPrintable(pair));
      return fail("JS8 passband missed a station");
    }
    // A JS8 period with nothing at the cursor and two stations with their
    // own clock offsets, decoded the way Js8Engine slices the audio: 2 s
    // before the 15 s boundary. One frame starts 0.4 s early, one 0.6 s late.
    {
      const int lead = 2 * 48000;
      std::vector<float> slice(static_cast<size_t>(18 * 48000), 0.0f);
      auto place = [&](const Js8Plan& plan, int shiftSamples) {
        for (size_t i = 0; i < plan.samples.size(); ++i) {
          const long long at = lead + shiftSamples + static_cast<long long>(i);
          if (at >= 0 && at < static_cast<long long>(slice.size()))
            slice[static_cast<size_t>(at)] += plan.samples[i];
        }
      };
      place(low, -int(0.4 * 48000));
      place(high, int(0.6 * 48000));
      std::mt19937 js8Rng(11);
      std::normal_distribution<float> js8Noise(0.0f, 0.05f);
      for (float& value : slice)
        value += js8Noise(js8Rng);
      QString period;
      for (const Js8Heard& hit : js8DecodePeriod(slice.data(), int(slice.size()), 48000, 1500, 2000))
        period += hit.text + QLatin1Char('|');
      if (!period.contains(QLatin1String("HELLO")) || !period.contains(QLatin1String("WORLD"))) {
        qCritical("JS8 period [%s]", qPrintable(period));
        return fail("JS8 passband scan missed a station off the cursor or off time");
      }
    }
  }
  {
    // AFC must hold a tone that is on tune and follow one that is 2 Hz off.
    // The second half-symbol correlator restarts its phase, so the carrier's
    // own advance has to come out of the estimate.
    const int rate = 8000;
    const int separ = 512;
    const int half = separ / 2;
    const double baud = 15.625;
    for (int tone = 0; tone < 4; ++tone) {
      const double nominal = 1500.0 - 15.0 * baud / 2.0 + double(tone) * baud;
      for (const double offset : {0.0, 2.0}) {
        std::vector<float> wave(static_cast<size_t>(separ * 3));
        for (size_t i = 0; i < wave.size(); ++i)
          wave[i] = float(std::sin(2.0 * 3.141592653589793 * (nominal + offset) * double(i) / rate + 0.3));
        SymbolClock clock;
        clock.setAfcEnabled(true);
        for (int round = 0; round < 200; ++round) {
          const double freq = nominal + clock.afcHz();
          double si0 = 0.0, sq0 = 0.0, si1 = 0.0, sq1 = 0.0;
          toneIq(wave.data(), int(wave.size()), separ, half, freq, rate, &si0, &sq0);
          toneIq(wave.data(), int(wave.size()), separ + half, half, freq, rate, &si1, &sq1);
          clock.observeFreq(si0, sq0, si1, sq1, half, rate, baud, freq, true, true);
        }
        if (std::fabs(clock.afcHz() - offset) > 0.3) {
          qCritical("AFC tone %d offset %.1f settled at %.2f", tone, offset, clock.afcHz());
          return fail("tone AFC does not settle on the signal");
        }
      }
    }
    SymbolClock ifk;
    ifk.setAfcEnabled(true);
    for (int round = 0; round < 200; ++round)
      ifk.observeOffset(3.0 - ifk.afcHz(), 15.625, true);
    if (std::fabs(ifk.afcHz() - 3.0) > 0.1)
      return fail("IFK AFC does not follow the grid peak");
  }
  {
    // Station A, two seconds of silence, then station B with its symbols half
    // a symbol away from A's. B must print: the timing loop has to pull in
    // within a few symbols, not one sample a symbol.
    auto twoStations = [](const QString& mode, std::vector<float>* out) {
      std::vector<float> first;
      std::vector<float> second;
      if (!buildTxWave(mode, QStringLiteral("CQ CQ DE N8SDR"), 1500, &first) ||
          !buildTxWave(mode, QStringLiteral("THE QUICK BROWN FOX JUMPS"), 1500, &second))
        return false;
      // MFSK16 and Thor 16 are both 15.625 baud: 3072 samples at 48 kHz.
      // Pad A plus about 2 s of silence to whole symbols, then add half a
      // symbol, so B's symbols sit exactly half a symbol off A's.
      constexpr size_t symbol48 = 3072;
      out->assign(first.begin(), first.end());
      size_t gap = out->size() + 2 * 48000;
      gap = (gap + symbol48 - 1) / symbol48 * symbol48 + symbol48 / 2;
      out->resize(gap, 0.0f);
      out->insert(out->end(), second.begin(), second.end());
      out->resize(out->size() + 48000, 0.0f);
      return true;
    };
    auto feedQuiet = [](auto* engine, const std::vector<float>& wave) {
      engine->setCenterHz(1500);
      engine->setSquelch(false, 0);
      engine->setAfc(false);
      engine->setArmed(true);
      for (int i = 0; i < int(wave.size()); i += 2048)
        engine->pushAudio(wave.data() + i, std::min(2048, int(wave.size()) - i), 48000);
    };
    std::vector<float> pair;
    if (!twoStations(QStringLiteral("MFSK16"), &pair))
      return fail("MFSK16 two-station wave did not build");
    MfskEngine mfskPull;
    QString mfskPullGot;
    QObject::connect(&mfskPull, &MfskEngine::textReady, [&](const QString& text) { mfskPullGot += text; });
    mfskPull.setMode(QStringLiteral("MFSK16"));
    feedQuiet(&mfskPull, pair);
    if (!mfskPullGot.contains(QLatin1String("BROWN FOX"))) {
      qCritical("MFSK16 second station [%s]", qPrintable(mfskPullGot));
      return fail("MFSK16 did not pull in on a second station");
    }
    if (!twoStations(QStringLiteral("Thor 16"), &pair))
      return fail("Thor 16 two-station wave did not build");
    ThorEngine thorPull;
    QString thorPullGot;
    QObject::connect(&thorPull, &ThorEngine::textReady, [&](const QString& text) { thorPullGot += text; });
    thorPull.setMode(QStringLiteral("Thor 16"));
    feedQuiet(&thorPull, pair);
    if (!thorPullGot.contains(QLatin1String("BROWN FOX"))) {
      qCritical("Thor 16 second station [%s]", qPrintable(thorPullGot));
      return fail("Thor 16 did not pull in on a second station");
    }
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
    const auto* band = window.findChild<WaterfallWidget*>();
    if (!band || !band->channelCentered())
      return fail("RTTY marker is not centered on the carrier");
    const bool cleared = echo->toPlainText().isEmpty() && box->toPlainText().isEmpty() &&
                         list->toPlainText().isEmpty();
    ft8Mode->trigger();
    if (band->channelCentered())
      return fail("FT8 marker is centered");
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
  // Keyboard modes take what went out off the TX box, as Fldigi does.
  if (textAfterSent(QStringLiteral("CQ CQ de N8SDR") + QChar(u'\n') + QStringLiteral("W1AW de N8SDR"),QStringLiteral("CQ CQ de N8SDR"), true) !=
          QLatin1String("W1AW de N8SDR") ||
      textAfterSent(QStringLiteral("CQ CQ de N8SDR"), QStringLiteral("CQ CQ"), false) !=
          QLatin1String(" de N8SDR") ||
      textAfterSent(QStringLiteral("  CQ test"), QStringLiteral("CQ test"), true) != QString() ||
      textAfterSent(QStringLiteral("edited text"), QStringLiteral("CQ test"), true) !=
          QLatin1String("edited text"))
    return fail("sent keyboard text is not taken off the TX box");
  const auto* js8 = window.findChild<QAction*>(QStringLiteral("modeJs8"));
  if (!js8 || !js8->toolTip().contains(QLatin1String("15")) ||
      !js8->toolTip().contains(QLatin1String("START")) ||
      js8->toolTip().contains(QLatin1String("does not key yet")))
    return fail("JS8 is missing");
  {
    // JS8 operating screen: shown only in JS8, fed by heard frames.
    QSettings keep;
    const QVariant callSaved = keep.value(QStringLiteral("dx/reportCall"));
    const QVariant gridSaved = keep.value(QStringLiteral("dx/reportGrid"));
    keep.setValue(QStringLiteral("dx/reportCall"), QStringLiteral("N8SDR"));
    keep.setValue(QStringLiteral("dx/reportGrid"), QStringLiteral("EM79"));
    auto restore = [&] {
      if (callSaved.isValid())
        keep.setValue(QStringLiteral("dx/reportCall"), callSaved);
      else
        keep.remove(QStringLiteral("dx/reportCall"));
      if (gridSaved.isValid())
        keep.setValue(QStringLiteral("dx/reportGrid"), gridSaved);
      else
        keep.remove(QStringLiteral("dx/reportGrid"));
      keep.sync();
    };
    auto* js8Action = window.findChild<QAction*>(QStringLiteral("modeJs8"));
    auto* ft8Action = window.findChild<QAction*>(QStringLiteral("modeFt8"));
    auto* engine = window.findChild<Js8Engine*>();
    auto* strip = window.findChild<QWidget*>(QStringLiteral("js8Strip"));
    auto* actions = window.findChild<QWidget*>(QStringLiteral("js8Actions"));
    auto* screen = window.findChild<QWidget*>(QStringLiteral("js8Receive"));
    auto* band = window.findChild<QTreeWidget*>(QStringLiteral("js8BandActivity"));
    auto* calls = window.findChild<QTreeWidget*>(QStringLiteral("js8CallActivity"));
    auto* talk = window.findChild<QTextEdit*>(QStringLiteral("js8Conversation"));
    auto* offset = window.findChild<QSpinBox*>(QStringLiteral("js8Offset"));
    auto* snrAsk = window.findChild<QPushButton*>(QStringLiteral("js8Snr"));
    if (!js8Action || !ft8Action || !engine || !strip || !actions || !screen || !band || !calls || !talk ||
        !offset || !snrAsk) {
      restore();
      return fail("the JS8 screen is missing");
    }
    auto* dialBox = window.findChild<QFrame*>(QStringLiteral("dialBox"));
    auto* dialFreq = window.findChild<QLabel*>(QStringLiteral("dialFreq"));
    auto* dialRole = window.findChild<QLabel*>(QStringLiteral("dialRole"));
    auto* dialSide = window.findChild<QLabel*>(QStringLiteral("dialSide"));
    auto* brand = window.findChild<QToolBar*>(QStringLiteral("brandBar"));
    if (!dialBox || !dialFreq || !dialRole || !dialSide || !brand || dialBox->parentWidget() == nullptr ||
        !brand->isAncestorOf(dialBox) || !dialBox->toolTip().contains(QLatin1String("green")) ||
        !dialBox->toolTip().contains(QLatin1String("red")) ||
        !dialBox->toolTip().contains(QLatin1String("DIGU")) ||
        !dialBox->styleSheet().contains(QLatin1String("#3a4250")) ||
        (dialSide->text() != QLatin1String("DIGU") && dialSide->text() != QLatin1String("DIGL")) ||
        !(dialFreq->alignment() & Qt::AlignHCenter) || dialFreq->font().weight() < QFont::Bold) {
      restore();
      return fail("the dial box is missing");
    }
    ft8Action->trigger();
    if (auto* host = dialBox->parentWidget()) {
      if (host->layout())
        host->layout()->activate();
      if (host->width() > 640 && dialBox->width() > 0) {
        const int center = dialBox->x() + dialBox->width() / 2;
        if (qAbs(center - host->width() / 2) > 140) {
          restore();
          return fail("the dial box is not centered");
        }
      }
    }
    if (strip->isVisibleTo(&window) || actions->isVisibleTo(&window) || screen->isVisibleTo(&window) ||
        !dialBox->isVisibleTo(&window) || dialSide->text() != QLatin1String("DIGU")) {
      restore();
      return fail("the JS8 screen shows outside JS8");
    }
    js8Action->trigger();
    const auto* echo = window.findChild<QTextEdit*>(QStringLiteral("rxEcho"));
    if (auto* host = dialBox->parentWidget(); host && host->layout())
      host->layout()->activate();
    if (!strip->isVisibleTo(&window) || !actions->isVisibleTo(&window) || !screen->isVisibleTo(&window) ||
        !dialBox->isVisibleTo(&window) ||
        (dialBox->x() > 0 && strip->x() > 0 && dialBox->x() >= strip->x()) ||
        (echo && echo->isVisibleTo(&window))) {
      restore();
      return fail("the JS8 screen does not replace the plain receive pane in JS8");
    }
    if (snrAsk->isEnabled()) {
      restore();
      return fail("JS8 directed buttons are live with no station picked");
    }
    offset->setValue(1500);
    // W4KUS answers our heartbeat on our offset in one frame; KN4CRD
    // sends a two-frame directed message elsewhere; a CQ gives a grid.
    emit engine->frameHeard(QStringLiteral("W4KUS: N8SDR HEARTBEAT SNR -11 "), -9, 1502, true);
    emit engine->frameHeard(QStringLiteral("KN4CRD: N8SDR  "), 3, 900, false);
    emit engine->frameHeard(QStringLiteral("HELLO RICK"), 2, 901, true);
    emit engine->frameHeard(QStringLiteral("K3ATA: @ALLCALL CQ CQ CQ FN20 "), -14, 2100, true);
    if (band->topLevelItemCount() != 3 || calls->topLevelItemCount() != 4) {
      qCritical("JS8 band %d calls %d", band->topLevelItemCount(), calls->topLevelItemCount());
      restore();
      return fail("JS8 activity lists did not fill");
    }
    if (!band->topLevelItem(0)->text(3).startsWith(QLatin1String("KN4CRD: N8SDR HELLO RICK"))) {
      qCritical("JS8 band row [%s]", qPrintable(band->topLevelItem(0)->text(3)));
      restore();
      return fail("JS8 frames on one offset did not join into one message");
    }
    QString k3Grid;
    for (int i = 0; i < calls->topLevelItemCount(); ++i) {
      if (calls->topLevelItem(i)->text(0) == QLatin1String("K3ATA"))
        k3Grid = calls->topLevelItem(i)->text(4);
    }
    if (calls->topLevelItem(0)->text(0) != QLatin1String("@ALLCALL") || k3Grid != QLatin1String("FN20")) {
      restore();
      return fail("JS8 call activity lost @ALLCALL or the CQ grid");
    }
    const QString said = talk->toPlainText();
    if (!said.contains(QLatin1String("(1502) - W4KUS: N8SDR HEARTBEAT SNR -11")) ||
        !said.contains(QLatin1String("KN4CRD: N8SDR HELLO RICK")) || said.contains(QLatin1String("K3ATA"))) {
      qCritical("JS8 conversation [%s]", qPrintable(said));
      restore();
      return fail("JS8 conversation did not keep just the messages to us");
    }
    emit calls->itemClicked(calls->topLevelItem(1), 0);
    const auto* to = window.findChild<QLabel*>(QStringLiteral("js8To"));
    if (!snrAsk->isEnabled() || !to || !to->text().endsWith(calls->topLevelItem(1)->text(0))) {
      restore();
      return fail("picking a JS8 station did not arm the directed buttons");
    }
    emit calls->itemDoubleClicked(calls->topLevelItem(1), 0);
    const auto* fall = window.findChild<WaterfallWidget*>();
    if (!fall || fall->txHz() != offset->value() || fall->rxHz() != offset->value()) {
      restore();
      return fail("JS8 double-click did not move the offset");
    }
    if (auto* clr = window.findChild<QPushButton*>(QStringLiteral("clrRx")))
      clr->click();
    if (band->topLevelItemCount() != 0 || !talk->toPlainText().isEmpty()) {
      restore();
      return fail("CLR RX left JS8 activity behind");
    }
    ft8Action->trigger();
    restore();
  }
  {
    // WEFAX: the profile follows the mode, the transmit controls and the
    // macros go away, and the stations pane tunes by region.
    auto* faxAction = window.findChild<QAction*>(QStringLiteral("modeWefax576"));
    auto* ft8Back = window.findChild<QAction*>(QStringLiteral("modeFt8"));
    auto* hellAction = window.findChild<QAction*>(QStringLiteral("modeFeldHell"));
    auto* profile = window.findChild<ModeProfile*>();
    if (!faxAction || !ft8Back || !profile)
      return fail("WEFAX mode action is missing");
    if (hellAction)
      hellAction->trigger();
    faxAction->trigger();
    if (profile->digitalMode() != QLatin1String("WEFAX-576")) {
      qCritical("WEFAX profile mode [%s]", qPrintable(profile->digitalMode()));
      return fail("WEFAX left the previous mode in charge");
    }
    const auto* macros = window.findChild<QDockWidget*>(QStringLiteral("macroDock"));
    const auto* txBox = window.findChild<QPlainTextEdit*>(QStringLiteral("txHell"));
    const auto* txKey = window.findChild<QPushButton*>(QStringLiteral("txSend"));
    const auto* stations = window.findChild<QWidget*>(QStringLiteral("wefaxStations"));
    auto* faxRegion = window.findChild<QComboBox*>(QStringLiteral("wefaxRegion"));
    if ((macros && macros->isVisibleTo(&window)) || (txBox && txBox->isVisibleTo(&window)) ||
        (txKey && txKey->isVisibleTo(&window)) || !stations || !stations->isVisibleTo(&window) || !faxRegion)
      return fail("WEFAX shows transmit controls or macros, or no stations pane");
    {
      // Left-edge shift turns every line: pixel 100 of the raw picture
      // becomes pixel 0 of the shown one.
      WefaxEngine shifted;
      shifted.setArmed(true);
      shifted.startNow();
      std::vector<unsigned char> gray(64 * 8);
      for (size_t i = 0; i < gray.size(); ++i)
        gray[i] = static_cast<unsigned char>((i % 64) < 32 ? 0 : 255);
      WefaxSpec spec;
      const std::vector<float> tone = wefaxModulateTest(spec, 48000, gray, 64, 8);
      shifted.pushAudio(tone.data(), int(tone.size()), 48000);
      const QImage raw = shifted.picture();
      shifted.setShift(100);
      const QImage moved = shifted.picture();
      if (raw.isNull() || moved.size() != raw.size() || moved.pixel(0, 2) != raw.pixel(100, 2) ||
          moved.pixel(raw.width() - 100, 2) != raw.pixel(0, 2))
        return fail("WEFAX left-edge shift does not turn the lines");
    }
    for (const char* name : {"hellSave", "hellOpen"}) {
      const auto* w = window.findChild<QWidget*>(QLatin1String(name));
      if (!w || w->isVisibleTo(&window))
        return fail("Hell tape Save shows outside Hell");
    }
    {
      // QSO bar: keyboard modes only, never WEFAX (or WSPR, JS8, FT8).
      auto* qsoBar = window.findChild<QWidget*>(QStringLiteral("qsoBar"));
      if (!qsoBar || qsoBar->isVisibleTo(&window))
        return fail("the QSO bar shows in WEFAX");
      if (hellAction) {
        hellAction->trigger();
        auto* call = window.findChild<QLineEdit*>(QStringLiteral("qsoBarCall"));
        auto* out = window.findChild<QLineEdit*>(QStringLiteral("qsoBarRstOut"));
        auto* on = window.findChild<QLabel*>(QStringLiteral("qsoBarOn"));
        auto* clearQso = window.findChild<QPushButton*>(QStringLiteral("qsoBarClear"));
        if (!qsoBar->isVisibleTo(&window) || !call || !out || !on || !clearQso)
          return fail("the QSO bar is missing in Hell");
        call->setText(QStringLiteral("W1AW"));
        if (on->text().isEmpty())
          return fail("the QSO bar did not set the time on");
        out->setText(QStringLiteral("579"));
        clearQso->click();
        if (!call->text().isEmpty() || out->text() != QLatin1String("599") || !on->text().isEmpty())
          return fail("QSO bar Clear did not reset the fields");
        faxAction->trigger();
      }
      if (!looksLikeCallsign(QStringLiteral("w1aw")) || !looksLikeCallsign(QStringLiteral("VE3/N8SDR/P")) ||
          looksLikeCallsign(QStringLiteral("HELLO")) || looksLikeCallsign(QStringLiteral("599")))
        return fail("QSO callsign check is wrong");
      if (rstFromDbm(-73.0, true) != QLatin1String("599") || rstFromDbm(-97.0, true) != QLatin1String("559") ||
          rstFromDbm(-140.0, true) != QLatin1String("519") || rstFromDbm(-40.0, false) != QLatin1String("59"))
        return fail("RST from the S-meter is wrong");
      {
        // Auto RST: a lone spike does not count, a steady signal over the
        // floor does, and a level under the floor does not.
        RstMeter meter;
        qint64 ms = 0;
        for (int i = 0; i < 40; ++i, ms += 250)
          meter.add(ms, -100.0);
        if (meter.level(ms))
          return fail("Auto RST counted the noise floor");
        meter.add(ms += 250, -60.0);
        if (meter.level(ms) && *meter.level(ms) > -73.0)
          return fail("Auto RST let one spike reach S9");
        for (int i = 0; i < 6; ++i)
          meter.add(ms += 250, -85.0);
        const auto steady = meter.level(ms);
        if (!steady || rstFromDbm(*steady, true) != QLatin1String("579"))
          return fail("Auto RST missed a steady signal");
        meter.addSnr(ms, 1.0);
        if (meter.level(ms))
          return fail("Auto RST ignored the radio's SNR");
      }
      QsoLog::Record qr;
      qr.call = QStringLiteral("w1aw");
      qr.name = QStringLiteral("Hiram");
      qr.qth = QStringLiteral("Newington");
      qr.state = QStringLiteral("CT");
      qr.rstRcvd = QStringLiteral("579");
      const QString line = QsoLog::adifRecord(qr);
      if (!line.contains(QLatin1String("<NAME:5>Hiram")) || !line.contains(QLatin1String("<QTH:9>Newington")) ||
          !line.contains(QLatin1String("<STATE:2>CT")) || !line.contains(QLatin1String("<RST_RCVD:3>579")))
        return fail("the QSO bar fields are not in the ADIF record");
      QHash<QString, QString> qsoTags;
      qsoTags.insert(QStringLiteral("<CALL>"), QStringLiteral("W1AW"));
      qsoTags.insert(QStringLiteral("<RST>"), QStringLiteral("579"));
      qsoTags.insert(QStringLiteral("<NAME>"), QStringLiteral("Hiram"));
      const QString sent = macroInsertText(QStringLiteral("<CALL> de <MYCALL> ur <RST> <NAME> <LOG><CLRQSO>"),
                                           QStringLiteral("N8SDR"), QStringLiteral("EM79"), qsoTags);
      if (sent != QLatin1String("W1AW de N8SDR ur 579 Hiram"))
        return fail("QSO bar tags did not fill the macro");
    }
    for (const char* name : {"wefaxAutoSave", "wefaxFit", "wefaxOpen", "wefaxStart", "wefaxSave"}) {
      const auto* w = window.findChild<QWidget*>(QLatin1String(name));
      if (!w || !w->isVisibleTo(&window))
        return fail("a WEFAX picture control is missing");
    }
    QSettings faxKeep;
    const QVariant regionSaved = faxKeep.value(QStringLiteral("wefax/region"));
    auto freqButtons = [&window] {
      QStringList out;
      for (auto* b : window.findChildren<QPushButton*>(QStringLiteral("wefaxFreq")))
        out.append(QString::number(b->property("khz").toDouble(), 'f', 1));
      return out;
    };
    faxRegion->setCurrentIndex(faxRegion->findData(QStringLiteral("1")));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QStringList europe = freqButtons();
    faxRegion->setCurrentIndex(faxRegion->findData(QStringLiteral("2")));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QStringList americas = freqButtons();
    QPushButton* boston = nullptr;
    for (auto* b : window.findChildren<QPushButton*>(QStringLiteral("wefaxFreq"))) {
      if (QString::number(b->property("khz").toDouble(), 'f', 1) == QLatin1String("4235.0"))
        boston = b;
    }
    auto* fallFax = window.findChild<WaterfallWidget*>();
    if (fallFax)
      fallFax->setRxHz(1500);
    if (boston)
      boston->click();
    if (regionSaved.isValid())
      faxKeep.setValue(QStringLiteral("wefax/region"), regionSaved);
    else
      faxKeep.remove(QStringLiteral("wefax/region"));
    if (!europe.contains(QLatin1String("3855.0")) || europe.contains(QLatin1String("4235.0")) ||
        !americas.contains(QLatin1String("4235.0")) || !boston || !fallFax || fallFax->rxHz() != 1900)
      return fail("WEFAX region stations or the 1900 Hz marker are wrong");
    ft8Back->trigger();
    if (profile->digitalMode() != QLatin1String("FT8") || !stations->isHidden())
      return fail("leaving WEFAX did not restore the mode");
  }
  {
    // WSPR rows stay in the receive colour after a red TX echo, and a rule
    // separates one 2-minute period from the next.
    auto* wsprAction = window.findChild<QAction*>(QStringLiteral("modeWspr"));
    auto* ft8Back = window.findChild<QAction*>(QStringLiteral("modeFt8"));
    auto* wsprEngine = window.findChild<WsprEngine*>();
    auto* echo = window.findChild<QTextEdit*>(QStringLiteral("rxEcho"));
    if (!wsprAction || !ft8Back || !wsprEngine || !echo)
      return fail("WSPR receive pieces are missing");
    wsprAction->trigger();
    QTextCursor red(echo->document());
    red.movePosition(QTextCursor::End);
    QTextCharFormat redFormat;
    redFormat.setForeground(txEchoColor());
    red.insertText(QStringLiteral("N8SDR EM79 37"), redFormat);
    WsprSpot spot;
    spot.message = QStringLiteral("AB8EF EN63 37");
    spot.audioHz = 1500.0;
    const qint64 period = 1793000000000LL;
    emit wsprEngine->spotDecoded(spot, period);
    spot.message = QStringLiteral("WR4I FM08 23");
    emit wsprEngine->spotDecoded(spot, period + 120000);
    QTextCursor last(echo->document());
    last.movePosition(QTextCursor::End);
    last.movePosition(QTextCursor::PreviousCharacter);
    const QColor shown = last.charFormat().foreground().color();
    const QString text = echo->toPlainText();
    ft8Back->trigger();
    if (shown != rxEchoColor())
      return fail("WSPR decodes took the red TX echo colour");
    if (!text.contains(QChar(0x2500)) || text.indexOf(QChar(0x2500)) < text.indexOf(QLatin1String("AB8EF")))
      return fail("WSPR periods are not separated by a rule");
  }
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
    // The operator's 7.074 MHz setup: one slot (restricted dial), Queue 4,
    // Periods 2, CNS on, CQ free on, sort by distance. Three stations answer
    // the CQ in one period; the first is worked, the other two queue.
    MaEngine live;
    live.setDigitalMode(QStringLiteral("FT8"));
    live.setRestricted(true);
    live.setStation(QStringLiteral("N8SDR"), QStringLiteral("EM79"));
    live.setMode(MaEngine::Mode::Standard);
    live.setSlotCount(1);
    live.setQueueLimit(4);
    live.setMaxTry(2, true, 15.0);
    live.setSort(1);
    live.setCns(true);
    live.setCqOnFreeSlot(true);
    live.setCqLine(QStringLiteral("CQ N8SDR EM79"));
    live.setDupeCheck([](const QString&) { return false; });
    live.txStarted();
    live.txEnded();
    live.decoded(QStringLiteral("N8SDR W9MDM EN61"), 14, 1442, false);
    live.decoded(QStringLiteral("N8SDR KC4WQ EM78"), 17, 1112, false);
    live.decoded(QStringLiteral("N8SDR WB9LWQ EM69"), -16, 2330, false);
    qInfo("MA live: now %d queue %d msg [%s]", int(live.now().size()), int(live.queue().size()),
          qPrintable(live.message()));
    if (live.now().size() != 1 || live.queue().size() != 2 || live.message().startsWith(QLatin1String("CQ")))
      return fail("MA Std with one slot did not work the first caller and queue the rest");
    // The next period opens: the report goes out and the queue stays.
    live.txStarted();
    if (live.message() != QLatin1String("W9MDM N8SDR +14") || live.queue().size() != 2)
      return fail("MA Std lost the caller when the TX period opened");
    live.txEnded();
    live.decoded(QStringLiteral("N8SDR W9MDM R-09"), 14, 1442, false);
    live.txStarted();
    if (live.message() != QLatin1String("W9MDM N8SDR RR73"))
      return fail("MA Std did not send RR73 after the R-report");
    const QVector<MaEngine::Logged> liveDone = live.txEnded();
    // Sort by distance takes the farthest caller next: WB9LWQ, then KC4WQ.
    if (liveDone.size() != 1 || !live.message().startsWith(QLatin1String("WB9LWQ N8SDR")) ||
        live.queue().size() != 1)
      return fail("MA Std did not move to the next queued caller after RR73");
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

    // MSHV TX Confirmation If 73 Or RR73: on answers their RR73 with 73,
    // off ends the QSO on it.
    for (const bool cfm : {true, false}) {
      MaEngine c;
      c.setStation(QStringLiteral("N8SDR"), QStringLiteral("EM79"));
      c.setMode(MaEngine::Mode::Standard);
      c.setSlotCount(2);
      c.setCfm73(cfm);
      c.decoded(QStringLiteral("N8SDR K1ABC FN42"), -5, 1200, false);
      c.decoded(QStringLiteral("N8SDR K1ABC R-10"), -5, 1200, false);
      c.decoded(QStringLiteral("N8SDR K1ABC RR73"), -5, 1200, false);
      const bool sends73 = c.message().contains(QLatin1String("K1ABC N8SDR 73"));
      if (sends73 != cfm)
        return fail(cfm ? "MA Std with TX Confirmation on did not answer RR73 with 73"
                        : "MA Std with TX Confirmation off still sent 73 after RR73");
    }

    // Contest mode: exchanges, Enter Sends Message, dupes, and the log fields.
    {
      const ContestTemplate& ru = contestTemplate(QStringLiteral("rtty-ru"));
      const ContestTemplate& fd = contestTemplate(QStringLiteral("field-day"));
      const ContestTemplate& ser = contestTemplate(QStringLiteral("serial"));
      const ContestTemplate& ww = contestTemplate(QStringLiteral("cqww-rtty"));
      if (contestExchange(ru, QStringLiteral("oh"), 5) != QLatin1String("599 OH") ||
          contestExchange(ser, QStringLiteral("#"), 7) != QLatin1String("599 007") ||
          contestExchange(fd, QStringLiteral("2A OH"), 1) != QLatin1String("2A OH"))
        return fail("contest exchange out is wrong");
      if (contestExchangeHeard(ru, QStringLiteral("N8SDR DE W1AW W1AW 599 MA 599 MA K"), QStringLiteral("W1AW"),
                               QStringLiteral("N8SDR")) != QLatin1String("MA") ||
          contestExchangeHeard(fd, QStringLiteral("N8SDR W1AW 3A CT 3A CT"), QStringLiteral("W1AW"),
                               QStringLiteral("N8SDR")) != QLatin1String("3A CT") ||
          contestExchangeHeard(ser, QStringLiteral("TU 599 012 012"), QStringLiteral("K8AB"),
                               QStringLiteral("N8SDR")) != QLatin1String("012") ||
          contestExchangeHeard(ww, QStringLiteral("N8SDR 599 05 OH 599 05 OH"), QStringLiteral("K8AB"),
                               QStringLiteral("N8SDR")) != QLatin1String("05 OH") ||
          !contestExchangeHeard(ru, QStringLiteral("CQ RU W1AW W1AW"), QStringLiteral("W1AW"),
                                QStringLiteral("N8SDR"))
               .isEmpty())
        return fail("contest exchange heard is wrong");
      if (contestEnterMessage(true, true, true) != 0 || contestEnterMessage(true, false, true) != 1 ||
          contestEnterMessage(true, false, false) != 2 || contestEnterMessage(false, true, true) != -1 ||
          contestEnterMessage(false, false, true) != 3 || contestEnterMessage(false, false, false) != 1)
        return fail("contest Enter sends the wrong message");
      for (const ContestTemplate& t : contestTemplates()) {
        if (t.run.size() != 8 || t.sp.size() != 8 || t.labels.size() != 8 || t.spLabels.size() != 8)
          return fail("contest template does not have eight messages");
        if (!t.run.at(2).contains(QLatin1String("<LOG>")) || !t.sp.at(1).contains(QLatin1String("<LOG>")))
          return fail("contest TU message does not log");
      }
      MacroContext mc;
      mc.myCall = QStringLiteral("N8SDR");
      mc.tags.insert(QStringLiteral("<CALL>"), QStringLiteral("W1AW"));
      mc.tags.insert(QStringLiteral("<XOUT>"), QStringLiteral("599 OH"));
      mc.tags.insert(QStringLiteral("<XIN>"), QString());
      const MacroPlan exchPlan = planMacro(ru.run.at(1), mc);
      if (exchPlan.text != QLatin1String("W1AW 599 OH 599 OH") || !exchPlan.keys || !exchPlan.backToRx)
        return fail("contest exchange message did not fill");
      if (!planMacro(QStringLiteral("<TX>TU <XIN><RX>"), mc).missing.contains(QStringLiteral("Exch")))
        return fail("an empty <XIN> was not reported");

      QTemporaryDir cdir;
      const QString adi = cdir.filePath(QStringLiteral("contest.adi"));
      QFile cf(adi);
      if (!cf.open(QIODevice::WriteOnly))
        return fail("contest sample log did not write");
      cf.write("<ADIF_VER:5>3.1.0<EOH>\n"
               "<CALL:4>W1AW<BAND:3>40m<QSO_DATE:8>20260103<TIME_ON:6>180000<CONTEST_ID:9>ARRL-RTTY<EOR>\n"
               "<CALL:4>K8AB<BAND:3>20m<QSO_DATE:8>20250104<TIME_ON:4>1200<CONTEST_ID:9>ARRL-RTTY<EOR>\n"
               "<CALL:4>N0XX<BAND:3>40m<QSO_DATE:8>20260103<TIME_ON:6>181000<CONTEST_ID:6>ARRL-FD<EOR>\n");
      cf.close();
      const QDateTime since(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::utc());
      const QSet<QString> worked = contestWorked(adi, QStringLiteral("ARRL-RTTY"), since);
      if (!worked.contains(contestDupeKey(QStringLiteral("w1aw"), QStringLiteral("40m"))) ||
          worked.contains(contestDupeKey(QStringLiteral("W1AW"), QStringLiteral("20m"))) ||
          worked.contains(contestDupeKey(QStringLiteral("K8AB"), QStringLiteral("20m"))) ||
          worked.contains(contestDupeKey(QStringLiteral("N0XX"), QStringLiteral("40m"))))
        return fail("contest dupes read the wrong QSOs");

      QsoLog::Record cr;
      cr.call = QStringLiteral("W1AW");
      cr.contestId = QStringLiteral("ARRL-RTTY");
      cr.stxString = QStringLiteral("599 007");
      cr.srxString = QStringLiteral("012");
      cr.stx = QStringLiteral("7");
      cr.srx = QStringLiteral("12");
      cr.timeOff = QDateTime::currentDateTimeUtc();
      const QString crText = QsoLog::adifRecord(cr);
      if (!crText.contains(QLatin1String("<CONTEST_ID:9>ARRL-RTTY")) ||
          !crText.contains(QLatin1String("<STX_STRING:7>599 007")) || !crText.contains(QLatin1String("<SRX:2>12")) ||
          !crText.contains(QLatin1String("<STX:1>7")))
        return fail("contest QSO is missing its ADIF fields");
    }

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
  {
    const QString body = QStringLiteral("A\nB") + QChar(0x00E9);
    const Mt63Plan line = planMt63(QStringLiteral("MT63-500S"), body, 1500);
    const QString back = mt63Decode(QStringLiteral("MT63-500S"), line.samples.data(),
                                    int(line.samples.size()), 48000, 1500);
    if (!back.contains(QStringLiteral("A\nB")) || !back.contains(QChar(0x00E9))) {
      qCritical("MT63 latin [%s]", qPrintable(back));
      return fail("MT63 newline or Latin-1 did not decode");
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
  std::vector<float> faxDirty = faxAudio;
  for (size_t i = 0; i < faxDirty.size(); ++i)
    faxDirty[i] += 0.8f * float(std::sin(2.0 * 3.141592653589793 * 400.0 * double(i) / 48000.0));
  const WefaxImage faxNoisy = wefaxDecode(faxSpec, faxDirty.data(), int(faxDirty.size()), 48000);
  if (faxNoisy.width != faxPixels || faxNoisy.height < 4)
    return fail("WEFAX filtered demod lost the picture");
  const auto faxLevel = [](const WefaxImage& picture, int row, double pos) {
    int x = int(pos * double(picture.width));
    if (x < 0)
      x = 0;
    if (x >= picture.width)
      x = picture.width - 1;
    return int(picture.gray[static_cast<size_t>(row * picture.width + x)]);
  };
  const int noisyRow = faxNoisy.height - 1;
  if (faxLevel(faxNoisy, noisyRow, 0.02) > 80 || faxLevel(faxNoisy, noisyRow, 0.20) > 80 ||
      faxLevel(faxNoisy, noisyRow, 0.60) < 180)
    return fail("WEFAX filtered demod is noisy");
  const std::vector<float> faxFast =
      wefaxModulateTest(faxSpec, 48000, faxGray, faxPixels, faxRows, 1.01);
  const WefaxImage faxFixed = wefaxDecode(faxSpec, faxFast.data(), int(faxFast.size()), 48000, 10000.0);
  const WefaxImage faxDrift = wefaxDecode(faxSpec, faxFast.data(), int(faxFast.size()), 48000, 0.0);
  if (faxFixed.height < 4 || faxLevel(faxFixed, faxFixed.height - 1, 0.02) > 80 ||
      faxLevel(faxFixed, faxFixed.height - 1, 0.60) < 180)
    return fail("WEFAX slant did not hold the picture");
  if (faxDrift.height < 4 || faxLevel(faxDrift, faxDrift.height - 1, 0.02) < 140)
    return fail("WEFAX slant was a no-op");
  std::vector<unsigned char> faxWmo(static_cast<size_t>(faxPixels * faxRows), 0);
  for (int y = 0; y < faxRows; ++y) {
    for (int x = 0; x < faxPixels; ++x) {
      if ((double(x) + 0.5) / double(faxPixels) < 0.045)
        faxWmo[static_cast<size_t>(y * faxPixels + x)] = 255;
    }
  }
  const std::vector<float> faxWmoAudio = wefaxModulateTest(faxSpec, 48000, faxWmo, faxPixels, faxRows);
  const WefaxImage faxWmoBack = wefaxDecode(faxSpec, faxWmoAudio.data(), int(faxWmoAudio.size()), 48000);
  if (faxWmoBack.height < 4 || faxLevel(faxWmoBack, faxWmoBack.height - 1, 0.02) < 180 ||
      faxLevel(faxWmoBack, faxWmoBack.height - 1, 0.50) > 80)
    return fail("WEFAX white phasing did not align");
  const auto* faxSlant = window.findChild<QSpinBox*>(QStringLiteral("wefaxSlant"));
  if (!faxSlant || faxSlant->minimum() != -20000 || faxSlant->maximum() != 20000 ||
      faxSlant->singleStep() != 10 ||
      !faxSlant->toolTip().contains(QLatin1String("does not key")) ||
      !faxSlant->toolTip().contains(QLatin1String("START")))
    return fail("WEFAX slant is missing");
  auto appendFaxTone = [](std::vector<float>* out, int hz, double seconds) {
    constexpr double kPiTone = 3.141592653589793;
    double phase = 0.0;
    const int n = int(48000.0 * seconds);
    for (int i = 0; i < n; ++i) {
      const bool high = std::fmod(double(i) / 48000.0 * double(hz), 1.0) < 0.5;
      const double freq = high ? 2300.0 : 1500.0;
      out->push_back(float(0.6 * std::sin(phase)));
      phase += 2.0 * kPiTone * freq / 48000.0;
      if (phase > 2.0 * kPiTone)
        phase -= 2.0 * kPiTone;
    }
  };
  auto feedFax = [](WefaxEngine* engine, const std::vector<float>& wave) {
    for (size_t i = 0; i < wave.size(); i += 2048) {
      const int n = std::min(2048, int(wave.size() - i));
      engine->pushAudio(wave.data() + i, n, 48000);
    }
  };
  WefaxEngine faxEng;
  faxEng.setArmed(true);
  faxEng.setCenterHz(1900);
  std::vector<float> faxNoise(static_cast<size_t>(48000 * 2));
  for (size_t i = 0; i < faxNoise.size(); ++i) {
    faxNoise[i] = 0.3f * float(std::sin(2.0 * 3.141592653589793 * 700.0 * double(i) / 48000.0)) +
                  0.3f * float(std::sin(2.0 * 3.141592653589793 * 2500.0 * double(i) / 48000.0));
  }
  feedFax(&faxEng, faxNoise);
  if (faxEng.rowCount() != 0)
    return fail("WEFAX locked onto noise");
  std::vector<float> faxLive;
  appendFaxTone(&faxLive, 300, 1.2);
  faxLive.insert(faxLive.end(), faxAudio.begin(), faxAudio.end());
  feedFax(&faxEng, faxLive);
  if (faxEng.rowCount() < 4 || faxEng.image().isNull())
    return fail("WEFAX start did not open a picture");
  bool faxLiveOk = false;
  for (int row = 0; row < faxEng.rowCount() && !faxLiveOk; ++row) {
    const unsigned char* liveScan = faxEng.image().scanLine(row);
    const auto liveAt = [&](double pos) {
      int x = int(pos * double(faxEng.image().width()));
      if (x < 0)
        x = 0;
      if (x >= faxEng.image().width())
        x = faxEng.image().width() - 1;
      return int(liveScan[x]);
    };
    faxLiveOk = liveAt(0.02) <= 80 && liveAt(0.20) <= 80 && liveAt(0.60) >= 180;
  }
  if (!faxLiveOk) {
    qCritical("WEFAX live rows %d", faxEng.rowCount());
    return fail("WEFAX live picture is wrong");
  }
  std::vector<float> faxStop;
  appendFaxTone(&faxStop, 450, 2.0);
  feedFax(&faxEng, faxStop);
  if (faxEng.rowCount() != 0)
    return fail("WEFAX stop did not reset");
  feedFax(&faxEng, faxAudio);
  if (faxEng.rowCount() < 4)
    return fail("WEFAX phasing did not start");
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
  // JT65 free text goes through MSHV's unpacktext(), which wrote past the
  // end of its string under Qt 6 until the build padded it.
  {
    std::vector<float> jtText;
    if (!buildTxWave(QStringLiteral("JT65A"), QStringLiteral("TNX RICK 73"), 1500, &jtText) ||
        jtText.size() < 48000)
      return fail("JT65 free-text encoder is missing");
    const QString textBack = decodeSequencedMessage(QStringLiteral("JT65A"), jtText);
    qInfo("JT65A text %s", qPrintable(textBack));
    if (!textBack.contains(QLatin1String("TNX RICK 73")))
      return fail("JT65A free text did not decode");
  }
  // Every JT65/Q65 Decode option on: the MSHV decoders must still copy.
  const QString jt65All = decodeSequencedMessage(QStringLiteral("JT65A"), jt65, true);
  qInfo("JT65A all options %s", qPrintable(jt65All));
  if (!jt65All.contains(QLatin1String("CQ N8SDR EM79")))
    return fail("JT65A did not decode with every Decode option on");
  const QString q65All = decodeSequencedMessage(QStringLiteral("Q65A"), q65, true);
  qInfo("Q65A all options %s", qPrintable(q65All));
  if (!q65All.contains(QLatin1String("CQ N8SDR EM79")))
    return fail("Q65A did not decode with every Decode option on");
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
  {
    // FreeDV Reporter, socket.io v4 protocol 2, as freedv-gui sends it.
    const QByteArray hello = freedvReporterConnectPacket(QStringLiteral("n8sdr"), QStringLiteral("EM79"),
                                                         QStringLiteral("STD 1"), false);
    const QJsonObject auth = QJsonDocument::fromJson(hello.mid(2)).object();
    if (!hello.startsWith("40{") || auth.value(QStringLiteral("role")).toString() != QLatin1String("report") ||
        auth.value(QStringLiteral("callsign")).toString() != QLatin1String("N8SDR") ||
        auth.value(QStringLiteral("grid_square")).toString() != QLatin1String("EM79") ||
        auth.value(QStringLiteral("protocol_version")).toInt() != 2 ||
        auth.value(QStringLiteral("rx_only")).toBool(true))
      return fail("FreeDV Reporter connect packet is wrong");
    const QJsonObject view = QJsonDocument::fromJson(
        freedvReporterConnectPacket(QString(), QStringLiteral("EM79"), QStringLiteral("STD 1"), false).mid(2)).object();
    if (view.value(QStringLiteral("role")).toString() != QLatin1String("view") || view.contains(QStringLiteral("callsign")))
      return fail("FreeDV Reporter reported without a callsign");
    const QByteArray freq = freedvReporterEvent(
        QStringLiteral("freq_change"),
        QJsonDocument(QJsonObject{{QStringLiteral("freq"), QJsonValue(qint64(14236000))}}).toJson(QJsonDocument::Compact));
    if (freq != QByteArray("42[\"freq_change\",{\"freq\":14236000}]") ||
        freedvReporterEvent(QStringLiteral("hide_self")) != QByteArray("42[\"hide_self\"]") ||
        freedvReporterModeName(false) != QLatin1String("RADEV1"))
      return fail("FreeDV Reporter event is wrong");
  }
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
  // IPFIX sets are padded to four bytes, as MSHV and WSJT-X send them. Walk
  // the sets after the 16-byte header: each length is a multiple of four
  // and the sets add up to the packet.
  for (const QString& rxName : {QStringLiteral("N8SDR"), QStringLiteral("KN4CRD"), QStringLiteral("VE3/N8SDR")}) {
    const QByteArray padded = pskReporterDatagram(rxName, QStringLiteral("EM79"), QStringLiteral("STD 0.1.0"),
                                                  QStringLiteral("dipole"), {spot}, 1700000000, 1, 1);
    int at = 16;
    int sets = 0;
    while (at + 4 <= padded.size()) {
      const int length = (quint8(padded.at(at + 2)) << 8) | quint8(padded.at(at + 3));
      if (length < 4 || length % 4 != 0)
        return fail("PSK Reporter set is not padded to four bytes");
      at += length;
      ++sets;
    }
    if (at != padded.size() || sets != 4)
      return fail("PSK Reporter sets do not add up to the packet");
  }
  // Read the packet the way an IPFIX collector does: learn each template
  // from the packet itself, then decode the data sets with it. The spot and
  // the receiver must come back field for field, with only padding left over.
  {
    PskSpot second = spot;
    second.call = QStringLiteral("K1JT");
    second.grid = QStringLiteral("FN20");
    second.freqHz = 7074321;
    second.snr = 3;
    second.mode = QStringLiteral("JS8");
    const QByteArray p = pskReporterDatagram(QStringLiteral("N8SDR"), QStringLiteral("EM79"),
                                             QStringLiteral("STD 0.1.0"), QStringLiteral("dipole"),
                                             {spot, second}, 1700000000, 1, 1);
    struct Field {
      int id;
      int length;
    };
    QHash<int, QVector<Field>> templates;
    QVector<QVector<QByteArray>> records;
    QVector<int> recordSet;
    const auto be16 = [&](int at) { return (quint8(p.at(at)) << 8) | quint8(p.at(at + 1)); };
    int at = 16;
    bool ipfixOk = p.size() >= 16 && be16(2) == p.size();
    while (ipfixOk && at + 4 <= p.size()) {
      const int setId = be16(at);
      const int length = be16(at + 2);
      const int end = at + length;
      if (length < 4 || end > p.size()) {
        ipfixOk = false;
        break;
      }
      int pos = at + 4;
      if (setId == 2 || setId == 3) {
        const int tid = be16(pos);
        const int count = be16(pos + 2);
        pos += setId == 3 ? 6 : 4;
        QVector<Field> fields;
        for (int f = 0; f < count; ++f) {
          const int id = be16(pos);
          fields.append(Field{id & 0x7fff, be16(pos + 2)});
          pos += (id & 0x8000) ? 8 : 4;
        }
        templates.insert(tid, fields);
      } else if (templates.contains(setId)) {
        const QVector<Field>& fields = templates.value(setId);
        while (pos < end) {
          QVector<QByteArray> values;
          int q = pos;
          for (const Field& f : fields) {
            int len = f.length;
            if (len == 0xffff) {
              if (q >= end)
                break;
              len = quint8(p.at(q));
              ++q;
            }
            if (q + len > end)
              break;
            values.append(p.mid(q, len));
            q += len;
          }
          if (values.size() != fields.size())
            break;  // what is left is padding
          records.append(values);
          recordSet.append(setId);
          pos = q;
        }
        for (int z = pos; z < end; ++z) {
          if (p.at(z) != 0)
            ipfixOk = false;
        }
        if (end - pos > 3)
          ipfixOk = false;
      } else {
        ipfixOk = false;
      }
      at = end;
    }
    const auto num = [](const QByteArray& b) {
      quint64 v = 0;
      for (char c : b)
        v = (v << 8) | quint8(c);
      return v;
    };
    bool good = ipfixOk && records.size() == 3 && recordSet.at(0) == 0x50e2;
    if (good) {
      const QVector<QByteArray>& rx = records.at(0);
      good = rx.size() == 4 && rx.at(0) == "N8SDR" && rx.at(1) == "EM79" && rx.at(2) == "STD 0.1.0" &&
             rx.at(3) == "dipole";
      const PskSpot* sent[2] = {&spot, &second};
      for (int r = 0; good && r < 2; ++r) {
        const QVector<QByteArray>& tx = records.at(r + 1);
        good = recordSet.at(r + 1) == 0x50e3 && tx.size() == 7 &&
               tx.at(0) == sent[r]->call.toLatin1() && num(tx.at(1)) == quint64(sent[r]->freqHz) &&
               qint8(tx.at(2).at(0)) == sent[r]->snr && tx.at(3) == sent[r]->mode.toLatin1() &&
               tx.at(4) == sent[r]->grid.toLatin1() && quint8(tx.at(5).at(0)) == 1 &&
               num(tx.at(6)) == sent[r]->unixTime;
      }
    }
    if (!good)
      return fail("PSK Reporter packet does not parse by its own templates");
  }
  // A busy period: 120 spots must all go out, split across packets that
  // each stay inside the 952-byte UDP limit.
  {
    QVector<PskSpot> many;
    for (int i = 0; i < 120; ++i) {
      PskSpot s = spot;
      s.call = QStringLiteral("K%1AB").arg(i % 10) + QChar(u'A' + i / 10);
      s.freqHz = 14074000 + i * 10;
      many.append(s);
    }
    quint32 seq = 5;
    const QVector<QByteArray> packets = pskReporterDatagrams(QStringLiteral("N8SDR"), QStringLiteral("EM79"),
                                                             QStringLiteral("STD 0.1.0"), QString(), many,
                                                             1700000000, &seq, 1);
    for (const QByteArray& p : packets) {
      if (p.isEmpty() || p.size() > kPskMaxDatagram)
        return fail("PSK Reporter packet is over the UDP limit");
    }
    for (const PskSpot& s : many) {
      bool sent = false;
      for (const QByteArray& p : packets)
        sent = sent || p.contains(char(s.call.size()) + s.call.toLatin1());
      if (!sent)
        return fail("PSK Reporter dropped a spot from a busy period");
    }
    if (packets.size() < 2 || seq != quint32(5 + packets.size()))
      return fail("PSK Reporter did not split a busy period");
  }
  // JS8: the sender is the call before the colon, never the addressee.
  {
    struct Js8Spot {
      const char* text;
      const char* call;
      const char* grid;
    };
    const Js8Spot js8Spots[] = {
        {"KN4CRD: W1AW SNR? ", "KN4CRD", ""},
        {"N8SDR: @HB HEARTBEAT EN80 ", "N8SDR", "EN80"},
        {"N8SDR: @ALLCALL CQ DX EN80 ", "N8SDR", "EN80"},
        {"VE3/N8SDR: ", "VE3/N8SDR", ""},
        {"N8SDR/P: KN4CRD ACK ", "N8SDR/P", ""},
    };
    for (const Js8Spot& s : js8Spots) {
      if (!pskSpotFromJs8(QString::fromLatin1(s.text), &spotCall, &spotGrid) ||
          spotCall != QLatin1String(s.call) || spotGrid != QLatin1String(s.grid)) {
        qCritical("JS8 spot [%s] -> [%s] [%s]", s.text, qPrintable(spotCall), qPrintable(spotGrid));
        return fail("PSK Reporter picked the wrong JS8 sender");
      }
    }
    if (pskSpotFromJs8(QStringLiteral("J1Y/P ACK "), &spotCall, &spotGrid) ||
        pskSpotFromJs8(QStringLiteral("HELLO WORLD"), &spotCall, &spotGrid))
      return fail("PSK Reporter spotted a JS8 frame with no sender");
  }
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

  {
    bool userOk = false;
    QString userWhy;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!dialog) {
        userWhy = QStringLiteral("user dialog did not open");
        return;
      }
      const auto calls = dialog->findChildren<QLineEdit*>(QStringLiteral("userCall"));
      const auto grids = dialog->findChildren<QLineEdit*>(QStringLiteral("userGrid"));
      if (calls.size() != 1 || grids.size() != 1) {
        userWhy = QStringLiteral("callsign or locator field changed");
        dialog->reject();
        return;
      }
      for (const MacroUserField& field : macroUserFields()) {
        const auto* edit = dialog->findChild<QLineEdit*>(field.objectName);
        if (!edit || !edit->toolTip().contains(field.tag) ||
            !edit->toolTip().contains(QLatin1String("does not key")) ||
            !edit->toolTip().contains(QLatin1String("START"))) {
          userWhy = QStringLiteral("missing ") + field.tag;
          dialog->reject();
          return;
        }
      }
      const auto* legend = dialog->findChild<QLabel*>(QStringLiteral("userTagLegend"));
      if (!legend || !legend->text().contains(QLatin1String("<MYCALL>")) ||
          !legend->text().contains(QLatin1String("<MYGRID>")) ||
          !legend->text().contains(QLatin1String("<MYNAME>")) ||
          !legend->text().contains(QLatin1String("<TX_PWR>"))) {
        userWhy = QStringLiteral("tag legend missing");
        dialog->reject();
        return;
      }
      const auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("userTagFields"));
      if (!scroll || scroll->minimumHeight() < 120) {
        userWhy = QStringLiteral("station tag scroller can collapse");
        dialog->reject();
        return;
      }
      const char* shown[] = {"userName", "userQth", "userAntenna", "userPower"};
      for (const char* name : shown) {
        const auto* edit = dialog->findChild<QLineEdit*>(QLatin1String(name));
        if (!edit || edit->minimumHeight() < 24 || scroll->findChild<QLineEdit*>(QLatin1String(name))) {
          userWhy = QStringLiteral("station field is not at full height");
          dialog->reject();
          return;
        }
      }
      userOk = true;
      dialog->reject();
    });
    if (auto* user = window.findChild<QAction*>(QStringLiteral("userSettings")))
      user->trigger();
    if (!userOk) {
      qCritical("user dialog [%s]", qPrintable(userWhy));
      return fail("Options User station tags are wrong");
    }
  }
  {
    const auto* echo = window.findChild<QTextEdit*>(QStringLiteral("rxEcho"));
    if (!echo || echo->verticalScrollBarPolicy() != Qt::ScrollBarAsNeeded)
      return fail("receive log has no vertical scrollbar");
    QTextEdit log;
    log.setReadOnly(true);
    log.setLineWrapMode(QTextEdit::NoWrap);
    log.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    log.resize(220, 36);
    log.show();
    QString block;
    for (int i = 0; i < 40; ++i)
      block += QStringLiteral("line %1 received copy\n").arg(i);
    appendKeyedEcho(&log, block, rxEchoColor());
    QApplication::processEvents();
    QScrollBar* bar = log.verticalScrollBar();
    if (!bar || bar->maximum() <= 0)
      return fail("receive log did not grow a vertical scrollbar");
    bar->setValue(0);
    QApplication::processEvents();
    const int parked = bar->value();
    appendKeyedEcho(&log, QStringLiteral("TX-ECHO"), txEchoColor());
    QApplication::processEvents();
    if (bar->value() != parked)
      return fail("receive log jumped away from earlier text");
    if (!log.toPlainText().contains(QLatin1String("line 0")) ||
        !log.toPlainText().contains(QLatin1String("TX-ECHO")))
      return fail("receive log replaced earlier text");
    QTextCursor color(log.document());
    color.setPosition(0);
    const QColor rx = color.charFormat().foreground().color();
    color.setPosition(log.toPlainText().lastIndexOf(QLatin1String("TX-ECHO")));
    const QColor tx = color.charFormat().foreground().color();
    if (rx != rxEchoColor() || tx != txEchoColor())
      return fail("receive and echo colors are wrong");
    breakEchoLine(&log);
    if (!log.toPlainText().endsWith(QLatin1Char('\n')))
      return fail("Hell echo did not break the line");
    const QString held = log.toPlainText();
    breakEchoLine(&log);
    if (log.toPlainText() != held)
      return fail("Hell echo added a second break");
    bar->setValue(bar->maximum());
    appendKeyedEcho(&log, QStringLiteral("MORE"), txEchoColor());
    QApplication::processEvents();
    if (bar->value() < bar->maximum() - 2)
      return fail("receive log did not follow the bottom");
  }
  qInfo("STD self-test ok");
  return 0;
}

}  // namespace

#if defined(_WIN32)
// STD_CRASH_TRACE=1: on a crash, print the call stack to stderr. Function
// names and lines need a build with debug info (std.pdb next to std.exe).
static LONG WINAPI crashTrace(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
      code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
      code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED && code != STATUS_HEAP_CORRUPTION)
    return EXCEPTION_CONTINUE_SEARCH;
  static bool once = false;
  if (once)
    return EXCEPTION_CONTINUE_SEARCH;
  once = true;
  const HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);
  fprintf(stderr, "\n=== STD crash 0x%08lx at %p, thread %lu ===\n", code,
          info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
  CONTEXT context = *info->ContextRecord;
  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  for (int depth = 0; depth < 48; ++depth) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        frame.AddrPC.Offset == 0)
      break;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 offset = 0;
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD lineOffset = 0;
    char module[MAX_PATH] = "?";
    if (const DWORD64 base = SymGetModuleBase64(process, frame.AddrPC.Offset))
      GetModuleFileNameA(reinterpret_cast<HMODULE>(base), module, MAX_PATH);
    const char* name = SymFromAddr(process, frame.AddrPC.Offset, &offset, symbol) ? symbol->Name : "?";
    if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineOffset, &line))
      fprintf(stderr, "  #%02d %s  %s:%lu\n", depth, name, line.FileName, line.LineNumber);
    else
      fprintf(stderr, "  #%02d %s+0x%llx  [%s]\n", depth, name, offset, strrchr(module, '\\') ? strrchr(module, '\\') + 1 : module);
  }
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char* argv[]) {
#if defined(_WIN32)
  if (qEnvironmentVariableIsSet("STD_CRASH_TRACE"))
    AddVectoredExceptionHandler(1, crashTrace);
#endif
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
