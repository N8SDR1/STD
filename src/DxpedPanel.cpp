// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DxpedPanel.h"

#include "HotList.h"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum Column { ColStatus = 0, ColCall, ColEntity, ColStart, ColEnd, ColBands, ColModes, ColQsl, ColInfo, ColCount };

// Roles on the Status item of each row; the filter reads them.
constexpr int kEntryRole = Qt::UserRole + 1;    // index into feed entries
constexpr int kDigitalRole = Qt::UserRole + 2;  // bool
constexpr int kModesRole = Qt::UserRole + 3;    // "CW SSB FT8"
constexpr int kActiveRole = Qt::UserRole + 4;   // bool
constexpr int kTextRole = Qt::UserRole + 5;     // call, entity, info for the search box
constexpr int kCallRole = Qt::UserRole + 6;     // on the Call item: the call the hot list holds
constexpr int kHotRole = Qt::UserRole + 7;      // on the Status item: 0 not hot, 1 hot, 2 heard, 3 heard lately

QString shortDate(const QDate& d) {
  return d.isValid() ? d.toString(QStringLiteral("yyyy-MM-dd")) : QString();
}

}  // namespace

class DxpedFilter : public QSortFilterProxyModel {
public:
  using QSortFilterProxyModel::QSortFilterProxyModel;

  void set(int mode, bool activeOnly, const QString& text) {
    beginFilterChange();
    mode_ = mode;
    activeOnly_ = activeOnly;
    text_ = text.trimmed();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
  }

protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
    const QModelIndex i = sourceModel()->index(row, ColStatus, parent);
    if (activeOnly_ && !i.data(kActiveRole).toBool())
      return false;
    const QStringList modes = i.data(kModesRole).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool any = modes.contains(QLatin1String("All")) || modes.contains(QLatin1String("Digital"));
    switch (mode_) {
      case DxpedPanel::Digital:
        if (!i.data(kDigitalRole).toBool())
          return false;
        break;
      case DxpedPanel::FtModes:
        if (!any && !modes.contains(QLatin1String("FT8")) && !modes.contains(QLatin1String("FT4")) &&
            !modes.contains(QLatin1String("FT2")))
          return false;
        break;
      case DxpedPanel::Rtty:
        if (!any && !modes.contains(QLatin1String("RTTY")))
          return false;
        break;
      case DxpedPanel::Psk:
        if (!any && std::none_of(modes.begin(), modes.end(),
                                 [](const QString& m) { return m.startsWith(QLatin1String("PSK")); }))
          return false;
        break;
      default:
        break;
    }
    if (!text_.isEmpty() && !i.data(kTextRole).toString().contains(text_, Qt::CaseInsensitive))
      return false;
    return true;
  }

private:
  int mode_ = DxpedPanel::Digital;
  bool activeOnly_ = false;
  QString text_;
};

DxpedPanel::DxpedPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("dxpedPanel"));
  feed_ = new DxpedFeed(this);
  spots_ = new SpotWatch(this);
  model_ = new QStandardItemModel(0, ColCount, this);
  model_->setHorizontalHeaderLabels({QStringLiteral("Status"), QStringLiteral("Call"),
                                     QStringLiteral("Entity"), QStringLiteral("Start"),
                                     QStringLiteral("End"), QStringLiteral("Bands"),
                                     QStringLiteral("Modes"), QStringLiteral("QSL"),
                                     QStringLiteral("Info")});
  proxy_ = new DxpedFilter(this);
  proxy_->setSourceModel(model_);
  proxy_->setSortRole(Qt::UserRole);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(4);

  auto* row = new QHBoxLayout;
  row->setSpacing(6);
  modes_ = new QComboBox(this);
  modes_->setObjectName(QStringLiteral("dxpedModes"));
  modes_->addItems({QStringLiteral("All modes"), QStringLiteral("Digital modes"),
                    QStringLiteral("FT8 / FT4 / FT2"), QStringLiteral("RTTY"), QStringLiteral("PSK")});
  modes_->setToolTip(QStringLiteral(
      "Show DXpeditions by the modes in their announcement. Digital modes takes FT8, FT4, RTTY, PSK, "
      "and the ones that say \"+ digital\" or \"all modes\". An announcement that names no mode is "
      "shown only under All modes."));
  modes_->setCurrentIndex(std::clamp(QSettings().value(QStringLiteral("dxped/filter"), int(Digital)).toInt(),
                                     0, modes_->count() - 1));
  row->addWidget(modes_);
  activeOnly_ = new QCheckBox(QStringLiteral("On the air now"), this);
  activeOnly_->setObjectName(QStringLiteral("dxpedActive"));
  activeOnly_->setToolTip(QStringLiteral("Show only the operations whose dates include today (UTC)."));
  activeOnly_->setChecked(QSettings().value(QStringLiteral("dxped/activeOnly"), false).toBool());
  row->addWidget(activeOnly_);
  search_ = new QLineEdit(this);
  search_->setObjectName(QStringLiteral("dxpedSearch"));
  search_->setPlaceholderText(QStringLiteral("Find call, entity, or text"));
  search_->setClearButtonEnabled(true);
  row->addWidget(search_, 1);
  refresh_ = new QPushButton(QStringLiteral("Refresh"), this);
  refresh_->setObjectName(QStringLiteral("dxpedRefresh"));
  refresh_->setToolTip(QStringLiteral(
      "Fetch the list from NG3K now. It is also fetched when this panel opens and once an hour "
      "while it stays open. This does not key."));
  row->addWidget(refresh_);
  auto* hotButton = new QPushButton(QStringLiteral("Hot list…"), this);
  hotButton->setObjectName(QStringLiteral("dxpedHotList"));
  hotButton->setToolTip(QStringLiteral(
      "The calls you are watching for. Tick a call in this table to add it. This does not key."));
  row->addWidget(hotButton);
  connect(hotButton, &QPushButton::clicked, this, [this] { emit hotListRequested(); });
  auto* site = new QPushButton(QStringLiteral("NG3K"), this);
  site->setObjectName(QStringLiteral("dxpedSite"));
  site->setToolTip(QStringLiteral("Open NG3K's Announced DX Operations page in the browser."));
  row->addWidget(site);
  layout->addLayout(row);

  // Cluster spots: off until ticked. Only digital spots of the digital
  // DXpeditions on the air are asked for.
  auto* spotRow = new QHBoxLayout;
  spotRow->setSpacing(6);
  spotsOn_ = new QCheckBox(QStringLiteral("Mark spotted DXpeditions"), this);
  spotsOn_->setObjectName(QStringLiteral("dxpedSpots"));
  spotsOn_->setToolTip(QStringLiteral(
      "Ask Spothole (spothole.app, which gathers the DX clusters and the RBN) for digital spots of the "
      "digital DXpeditions on the air: FT8, FT4, RTTY, PSK and the other data modes. A spotted "
      "DXpedition reads SPOTTED with its band and mode, and a hot list call is marked. CW and phone "
      "spots, and stations that are not DXpeditions, are never shown. No callsign or login is sent. "
      "This does not key."));
  spotsOn_->setChecked(spots_->enabled());
  spotRow->addWidget(spotsOn_);
  spotRow->addWidget(new QLabel(QStringLiteral("Check every"), this));
  spotEvery_ = new QComboBox(this);
  spotEvery_->setObjectName(QStringLiteral("dxpedSpotEvery"));
  spotEvery_->addItem(QStringLiteral("90 s"), 90);
  spotEvery_->addItem(QStringLiteral("180 s"), 180);
  spotEvery_->setCurrentIndex(spots_->pollSecs() == 180 ? 1 : 0);
  spotEvery_->setToolTip(QStringLiteral(
      "How often STD asks Spothole for new spots, in one query for all the DXpeditions watched. "
      "Spothole is run by a volunteer, so STD asks no more often than this."));
  spotRow->addWidget(spotEvery_);
  spotRow->addWidget(new QLabel(QStringLiteral("Keep spots"), this));
  spotKeep_ = new QComboBox(this);
  spotKeep_->setObjectName(QStringLiteral("dxpedSpotKeep"));
  spotKeep_->addItem(QStringLiteral("10 min"), 10);
  spotKeep_->addItem(QStringLiteral("20 min"), 20);
  spotKeep_->addItem(QStringLiteral("30 min"), 30);
  spotKeep_->setCurrentIndex(spots_->keepMins() / 10 - 1);
  spotKeep_->setToolTip(QStringLiteral("How long a DXpedition stays marked SPOTTED after its last spot."));
  spotRow->addWidget(spotKeep_);
  spotRow->addStretch(1);
  layout->addLayout(spotRow);

  table_ = new QTableView(this);
  table_->setObjectName(QStringLiteral("dxpedTable"));
  table_->setModel(proxy_);
  table_->setSortingEnabled(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setAlternatingRowColors(true);
  table_->setWordWrap(false);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->setToolTip(QStringLiteral(
      "Click a title to sort. Double-click a row to move STD to its band and digital mode. Tick a call "
      "to put it on the hot list. Right-click to put the call in the QSO bar or open the info page. "
      "Nothing here keys or sends START."));
  table_->sortByColumn(ColStatus, Qt::AscendingOrder);
  layout->addWidget(table_, 1);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("dxpedStatus"));
  status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(status_);

  poll_ = new QTimer(this);
  poll_->setInterval(60 * 60 * 1000);
  connect(poll_, &QTimer::timeout, this, [this] { feed_->refresh(false); });
  // Red fades to amber 15 minutes after a hot call is heard.
  fade_ = new QTimer(this);
  fade_->setInterval(60 * 1000);
  connect(fade_, &QTimer::timeout, this, [this] {
    spots_->prune();  // a spot older than the keep time unmarks its row
    if (spots_->spottedCount() > 0 ||
        (hot_ && std::any_of(hot_->entries().begin(), hot_->entries().end(),
                             [](const HotEntry& h) { return h.heard.isValid(); })))
      rebuild();
  });

  auto applyFilter = [this] {
    proxy_->set(modes_->currentIndex(), activeOnly_->isChecked(), search_->text());
    showStatus();
  };
  connect(modes_, &QComboBox::currentIndexChanged, this, [this, applyFilter](int index) {
    QSettings().setValue(QStringLiteral("dxped/filter"), index);
    applyFilter();
  });
  connect(activeOnly_, &QCheckBox::toggled, this, [applyFilter](bool on) {
    QSettings().setValue(QStringLiteral("dxped/activeOnly"), on);
    applyFilter();
  });
  connect(search_, &QLineEdit::textChanged, this, [applyFilter](const QString&) { applyFilter(); });
  connect(refresh_, &QPushButton::clicked, this, [this] { feed_->refresh(true); });
  connect(site, &QPushButton::clicked, this,
          [] { QDesktopServices::openUrl(QUrl(DxpedFeed::listUrl())); });
  connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) { pickRow(index); });
  connect(model_, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item) {
    if (building_ || !hot_ || item->column() != ColCall)
      return;
    const QString call = item->data(kCallRole).toString();
    const bool want = item->checkState() == Qt::Checked;
    if (want == hot_->contains(call))
      return;
    // The list change rebuilds this table; do it after the model has
    // finished with the item that was clicked.
    QTimer::singleShot(0, this, [this, call, want] {
      if (!hot_ || want == hot_->contains(call))
        return;
      if (want)
        hot_->add(call);
      else
        hot_->remove(call);
      emit statusMessage(want ? QStringLiteral("%1 is on the hot list. Nothing was keyed.").arg(call)
                              : QStringLiteral("%1 is off the hot list.").arg(call));
    });
  });
  connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { contextMenu(pos); });
  connect(feed_, &DxpedFeed::updated, this, [this] { rebuild(); });
  connect(spots_, &SpotWatch::changed, this, [this] {
    if (!building_)
      rebuild();
  });
  connect(spots_, &SpotWatch::spotted, this,
          [this](const QString& call, const DxSpot& spot) { emit spotted(call, spot); });
  connect(spotsOn_, &QCheckBox::toggled, this, [this](bool on) {
    spots_->setEnabled(on);
    emit statusMessage(on ? QStringLiteral("Marking spotted DXpeditions from Spothole, digital spots only. "
                                           "Nothing was keyed.")
                          : QStringLiteral("Cluster spots are off."));
  });
  connect(spotEvery_, &QComboBox::currentIndexChanged, this,
          [this](int) { spots_->setPollSecs(spotEvery_->currentData().toInt()); });
  connect(spotKeep_, &QComboBox::currentIndexChanged, this,
          [this](int) { spots_->setKeepMins(spotKeep_->currentData().toInt()); });

  proxy_->set(modes_->currentIndex(), activeOnly_->isChecked(), QString());
  feed_->loadCache();
  rebuild();
}

void DxpedPanel::setHotList(HotList* list) {
  hot_ = list;
  if (hot_)
    connect(hot_, &HotList::changed, this, [this] { rebuild(); });
  rebuild();
}

void DxpedPanel::setToday(const QDate& day) {
  today_ = day;
  pinnedToday_ = day.isValid();
  rebuild();
}

const DxSpot* DxpedPanel::spotFor(const DxpedEntry& e) const {
  return spots_->latest(e.lookup);
}

void DxpedPanel::updateWatch() {
  // The digital DXpeditions on the air, with a day either side for the
  // dates the announcement gives. Their full calls (3B9KW for "3B9").
  QStringList calls;
  for (const DxpedEntry& e : feed_->entries()) {
    if (!e.digital || !e.start.isValid() || !e.end.isValid())
      continue;
    if (e.start.addDays(-1) <= today_ && today_ <= e.end.addDays(1))
      calls << e.lookup;
  }
  spots_->setWatch(calls);
}

int DxpedPanel::shownRows() const {
  return proxy_->rowCount();
}

void DxpedPanel::rebuild() {
  if (!pinnedToday_)
    today_ = QDateTime::currentDateTimeUtc().date();
  const QList<DxpedEntry>& list = feed_->entries();
  const bool widthsSet = model_->rowCount() > 0;
  updateWatch();
  const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
  building_ = true;
  model_->removeRows(0, model_->rowCount());
  const QBrush activeBack(QColor(46, 160, 67, 70));
  const QBrush spottedBack(QColor(40, 200, 90, 150));
  for (int n = 0; n < list.size(); ++n) {
    const DxpedEntry& e = list.at(n);
    const DxSpot* spot = spotFor(e);
    const bool active = e.activeOn(today_) || spot;
    const bool ended = e.end.isValid() && e.end < today_;
    QString status;
    qint64 statusKey = 0;
    if (spot) {
      const qint64 age = std::max<qint64>(0, spot->time.secsTo(nowUtc) / 60);
      const QString band = spot->metres > 0 ? QStringLiteral("%1 m ").arg(spot->metres) : QString();
      status = QStringLiteral("SPOTTED %1%2%3 · %4 min")
                   .arg(band, spot->mode, spot->modeGuessed ? QStringLiteral("?") : QString())
                   .arg(age);
      statusKey = -3000000 + age;
    } else if (active) {
      const qint64 left = today_.daysTo(e.end);
      status = left == 0 ? QStringLiteral("NOW, last day") : QStringLiteral("NOW, %1 d left").arg(left);
      statusKey = -1000000 + e.end.toJulianDay();
    } else if (ended) {
      status = QStringLiteral("ended");
      statusKey = 2000000000 + e.end.toJulianDay();
    } else if (e.start.isValid()) {
      const qint64 in = today_.daysTo(e.start);
      status = in == 1 ? QStringLiteral("tomorrow") : QStringLiteral("in %1 d").arg(in);
      statusKey = e.start.toJulianDay();
    } else {
      status = QStringLiteral("?");
      statusKey = 1000000000;
    }
    const QString modes = e.modes.join(QLatin1Char(' '));
    QList<QStandardItem*> items;
    auto add = [&](const QString& text, const QVariant& key) {
      auto* item = new QStandardItem(text);
      item->setData(key, Qt::UserRole);
      if (spot)
        item->setBackground(spottedBack);
      else if (active)
        item->setBackground(activeBack);
      items << item;
    };
    add(status, statusKey);
    add(e.call, e.call.toLower());
    add(e.entity, e.entity.toLower());
    add(shortDate(e.start), e.start.isValid() ? e.start.toJulianDay() : 0);
    add(shortDate(e.end), e.end.isValid() ? e.end.toJulianDay() : 0);
    add(e.bands, -e.bandKey);
    add(modes.isEmpty() ? QStringLiteral("—") : modes, modes.toLower());
    add(e.qsl, e.qsl.toLower());
    add(e.info, e.info.toLower());
    QStandardItem* first = items.first();
    first->setData(n, kEntryRole);
    first->setData(e.digital, kDigitalRole);
    first->setData(modes, kModesRole);
    first->setData(active, kActiveRole);
    first->setData(QStringList{e.call, e.lookup, e.entity, e.info, e.qsl}.join(QLatin1Char(' ')), kTextRole);
    QStandardItem* callItem = items.at(ColCall);
    callItem->setData(e.lookup, kCallRole);
    if (hot_) {
      callItem->setCheckable(true);
      callItem->setCheckState(hot_->contains(e.lookup) ? Qt::Checked : Qt::Unchecked);
    }
    if (active) {
      QFont bold = first->font();
      bold.setBold(true);
      first->setFont(bold);
      items.at(ColCall)->setFont(bold);
    }
    // Hot list: tinted orange; heard since STD started, amber; heard in the
    // last 15 minutes, red. The same colours as the chips on the top bar.
    int hotLevel = 0;
    HotEntry hot;
    if (hot_) {
      hot = hot_->entry(e.lookup);
      if (hot.call.isEmpty())
        hot = hot_->entry(e.call);
      if (!hot.call.isEmpty())
        hotLevel = hot.recent() ? 3 : hot.heard.isValid() ? 2 : 1;
    }
    first->setData(hotLevel, kHotRole);
    if (hotLevel > 0) {
      const QBrush back(hotLevel == 3   ? QColor(192, 57, 43, 210)
                        : hotLevel == 2 ? QColor(176, 122, 42, 170)
                                        : QColor(255, 138, 61, 38));
      const QBrush text(hotLevel == 3   ? QColor(Qt::white)
                        : hotLevel == 2 ? QColor(255, 211, 138)
                                        : QColor(255, 160, 100));
      for (QStandardItem* item : items) {
        item->setBackground(back);
        if (hotLevel >= 2)
          item->setForeground(text);
      }
      for (int c : {int(ColStatus), int(ColCall)}) {
        items.at(c)->setForeground(text);
        QFont bold = items.at(c)->font();
        bold.setBold(hotLevel >= 2 || active);
        items.at(c)->setFont(bold);
      }
    }
    QString tip = QStringLiteral("%1 — %2\n%3").arg(e.call, e.entity, e.dates);
    if (spot)
      tip += QStringLiteral("\nSpotted %1Z on %2 MHz %3%4 by %5 (%6)%7")
                 .arg(spot->time.toUTC().toString(QStringLiteral("HH:mm")))
                 .arg(double(spot->freqHz) / 1.0e6, 0, 'f', 4)
                 .arg(spot->mode, spot->modeGuessed ? QStringLiteral(" (mode from the band plan)") : QString(),
                      spot->spotter.isEmpty() ? QStringLiteral("?") : spot->spotter, spot->source,
                      spot->comment.isEmpty() ? QString() : QStringLiteral(": ") + spot->comment);
    if (hotLevel == 1)
      tip += QStringLiteral("\nOn the hot list, not heard since STD started");
    else if (hotLevel >= 2)
      tip += QStringLiteral("\nOn the hot list, heard %1Z%2")
                 .arg(hot.heard.toUTC().toString(QStringLiteral("HH:mm")),
                      hot.where.isEmpty() ? QString() : QStringLiteral(", ") + hot.where);
    if (e.lookup != e.call)
      tip += QStringLiteral("\nWorked as %1").arg(e.lookup);
    if (!e.qsl.isEmpty())
      tip += QStringLiteral("\nQSL: %1").arg(e.qsl);
    if (!e.info.isEmpty())
      tip += QStringLiteral("\n%1").arg(e.info);
    if (!e.source.isEmpty())
      tip += QStringLiteral("\nSource: %1").arg(e.source);
    for (QStandardItem* item : items)
      item->setToolTip(tip);
    model_->appendRow(items);
  }
  building_ = false;
  if (!widthsSet && model_->rowCount() > 0) {
    table_->resizeColumnsToContents();
    if (table_->columnWidth(ColEntity) > 180)
      table_->setColumnWidth(ColEntity, 180);
    if (table_->columnWidth(ColQsl) > 140)
      table_->setColumnWidth(ColQsl, 140);
    if (table_->columnWidth(ColBands) > 140)
      table_->setColumnWidth(ColBands, 140);
  }
  showStatus();
}

void DxpedPanel::showStatus() {
  const int total = model_->rowCount();
  int active = 0;
  for (int r = 0; r < total; ++r) {
    if (model_->index(r, ColStatus).data(kActiveRole).toBool())
      ++active;
  }
  QString text;
  if (total == 0) {
    text = feed_->busy() ? QStringLiteral("Fetching the NG3K list…")
                         : QStringLiteral("No list yet. It is fetched from NG3K when this panel opens.");
  } else {
    text = QStringLiteral("%1 of %2 shown, %3 on the air now.").arg(shownRows()).arg(total).arg(active);
    if (feed_->fetchedAt().isValid())
      text += QStringLiteral(" List from %1Z.").arg(
          feed_->fetchedAt().toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    if (feed_->busy())
      text += QStringLiteral(" Fetching…");
  }
  if (!feed_->lastError().isEmpty())
    text += QStringLiteral(" %1 Showing the saved list.").arg(feed_->lastError());
  text += QStringLiteral(" Source: NG3K ADXO.");
  if (spots_->enabled()) {
    text += QStringLiteral(" Spots: %1 of %2 watched spotted")
                .arg(spots_->spottedCount())
                .arg(spots_->watch().size());
    if (spots_->checkedAt().isValid())
      text += QStringLiteral(", checked %1Z").arg(spots_->checkedAt().toUTC().toString(QStringLiteral("HH:mm")));
    text += QStringLiteral(" (Spothole).");
    if (!spots_->lastError().isEmpty())
      text += QStringLiteral(" ") + spots_->lastError();
  }
  status_->setText(text);
  refresh_->setEnabled(!feed_->busy());
}

const DxpedEntry* DxpedPanel::entryAt(const QModelIndex& proxyIndex) const {
  if (!proxyIndex.isValid())
    return nullptr;
  const QModelIndex source = proxy_->mapToSource(proxyIndex);
  const int n = model_->index(source.row(), ColStatus).data(kEntryRole).toInt();
  const QList<DxpedEntry>& list = feed_->entries();
  return n >= 0 && n < list.size() ? &list.at(n) : nullptr;
}

void DxpedPanel::pickRow(const QModelIndex& index) {
  if (const DxpedEntry* e = entryAt(index))
    emit tuneRequested(*e);
}

void DxpedPanel::contextMenu(const QPoint& pos) {
  const QModelIndex index = table_->indexAt(pos);
  const DxpedEntry* e = entryAt(index);
  QMenu menu(this);
  if (e) {
    const QString call = e->lookup;
    const DxpedEntry entry = *e;
    menu.addAction(QStringLiteral("Tune STD to %1 (band and mode)").arg(call), this,
                   [this, entry] { emit tuneRequested(entry); });
    if (hot_) {
      const bool on = hot_->contains(call);
      menu.addAction(on ? QStringLiteral("Take %1 off the hot list").arg(call)
                        : QStringLiteral("Put %1 on the hot list").arg(call),
                     this, [this, call] { hot_->toggle(call); });
    }
    menu.addAction(QStringLiteral("Put %1 in the QSO call").arg(call), this,
                   [this, call] { emit callPicked(call); });
    menu.addAction(QStringLiteral("Copy %1").arg(call), this, [this, call] {
      QApplication::clipboard()->setText(call);
      emit statusMessage(QStringLiteral("Copied %1. Nothing was keyed.").arg(call));
    });
    const QString link = e->link.isEmpty() ? DxpedFeed::listUrl() : e->link;
    menu.addAction(QStringLiteral("Open info page"), this,
                   [link] { QDesktopServices::openUrl(QUrl(link)); });
    menu.addSeparator();
  }
  menu.addAction(QStringLiteral("Open the NG3K list"), this,
                 [] { QDesktopServices::openUrl(QUrl(DxpedFeed::listUrl())); });
  menu.exec(table_->viewport()->mapToGlobal(pos));
}

void DxpedPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  feed_->refresh(false);
  poll_->start();
  fade_->start();
  rebuild();  // NOW and "in N d" follow the date
}

void DxpedPanel::hideEvent(QHideEvent* event) {
  QWidget::hideEvent(event);
  poll_->stop();
  fade_->stop();
}
