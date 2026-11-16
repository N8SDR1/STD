// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QWidget>

class HotList;
class QGridLayout;
class QLabel;
class QListWidget;
class QToolButton;

// The hot list on the top bar, right of the dial: the watched calls as
// chips, red when heard in the last 15 minutes. The alerts chip cycles
// Off, Visual, Visual + Voice. A click on a call offers it to the QSO bar.
class HotListBar : public QWidget {
  Q_OBJECT

public:
  explicit HotListBar(HotList* list, QWidget* parent = nullptr);
  void refresh();
  int chipCount() const;

  static constexpr int kMaxChips = 12;

signals:
  void callPicked(const QString& call);
  void manageRequested();
  // Double-click a call: tune to where it was heard, or its DXpedition.
  void tuneRequested(const QString& call);

protected:
  void contextMenuEvent(QContextMenuEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  HotList* list_ = nullptr;
  QLabel* count_ = nullptr;
  QToolButton* alerts_ = nullptr;
  QWidget* chips_ = nullptr;
  QGridLayout* grid_ = nullptr;
  QLabel* empty_ = nullptr;
};

// View → Hot list: the calls, which ones the top bar shows, alerts, and
// add, remove, and clear.
class HotListDialog : public QDialog {
  Q_OBJECT

public:
  explicit HotListDialog(HotList* list, QWidget* parent = nullptr);

private:
  void reload();

  HotList* list_ = nullptr;
  QListWidget* calls_ = nullptr;
  bool loading_ = false;
};
