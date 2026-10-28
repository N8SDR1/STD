// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QIcon>
#include <QMainWindow>
#include <QString>
#include <QVector>

#include <vector>

class QCloseEvent;

class Ft8Decode;
class ModeProfile;
class QAction;
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
  void closeEvent(QCloseEvent* event) override;

private:
  void buildBrandBar();
  void buildDocks();
  void buildMenus();
  void arrangeDefault();
  void resizeToHero();
  void showAbout();
  void showGuide();
  void tuneActiveBand();
  void activateDigitalMode(const QString& modeName);
  void cycleDigitalMode();
  void offerLayoutSave();
  void captureLayoutBaseline();
  void openRadioDialog();
  void syncConnectOptions();
  void maybeAutoConnect();
  void applyMonitorLabel();
  void applyDxOptions();
  void refreshDecodeHeader();
  void reloadMacros();
  void openTimeDialog();
  void openMacrosDialog();
  void openNetworkDialog();
  void setReceivePlaceholder(bool connected);
  void showRadioDown(const QString& error);
  void showRadioUp(const QString& host, quint16 port);
  void showDial(qint64 hz);

  QWidget* makeTextPanel(const QString& editName, const QString& placeholder,
                         bool readOnly, const QString& note);
  QWidget* makeReceivePanel();
  QWidget* makeMacroRow();
  void refreshDecodes();
  void saveModeLayout(bool force = false);
  bool restoreModeLayout();

  struct DecodeRow {
    QString time;
    int db = 0;
    QString dt;
    QString message;
    int freqHz = 0;
  };

  QToolBar* brandBar_ = nullptr;
  WaterfallWidget* waterfall_ = nullptr;
  ModeProfile* profile_ = nullptr;
  StatusStrip* statusStrip_ = nullptr;
  TciClient* tci_ = nullptr;
  Ft8Decode* ft8_ = nullptr;
  QAction* autoConnectAction_ = nullptr;
  QAction* autoMonitorAction_ = nullptr;
  bool autoMonitor_ = true;
  bool showTime_ = true;
  bool showFreq_ = true;
  bool clearEachPeriod_ = false;
  int maxSignals_ = 0;
  QString lastPeriod_;
  float rxGain_ = 1.0f;
  std::vector<float> rxScaled_;
  QString layoutMode_ = QStringLiteral("FT8");
  QByteArray layoutBaseline_;
  int layoutEpoch_ = 0;
  bool layoutBaselinePending_ = false;
  bool layoutReady_ = false;
  bool layoutGuard_ = false;
  QVector<DecodeRow> decodes_;
  QString dialText_ = QString::fromUtf8("—");
  QDockWidget* waterfallDock_ = nullptr;
  QDockWidget* receiveDock_ = nullptr;
  QDockWidget* transmitDock_ = nullptr;
  QDockWidget* macroDock_ = nullptr;
  QDockWidget* statusDock_ = nullptr;
  bool heroSized_ = false;
};
