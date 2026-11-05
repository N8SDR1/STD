// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "spot/FreeDvReporter.h"

#include <QStringList>
#include <QWidget>

class ModeProfile;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QResizeEvent;

// RADE operating screen. It is not the Hell tape and not the FT8 sequence.
// Mic and speaker are the devices already chosen for Speech. Identity stays
// in Options, User. This widget does not key and does not send START.
class RadeScreen : public QWidget {
  Q_OBJECT

public:
  explicit RadeScreen(QWidget* parent = nullptr);

  // Match the Mode menu. Does not emit variantChosen.
  void showMode(const QString& modeName);
  // Moves the audio strip into this screen. Columns stacks it. Rows keep one
  // line and wrap it when the panel is narrow.
  void placeAudio(QWidget* bar, ModeProfile* profile);
  // True: audio, status, and waveform side by side. False: waveform and meters
  // on top, audio underneath.
  void setColumns(bool columns);
  bool columns() const { return columns_; }
  void setLinkReady(bool ready);
  // offsetHz: the received station's frequency offset, as FreeDV shows it.
  void setSync(bool on, float snrDb, float offsetHz = 0.0f);
  // Decoder input in dBFS, whether it clipped, and the Auto RX offset in dB.
  void setRxLevel(float db, bool clip, int autoDb);
  // Callsign read from the last end-of-over, and when.
  void setHeard(const QString& call, const QString& utc);
  // Reloads the FreeDV Reporter message from settings.
  void refreshMessage();
  // Replaces the station table. The filter and the selected row stay.
  void showReporter(const QVector<FreeDvReporter::Station>& rows);
  // How many reporter messages are saved. Never more than 15.
  int savedMessageCount() const;
  // Saves one message and drops the oldest past 15. Empty text is ignored.
  void storeReporterMessage(const QString& text);
  void reloadSavedMessages();
  // Lets Request QSY be pressed again after a send that did not go out.
  void releaseQsyRequest();
  bool ignoreQsy() const;
  int txLimitSeconds() const;
  // Dial used by the "This band" reporter filter. Does not tune.
  void setReporterDial(qint64 hz);
  // RX chip: 0 receiving (green), 1 transmitting (unlit), 2 ending the
  // over (amber). Does not emit rxRequested.
  void setTxState(int state);
  // Lights the Scan chip. Does not emit scanToggled.
  void setScanning(bool on);
  int scanDwellSeconds() const;
  // True: stop on the first dial that syncs. False: listen while it is in
  // sync, then move on.
  bool scanStay() const;
  // Bands Scan uses, as meters ("40", "20"). Empty means all of them.
  QStringList scanBands() const;

signals:
  void variantChosen(const QString& modeName);
  void optionsChanged();
  void scanToggled(bool on);
  // RX clicked: end the over properly, with the end-of-over and callsign.
  void rxRequested();
  void scanBandsChanged();
  // Double-click a station: move the dial there. Does not key.
  void stationTune(qint64 hz);
  // Ask the selected station to move to this dial.
  void stationQsy(const QString& sid);
  // Log the selected station, or the last one heard when none is selected.
  void stationLog(const QString& station, const QString& grid, int snr, qint64 hz);
  void reporterMessageChanged();

private:
  void showScanBandMenu();
  void refreshScanLabel();

  void loadBpf(bool v2);
  void rememberBpf(bool on);
  void refreshHeardClock();
  void applyReporterFilter();
  void refreshReporterColors();
  void requestSelectedQsy();
  void logSelectedStation();
  QString selectedSid() const;
  void applyArrangement();
  void refreshLayoutButton();
  void resizeEvent(QResizeEvent* event) override;
  // Rows only: status drops under the waveform when the two no longer fit.
  void fitRows();

  QComboBox* variant_ = nullptr;
  QCheckBox* bpf_ = nullptr;
  QLabel* sync_ = nullptr;
  QLabel* snr_ = nullptr;
  QLabel* heard_ = nullptr;
  QLabel* heardClock_ = nullptr;
  QLabel* offset_ = nullptr;
  qint64 heardAtMs_ = 0;
  class QLineEdit* message_ = nullptr;
  class QLineEdit* reporterFilter_ = nullptr;
  class QComboBox* savedMessages_ = nullptr;
  class QComboBox* reporterScope_ = nullptr;
  qint64 reporterDialHz_ = 0;
  class QTableWidget* reporterTable_ = nullptr;
  class QLabel* reporterCount_ = nullptr;
  class QPushButton* requestQsy_ = nullptr;
  class QCheckBox* ignoreQsy_ = nullptr;
  class QSpinBox* txLimit_ = nullptr;
  QString lastHeard_;
  QString qsySentSid_;
  class QPushButton* scan_ = nullptr;
  class QPushButton* rx_ = nullptr;
  class QSpinBox* scanDwell_ = nullptr;
  QLabel* dwellLabel_ = nullptr;
  QCheckBox* scanStay_ = nullptr;
  QLabel* link_ = nullptr;
  QLabel* waveLabel_ = nullptr;
  QWidget* lamp_ = nullptr;
  QWidget* snrBar_ = nullptr;
  QWidget* snrTrace_ = nullptr;
  QWidget* rxBar_ = nullptr;
  QCheckBox* autoRx_ = nullptr;
  QLabel* autoValue_ = nullptr;
  QWidget* operate_ = nullptr;
  QWidget* meterCol_ = nullptr;
  QWidget* audioHost_ = nullptr;
  QWidget* body_ = nullptr;
  QPushButton* layoutPick_ = nullptr;
  QWidget* audioBar_ = nullptr;
  ModeProfile* audioProfile_ = nullptr;
  bool columns_ = true;
  bool rowsStacked_ = false;
  bool showingV2_ = false;
};
