// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QIcon>
#include <QMainWindow>
#include <QString>

class Ft8Decode;
class ModeProfile;
class QDockWidget;
class QShowEvent;
class QToolBar;
class StatusStrip;
class TciClient;
class WaterfallWidget;

// Square window icon cropped from resources/std-icon.jpg, at the sizes a
// window manager asks for. A single large file often never reaches _NET_WM_ICON.
QIcon stdIcon();

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(QWidget* parent = nullptr);

protected:
  void showEvent(QShowEvent* event) override;

private:
  void buildBrandBar();
  void buildDocks();
  void buildMenus();
  void arrangeDefault();
  void resizeToHero();
  void showAbout();
  void showGuide();
  void tuneActiveBand();
  void openRadioDialog();
  void setReceivePlaceholder(bool connected);
  void showRadioDown(const QString& error);
  void showRadioUp(const QString& host, quint16 port);
  void showDial(qint64 hz);

  QWidget* makeTextPanel(const QString& editName, const QString& placeholder,
                         bool readOnly, const QString& note);
  QWidget* makeMacroRow();

  QToolBar* brandBar_ = nullptr;
  WaterfallWidget* waterfall_ = nullptr;
  ModeProfile* profile_ = nullptr;
  StatusStrip* statusStrip_ = nullptr;
  TciClient* tci_ = nullptr;
  Ft8Decode* ft8_ = nullptr;
  QString dialText_ = QString::fromUtf8("—");
  QDockWidget* waterfallDock_ = nullptr;
  QDockWidget* receiveDock_ = nullptr;
  QDockWidget* transmitDock_ = nullptr;
  QDockWidget* macroDock_ = nullptr;
  QDockWidget* statusDock_ = nullptr;
  bool heroSized_ = false;
};
