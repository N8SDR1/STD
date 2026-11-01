// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// JS8 operating screen, shown only in JS8 mode: the clock, offset, filter
// and RX/TX lamps in the top bar, band and call activity beside a
// conversation pane, and the JS8Call message buttons over the transmit box.
// Callsign and grid come from Options → User; nothing here edits them.

#include "MainWindow.h"

#include "EchoLog.h"
#include "TciClient.h"
#include "WaterfallWidget.h"
#include "js8/Js8Modem.h"

#include <algorithm>

#include <QCheckBox>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QFont js8Ui(int pixelSize, QFont::Weight weight = QFont::Normal) {
  QFont font("Inter");
  font.setPixelSize(pixelSize);
  font.setWeight(weight);
  return font;
}

QFont js8Mono(int pixelSize) {
  QFont font("JetBrains Mono");
  font.setStyleHint(QFont::Monospace);
  font.setPixelSize(pixelSize);
  return font;
}

// A message is kept on its offset this long without a closing frame
// before it is written out as it stands.
constexpr qint64 kJs8OpenMs = 45000;
// Rows and calls not heard for an hour drop off the lists.
constexpr qint64 kJs8KeepMs = 60 * 60 * 1000;
constexpr int kJs8SameOffsetHz = 10;

QString snrText(int snr) {
  return QString::asprintf("%+03d", snr);
}

QString ageText(qint64 lastMs, qint64 now) {
  return QString::number(std::max<qint64>(0, (now - lastMs) / 60000));
}

void setLit(QLabel* lamp, bool lit) {
  if (!lamp || lamp->property("lit").toBool() == lit)
    return;
  lamp->setProperty("lit", lit);
  lamp->style()->unpolish(lamp);
  lamp->style()->polish(lamp);
}

QPushButton* actionButton(const QString& text, const char* name, const QString& tip, QWidget* parent) {
  auto* button = new QPushButton(text, parent);
  button->setObjectName(QLatin1String(name));
  button->setFont(js8Ui(11));
  button->setFocusPolicy(Qt::NoFocus);
  button->setToolTip(tip);
  return button;
}

}  // namespace

QWidget* MainWindow::makeJs8Strip(QWidget* parent) {
  auto* strip = new QWidget(parent);
  strip->setObjectName(QStringLiteral("js8Strip"));
  auto* row = new QHBoxLayout(strip);
  row->setContentsMargins(24, 0, 0, 0);
  row->setSpacing(22);

  auto* clock = new QWidget(strip);
  auto* clockCol = new QVBoxLayout(clock);
  clockCol->setContentsMargins(0, 0, 0, 0);
  clockCol->setSpacing(0);
  auto* utc = new QLabel(QStringLiteral("00:00:00"), clock);
  utc->setObjectName(QStringLiteral("js8Utc"));
  utc->setFont(js8Mono(28));
  utc->setToolTip(QStringLiteral("UTC. JS8 frames start on the 15 second marks. This does not key."));
  auto* date = new QLabel(clock);
  date->setObjectName(QStringLiteral("js8Date"));
  date->setFont(js8Ui(13));
  clockCol->addWidget(utc);
  clockCol->addWidget(date);
  row->addWidget(clock);

  auto* tune = new QWidget(strip);
  auto* grid = new QGridLayout(tune);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(6);
  auto* offsetLabel = new QLabel(QStringLiteral("Offset"), tune);
  offsetLabel->setObjectName(QStringLiteral("js8StripLabel"));
  offsetLabel->setFont(js8Ui(12));
  auto* offset = new QSpinBox(tune);
  offset->setObjectName(QStringLiteral("js8Offset"));
  offset->setRange(200, 2900);
  offset->setSingleStep(10);
  offset->setSuffix(QStringLiteral(" Hz"));
  offset->setFont(js8Mono(15));
  offset->setValue(waterfall_ ? waterfall_->txHz() : 1500);
  offset->setToolTip(QStringLiteral(
      "Your JS8 offset. Sets the RX and TX markers together, as JS8Call does. "
      "Double-click a row in either list to move here. This does not key."));
  auto* filterOn = new QCheckBox(QStringLiteral("Filter"), tune);
  filterOn->setObjectName(QStringLiteral("js8Filter"));
  filterOn->setFont(js8Ui(12));
  filterOn->setChecked(QSettings().value(QStringLiteral("js8/filterOn"), false).toBool());
  filterOn->setToolTip(QStringLiteral(
      "List only stations within Width of your offset. Off lists the whole passband. "
      "This does not key."));
  auto* width = new QSpinBox(tune);
  width->setObjectName(QStringLiteral("js8FilterWidth"));
  width->setRange(200, 2500);
  width->setSingleStep(100);
  width->setSuffix(QStringLiteral(" Hz"));
  width->setPrefix(QStringLiteral("Width "));
  width->setFont(js8Mono(13));
  width->setValue(QSettings().value(QStringLiteral("js8/filterWidth"), 1000).toInt());
  width->setToolTip(QStringLiteral("Filter width, centred on your offset. This does not key."));
  grid->addWidget(offsetLabel, 0, 0);
  grid->addWidget(offset, 0, 1);
  grid->addWidget(filterOn, 1, 0);
  grid->addWidget(width, 1, 1);
  row->addWidget(tune);

  auto* to = new QLabel(strip);
  to->setObjectName(QStringLiteral("js8To"));
  to->setFont(js8Mono(22));
  to->setMinimumWidth(220);
  to->setToolTip(QStringLiteral(
      "The station your directed buttons go to. Click a call in either list to pick it. "
      "This does not key."));
  row->addWidget(to);

  auto* lamps = new QWidget(strip);
  auto* lampCol = new QVBoxLayout(lamps);
  lampCol->setContentsMargins(0, 0, 0, 0);
  lampCol->setSpacing(6);
  auto* rx = new QLabel(QStringLiteral("RX"), lamps);
  rx->setObjectName(QStringLiteral("js8RxLamp"));
  rx->setToolTip(QStringLiteral("Lit while the radio is connected and receiving."));
  auto* tx = new QLabel(QStringLiteral("TX"), lamps);
  tx->setObjectName(QStringLiteral("js8TxLamp"));
  tx->setToolTip(QStringLiteral("Lit while STD is transmitting."));
  for (QLabel* lamp : {rx, tx}) {
    lamp->setFont(js8Ui(12, QFont::DemiBold));
    lamp->setAlignment(Qt::AlignCenter);
    lamp->setFixedSize(84, 26);
    lamp->setProperty("lit", false);
    lampCol->addWidget(lamp);
  }
  row->addWidget(lamps);

  connect(offset, qOverload<int>(&QSpinBox::valueChanged), this, [this](int hz) {
    if (!waterfall_)
      return;
    waterfall_->setRxHz(hz);
    waterfall_->setTxHz(hz);
  });
  connect(filterOn, &QCheckBox::toggled, this, [](bool on) {
    QSettings().setValue(QStringLiteral("js8/filterOn"), on);
  });
  connect(width, qOverload<int>(&QSpinBox::valueChanged), this, [](int hz) {
    QSettings().setValue(QStringLiteral("js8/filterWidth"), hz);
  });
  strip->hide();
  return strip;
}

QWidget* MainWindow::makeJs8Receive(QWidget* parent) {
  auto* host = new QWidget(parent);
  host->setObjectName(QStringLiteral("js8Receive"));
  auto* layout = new QVBoxLayout(host);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* split = new QSplitter(Qt::Horizontal, host);
  split->setObjectName(QStringLiteral("js8Split"));
  split->setChildrenCollapsible(false);

  auto makeTree = [this, split](const char* name, const QStringList& titles, const QString& tip) {
    auto* tree = new QTreeWidget(split);
    tree->setObjectName(QLatin1String(name));
    tree->setColumnCount(int(titles.size()));
    tree->setHeaderLabels(titles);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setFont(js8Mono(12));
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setToolTip(tip);
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(int(titles.size()) - 1, QHeaderView::Stretch);
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
      const QString call = item ? item->data(0, Qt::UserRole + 1).toString() : QString();
      if (!call.isEmpty())
        js8Select(call);
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
      if (!item)
        return;
      const QString call = item->data(0, Qt::UserRole + 1).toString();
      if (!call.isEmpty())
        js8Select(call);
      const int hz = item->data(0, Qt::UserRole).toInt();
      if (hz > 0) {
        if (auto* offset = findChild<QSpinBox*>(QStringLiteral("js8Offset")))
          offset->setValue(hz);
      }
    });
    return tree;
  };

  makeTree("js8BandActivity", {QStringLiteral("Offset"), QStringLiteral("Age"), QStringLiteral("SNR"),
                               QStringLiteral("Message")},
           QStringLiteral("Band activity: every JS8 offset heard, newest text on each. Age is minutes. "
                          "Click to pick the sender, double-click to move your offset there. "
                          "♢ ends a message. This does not key."));

  auto* talk = new QTextEdit(split);
  talk->setObjectName(QStringLiteral("js8Conversation"));
  talk->setReadOnly(true);
  talk->setAcceptRichText(false);
  talk->setFont(js8Mono(12));
  talk->setPlaceholderText(QStringLiteral(
      "Messages to you, from the station you picked, and on your offset. Yours print in red."));
  talk->setToolTip(QStringLiteral(
      "Conversation. Gold lines name your call. Red lines are what you sent. This does not key."));

  makeTree("js8CallActivity", {QStringLiteral("Callsign"), QStringLiteral("Age"), QStringLiteral("SNR"),
                               QStringLiteral("Offset"), QStringLiteral("Grid")},
           QStringLiteral("Call activity: every station heard, newest first. @ALLCALL reaches everyone. "
                          "Click to pick a station for the directed buttons, double-click to move "
                          "your offset to it. This does not key."));

  const QList<int> sizes = [] {
    QList<int> out;
    for (const QVariant& v : QSettings().value(QStringLiteral("js8/split")).toList())
      out.append(v.toInt());
    return out.size() == 3 ? out : QList<int>{420, 560, 380};
  }();
  split->setSizes(sizes);
  connect(split, &QSplitter::splitterMoved, this, [split] {
    QVariantList out;
    for (int size : split->sizes())
      out.append(size);
    QSettings().setValue(QStringLiteral("js8/split"), out);
  });
  layout->addWidget(split, 1);
  host->hide();
  return host;
}

QWidget* MainWindow::makeJs8Actions(QWidget* parent) {
  auto* row = new QWidget(parent);
  row->setObjectName(QStringLiteral("js8Actions"));
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 2, 0, 2);
  layout->setSpacing(4);
  const QString sendsNow = QStringLiteral(" Sends at the next 15 second period. START is never sent.");

  auto* hb = actionButton(QStringLiteral("HB"), "js8Hb",
                          QStringLiteral("Heartbeat: @HB HEARTBEAT and your grid.") + sendsNow, row);
  auto* cq = actionButton(QStringLiteral("CQ"), "js8Cq",
                          QStringLiteral("CQ CQ CQ and your grid to @ALLCALL.") + sendsNow, row);
  auto* reply = actionButton(QStringLiteral("Reply"), "js8Reply",
                             QStringLiteral("Put the picked call in the box so you can type the rest. "
                                            "Press TX to send. This does not key."),
                             row);
  auto* snr = actionButton(QStringLiteral("SNR?"), "js8Snr",
                           QStringLiteral("Ask the picked station for your signal report.") + sendsNow, row);
  auto* info = actionButton(QStringLiteral("INFO?"), "js8Info",
                            QStringLiteral("Ask the picked station for its station info.") + sendsNow, row);
  auto* status = actionButton(QStringLiteral("STATUS?"), "js8Status",
                              QStringLiteral("Ask the picked station for its status.") + sendsNow, row);
  auto* gridAsk = actionButton(QStringLiteral("GRID?"), "js8GridAsk",
                               QStringLiteral("Ask the picked station for its grid.") + sendsNow, row);

  auto* directed = new QToolButton(row);
  directed->setObjectName(QStringLiteral("js8Directed"));
  directed->setText(QStringLiteral("Directed"));
  directed->setFont(js8Ui(11));
  directed->setPopupMode(QToolButton::InstantPopup);
  directed->setFocusPolicy(Qt::NoFocus);
  directed->setToolTip(QStringLiteral("More JS8Call commands to the picked station.") + sendsNow);
  auto* menu = new QMenu(directed);
  struct Command {
    const char* label;
    const char* text;
  };
  const Command commands[] = {
      {"HEARING?", " HEARING?"}, {"QSL?", " QSL?"}, {"AGN?", " AGN?"}, {"HW CPY?", " HW CPY?"},
      {nullptr, nullptr},
      {"Send their SNR", "snr"}, {"Send my GRID", "grid"},
      {nullptr, nullptr},
      {"ACK", " ACK"}, {"RR", " RR"}, {"FB", " FB"}, {"YES", " YES"}, {"NO", " NO"}, {"73", " 73"},
      {"SK", " SK"},
  };
  for (const Command& c : commands) {
    if (!c.label) {
      menu->addSeparator();
      continue;
    }
    const QString what = QLatin1String(c.text);
    menu->addAction(QLatin1String(c.label), this, [this, what] {
      if (js8Selected_.isEmpty())
        return;
      if (what == QLatin1String("snr")) {
        const auto it = js8Calls_.constFind(js8Selected_);
        if (it == js8Calls_.constEnd()) {
          setTxStatus(QStringLiteral("%1 has not been heard, so there is no report to send. Nothing was keyed.")
                          .arg(js8Selected_));
          return;
        }
        js8Compose(js8Selected_ + QLatin1String(" SNR ") + snrText(it->snr), true);
      } else if (what == QLatin1String("grid")) {
        const QString grid = stationGrid().trimmed().toUpper().left(4);
        if (grid.isEmpty()) {
          setTxStatus(QStringLiteral("Set your locator under Options → User. Nothing was keyed."));
          return;
        }
        js8Compose(js8Selected_ + QLatin1String(" GRID ") + grid, true);
      } else {
        js8Compose(js8Selected_ + what, true);
      }
    });
  }
  directed->setMenu(menu);

  auto* deselect = actionButton(QStringLiteral("Deselect"), "js8Deselect",
                                QStringLiteral("Clear the picked station. This does not key."), row);

  connect(hb, &QPushButton::clicked, this, [this] {
    const QString grid = stationGrid().trimmed().toUpper().left(4);
    js8Compose((QStringLiteral("@HB HEARTBEAT ") + grid).trimmed(), true);
  });
  connect(cq, &QPushButton::clicked, this, [this] {
    const QString grid = stationGrid().trimmed().toUpper().left(4);
    js8Compose((QStringLiteral("CQ CQ CQ ") + grid).trimmed(), true);
  });
  connect(reply, &QPushButton::clicked, this, [this] {
    if (!js8Selected_.isEmpty())
      js8Compose(js8Selected_ + QLatin1Char(' '), false);
  });
  for (const auto& [button, ask] : {std::pair{snr, " SNR?"}, std::pair{info, " INFO?"},
                                    std::pair{status, " STATUS?"}, std::pair{gridAsk, " GRID?"}}) {
    const QString text = QLatin1String(ask);
    connect(button, &QPushButton::clicked, this, [this, text] {
      if (!js8Selected_.isEmpty())
        js8Compose(js8Selected_ + text, true);
    });
  }
  connect(deselect, &QPushButton::clicked, this, [this] { js8Select(QString()); });

  for (QWidget* w : std::initializer_list<QWidget*>{hb, cq, reply, snr, info, status, gridAsk, directed, deselect})
    layout->addWidget(w);
  layout->addStretch(1);
  row->hide();
  return row;
}

void MainWindow::wireJs8() {
  connect(js8_, &Js8Engine::frameHeard, this, &MainWindow::js8FrameIn);
  if (waterfall_) {
    connect(waterfall_, &WaterfallWidget::frequenciesChanged, this, [this](int, int txHz) {
      if (auto* offset = findChild<QSpinBox*>(QStringLiteral("js8Offset"))) {
        const QSignalBlocker block(offset);
        offset->setValue(txHz);
      }
    });
  }
  js8Clock_ = new QTimer(this);
  js8Clock_->setInterval(500);
  connect(js8Clock_, &QTimer::timeout, this, &MainWindow::tickJs8);
  js8Select(QString());
}

void MainWindow::showJs8Layout(bool on) {
  for (const char* name : {"js8Strip", "js8Actions", "js8Receive"}) {
    if (auto* w = findChild<QWidget*>(QLatin1String(name)))
      w->setVisible(on);
  }
  // JS8 messages are short. Keep the transmit box to a few lines so the
  // activity lists get the height, as in JS8Call.
  if (auto* box = findChild<QPlainTextEdit*>(QStringLiteral("txHell")))
    box->setMaximumHeight(on ? 120 : QWIDGETSIZE_MAX);
  if (on && waterfall_) {
    if (auto* offset = findChild<QSpinBox*>(QStringLiteral("js8Offset"))) {
      const QSignalBlocker block(offset);
      offset->setValue(waterfall_->txHz());
    }
  }
  if (js8Clock_) {
    if (on && !js8Clock_->isActive()) {
      js8Clock_->start();
      tickJs8();
    } else if (!on) {
      js8Clock_->stop();
    }
  }
}

void MainWindow::tickJs8() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (auto* utc = findChild<QLabel*>(QStringLiteral("js8Utc")))
    utc->setText(now.toString(QStringLiteral("HH:mm:ss")));
  if (auto* date = findChild<QLabel*>(QStringLiteral("js8Date")))
    date->setText(now.toString(QStringLiteral("yyyy MMM dd")) + QLatin1String("  UTC"));
  const bool tx = txKeySent_ || txTune_;
  setLit(findChild<QLabel*>(QStringLiteral("js8TxLamp")), tx);
  setLit(findChild<QLabel*>(QStringLiteral("js8RxLamp")), !tx && tci_ && tci_->isOpen());

  // Write out messages that never sent their closing frame, and refresh
  // the ages once a minute.
  const qint64 ms = now.toMSecsSinceEpoch();
  bool changed = false;
  for (Js8BandRow& row : js8Band_) {
    if (row.open && ms - row.lastMs > kJs8OpenMs) {
      row.open = false;
      js8Finish(row);
      changed = true;
    }
  }
  const int minute = int(ms / 60000);
  if (changed || minute != js8LastAgeMinute_) {
    js8LastAgeMinute_ = minute;
    paintJs8Activity();
  }
}

void MainWindow::js8FrameIn(const QString& text, int snrDb, int freqHz, bool last) {
  if (text.isEmpty())
    return;
  const int mine = waterfall_ ? waterfall_->txHz() : 1500;
  const auto* filterOn = findChild<QCheckBox*>(QStringLiteral("js8Filter"));
  const auto* width = findChild<QSpinBox*>(QStringLiteral("js8FilterWidth"));
  if (filterOn && filterOn->isChecked() && width && std::abs(freqHz - mine) > width->value() / 2)
    return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  Js8BandRow* row = nullptr;
  for (Js8BandRow& r : js8Band_) {
    if (std::abs(r.offset - freqHz) <= kJs8SameOffsetHz) {
      row = &r;
      break;
    }
  }
  if (!row) {
    js8Band_.append(Js8BandRow{});
    row = &js8Band_.last();
  }
  const bool continues = row->open && now - row->lastMs < kJs8OpenMs;
  if (continues) {
    row->message += text;
  } else {
    row->message = text;
    static const QRegularExpression fromRe(QStringLiteral("^([A-Z0-9/]+):\\s"));
    const auto m = fromRe.match(text);
    row->from = m.hasMatch() ? m.captured(1) : QString();
  }
  row->offset = freqHz;
  row->snr = snrDb;
  row->lastMs = now;
  row->open = !last;

  if (!row->from.isEmpty()) {
    Js8CallRow& call = js8Calls_[row->from];
    call.call = row->from;
    call.snr = snrDb;
    call.offset = freqHz;
    call.lastMs = now;
    static const QRegularExpression gridRe(
        QStringLiteral("(?:HEARTBEAT|CQ(?: [A-Z]+)*|GRID)\\s+([A-R]{2}[0-9]{2})\\b"));
    const auto g = gridRe.match(row->message);
    if (g.hasMatch())
      call.grid = g.captured(1);
  }
  if (last)
    js8Finish(*row);
  paintJs8Activity();
}

void MainWindow::js8Finish(const Js8BandRow& row) {
  const QString text = row.message.simplified();
  if (text.isEmpty())
    return;
  const QString me = stationCall().trimmed().toUpper();
  const QRegularExpression named(
      QStringLiteral("(^|[^A-Z0-9/])%1([^A-Z0-9/]|$)").arg(QRegularExpression::escape(me)));
  const bool toMe = !me.isEmpty() && named.match(text).hasMatch();
  const bool picked = !js8Selected_.isEmpty() && row.from == js8Selected_;
  const int mine = waterfall_ ? waterfall_->txHz() : 1500;
  const bool here = std::abs(row.offset - mine) <= kJs8SameOffsetHz;
  if (!toMe && !picked && !here)
    return;
  js8ConversationLine(text, toMe ? QColor(0xff, 0xd2, 0x7a) : rxEchoColor(), row.offset);
}

void MainWindow::js8ConversationLine(const QString& text, const QColor& color, int offsetHz) {
  auto* talk = findChild<QTextEdit*>(QStringLiteral("js8Conversation"));
  if (!talk)
    return;
  const QString line = QStringLiteral("%1 - (%2) - %3 ♢")
                           .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss")))
                           .arg(offsetHz)
                           .arg(text);
  QTextCharFormat format;
  format.setForeground(color);
  QTextCursor cursor(talk->document());
  cursor.movePosition(QTextCursor::End);
  if (!talk->document()->isEmpty())
    cursor.insertBlock();
  cursor.insertText(line, format);
  talk->setTextCursor(cursor);
  talk->ensureCursorVisible();
}

void MainWindow::js8NoteSent(const QString& text) {
  const QString me = stationCall().trimmed().toUpper();
  const Js8Header head = js8Header(text, me, stationGrid());
  const QString shown = head.bits.empty() ? me + QLatin1String(": ") + text.simplified().toUpper() : head.display;
  js8ConversationLine(shown, txEchoColor(), waterfall_ ? waterfall_->txHz() : 1500);
  if (!head.toCall.isEmpty() && !head.toCall.startsWith(QLatin1Char('@')) && head.toCall != js8Selected_)
    js8Select(head.toCall);
}

void MainWindow::js8Compose(const QString& text, bool send) {
  if (stationCall().trimmed().isEmpty()) {
    setTxStatus(QStringLiteral("Set your callsign under Options → User. Nothing was keyed. START was not sent."));
    return;
  }
  auto* edit = findChild<QPlainTextEdit*>(QStringLiteral("txHell"));
  if (!edit)
    return;
  auto* tx = findChild<QPushButton*>(QStringLiteral("txSend"));
  if (send && tx && tx->isChecked()) {
    setTxStatus(QStringLiteral(
        "TX is already running. The box was left alone. Try again when this message ends. "
        "START was not sent."));
    return;
  }
  edit->setPlainText(text);
  QTextCursor cursor = edit->textCursor();
  cursor.movePosition(QTextCursor::End);
  edit->setTextCursor(cursor);
  if (!send) {
    edit->setFocus();
    setTxStatus(QStringLiteral("Type the rest of the message, then press TX. This does not key."));
    return;
  }
  if (tx)
    tx->setChecked(true);
}

void MainWindow::js8Select(const QString& call) {
  js8Selected_ = call;
  if (auto* to = findChild<QLabel*>(QStringLiteral("js8To")))
    to->setText(call.isEmpty() ? QStringLiteral("To  —") : QStringLiteral("To  ") + call);
  const bool picked = !call.isEmpty();
  for (const char* name : {"js8Reply", "js8Snr", "js8Info", "js8Status", "js8GridAsk", "js8Directed",
                           "js8Deselect"}) {
    if (auto* w = findChild<QWidget*>(QLatin1String(name)))
      w->setEnabled(picked);
  }
  paintJs8Activity();
}

void MainWindow::clearJs8Activity() {
  js8Band_.clear();
  js8Calls_.clear();
  if (auto* talk = findChild<QTextEdit*>(QStringLiteral("js8Conversation")))
    talk->clear();
  paintJs8Activity();
}

void MainWindow::paintJs8Activity() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  js8Band_.erase(std::remove_if(js8Band_.begin(), js8Band_.end(),
                                [now](const Js8BandRow& r) { return now - r.lastMs > kJs8KeepMs; }),
                 js8Band_.end());
  for (auto it = js8Calls_.begin(); it != js8Calls_.end();) {
    if (now - it->lastMs > kJs8KeepMs)
      it = js8Calls_.erase(it);
    else
      ++it;
  }

  if (auto* band = findChild<QTreeWidget*>(QStringLiteral("js8BandActivity"))) {
    QVector<Js8BandRow> rows = js8Band_;
    std::sort(rows.begin(), rows.end(), [](const Js8BandRow& a, const Js8BandRow& b) { return a.offset < b.offset; });
    const QSignalBlocker block(band);
    band->clear();
    for (const Js8BandRow& r : rows) {
      auto* item = new QTreeWidgetItem(band);
      item->setText(0, QString::number(r.offset));
      item->setText(1, ageText(r.lastMs, now));
      item->setText(2, snrText(r.snr));
      item->setText(3, r.message.simplified() + (r.open ? QString() : QStringLiteral(" ♢")));
      item->setData(0, Qt::UserRole, r.offset);
      item->setData(0, Qt::UserRole + 1, r.from);
      for (int c = 0; c < 3; ++c)
        item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
      if (!r.from.isEmpty() && r.from == js8Selected_)
        band->setCurrentItem(item);
    }
  }
  if (auto* calls = findChild<QTreeWidget*>(QStringLiteral("js8CallActivity"))) {
    QVector<Js8CallRow> rows;
    for (const Js8CallRow& r : js8Calls_)
      rows.append(r);
    std::sort(rows.begin(), rows.end(), [](const Js8CallRow& a, const Js8CallRow& b) { return a.lastMs > b.lastMs; });
    const QSignalBlocker block(calls);
    calls->clear();
    auto* all = new QTreeWidgetItem(calls);
    all->setText(0, QStringLiteral("@ALLCALL"));
    all->setData(0, Qt::UserRole + 1, QStringLiteral("@ALLCALL"));
    if (js8Selected_ == QLatin1String("@ALLCALL"))
      calls->setCurrentItem(all);
    for (const Js8CallRow& r : rows) {
      auto* item = new QTreeWidgetItem(calls);
      item->setText(0, r.call);
      item->setText(1, ageText(r.lastMs, now));
      item->setText(2, snrText(r.snr));
      item->setText(3, QString::number(r.offset));
      item->setText(4, r.grid);
      item->setData(0, Qt::UserRole, r.offset);
      item->setData(0, Qt::UserRole + 1, r.call);
      if (r.call == js8Selected_)
        calls->setCurrentItem(item);
    }
  }
}
