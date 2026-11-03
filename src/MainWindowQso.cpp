// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QSO bar for the keyboard modes (PSK, RTTY, Olivia, Hell and the rest):
// the worked station's call, name, QTH, state, province, locator, RST in,
// RST out and notes, with Clear and Log. A right-click in the receive text
// puts a word into one of the fields, and a double-click on a callsign
// fills Call. The macro tags <CALL> <NAME> <QTH> <ST> <PR> <LOC> <RST>
// <QSOTIME> read these fields. RST out can follow the TCI S-meter.
//
// Call and Loc here are the OTHER station. Your own callsign and locator
// stay in Options → User and nothing here edits them. The fields are not
// saved as settings. Nothing here keys the radio or sends START.

#include "MainWindow.h"

#include "DigitalMode.h"
#include "ModeProfile.h"
#include "QsoLog.h"
#include "TciClient.h"
#include "WaterfallWidget.h"
#include "WxFeed.h"

#include <algorithm>
#include <vector>
#include <cmath>
#include <functional>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimeZone>
#include <QTimer>

namespace {

QFont qsoUi(int pixelSize) {
  QFont font("Inter");
  font.setPixelSize(pixelSize);
  return font;
}

QFont qsoMono(int pixelSize) {
  QFont font("JetBrains Mono");
  font.setStyleHint(QFont::Monospace);
  font.setPixelSize(pixelSize);
  return font;
}

// HF S-meter scale: S9 = -73 dBm, 6 dB per S-unit, S1..S9.
constexpr double kS9Dbm = -73.0;
constexpr double kDbPerS = 6.0;

struct QsoField {
  const char* name;   // object name
  const char* label;  // shown before the field
  const char* tag;    // macro tag, or nullptr
  int width;          // fixed width in pixels, 0 to stretch
  bool upper;
  const char* tip;
};

// Row 1 and row 2 of the bar, in order.
const QsoField kRow1[] = {
    {"qsoBarCall", "Call", "<CALL>", 110, true, "The other station's callsign. <CALL> in a macro."},
    {"qsoBarName", "Name", "<NAME>", 130, false, "Their name. <NAME> in a macro."},
    {"qsoBarQth", "QTH", "<QTH>", 200, false, "Their town. <QTH> in a macro."},
    {"qsoBarSt", "St", "<ST>", 44, true, "Their state. <ST> in a macro."},
    {"qsoBarPr", "Pr", "<PR>", 44, true, "Their province. <PR> in a macro."},
    {"qsoBarLoc", "Loc", "<LOC>", 74, true, "Their locator. <LOC> in a macro."},
};
const QsoField kRow2[] = {
    {"qsoBarRstIn", "RST in", nullptr, 56, false,
     "In: the report they send you, how well they copy you. Type it from their text. Logged as RST_RCVD."},
    {"qsoBarRstOut", "Out", "<RST>", 56, false,
     "Out: the report you send them, how well you copy them. Auto RST fills it from your S-meter. "
     "<RST> in a macro. Logged as RST_SENT."},
    {"qsoBarNotes", "Notes", nullptr, 0, false, "Notes. Logged as COMMENT."},
};

QString wordAt(const QTextEdit* edit, const QPoint& viewportPos) {
  QTextCursor cursor = edit->textCursor();
  if (cursor.hasSelection()) {
    const QString picked = cursor.selectedText().simplified();
    if (!picked.isEmpty())
      return picked;
  }
  cursor = edit->cursorForPosition(viewportPos);
  const QString line = cursor.block().text();
  int at = cursor.positionInBlock();
  auto part = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('/') || c == QLatin1Char('-') ||
                                   c == QLatin1Char('+'); };
  if (at >= line.size() || (at < line.size() && !part(line.at(at))))
    if (at > 0 && at - 1 < line.size() && part(line.at(at - 1)))
      --at;
  if (at < 0 || at >= line.size() || !part(line.at(at)))
    return {};
  int start = at;
  int end = at;
  while (start > 0 && part(line.at(start - 1)))
    --start;
  while (end + 1 < line.size() && part(line.at(end + 1)))
    ++end;
  return line.mid(start, end - start + 1);
}

// Watches the receive text for a double-click and passes the word on.
class QsoPicker : public QObject {
public:
  QsoPicker(QObject* parent, std::function<void(const QPoint&)> hit) : QObject(parent), hit_(std::move(hit)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonDblClick)
      hit_(static_cast<QMouseEvent*>(event)->position().toPoint());
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void(const QPoint&)> hit_;
};

}  // namespace

void RstMeter::add(qint64 ms, double dbm) {
  readings_.emplace_back(ms, dbm);
  while (!readings_.empty() && ms - readings_.front().first > kFloorMs)
    readings_.pop_front();
}

void RstMeter::addSnr(qint64 ms, double snrDb) {
  snrMs_ = ms;
  snrDb_ = snrDb;
}

void RstMeter::reset() {
  readings_.clear();
  snrMs_ = 0;
}

std::optional<double> RstMeter::average(qint64 ms) const {
  // The median of the last 1.25 s: one spike or crash cannot move it.
  std::vector<double> recent;
  for (auto it = readings_.rbegin(); it != readings_.rend() && ms - it->first <= kAverageMs; ++it)
    recent.push_back(it->second);
  if (recent.size() < 3)
    return std::nullopt;
  const size_t mid = recent.size() / 2;
  std::nth_element(recent.begin(), recent.begin() + mid, recent.end());
  return recent[mid];
}

std::optional<double> RstMeter::floor() const {
  if (readings_.size() < 8)
    return std::nullopt;
  std::vector<double> all;
  all.reserve(readings_.size());
  for (const auto& r : readings_)
    all.push_back(r.second);
  const size_t k = all.size() / 5;
  std::nth_element(all.begin(), all.begin() + k, all.end());
  return all[k];
}

std::optional<double> RstMeter::level(qint64 ms) const {
  const std::optional<double> avg = average(ms);
  if (!avg)
    return std::nullopt;
  double snr = 0.0;
  if (snrMs_ > 0 && ms - snrMs_ < 3000) {
    snr = snrDb_;
  } else {
    const std::optional<double> noise = floor();
    if (!noise)
      return std::nullopt;
    snr = *avg - *noise;
  }
  if (snr < kGateDb)
    return std::nullopt;
  return avg;
}

bool looksLikeCallsign(const QString& word) {
  static const QRegularExpression call(
      QStringLiteral("^([A-Z0-9]{1,4}/)?[A-Z0-9]{1,3}[0-9][A-Z]{1,4}(/[A-Z0-9]{1,4})?$"));
  return call.match(word.trimmed().toUpper()).hasMatch();
}

QString rstFromDbm(double dbm, bool threeDigits) {
  const int s = std::max(1, std::min(9, int(std::lround((dbm - kS9Dbm) / kDbPerS)) + 9));
  // "5%19" would read as placeholder %19, so build it by hand.
  const QString text = QLatin1Char('5') + QString::number(s);
  return threeDigits ? text + QLatin1Char('9') : text;
}

QWidget* MainWindow::makeQsoBar(QWidget* parent) {
  auto* bar = new QWidget(parent);
  bar->setObjectName(QStringLiteral("qsoBar"));
  bar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  auto* grid = new QGridLayout(bar);
  grid->setContentsMargins(0, 0, 0, 4);
  grid->setHorizontalSpacing(6);
  grid->setVerticalSpacing(4);

  auto addField = [&](QHBoxLayout* row, const QsoField& f) {
    auto* label = new QLabel(QString::fromLatin1(f.label), bar);
    label->setObjectName(QStringLiteral("qsoBarLabel"));
    label->setFont(qsoUi(12));
    auto* edit = new QLineEdit(bar);
    edit->setObjectName(QLatin1String(f.name));
    edit->setFont(qsoMono(13));
    edit->setClearButtonEnabled(false);
    edit->setToolTip(QString::fromLatin1(f.tip) +
                     QStringLiteral(" Right-click a word in the receive text to fill it. This is the other "
                                    "station, not you. Not saved as a setting. This does not key."));
    if (f.width > 0)
      edit->setFixedWidth(f.width);
    if (f.upper)
      connect(edit, &QLineEdit::textEdited, edit, [edit](const QString& text) {
        const int at = edit->cursorPosition();
        edit->setText(text.toUpper());
        edit->setCursorPosition(at);
      });
    label->setBuddy(edit);
    row->addWidget(label);
    row->addWidget(edit, f.width > 0 ? 0 : 1);
  };

  auto* top = new QHBoxLayout;
  top->setSpacing(6);
  for (const QsoField& f : kRow1)
    addField(top, f);
  top->addStretch(1);
  grid->addLayout(top, 0, 0);

  auto* bottom = new QHBoxLayout;
  bottom->setSpacing(6);
  addField(bottom, kRow2[0]);
  addField(bottom, kRow2[1]);
  auto* mark = new QLabel(bar);
  mark->setObjectName(QStringLiteral("qsoBarRstTag"));
  mark->setFont(qsoUi(10));
  bottom->addWidget(mark);
  addField(bottom, kRow2[2]);

  auto* on = new QLabel(bar);
  on->setObjectName(QStringLiteral("qsoBarOn"));
  on->setFont(qsoMono(12));
  on->setToolTip(QStringLiteral("Time on, UTC. Set when Call is first filled. <QSOTIME> in a macro."));
  bottom->addWidget(on);

  auto* seen = new QLabel(bar);
  seen->setObjectName(QStringLiteral("qsoBarHistory"));
  seen->setFont(qsoUi(11));
  seen->setToolTip(QStringLiteral(
      "Your log for this call. Worked before fills empty Name, QTH, St, and Loc from the last QSO, in blue "
      "until you change them. <WELCOME> sends nice to work you again, and <LASTQSO> the last date. "
      "This does not key."));
  bottom->addWidget(seen);

  auto* autoRst = new QCheckBox(QStringLiteral("Auto RST"), bar);
  autoRst->setObjectName(QStringLiteral("qsoBarAutoRst"));
  autoRst->setFont(qsoUi(12));
  autoRst->setChecked(QSettings().value(QStringLiteral("qso/autoRst"), false).toBool());
  autoRst->setToolTip(QStringLiteral(
      "Fill RST out, the report you send, from your S-meter over TCI (S9 = -73 dBm, 6 dB per S-unit). "
      "Your S-meter measures their signal, so it can only give the report you send. It keeps the "
      "strongest reading while Call is filled and you are not transmitting. Typing in Out stops it "
      "until the next call. The S-meter reads the whole receive passband, so a busy band can read high. "
      "Turning it on asks the radio for S-meter readings; it never turns them off for other programs. "
      "This does not key and does not send START."));
  bottom->addWidget(autoRst);

  auto* clear = new QPushButton(QStringLiteral("Clear"), bar);
  clear->setObjectName(QStringLiteral("qsoBarClear"));
  clear->setFont(qsoUi(12));
  clear->setFocusPolicy(Qt::NoFocus);
  clear->setToolTip(QStringLiteral("Clear the QSO fields. <CLRQSO> in a macro. This does not key."));
  bottom->addWidget(clear);

  auto* log = new QPushButton(QStringLiteral("Log"), bar);
  log->setObjectName(QStringLiteral("qsoBarLog"));
  log->setFont(qsoUi(12));
  log->setFocusPolicy(Qt::NoFocus);
  log->setToolTip(QStringLiteral(
      "Log this QSO to the ADIF file and any logger turned on under Options. <LOG> in a macro. "
      "This does not key and does not send START."));
  bottom->addWidget(log);
  grid->addLayout(bottom, 1, 0);

  connect(clear, &QPushButton::clicked, this, [this] {
    clearQsoBar();
    setTxStatus(QStringLiteral("QSO fields cleared. Nothing was keyed. START was not sent."));
  });
  connect(log, &QPushButton::clicked, this, [this] { setTxStatus(logQsoBar()); });
  connect(autoRst, &QCheckBox::toggled, this, [this](bool checked) {
    QSettings().setValue(QStringLiteral("qso/autoRst"), checked);
    if (checked && tci_)
      tci_->enableRxSensors();
    showQsoRstTag();
  });
  if (auto* call = bar->findChild<QLineEdit*>(QStringLiteral("qsoBarCall")))
    connect(call, &QLineEdit::textChanged, this, [this](const QString& text) { qsoCallChanged(text); });
  // Each field: a click or Tab takes an offered value; typing makes a
  // log-filled value yours, so it loses the blue.
  for (auto* edit : bar->findChildren<QLineEdit*>()) {
    edit->installEventFilter(this);
    connect(edit, &QLineEdit::textEdited, this, [this, edit](const QString&) {
      if (qsoFromLog_.remove(edit->objectName()))
        edit->setStyleSheet(QString());
    });
    connect(edit, &QLineEdit::textChanged, this, [this](const QString&) { showQsoSuggestions(); });
  }
  if (auto* in = bar->findChild<QLineEdit*>(QStringLiteral("qsoBarRstOut")))
    connect(in, &QLineEdit::textEdited, this, [this](const QString&) {
      qsoRstAuto_ = false;
      showQsoRstTag();
    });
  bar->hide();
  // Keeps the auto tag honest: it shows "no meter" when readings stop.
  auto* tick = new QTimer(bar);
  tick->setInterval(1000);
  connect(tick, &QTimer::timeout, this, [this] { showQsoRstTag(); });
  tick->start();
  QTimer::singleShot(0, this, [this] { clearQsoBar(); });
  return bar;
}

void MainWindow::qsoCallChanged(const QString& text) {
  const QString call = text.trimmed();
  if (call == qsoBarLastCall_)
    return;
  // A different station after one was being worked: their words go too.
  // From empty to a call keeps them, since the call usually came in the
  // same over as their name and QTH.
  const bool wasWorking = !qsoBarLastCall_.isEmpty() && qsoBarLastCall_ != QString(QChar(1));
  if (wasWorking)
    qsoRxTail_.clear();
  qsoBarLastCall_ = call;
  qsoLookupCall(call);
  if (!qsoRxTail_.isEmpty())
    qsoScanRx(QString());
  // A new station: fresh time on, and RST out follows the meter again.
  qsoRstAuto_ = true;
  qsoPeakDbm_ = -1.0e9;
  qsoBarOnMs_ = call.isEmpty() ? 0 : QDateTime::currentMSecsSinceEpoch();
  if (auto* on = findChild<QLabel*>(QStringLiteral("qsoBarOn")))
    on->setText(qsoBarOnMs_ > 0 ? QStringLiteral("On %1Z").arg(
                                      QDateTime::fromMSecsSinceEpoch(qsoBarOnMs_, QTimeZone::utc())
                                          .toString(QStringLiteral("HHmm")))
                                : QString());
  showQsoRstTag();
}

void MainWindow::showQsoRstTag() {
  auto* tag = findChild<QLabel*>(QStringLiteral("qsoBarRstTag"));
  if (!tag)
    return;
  const auto* autoRst = findChild<QCheckBox*>(QStringLiteral("qsoBarAutoRst"));
  if (!autoRst || !autoRst->isChecked()) {
    tag->clear();
    tag->setToolTip(QString());
    return;
  }
  const bool fresh = qsoMeterMs_ > 0 && QDateTime::currentMSecsSinceEpoch() - qsoMeterMs_ < 3000;
  QString reading = QStringLiteral(" No S-meter reading in the last 3 seconds.");
  if (fresh) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const std::optional<double> avg = qsoRstMeter_.average(now);
    const std::optional<double> noise = qsoRstMeter_.floor();
    reading = QStringLiteral(" S-meter %1 dBm").arg(qsoMeterDbm_, 0, 'f', 1);
    if (avg)
      reading += QStringLiteral(", 1.25 s median %1 dBm (%2)").arg(*avg, 0, 'f', 1).arg(rstFromDbm(*avg, true));
    if (noise)
      reading += QStringLiteral(", floor %1 dBm").arg(*noise, 0, 'f', 1);
    reading += qsoRstMeter_.level(now) ? QStringLiteral(". Counting: over the floor.")
                                       : QStringLiteral(". Not counting: less than 3 dB over the floor.");
    if (qsoPeakDbm_ > -1.0e8)
      reading += QStringLiteral(" Held %1 dBm.").arg(qsoPeakDbm_, 0, 'f', 1);
  }
  if (!tci_ || !tci_->isOpen()) {
    tag->setText(QStringLiteral("no TCI"));
    tag->setProperty("state", QStringLiteral("off"));
    reading = QStringLiteral(" The radio is not connected.");
  } else if (!fresh) {
    tag->setText(QStringLiteral("no meter"));
    tag->setProperty("state", QStringLiteral("off"));
  } else {
    tag->setText(qsoRstAuto_ ? QStringLiteral("auto") : QStringLiteral("manual"));
    tag->setProperty("state", qsoRstAuto_ ? QStringLiteral("auto") : QStringLiteral("manual"));
  }
  tag->setToolTip(QStringLiteral("auto: Out follows your S-meter once Call is filled. manual: you typed it. "
                                 "no meter: the radio sends no S-meter readings. A new call puts it back "
                                 "to auto.") + reading);
  tag->style()->unpolish(tag);
  tag->style()->polish(tag);
}

void MainWindow::qsoSmeter(double dbm) {
  const bool wasFresh = qsoMeterMs_ > 0 && QDateTime::currentMSecsSinceEpoch() - qsoMeterMs_ < 3000;
  qsoMeterMs_ = QDateTime::currentMSecsSinceEpoch();
  qsoMeterDbm_ = dbm;
  // Transmit readings are our own signal; keep them out of the floor too.
  if (!(tci_ && tci_->isTransmitting()))
    qsoRstMeter_.add(qsoMeterMs_, dbm);
  if (!wasFresh)
    showQsoRstTag();
  const auto* autoRst = findChild<QCheckBox*>(QStringLiteral("qsoBarAutoRst"));
  const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
  if (!autoRst || !autoRst->isChecked() || !qsoRstAuto_ || !bar || !bar->isVisible())
    return;
  if (tci_ && tci_->isTransmitting())
    return;
  const auto* call = findChild<QLineEdit*>(QStringLiteral("qsoBarCall"));
  auto* in = findChild<QLineEdit*>(QStringLiteral("qsoBarRstOut"));
  if (!call || !in || call->text().trimmed().isEmpty())
    return;
  // Peak hold on the averaged level, and only when it stands over the
  // noise floor: it climbs to their strongest steady reading, and a
  // single crash or spike cannot peg it at S9.
  const std::optional<double> level = qsoRstMeter_.level(qsoMeterMs_);
  if (!level || *level <= qsoPeakDbm_)
    return;
  qsoPeakDbm_ = *level;
  const QString rst = rstFromDbm(*level, true);
  if (in->text() != rst)
    in->setText(rst);
}

void MainWindow::clearQsoBar() {
  for (const QsoField& f : kRow1)
    if (auto* e = findChild<QLineEdit*>(QLatin1String(f.name)))
      e->clear();
  for (const QsoField& f : kRow2)
    if (auto* e = findChild<QLineEdit*>(QLatin1String(f.name)))
      e->clear();
  if (auto* out = findChild<QLineEdit*>(QStringLiteral("qsoBarRstOut")))
    out->setText(QStringLiteral("599"));
  qsoBarLastCall_ = QStringLiteral("\x01");
  qsoRxTail_.clear();
  qsoHints_ = QsoHints{};
  qsoCallChanged(QString());
  showQsoSuggestions();
}

QHash<QString, QString> MainWindow::qsoBarTags() const {
  QHash<QString, QString> tags;
  auto value = [this](const char* name) {
    const auto* e = findChild<QLineEdit*>(QLatin1String(name));
    return e ? e->text().trimmed() : QString();
  };
  for (const QsoField& f : kRow1)
    if (f.tag)
      tags.insert(QLatin1String(f.tag), value(f.name));
  for (const QsoField& f : kRow2)
    if (f.tag)
      tags.insert(QLatin1String(f.tag), value(f.name));
  const QDateTime on = qsoBarOnMs_ > 0 ? QDateTime::fromMSecsSinceEpoch(qsoBarOnMs_, QTimeZone::utc())
                                       : QDateTime::currentDateTimeUtc();
  tags.insert(QStringLiteral("<QSOTIME>"), on.toString(QStringLiteral("HHmm")));
  return tags;
}

QString MainWindow::logQsoBar() {
  auto value = [this](const char* name) {
    const auto* e = findChild<QLineEdit*>(QLatin1String(name));
    return e ? e->text().trimmed() : QString();
  };
  QsoLog::Record record;
  record.call = value("qsoBarCall").toUpper();
  if (record.call.isEmpty())
    return QStringLiteral("Nothing logged: Call is empty. Nothing was keyed. START was not sent.");
  record.grid = value("qsoBarLoc").toUpper();
  record.name = value("qsoBarName");
  record.qth = value("qsoBarQth");
  record.state = value("qsoBarSt").toUpper();
  record.veProv = value("qsoBarPr").toUpper();
  record.rstRcvd = value("qsoBarRstIn");
  record.rstSent = value("qsoBarRstOut");
  record.comment = value("qsoBarNotes");
  record.mode = profile_ ? profile_->digitalMode() : QString();
  record.myCall = stationCall();
  record.myGrid = stationGrid();
  record.freqHz = profile_ ? profile_->dialHz() : 0;
  if (waterfall_)
    record.freqHz += waterfall_->txHz();
  record.timeOff = QDateTime::currentDateTimeUtc();
  record.timeOn = qsoBarOnMs_ > 0 ? QDateTime::fromMSecsSinceEpoch(qsoBarOnMs_, QTimeZone::utc()) : record.timeOff;
  qsoBarLastRecord_ = QsoLog::adifRecord(record);
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return QStringLiteral("Logged %1. Nothing was keyed. START was not sent.").arg(record.call);
  const QsoLog::Result result = qsoLog_ ? qsoLog_->store(record) : QsoLog::Result{};
  if (!result.wrote)
    return QStringLiteral("%1 was not logged. %2 Nothing was keyed. START was not sent.")
        .arg(record.call, result.detail);
  return QStringLiteral("Logged %1 to %2. Nothing was keyed. START was not sent.").arg(record.call, result.path);
}

void MainWindow::qsoBarPut(const QString& field, const QString& word) {
  auto* edit = findChild<QLineEdit*>(field);
  if (!edit || word.isEmpty())
    return;
  if (field == QLatin1String("qsoBarCall"))
    cancelMacroRepeat(QStringLiteral("You picked %1.").arg(word.toUpper()));
  if (field == QLatin1String("qsoBarNotes") && !edit->text().trimmed().isEmpty())
    edit->setText(edit->text().trimmed() + QLatin1Char(' ') + word);
  else if (field == QLatin1String("qsoBarName") || field == QLatin1String("qsoBarQth"))
    edit->setText(word.left(1).toUpper() + word.mid(1).toLower());
  else if (field == QLatin1String("qsoBarRstOut")) {
    edit->setText(word);
    qsoRstAuto_ = false;
    showQsoRstTag();
  } else
    edit->setText(field == QLatin1String("qsoBarRstIn") ? word : word.toUpper());
}

void MainWindow::installQsoRxPicker(QTextEdit* echo) {
  echo->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(echo, &QWidget::customContextMenuRequested, this, [this, echo](const QPoint& pos) {
    QMenu* menu = echo->createStandardContextMenu(pos);
    const QString word = wordAt(echo, pos);
    const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
    if (bar && bar->isVisible() && !word.isEmpty()) {
      QAction* first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
      const struct {
        const char* label;
        const char* field;
      } picks[] = {{"Call", "qsoBarCall"},     {"Name", "qsoBarName"},     {"QTH", "qsoBarQth"},
                   {"St", "qsoBarSt"},         {"Pr", "qsoBarPr"},         {"Loc", "qsoBarLoc"},
                   {"RST in", "qsoBarRstIn"},  {"RST out", "qsoBarRstOut"}, {"Notes", "qsoBarNotes"}};
      const QString shown = word.size() > 24 ? word.left(24) + QStringLiteral("…") : word;
      for (const auto& p : picks) {
        auto* action = new QAction(QStringLiteral("%1: %2").arg(QLatin1String(p.label), shown), menu);
        const QString field = QLatin1String(p.field);
        connect(action, &QAction::triggered, this, [this, field, word] { qsoBarPut(field, word); });
        menu->insertAction(first, action);
      }
      menu->insertSeparator(first);
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(echo->viewport()->mapToGlobal(pos));
  });
  echo->viewport()->installEventFilter(new QsoPicker(echo, [this, echo](const QPoint& pos) {
    const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
    if (!bar || !bar->isVisible())
      return;
    const QString word = wordAt(echo, pos);
    if (looksLikeCallsign(word))
      qsoBarPut(QStringLiteral("qsoBarCall"), word.toUpper());
  }));
}

MacroContext MainWindow::macroContext() const {
  MacroContext c;
  c.myCall = stationCall();
  c.myLoc = stationGrid();
  const QSettings store;
  for (const MacroUserField& field : macroUserFields())
    c.tags.insert(field.tag, store.value(field.key).toString());
  const QHash<QString, QString> qso = qsoBarTags();
  for (auto it = qso.cbegin(); it != qso.cend(); ++it)
    c.tags.insert(it.key(), it.value());
  if (wx_) {
    const QHash<QString, QString> weather = wx_->macroTags();
    for (auto it = weather.cbegin(); it != weather.cend(); ++it)
      c.tags.insert(it.key(), it.value());
  }
  // Log memory: blank on a first QSO, so a macro can always carry them.
  c.tags.insert(QStringLiteral("<WELCOME>"),
                qsoHistory_.count > 0 ? QStringLiteral("nice to work you again") : QString());
  c.tags.insert(QStringLiteral("<LASTQSO>"), qsoHistory_.lastDate);
  c.mode = profile_ ? profile_->digitalMode() : QString();
  const qint64 dial = profile_ ? profile_->dialHz() : 0;
  c.band = qsoBandForHz(dial);
  c.freqKhz = dial > 0 ? QString::number(double(dial) / 1000.0, 'f', 3) : QString();
  c.version = QStringLiteral("STD " STD_VERSION);
  c.serial = std::max(1, store.value(QStringLiteral("dx/macroSerial"), 1).toInt());
  return c;
}

// <LOG>, <CLRQSO>, <CLRRX>, <INCR> and <DECR>. A keyed macro runs these
// when its text has gone out; any other macro runs them when pressed.
void MainWindow::runMacroActions(const MacroPlan& plan) {
  QStringList done;
  if (plan.log) {
    const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
    if (!bar || !bar->isVisible()) {
      done << QStringLiteral("Not logged: the QSO bar is off for this mode.");
    } else {
      if (!plan.logNote.isEmpty())
        qsoBarPut(QStringLiteral("qsoBarNotes"), plan.logNote);
      done << logQsoBar();
    }
  }
  if (plan.clearQso)
    clearQsoBar();
  if (plan.clearRx) {
    if (auto* clr = findChild<QPushButton*>(QStringLiteral("clrRx")))
      clr->click();
  }
  if (plan.serialStep != 0) {
    QSettings store;
    const int serial = std::max(1, store.value(QStringLiteral("dx/macroSerial"), 1).toInt() + plan.serialStep);
    store.setValue(QStringLiteral("dx/macroSerial"), serial);
    done << QStringLiteral("Serial is now %1.").arg(serial, 3, 10, QLatin1Char('0'));
  }
  if (!done.isEmpty())
    setTxStatus(done.join(QLatin1Char(' ')));
  showMacroSet();
}

void MainWindow::macroSendFinished(bool completed) {
  if (!macroAfterPending_)
    return;
  const MacroPlan plan = macroAfter_;
  const int index = macroAfterIndex_;
  macroAfterPending_ = false;
  macroAfterIndex_ = -1;
  if (!completed) {
    if (plan.log || plan.clearQso || plan.serialStep != 0)
      setTxStatus(QStringLiteral(
          "TX stopped before the macro was sent, so it was not logged and nothing was cleared. "
          "START was not sent."));
    return;
  }
  runMacroActions(plan);
  if (plan.repeatSeconds > 0 && index >= 0) {
    macroRepeatIndex_ = index;
    macroHeardTail_.clear();
    if (!macroRepeatTimer_) {
      macroRepeatTimer_ = new QTimer(this);
      macroRepeatTimer_->setSingleShot(true);
      connect(macroRepeatTimer_, &QTimer::timeout, this, [this] {
        const int again = macroRepeatIndex_;
        macroRepeatIndex_ = -1;
        if (again >= 0)
          insertMacro(again);
      });
    }
    macroRepeatTimer_->start(plan.repeatSeconds * 1000);
    setTxStatus(QStringLiteral(
                    "Listening. This macro goes again in %1 s. Typing, a macro, TX, Halt, picking a call, "
                    "or your call in the receive text stops it. START was not sent.")
                    .arg(plan.repeatSeconds));
  }
}

// Smart capture: their received text, read for name, QTH, report, and
// locator, offered in the empty QSO bar fields. Nothing is filled until
// the operator takes it with a click or Tab.
void MainWindow::qsoScanRx(const QString& text) {
  const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
  if (!bar || !bar->isVisible())
    return;
  qsoRxTail_ = (qsoRxTail_ + text).right(600);
  qsoHints_ = scanQsoText(qsoRxTail_, stationCall());
  showQsoSuggestions();
}

void MainWindow::showQsoSuggestions() {
  const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
  if (!bar)
    return;
  const QList<QPair<QString, QString>> offers = {
      {QStringLiteral("qsoBarCall"), qsoHints_.call}, {QStringLiteral("qsoBarName"), qsoHints_.name},
      {QStringLiteral("qsoBarQth"), qsoHints_.qth},   {QStringLiteral("qsoBarSt"), qsoHints_.st},
      {QStringLiteral("qsoBarLoc"), qsoHints_.loc},   {QStringLiteral("qsoBarRstIn"), qsoHints_.rstIn},
  };
  for (const auto& offer : offers) {
    const QString& field = offer.first;
    const QString& value = offer.second;
    auto* edit = bar->findChild<QLineEdit*>(field);
    if (!edit)
      continue;
    if (!value.isEmpty() && edit->text().trimmed().isEmpty()) {
      qsoSuggest_.insert(field, value);
      edit->setPlaceholderText(value + QLatin1Char('?'));
      if (!qsoFromLog_.contains(field))
        edit->setStyleSheet(QStringLiteral("QLineEdit { border: 1px dashed #d29922; }"));
    } else if (qsoSuggest_.remove(field)) {
      edit->setPlaceholderText(QString());
      if (!qsoFromLog_.contains(field))
        edit->setStyleSheet(QString());
    }
  }
}

// A click or Tab in an empty field takes the offered value.
bool MainWindow::acceptQsoSuggestion(QObject* field) {
  auto* edit = qobject_cast<QLineEdit*>(field);
  if (!edit || !edit->text().trimmed().isEmpty())
    return false;
  const QString value = qsoSuggest_.value(edit->objectName());
  if (value.isEmpty())
    return false;
  qsoBarPut(edit->objectName(), value);
  setTxStatus(QStringLiteral("Took %1 from their text. This does not key.").arg(value));
  return true;
}

// Log memory: an earlier QSO with this call fills the empty fields.
void MainWindow::qsoLookupCall(const QString& call) {
  // Values an earlier lookup filled and nobody changed go when the call changes.
  for (auto it = qsoFromLog_.cbegin(); it != qsoFromLog_.cend(); ++it) {
    if (auto* edit = findChild<QLineEdit*>(it.key())) {
      if (edit->text() == it.value())
        edit->clear();
      edit->setStyleSheet(QString());
    }
  }
  qsoFromLog_.clear();
  qsoHistory_ = QsoHistory{};
  auto* seen = findChild<QLabel*>(QStringLiteral("qsoBarHistory"));
  if (seen)
    seen->clear();
  static const QRegularExpression full(QStringLiteral("^[A-Z0-9/]{3,}$"));
  if (call.isEmpty() || !full.match(call.toUpper()).hasMatch())
    return;
  const QSettings store;
  QString path = store.value(QStringLiteral("log/adifPath")).toString().trimmed();
  if (path.isEmpty())
    path = QsoLog::defaultAdifPath();
  qsoMemory_.setPath(path);
  qsoHistory_ = qsoMemory_.lookup(call);
  if (seen)
    seen->setText(qsoHistory_.count == 0
                      ? QStringLiteral("New station")
                      : QStringLiteral("Worked %1x, last %2 %3 %4")
                            .arg(qsoHistory_.count)
                            .arg(qsoHistory_.lastDate, qsoHistory_.lastBand, qsoHistory_.lastMode)
                            .simplified());
  if (qsoHistory_.count == 0)
    return;
  const QList<QPair<QString, QString>> known = {
      {QStringLiteral("qsoBarName"), qsoHistory_.name},
      {QStringLiteral("qsoBarQth"), qsoHistory_.qth},
      {QStringLiteral("qsoBarSt"), qsoHistory_.state.toUpper()},
      {QStringLiteral("qsoBarLoc"), qsoHistory_.grid.left(4).toUpper() + qsoHistory_.grid.mid(4).toLower()},
  };
  for (const auto& entry : known) {
    auto* edit = findChild<QLineEdit*>(entry.first);
    if (!edit || entry.second.isEmpty() || !edit->text().trimmed().isEmpty())
      continue;
    edit->setText(entry.second);
    edit->setStyleSheet(QStringLiteral("QLineEdit { color: #58a6ff; font-style: italic; }"));
    qsoFromLog_.insert(entry.first, entry.second);
  }
}

// Outlines the empty QSO bar fields a macro needs, for a second and a half.
void MainWindow::flashQsoFields(const QStringList& labels) {
  static const QHash<QString, QString> fields = {
      {QStringLiteral("Call"), QStringLiteral("qsoBarCall")}, {QStringLiteral("Name"), QStringLiteral("qsoBarName")},
      {QStringLiteral("QTH"), QStringLiteral("qsoBarQth")},   {QStringLiteral("St"), QStringLiteral("qsoBarSt")},
      {QStringLiteral("Pr"), QStringLiteral("qsoBarPr")},     {QStringLiteral("Loc"), QStringLiteral("qsoBarLoc")},
      {QStringLiteral("RST out"), QStringLiteral("qsoBarRstOut")},
  };
  for (const QString& label : labels) {
    auto* edit = findChild<QLineEdit*>(fields.value(label));
    if (!edit)
      continue;
    const QString before = edit->styleSheet();
    edit->setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid #e5484d; }"));
    QPointer<QLineEdit> guard(edit);
    QTimer::singleShot(1500, this, [guard, before] {
      if (guard)
        guard->setStyleSheet(before);
    });
  }
  if (!labels.isEmpty()) {
    if (auto* first = findChild<QLineEdit*>(fields.value(labels.first())))
      first->setFocus();
  }
}

void MainWindow::cancelMacroRepeat(const QString& why) {
  if (macroRepeatIndex_ < 0)
    return;
  macroRepeatIndex_ = -1;
  if (macroRepeatTimer_)
    macroRepeatTimer_->stop();
  if (!why.isEmpty())
    setTxStatus(why + QStringLiteral(" The repeating macro stopped. Nothing was keyed."));
}
