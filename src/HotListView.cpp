// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HotListView.h"

#include "HotList.h"

#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString heardText(const HotEntry& e) {
  if (!e.heard.isValid())
    return QStringLiteral("not heard since STD started");
  QString text = QStringLiteral("%1 %2Z")
                     .arg(e.spotted ? QStringLiteral("cluster spot") : QStringLiteral("heard"),
                          e.heard.toUTC().toString(QStringLiteral("HH:mm")));
  if (!e.where.isEmpty())
    text += QStringLiteral(", ") + e.where;
  return text;
}


}  // namespace

HotListBar::HotListBar(HotList* list, QWidget* parent) : QWidget(parent), list_(list) {
  setObjectName(QStringLiteral("hotListBar"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(3);

  auto* head = new QHBoxLayout;
  head->setSpacing(6);
  auto* title = new QLabel(QStringLiteral("HOT LIST"), this);
  title->setObjectName(QStringLiteral("hotListTitle"));
  title->setStyleSheet(QStringLiteral("color: #ff8a3d; font-weight: 700; font-size: 11px; background: transparent;"));
  count_ = new QLabel(this);
  count_->setObjectName(QStringLiteral("hotListCount"));
  count_->setStyleSheet(QStringLiteral("color: #9fb1bf; font-size: 11px; background: transparent;"));
  alerts_ = new QToolButton(this);
  alerts_->setObjectName(QStringLiteral("hotListAlerts"));
  alerts_->setAutoRaise(true);
  alerts_->setFocusPolicy(Qt::NoFocus);
  alerts_->setToolTip(QStringLiteral(
      "Hot list alerts. Off marks nothing. Visual turns a call red when STD decodes it. Visual + Voice "
      "also says it with the Windows voice from Options → Speech, once per call every 15 minutes. "
      "Click to cycle. This does not key."));
  connect(alerts_, &QToolButton::clicked, this, [this] { list_->cycleAlerts(); });
  head->addWidget(title);
  head->addWidget(count_);
  head->addStretch(1);
  head->addWidget(alerts_);
  col->addLayout(head);

  chips_ = new QWidget(this);
  grid_ = new QGridLayout(chips_);
  grid_->setContentsMargins(0, 0, 0, 0);
  grid_->setHorizontalSpacing(4);
  grid_->setVerticalSpacing(3);
  col->addWidget(chips_);
  empty_ = new QLabel(QStringLiteral("Tick a call in View → DXpeditions, or right-click here to add one."), this);
  empty_->setWordWrap(true);
  empty_->setStyleSheet(QStringLiteral("color: #8796a3; font-size: 11px; background: transparent;"));
  col->addWidget(empty_);
  col->addStretch(1);

  setToolTip(QStringLiteral(
      "Calls you are watching for. Right-click to manage the list, choose which calls show here, or "
      "hide this from the top bar (View → Hot list on top bar brings it back). Nothing here keys."));
  connect(list_, &HotList::changed, this, [this] { refresh(); });
  auto* tick = new QTimer(this);
  tick->setInterval(30 * 1000);  // red fades to amber 15 minutes after a call is heard
  connect(tick, &QTimer::timeout, this, [this] { refresh(); });
  tick->start();
  refresh();
}

int HotListBar::chipCount() const {
  return int(chips_->findChildren<QToolButton*>(QStringLiteral("hotListChip")).size());
}

void HotListBar::refresh() {
  while (QLayoutItem* item = grid_->takeAt(0)) {
    // A chip can be the sender of the click that got here: retire it now,
    // delete it when the event loop is back.
    if (QWidget* w = item->widget()) {
      w->hide();
      w->setObjectName(QString());
      w->deleteLater();
    }
    delete item;
  }
  const QList<HotEntry> shown = list_->displayed();
  const int total = int(list_->entries().size());
  count_->setText(total == 0 ? QString()
                             : shown.size() == total ? QStringLiteral("%1").arg(total)
                                                     : QStringLiteral("%1 of %2").arg(shown.size()).arg(total));
  alerts_->setText(HotList::alertsName(list_->alerts()).toUpper());
  alerts_->setStyleSheet(list_->alerts() == HotList::Off
                             ? QStringLiteral("QToolButton { color: #8796a3; font-size: 10px; border: 1px solid #4a5560; border-radius: 3px; padding: 0 5px; }")
                             : QStringLiteral("QToolButton { color: #7ee2a0; font-size: 10px; font-weight: 700; border: 1px solid #3fae6a; border-radius: 3px; padding: 0 5px; }"));
  constexpr int columns = 4;
  int n = 0;
  for (const HotEntry& e : shown) {
    if (n >= kMaxChips - (shown.size() > kMaxChips ? 1 : 0))
      break;
    auto* chip = new QToolButton(chips_);
    chip->setObjectName(QStringLiteral("hotListChip"));
    chip->setText(e.call);
    chip->setFocusPolicy(Qt::NoFocus);
    chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QString style;
    if (e.recent())
      style = QStringLiteral("background: #c0392b; color: white; font-weight: 700; border: 1px solid #ff6b5b;");
    else if (e.heard.isValid())
      style = QStringLiteral("background: #3b2a12; color: #ffc46b; border: 1px solid #b07a2a;");
    else
      style = QStringLiteral("background: #141a20; color: #7d8a95; border: 1px solid #2a333c;");
    chip->setStyleSheet(QStringLiteral("QToolButton { %1 border-radius: 3px; font-size: 11px; padding: 1px 4px; }")
                            .arg(style));
    chip->setToolTip(QStringLiteral("%1, %2. Click to offer the call to the QSO bar. Double-click to tune "
                                    "to where it was heard, or to its DXpedition's band and mode. This "
                                    "does not key.")
                         .arg(e.call, heardText(e)));
    chip->setProperty("hotCall", e.call);
    chip->installEventFilter(this);
    const QString call = e.call;
    connect(chip, &QToolButton::clicked, this, [this, call] { emit callPicked(call); });
    grid_->addWidget(chip, n / columns, n % columns);
    ++n;
  }
  if (shown.size() > n) {
    auto* more = new QToolButton(chips_);
    more->setObjectName(QStringLiteral("hotListMore"));
    more->setText(QStringLiteral("+%1").arg(shown.size() - n));
    more->setToolTip(QStringLiteral("More calls. Click to open the hot list."));
    more->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(more, &QToolButton::clicked, this, [this] { emit manageRequested(); });
    grid_->addWidget(more, n / columns, n % columns);
  }
  empty_->setVisible(shown.isEmpty());
  if (shown.isEmpty() && total > 0)
    empty_->setText(list_->show() == HotList::ShowHeard
                        ? QStringLiteral("No watched call heard yet.")
                        : QStringLiteral("No call is ticked to show here."));
  else if (total == 0)
    empty_->setText(QStringLiteral("Tick a call in View → DXpeditions, or right-click here to add one."));
}

bool HotListBar::eventFilter(QObject* watched, QEvent* event) {
  // The second press of a double-click: tune, and do not click again.
  if (event->type() == QEvent::MouseButtonDblClick) {
    const QString call = watched->property("hotCall").toString();
    if (!call.isEmpty()) {
      emit tuneRequested(call);
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void HotListBar::contextMenuEvent(QContextMenuEvent* event) {
  QMenu menu(this);
  menu.addAction(QStringLiteral("Manage hot list…"), this, [this] { emit manageRequested(); });
  QMenu* alerts = menu.addMenu(QStringLiteral("Alerts"));
  for (HotList::Alerts a : {HotList::Off, HotList::Visual, HotList::Voice}) {
    QAction* act = alerts->addAction(HotList::alertsName(a), this, [this, a] { list_->setAlerts(a); });
    act->setCheckable(true);
    act->setChecked(list_->alerts() == a);
  }
  QMenu* show = menu.addMenu(QStringLiteral("Show"));
  const struct {
    HotList::Show value;
    const char* label;
  } shows[] = {{HotList::ShowAll, "All calls"},
               {HotList::ShowTicked, "Ticked calls"},
               {HotList::ShowHeard, "Heard calls"}};
  for (const auto& s : shows) {
    const HotList::Show value = s.value;
    QAction* act = show->addAction(QString::fromLatin1(s.label), this, [this, value] { list_->setShow(value); });
    act->setCheckable(true);
    act->setChecked(list_->show() == value);
  }
  menu.addAction(QStringLiteral("Clear heard marks"), this, [this] { list_->clearHeard(); });
  menu.addSeparator();
  menu.addAction(QStringLiteral("Hide from the top bar"), this, [this] { list_->setOnBar(false); });
  menu.exec(event->globalPos());
}

HotListDialog::HotListDialog(HotList* list, QWidget* parent) : QDialog(parent), list_(list) {
  setObjectName(QStringLiteral("hotListDialog"));
  setWindowTitle(QStringLiteral("Hot list"));
  resize(460, 460);
  auto* col = new QVBoxLayout(this);

  auto* note = new QLabel(QStringLiteral(
      "Calls to watch for. When STD decodes one (FT8 family, JS8, WSPR, or received keyboard text), it "
      "turns red on the top bar. Ticked calls are the ones Show: Ticked calls puts on the top bar. "
      "Nothing here keys, and START is never sent."), this);
  note->setWordWrap(true);
  col->addWidget(note);

  auto* form = new QFormLayout;
  auto* alerts = new QComboBox(this);
  alerts->setObjectName(QStringLiteral("hotListAlertsBox"));
  for (HotList::Alerts a : {HotList::Off, HotList::Visual, HotList::Voice})
    alerts->addItem(HotList::alertsName(a), int(a));
  alerts->setToolTip(QStringLiteral(
      "Visual + Voice uses the voice and sound device in Options → Speech, once per call every 15 minutes."));
  form->addRow(QStringLiteral("Alerts"), alerts);
  auto* show = new QComboBox(this);
  show->setObjectName(QStringLiteral("hotListShowBox"));
  show->addItem(QStringLiteral("All calls"), int(HotList::ShowAll));
  show->addItem(QStringLiteral("Ticked calls"), int(HotList::ShowTicked));
  show->addItem(QStringLiteral("Heard calls"), int(HotList::ShowHeard));
  form->addRow(QStringLiteral("Top bar shows"), show);
  auto* onBar = new QCheckBox(QStringLiteral("Show the hot list on the top bar"), this);
  onBar->setObjectName(QStringLiteral("hotListOnBar"));
  form->addRow(QString(), onBar);
  col->addLayout(form);

  calls_ = new QListWidget(this);
  calls_->setObjectName(QStringLiteral("hotListCalls"));
  calls_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  col->addWidget(calls_, 1);

  auto* addRow = new QHBoxLayout;
  auto* entry = new QLineEdit(this);
  entry->setObjectName(QStringLiteral("hotListEntry"));
  entry->setPlaceholderText(QStringLiteral("Calls, separated by spaces or commas"));
  auto* add = new QPushButton(QStringLiteral("Add"), this);
  auto* remove = new QPushButton(QStringLiteral("Remove"), this);
  auto* clear = new QPushButton(QStringLiteral("Clear all"), this);
  addRow->addWidget(entry, 1);
  addRow->addWidget(add);
  addRow->addWidget(remove);
  addRow->addWidget(clear);
  col->addLayout(addRow);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  col->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto sync = [this, alerts, show, onBar] {
    const QSignalBlocker a(alerts), b(show), c(onBar);
    alerts->setCurrentIndex(alerts->findData(int(list_->alerts())));
    show->setCurrentIndex(show->findData(int(list_->show())));
    onBar->setChecked(list_->onBar());
    reload();
  };
  connect(list_, &HotList::changed, this, sync);
  connect(alerts, &QComboBox::currentIndexChanged, this,
          [this, alerts](int) { list_->setAlerts(HotList::Alerts(alerts->currentData().toInt())); });
  connect(show, &QComboBox::currentIndexChanged, this,
          [this, show](int) { list_->setShow(HotList::Show(show->currentData().toInt())); });
  connect(onBar, &QCheckBox::toggled, this, [this](bool on) { list_->setOnBar(on); });
  auto doAdd = [this, entry] {
    list_->add(entry->text());
    entry->clear();
  };
  connect(add, &QPushButton::clicked, this, doAdd);
  connect(entry, &QLineEdit::returnPressed, this, doAdd);
  connect(remove, &QPushButton::clicked, this, [this] {
    QStringList gone;
    for (QListWidgetItem* item : calls_->selectedItems())
      gone << item->data(Qt::UserRole).toString();
    for (const QString& call : gone)
      list_->remove(call);
  });
  connect(clear, &QPushButton::clicked, this, [this] {
    if (list_->entries().isEmpty())
      return;
    if (QMessageBox::question(this, QStringLiteral("Hot list"),
                              QStringLiteral("Clear all %1 calls from the hot list?").arg(list_->entries().size())) ==
        QMessageBox::Yes)
      list_->clear();
  });
  connect(calls_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    if (!loading_)
      list_->setShown(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
  });
  sync();
}

void HotListDialog::reload() {
  loading_ = true;
  calls_->clear();
  for (const HotEntry& e : list_->entries()) {
    auto* item = new QListWidgetItem(QStringLiteral("%1    %2").arg(e.call, heardText(e)), calls_);
    item->setData(Qt::UserRole, e.call);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(e.shown ? Qt::Checked : Qt::Unchecked);
    item->setToolTip(QStringLiteral("Ticked: shown on the top bar when it shows Ticked calls."));
  }
  loading_ = false;
}
