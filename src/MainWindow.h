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
class QEvent;

class Ft8Decode;
class HellEngine;
class ModeProfile;
class QAction;
class QDockWidget;
class QResizeEvent;
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
  void resizeEvent(QResizeEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

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
  void applyTextStyle();
  void refreshDecodeHeader();
  void reloadMacros();
  void openTimeDialog();
  void openMacrosDialog();
  void openNetworkDialog();
  void openUserDialog();
  void applyStationLock();
  void setReceivePlaceholder(bool connected);
  void showRadioDown(const QString& error);
  void showRadioUp(const QString& host, quint16 port);
  void showDial(qint64 hz);

  QWidget* makeTextPanel(const QString& editName, const QString& placeholder,
                         bool readOnly, const QString& note);
  QWidget* makeReceivePanel();
  void showHellReceive(bool on);
  QWidget* makeMacroRow();
  void refreshDecodes();
  void applyChannelWidth();
  void alignDecodeHeader();
  void setUserTx(bool on);
  void setTune(bool on);
  void stopTune(bool uncheck);
  void pumpUserTx();
  void stopUserTx(bool uncheck);
  void setTxStatus(const QString& text);
  void loadReplyFromLine(int line);
  void refreshTxFields();
  void selectTxSlot(int index);
  int selectedTxSlot() const;
  bool periodReady(const QString& mode);
  QString transmitMessage() const;
  QString stationCall() const;
  QString stationGrid() const;
  void openDecodeFilters();
  bool keepDecode(const QString& message) const;
  QString distanceText(const QString& message) const;
  void saveModeLayout(bool force = false);
  bool restoreModeLayout();
  void rememberModeTone();
  void applyModeTone();

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
  HellEngine* hell_ = nullptr;
  QAction* autoConnectAction_ = nullptr;
  QAction* autoMonitorAction_ = nullptr;
  bool autoMonitor_ = true;
  bool showTime_ = true;
  bool showFreq_ = true;
  bool showDist_ = true;
  bool distMiles_ = true;
  bool filtersOn_ = false;
  bool filterCq_ = false;
  bool filterCq73_ = false;
  QString filterText_;
  bool hideContinent_[7] = {};
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
  bool headerAlignQueued_ = false;
  QTimer* txTimer_ = nullptr;
  std::vector<float> txSamples_;
  int txCursor_ = 0;
  int txHold_ = 0;
  bool txKeySent_ = false;
  bool txTune_ = false;
  double txTunePhase_ = 0.0;
  int txTuneHz_ = 1500;
  qint64 txTuneStartMs_ = 0;
  int txTuneLimitMs_ = 0;
  QString txHisCall_;
  QString txReport_;
  QString txQrg_;
  int txPeriodMark_ = -1;
  QVector<DecodeRow> shownDecodes_;
};
