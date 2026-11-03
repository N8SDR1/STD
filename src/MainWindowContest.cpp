// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

// Contest mode for the keyboard modes: a contest row on the QSO bar with
// Run and S&P messages on F1 to F8, their exchange, your exchange with the
// serial, and a dupe check against the log for this contest session.
// Nothing here keys by itself; a message keys only as a macro would, with
// Macros key on. START is never sent.

#include "MainWindow.h"
#include "ContestMode.h"
#include "ModeProfile.h"
#include "QsoLog.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

namespace {

QFont contestUi(int pixelSize) {
  QFont font("Inter");
  font.setPixelSize(pixelSize);
  return font;
}

QFont contestMono(int pixelSize) {
  QFont font("JetBrains Mono");
  font.setStyleHint(QFont::Monospace);
  font.setPixelSize(pixelSize);
  return font;
}

const ContestTemplate& activeTemplate() {
  return contestTemplate(QSettings().value(QStringLiteral("contest/template")).toString());
}

bool contestRunning() {
  return QSettings().value(QStringLiteral("contest/run"), true).toBool();
}

QDateTime contestSession(const QString& id) {
  return QDateTime::fromString(QSettings().value(QStringLiteral("contest/session/%1").arg(id)).toString(),
                               Qt::ISODate);
}

}  // namespace

QWidget* MainWindow::makeContestRow(QWidget* parent) {
  auto* row = new QWidget(parent);
  row->setObjectName(QStringLiteral("contestRow"));
  auto* lay = new QHBoxLayout(row);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(4);

  auto* name = new QPushButton(row);
  name->setObjectName(QStringLiteral("contestName"));
  name->setFont(contestUi(12));
  name->setFocusPolicy(Qt::NoFocus);
  name->setToolTip(QStringLiteral("The contest. Click for Options → Contest Mode: the contest, your exchange, "
                                  "the serial, the F1 to F8 messages, and a new session. This does not key."));
  connect(name, &QPushButton::clicked, this, [this] { openContestDialog(); });
  lay->addWidget(name);

  auto* runSp = new QPushButton(row);
  runSp->setObjectName(QStringLiteral("contestRunSp"));
  runSp->setFont(contestUi(12));
  runSp->setFocusPolicy(Qt::NoFocus);
  runSp->setToolTip(QStringLiteral(
      "Run: you call CQ and they answer. S&P: search and pounce, you answer their CQ. Click to switch; "
      "F1 to F8 and Enter follow it. This does not key."));
  connect(runSp, &QPushButton::clicked, this, [this] {
    QSettings().setValue(QStringLiteral("contest/run"), !contestRunning());
    applyContestRow();
  });
  lay->addWidget(runSp);

  for (int i = 0; i < 8; ++i) {
    auto* f = new QPushButton(row);
    f->setObjectName(QStringLiteral("contestF%1").arg(i + 1));
    f->setFont(contestMono(11));
    f->setFocusPolicy(Qt::NoFocus);
    connect(f, &QPushButton::clicked, this, [this, i] { sendContestMessage(i); });
    lay->addWidget(f);
  }

  auto* exchLabel = new QLabel(QStringLiteral("Exch"), row);
  exchLabel->setObjectName(QStringLiteral("qsoBarLabel"));
  exchLabel->setFont(contestUi(12));
  auto* exch = new QLineEdit(row);
  exch->setObjectName(QStringLiteral("qsoBarExch"));
  exch->setProperty("qsoLabel", QStringLiteral("Exch"));
  exch->setFont(contestMono(13));
  exch->setFixedWidth(150);
  connect(exch, &QLineEdit::textEdited, exch, [exch](const QString& text) {
    const int at = exch->cursorPosition();
    exch->setText(text.toUpper());
    exch->setCursorPosition(at);
  });
  exchLabel->setBuddy(exch);
  lay->addWidget(exchLabel);
  lay->addWidget(exch);

  auto* sent = new QLabel(row);
  sent->setObjectName(QStringLiteral("contestSent"));
  sent->setFont(contestMono(12));
  sent->setToolTip(QStringLiteral("Your exchange as it goes out: <XOUT> in a message. The serial goes up by one "
                                  "each time a contest QSO is logged."));
  lay->addWidget(sent);

  auto* dupe = new QLabel(row);
  dupe->setObjectName(QStringLiteral("contestDupe"));
  dupe->setFont(contestUi(12));
  dupe->setToolTip(QStringLiteral("DUPE: this call is already in the log for this contest on this band since the "
                                  "session began. F6 sends QSO B4."));
  lay->addWidget(dupe);
  lay->addStretch(1);
  row->hide();

  // F1..F8 and Insert, only while contest mode is on in a keyboard mode.
  for (int i = 0; i < 8; ++i) {
    auto* key = new QShortcut(QKeySequence(Qt::Key_F1 + i), this);
    key->setObjectName(QStringLiteral("contestKeyF%1").arg(i + 1));
    key->setContext(Qt::WindowShortcut);
    key->setEnabled(false);
    connect(key, &QShortcut::activated, this, [this, i] { sendContestMessage(i); });
  }
  auto* insert = new QShortcut(QKeySequence(Qt::Key_Insert), this);
  insert->setObjectName(QStringLiteral("contestKeyInsert"));
  insert->setContext(Qt::WindowShortcut);
  insert->setEnabled(false);
  connect(insert, &QShortcut::activated, this, [this] { sendContestMessage(1); });
  return row;
}

bool MainWindow::contestActive() const {
  if (!QSettings().value(QStringLiteral("contest/on"), false).toBool())
    return false;
  const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
  return bar && bar->isVisible();
}

void MainWindow::applyContestRow() {
  auto* row = findChild<QWidget*>(QStringLiteral("contestRow"));
  if (!row)
    return;
  const bool on = contestActive();
  row->setVisible(on);
  for (auto* key : findChildren<QShortcut*>()) {
    if (key->objectName().startsWith(QLatin1String("contestKey")))
      key->setEnabled(on);
  }
  if (!on)
    return;
  const QSettings store;
  const ContestTemplate& t = activeTemplate();
  const bool run = contestRunning();
  if (auto* name = findChild<QPushButton*>(QStringLiteral("contestName")))
    name->setText(t.name);
  if (auto* runSp = findChild<QPushButton*>(QStringLiteral("contestRunSp"))) {
    runSp->setText(run ? QStringLiteral("Run") : QStringLiteral("S&&P"));
    runSp->setStyleSheet(run ? QStringLiteral("QPushButton#contestRunSp { background: #123040; color: #00e5ff; "
                                              "font-weight: bold; padding: 2px 8px; }")
                             : QStringLiteral("QPushButton#contestRunSp { background: #2a2410; color: #f5c542; "
                                              "font-weight: bold; padding: 2px 8px; }"));
  }
  const QStringList texts = contestMessages(t, run);
  const QStringList labels = contestLabels(t, run);
  const MacroContext context = macroContext();
  for (int i = 0; i < 8; ++i) {
    auto* f = findChild<QPushButton*>(QStringLiteral("contestF%1").arg(i + 1));
    if (!f)
      continue;
    const QString label = i < labels.size() ? labels.at(i) : QString();
    const QString text = i < texts.size() ? texts.at(i) : QString();
    f->setText(QStringLiteral("F%1 %2").arg(i + 1).arg(label));
    const MacroPlan plan = planMacro(text, context);
    QString tip = plan.text.isEmpty() ? QStringLiteral("(empty)") : plan.text;
    if (!plan.missing.isEmpty())
      tip += QStringLiteral("\n\nEmpty now: %1.").arg(plan.missing.join(QStringLiteral(", ")));
    tip += QStringLiteral("\n\nF%1 on the keyboard sends it too. It keys only when Macros key is on, as any "
                          "macro does. START is never sent.")
               .arg(i + 1);
    f->setToolTip(tip);
    f->setEnabled(!text.trimmed().isEmpty());
  }
  if (auto* sent = findChild<QLabel*>(QStringLiteral("contestSent")))
    sent->setText(QStringLiteral("Sent %1")
                      .arg(contestExchange(t, store.value(QStringLiteral("contest/myExch")).toString(),
                                           store.value(QStringLiteral("dx/macroSerial"), 1).toInt())));
  if (auto* exch = findChild<QLineEdit*>(QStringLiteral("qsoBarExch")))
    exch->setToolTip(QStringLiteral("Their exchange: %1. Read from their text after their call and offered with "
                                    "a dashed border; a click, Tab, or Enter takes it. <XIN> in a message. Logged "
                                    "as SRX_STRING. Space in Call jumps here.")
                         .arg(t.theirHint));
  updateContestDupe();
}

void MainWindow::sendContestMessage(int index) {
  if (!contestActive() || index < 0 || index >= 8)
    return;
  const QStringList texts = contestMessages(activeTemplate(), contestRunning());
  if (index >= texts.size() || texts.at(index).trimmed().isEmpty())
    return;
  sendMacroText(texts.at(index), -1);
}

void MainWindow::updateContestDupe() {
  auto* dupe = findChild<QLabel*>(QStringLiteral("contestDupe"));
  if (!dupe)
    return;
  const auto* callEdit = findChild<QLineEdit*>(QStringLiteral("qsoBarCall"));
  const QString call = callEdit ? callEdit->text().trimmed().toUpper() : QString();
  if (!contestActive() || call.size() < 3) {
    dupe->clear();
    dupe->setStyleSheet(QString());
    return;
  }
  const ContestTemplate& t = activeTemplate();
  const QString path = logbookPath();
  const QDateTime since = contestSession(t.id);
  const QString source = path + QLatin1Char('|') + t.adifId + QLatin1Char('|') + since.toString(Qt::ISODate);
  const QDateTime stamp = QFileInfo(path).lastModified();
  if (source != contestDupeSource_ || stamp != contestDupeStamp_) {
    contestDupes_ = contestWorked(path, t.adifId, since);
    contestDupeSource_ = source;
    contestDupeStamp_ = stamp;
  }
  const QString band = qsoBandForHz(profile_ ? profile_->dialHz() : 0);
  if (contestDupes_.contains(contestDupeKey(call, band))) {
    dupe->setText(QStringLiteral("DUPE"));
    dupe->setStyleSheet(QStringLiteral("QLabel { background: #b42318; color: white; font-weight: bold; "
                                       "padding: 1px 6px; border-radius: 3px; }"));
  } else {
    dupe->setText(QStringLiteral("New"));
    dupe->setStyleSheet(QStringLiteral("QLabel { color: #2ee66b; font-weight: bold; padding: 1px 6px; }"));
  }
}

// After a contest QSO is logged: it is a dupe from now on, and the serial
// moves on to the next QSO.
void MainWindow::contestLogged(const QString& call, qint64 dialHz) {
  contestDupes_.insert(contestDupeKey(call, qsoBandForHz(dialHz)));
  QSettings store;
  const int serial = std::max(1, store.value(QStringLiteral("dx/macroSerial"), 1).toInt()) + 1;
  store.setValue(QStringLiteral("dx/macroSerial"), serial);
  applyContestRow();
}

// Enter Sends Message in Call and Exch, and Space in Call goes to Exch.
bool MainWindow::contestKey(QObject* watched, QKeyEvent* key) {
  const QString name = watched->objectName();
  if (name != QLatin1String("qsoBarCall") && name != QLatin1String("qsoBarExch"))
    return false;
  if (!contestActive() || key->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
    return false;
  auto* call = findChild<QLineEdit*>(QStringLiteral("qsoBarCall"));
  auto* exch = findChild<QLineEdit*>(QStringLiteral("qsoBarExch"));
  if (!call || !exch)
    return false;
  if (key->key() == Qt::Key_Space && name == QLatin1String("qsoBarCall")) {
    exch->setFocus();
    return true;
  }
  if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter)
    return false;
  // An exchange read from their text and still offered is taken first.
  if (exch->text().trimmed().isEmpty() && !contestHeard_.isEmpty())
    acceptQsoSuggestion(exch);
  const int n = contestEnterMessage(contestRunning(), call->text().trimmed().isEmpty(),
                                    exch->text().trimmed().isEmpty());
  if (n >= 0)
    sendContestMessage(n);
  if (n == 1 && contestRunning())
    exch->setFocus();
  else if (n == 3)
    exch->setFocus();
  return true;
}

void MainWindow::openContestDialog() {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("contestDialog"));
  dialog.setWindowTitle(QStringLiteral("Contest Mode"));
  QSettings store;

  auto* on = new QCheckBox(QStringLiteral("Contest mode on (RTTY, PSK, Hell, and the other keyboard modes)"),
                           &dialog);
  on->setObjectName(QStringLiteral("contestOn"));
  on->setChecked(store.value(QStringLiteral("contest/on"), false).toBool());

  auto* pick = new QComboBox(&dialog);
  pick->setObjectName(QStringLiteral("contestTemplate"));
  for (const ContestTemplate& t : contestTemplates())
    pick->addItem(t.name, t.id);
  const int at = pick->findData(activeTemplate().id);
  pick->setCurrentIndex(at < 0 ? 0 : at);

  auto* mine = new QLineEdit(store.value(QStringLiteral("contest/myExch")).toString(), &dialog);
  mine->setObjectName(QStringLiteral("contestMyExch"));
  auto* mineHint = new QLabel(&dialog);
  mineHint->setWordWrap(true);

  auto* serial = new QSpinBox(&dialog);
  serial->setObjectName(QStringLiteral("contestSerial"));
  serial->setRange(1, 9999);
  serial->setValue(std::max(1, store.value(QStringLiteral("dx/macroSerial"), 1).toInt()));
  serial->setToolTip(QStringLiteral("The next serial number you send. # in your exchange, <CNTR> in a macro."));

  auto* session = new QLabel(&dialog);
  session->setWordWrap(true);
  auto* fresh = new QPushButton(QStringLiteral("New session"), &dialog);
  fresh->setObjectName(QStringLiteral("contestNewSession"));
  fresh->setToolTip(QStringLiteral("Start this contest over: dupes count only QSOs logged from now on, and the "
                                   "serial goes back to 001. Your log is not changed."));

  auto* table = new QTableWidget(8, 5, &dialog);
  table->setObjectName(QStringLiteral("contestMessages"));
  table->setHorizontalHeaderLabels({QStringLiteral("Key"), QStringLiteral("Run name"), QStringLiteral("Run message"),
                                    QStringLiteral("S&P name"), QStringLiteral("S&P message")});
  table->verticalHeader()->hide();
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  table->setMinimumWidth(980);
  table->setMinimumHeight(290);
  auto* reset = new QPushButton(QStringLiteral("Reset messages"), &dialog);
  reset->setToolTip(QStringLiteral("Put back this contest's own F1 to F8 messages."));

  // Edits are kept per contest while the dialog is open.
  QHash<QString, QStringList> edited;
  QString shown;
  auto tableTexts = [table] {
    QStringList out;
    for (int r = 0; r < 8; ++r)
      for (int c = 1; c < 5; ++c)
        out << (table->item(r, c) ? table->item(r, c)->text() : QString());
    return out;
  };
  auto fill = [&](const QString& id, bool defaults) {
    const ContestTemplate& t = contestTemplate(id);
    QStringList cells;
    if (!defaults && edited.contains(id)) {
      cells = edited.value(id);
    } else {
      const QStringList rn = defaults ? t.labels : contestLabels(t, true);
      const QStringList rt = defaults ? t.run : contestMessages(t, true);
      const QStringList sn = defaults ? t.spLabels : contestLabels(t, false);
      const QStringList st = defaults ? t.sp : contestMessages(t, false);
      for (int r = 0; r < 8; ++r)
        cells << rn.value(r) << rt.value(r) << sn.value(r) << st.value(r);
    }
    for (int r = 0; r < 8; ++r) {
      auto* keyItem = new QTableWidgetItem(QStringLiteral("F%1").arg(r + 1));
      keyItem->setFlags(Qt::ItemIsEnabled);
      table->setItem(r, 0, keyItem);
      for (int c = 1; c < 5; ++c)
        table->setItem(r, c, new QTableWidgetItem(cells.value(r * 4 + c - 1)));
    }
    mine->setPlaceholderText(t.myExchHint);
    mineHint->setText(QStringLiteral("Your exchange, without the 599: %1. Logged as CONTEST_ID %2.")
                          .arg(t.myExchHint, t.adifId.isEmpty() ? QStringLiteral("(none)") : t.adifId));
    const QDateTime since = contestSession(id);
    session->setText(since.isValid()
                         ? QStringLiteral("This session began %1 UTC. Dupes count from then.")
                               .arg(since.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                         : QStringLiteral("No session yet: one begins when you press OK with contest mode on."));
    shown = id;
  };
  fill(pick->currentData().toString(), false);
  connect(pick, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) {
    edited.insert(shown, tableTexts());
    fill(pick->currentData().toString(), false);
  });
  connect(reset, &QPushButton::clicked, &dialog, [&] { fill(shown, true); });
  connect(fresh, &QPushButton::clicked, &dialog, [&] {
    if (!qEnvironmentVariableIsSet("STD_SELFTEST") &&
        QMessageBox::question(&dialog, QStringLiteral("New session"),
                              QStringLiteral("Start %1 over? Dupes will count only QSOs from now on and the serial "
                                             "goes back to 001. Your log is not changed.")
                                  .arg(pick->currentText())) != QMessageBox::Yes)
      return;
    QSettings().setValue(QStringLiteral("contest/session/%1").arg(shown),
                         QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    serial->setValue(1);
    fill(shown, false);
  });

  auto* form = new QFormLayout;
  form->addRow(on);
  form->addRow(QStringLiteral("Contest"), pick);
  form->addRow(QStringLiteral("Your exchange"), mine);
  form->addRow(QString(), mineHint);
  form->addRow(QStringLiteral("Next serial"), serial);
  auto* sessionRow = new QHBoxLayout;
  sessionRow->addWidget(session, 1);
  sessionRow->addWidget(fresh);
  form->addRow(QStringLiteral("Session"), sessionRow);

  auto* help = new QLabel(QStringLiteral(
                              "F1 to F8 send these messages, as macros. <XOUT> is your exchange with the serial, "
                              "<XIN> theirs, <CALL> their call. <LOG> logs once sent and moves the serial on; "
                              "<CLRQSO> clears the bar for the next one. Enter in Call or Exch sends the next "
                              "message (Run: CQ, then his call and exchange, then TU and log; S&P: your call, then "
                              "TU, exchange, and log). Space in Call goes to Exch. Insert sends F2. A message keys "
                              "only when Macros key is on. START is never sent."),
                          &dialog);
  help->setWordWrap(true);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto* bottom = new QHBoxLayout;
  bottom->addWidget(reset);
  bottom->addStretch(1);
  bottom->addWidget(buttons);

  auto* lay = new QVBoxLayout(&dialog);
  lay->addLayout(form);
  lay->addWidget(table, 1);
  lay->addWidget(help);
  lay->addLayout(bottom);

  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  if (dialog.exec() != QDialog::Accepted)
    return;

  edited.insert(shown, tableTexts());
  QSettings out;
  for (auto it = edited.cbegin(); it != edited.cend(); ++it) {
    const ContestTemplate& t = contestTemplate(it.key());
    const QStringList& cells = it.value();
    for (int r = 0; r < 8; ++r) {
      const QString rn = cells.value(r * 4), rt = cells.value(r * 4 + 1);
      const QString sn = cells.value(r * 4 + 2), st = cells.value(r * 4 + 3);
      auto keep = [&](const QString& key, const QString& value, const QString& base) {
        if (value == base)
          out.remove(key);
        else
          out.setValue(key, value);
      };
      keep(QStringLiteral("contest/msg/%1/runlabel%2").arg(t.id).arg(r), rn, t.labels.value(r));
      keep(QStringLiteral("contest/msg/%1/run%2").arg(t.id).arg(r), rt, t.run.value(r));
      keep(QStringLiteral("contest/msg/%1/splabel%2").arg(t.id).arg(r), sn, t.spLabels.value(r));
      keep(QStringLiteral("contest/msg/%1/sp%2").arg(t.id).arg(r), st, t.sp.value(r));
    }
  }
  const QString id = pick->currentData().toString();
  out.setValue(QStringLiteral("contest/on"), on->isChecked());
  out.setValue(QStringLiteral("contest/template"), id);
  out.setValue(QStringLiteral("contest/myExch"), mine->text().simplified().toUpper());
  out.setValue(QStringLiteral("dx/macroSerial"), serial->value());
  if (on->isChecked() && !contestSession(id).isValid())
    out.setValue(QStringLiteral("contest/session/%1").arg(id), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  contestDupeSource_.clear();
  applyContestRow();
  showMacroSet();
  const auto* bar = findChild<QWidget*>(QStringLiteral("qsoBar"));
  setTxStatus(!on->isChecked() ? QStringLiteral("Contest mode is off. Nothing was keyed. START was not sent.")
              : bar && bar->isVisible()
                  ? QStringLiteral("Contest mode on: %1. F1 to F8 send its messages. Nothing was keyed. START was "
                                   "not sent.")
                        .arg(pick->currentText())
                  : QStringLiteral("Contest mode on: %1. It shows in RTTY, PSK, Hell, and the other keyboard modes. "
                                   "Nothing was keyed. START was not sent.")
                        .arg(pick->currentText()));
}
