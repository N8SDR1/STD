// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDate>
#include <QModelIndex>
#include <QPoint>
#include <QWidget>

#include "DxpedFeed.h"
#include "SpotWatch.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;
class QTimer;
class DxpedFilter;
class HotList;

// View → DXpeditions: the announced DX operations from NG3K, filtered to
// the ones that run digital modes by default. With Mark spotted DXpeditions
// on, digital cluster spots of the ones on the air (from Spothole) mark their
// rows. A double-click moves STD to the band and mode; nothing here keys or
// sends START.
class DxpedPanel : public QWidget {
  Q_OBJECT

public:
  explicit DxpedPanel(QWidget* parent = nullptr);

  DxpedFeed* feed() const { return feed_; }
  // Cluster spots of the digital DXpeditions on the air (Spothole).
  SpotWatch* spots() const { return spots_; }
  // The latest cluster spot of a DXpedition, or nullptr.
  const DxSpot* spotFor(const DxpedEntry& e) const;
  // The call column's tick puts the call on this hot list.
  void setHotList(HotList* list);
  QTableView* table() const { return table_; }
  // Set "today" for the NOW column; the self-test pins it.
  void setToday(const QDate& day);
  int shownRows() const;

  // Filter names stored in dxped/filter.
  enum Filter { AllModes = 0, Digital, FtModes, Rtty, Psk };

signals:
  void callPicked(const QString& call);
  // Double-click: move STD to this DXpedition's band and digital mode.
  void tuneRequested(const DxpedEntry& entry);
  void hotListRequested();
  void statusMessage(const QString& text);
  // A watched DXpedition was spotted on a digital mode.
  void spotted(const QString& call, const DxSpot& spot);

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  void rebuild();
  void updateWatch();
  void showStatus();
  void pickRow(const QModelIndex& index);
  void contextMenu(const QPoint& pos);
  const DxpedEntry* entryAt(const QModelIndex& proxyIndex) const;

  DxpedFeed* feed_ = nullptr;
  SpotWatch* spots_ = nullptr;
  QCheckBox* spotsOn_ = nullptr;
  QComboBox* spotEvery_ = nullptr;
  QComboBox* spotKeep_ = nullptr;
  HotList* hot_ = nullptr;
  bool building_ = false;
  QStandardItemModel* model_ = nullptr;
  DxpedFilter* proxy_ = nullptr;
  QTableView* table_ = nullptr;
  QComboBox* modes_ = nullptr;
  QCheckBox* activeOnly_ = nullptr;
  QLineEdit* search_ = nullptr;
  QPushButton* refresh_ = nullptr;
  QLabel* status_ = nullptr;
  QTimer* poll_ = nullptr;
  QTimer* fade_ = nullptr;
  QDate today_;
  bool pinnedToday_ = false;
};
