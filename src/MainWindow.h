// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "MaEngine.h"
#include "MacroFile.h"

#include <QByteArray>
#include <QIcon>
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <QVector>

#include <initializer_list>
#include <vector>

class QCloseEvent;
class QEvent;

class Ft8Decode;
class HellEngine;
class DominoEngine;
class ThorEngine;
class MfskEngine;
class Mt63Engine;
class OliviaEngine;
class PskEngine;
class RttyEngine;
class WefaxEngine;
class Js8Engine;
class WsprEngine;
class PskUploader;
class WsprNetUploader;
class UpdateChecker;
class ModeProfile;
class QAction;
class QDockWidget;
class QResizeEvent;
class QShowEvent;
class QToolBar;
class StatusStrip;
class QsoLog;
class TciClient;
class WaterfallWidget;

// Square window icon cropped from resources/std-icon.jpg, at the sizes a
// window manager asks for. A single large file often never reaches _NET_WM_ICON.
QIcon stdIcon();

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(QWidget* parent = nullptr);
  // Self-test plants a call on Tx1–Tx5. It does not key and does not send START.
  void placeAnswer(const QString& call);
  // Puts the waterfall RHI widget in the tree before the window is shown.
  void primeGraphics();

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
  void clearModePanes();
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
  void loadMacroStore();
  void storeMacros();
  void showMacroSet();
  void openMacroFile(QWidget* parent = nullptr);
  void saveMacroFile(QWidget* parent = nullptr);
  void insertMacro(int index);
  void cycleMacroBank(int step);
  void openTimeDialog();
  void openMacrosDialog(int index = -1);
  void openNetworkDialog();
  void openUserDialog();
  void openLogDialog();
  void backupSettings();
  void importSettings();
  void backupLogbook();
  void exportAdif();
  QString logbookPath() const;
  void rememberQso(const QString& call, const QString& grid, const QString& theirReport, bool fresh,
                   const QString& rxMessage = QString());
  QString maybeLogQso(const QString& sent);
  void applyStationLock();
  void setReceivePlaceholder(bool connected);
  void showRadioDown(const QString& error);
  void showRadioUp(const QString& host, quint16 port);
  void showDial(qint64 hz);

  QWidget* makeTextPanel(const QString& editName, const QString& placeholder,
                         bool readOnly, const QString& note);
  QWidget* makeReceivePanel();
  void placeDockTitle(QDockWidget* dock, const QString& title, std::initializer_list<const char*> buttons);
  void applyMacroBar();
  void showKeyboardReceive(const QString& mode);
  void revealSentEcho();
  void appendTxEcho(const QString& text);
  void appendRxEcho(const QString& text);
  QWidget* makeMacroRow();
  void refreshDecodes();
  void applyChannelWidth();
  void applyHellSquelch();
  void applyAfc();
  void syncWfHellControls();
  void alignDecodeHeader();
  void alignWsprHeader();
  void setUserTx(bool on);
  void setTune(bool on);
  void haltTx();
  void stopTune(bool uncheck);
  void pumpUserTx();
  void onTxPull(int sampleRateHz, int length, int channels);
  void fillTune(float* dst, int frames);
  void fillWave(float* dst, int frames, int step);
  void sendTxBlock(const float* mono, int frames, int rate, int channels);
  bool slotShouldEnd(const QString& mode) const;
  void pushRealtimeTx();
  void stopUserTx(bool uncheck);
  void setTxStatus(const QString& text);
  void loadReplyFromLine(int line);
  void qsyDecodeLine(int line);
  void markWaterfallTx();
  void refreshTxFields();
  void refreshTxChrome();
  void selectTxSlot(int index);
  int selectedTxSlot() const;
  bool periodReady(const QString& mode);
  bool halfIsFirst(const QString& mode) const;
  bool txHalfOpen(const QString& mode) const;
  int periodLengthMs(const QString& mode) const;
  int utcIntoPeriodMs(int periodMs) const;
  int txGuardMs(const QString& mode) const;
  bool theirHalfIsFirst(const QString& mode, const QString& stamp) const;
  bool slotStillOpen(const QString& mode) const;
  bool sequencedMode(const QString& mode) const;
  bool textIs73(const QString& text) const;
  bool triesExhausted() const;
  bool cnsOn() const;
  bool cqOnFreeOn() const;
  QString txSlotName() const;
  void armAnswerSlot(const QString& stamp, int msgPos);
  void armUserTx();
  void retargetLiveTx();
  bool rebuildTxWave();
  void finishTxPass();
  void appendQso(const QString& side, const QString& text);
  void noteTheirReply(const QString& time, int db, const QString& message);
  void applyTheirReply(const QString& time, int db, const QString& message);
  void enqueueCaller(const QString& call, int db, int dist, int slot = 1);
  bool queueFollows() const;
  // MSHV Multi Answering engine (MA Std / MA DX) for FT8, FT4, FT2, Q65.
  bool maActive() const { return ma_.active(); }
  void syncMaEngine();
  bool maRestrictedDial() const;
  void logMaQsos(const QVector<MaEngine::Logged>& done);
  // MSHV xplaymessage delay before the first symbol, in ms.
  int txStartLeadMs(const QString& mode) const;
  void startQueueHead(const QString& time);
  // Removes the station just finished from Queue and Now. A later decode
  // does not put that call back; CLR Queue, CLR Now, or a clicked answer does.
  void dropWorkedCall();
  void addTypedQueue();
  void deleteQueueRow(bool nowList);
  void promoteNow();
  void sortQueue();
  void paintMultiLists();
  void syncCqEditor(bool stock);
  void flipMaxPeriods();
  int txActivityId() const;
  QString serialText() const;
  void advanceLoggedSerial();
  void openContestTags();
  QString composedCq(const QString& kind) const;
  QString transmitMessage() const;
  QString wsprOutbound() const;
  int wsprTxPct() const;
  void maybeWsprHop();
  void beginWsprTune();
  // DIGU and vfo:0,0 when the radio is open. Does not key. START is never sent.
  void qsyDial(qint64 hz);
  QString stationCall() const;
  QString stationGrid() const;
  void openPskReporterMap() const;
  void openWsprnetReports() const;
  void noteDecodeSpot(const QString& mode, const QString& message, int db, int audioHz,
                      const QString& dt = QString(), const QString& utc = QString());
  void checkForUpdates(bool manual);
  void onUpdateAvailable(const QString& tag, const QString& url, const QString& body);
  void onNoUpdate();
  void onUpdateCheckFailed(const QString& reason);
  void openDecodeFilters();
  bool keepDecode(const QString& message) const;
  QString distanceText(const QString& message) const;
  void saveModeLayout(bool force = false);
  bool restoreModeLayout();
  void openNamedLayouts();
  void saveNamedLayout(int slot, const QString& name);
  bool recallNamedLayout(int slot);
  void rememberModeTone();
  void applyModeTone();
  void ensureWorkedCalls() const;

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
  QVector<MacroSlot> macros_;
  int macroBank_ = 0;
  QString macroFile_;
  bool macrosDirty_ = false;
  QAction* autoConnectAction_ = nullptr;
  QAction* autoMonitorAction_ = nullptr;
  bool autoMonitor_ = true;
  bool showTime_ = true;
  bool showFreq_ = true;
  bool showDist_ = true;
  bool qsyOnDecode_ = true;
  bool distMiles_ = true;
  bool filtersOn_ = false;
  bool filterCq_ = false;
  bool filterCq73_ = false;
  QString filterText_;
  QString filterHideCall_;
  QString filterHidePrefix_;
  bool filterQsoB4_ = false;
  bool hideContinent_[7] = {};
  mutable QSet<QString> workedCalls_;
  mutable QString workedPath_;
  mutable qint64 workedStamp_ = 0;
  mutable qint64 workedSize_ = -1;
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
  qint64 liveDialHz_ = 0;
  MaEngine ma_;
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
  struct QueuedCall {
    QString call;
    int db = 0;
    int dist = 0;
    // Tx slot to send when this call reaches the front. 1 is the report.
    int slot = 1;
  };

  QsoLog* qsoLog_ = nullptr;
  QString qsoCall_;
  QString qsoRxMessage_;
  QString qsoGrid_;
  QString qsoRstRcvd_;
  qint64 qsoStartedMs_ = 0;
  bool qsoLogged_ = false;
  QString txHisCall_;
  QString txReport_;
  QString txQrg_;
  QVector<QueuedCall> txQueue_;
  QVector<QueuedCall> txNow_;
  // Calls removed by 73 or by Periods / Max Time. Skipped by the auto queue
  // so the tail of that QSO does not fill the list again.
  QVector<QString> txFinishedCalls_;
  bool txFreeArmed_ = false;
  bool txLoggedThisPass_ = false;
  int txPeriodMark_ = -1;
  bool txSequence_ = false;
  bool txJoinOpen_ = false;
  bool txRetargeted_ = false;
  bool txCqFreeLatched_ = false;
  // True after an answer to this station has gone on the air. Further
  // callers wait in the queue until Periods, Max Time, or 73.
  bool txWorkStarted_ = false;
  // Their reply arrived. The period count starts over, as MSHV resets
  // the per-station try clock.
  bool txAnswered_ = false;
  int txTries_ = 0;
  qint64 txSeqStartMs_ = 0;
  qint64 txPlayOriginMs_ = 0;
  qint64 txLastPullMs_ = 0;
  qint64 txGuardDeadlineMs_ = 0;
  qint64 txPeriodEndMs_ = 0;
  int txTuneSent_ = 0;
  int txSymbolSamples_ = 0;
  QString txSentText_;
  // JS8 snapshot taken when TX is pressed. One frame leaves per 15 s
  // period; txJs8Next_ is how far into that snapshot has already gone out.
  QString txJs8Text_;
  QString txJs8Part_;
  int txJs8Next_ = 0;
  int txJs8Pending_ = 0;
  int txEchoCount_ = 0;
  int txEchoSamples_ = 0;
  std::vector<int> txEchoAt_;
  RttyEngine* rtty_ = nullptr;
  PskEngine* psk_ = nullptr;
  OliviaEngine* olivia_ = nullptr;
  Mt63Engine* mt63_ = nullptr;
  MfskEngine* mfsk_ = nullptr;
  DominoEngine* domino_ = nullptr;
  ThorEngine* thor_ = nullptr;
  WefaxEngine* wefax_ = nullptr;
  Js8Engine* js8_ = nullptr;
  WsprEngine* wspr_ = nullptr;
  int wsprHopMark_ = -1;
  bool wsprHopTune_ = false;
  PskUploader* spots_ = nullptr;
  WsprNetUploader* wsprnet_ = nullptr;
  UpdateChecker* updates_ = nullptr;
  QAction* updateAction_ = nullptr;
  QString pendingUpdateUrl_;
  bool updateCheckManual_ = false;
  QVector<DecodeRow> shownDecodes_;
};
