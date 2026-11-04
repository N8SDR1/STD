// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QPair>
#include <QStringList>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

// The controls an MSHV-family mode shows under the waterfall. Keyboard
// modes (RTTY, PSK31) hide this bar. Their own menus are not here yet.
class ModeProfile : public QWidget {
  Q_OBJECT

public:
  explicit ModeProfile(QWidget* parent = nullptr);

  void setMshvFamily(bool mshv);
  bool mshvFamily() const { return family_; }
  void setDigitalMode(const QString& mode);
  QString digitalMode() const { return mode_; }

  int speed() const;
  int startHz() const;
  int bandwidthHz() const;
  bool autoDecode() const;
  bool hound() const;
  bool superHound() const { return hound_; }
  bool superFox() const { return fox_; }
  bool txFirst() const { return txFirst_; }
  // Remembers the even or odd slot. Does not key.
  void setTxFirst(bool first);
  // Super Hound answers on TX SECOND. Does not key.
  void setTxFirstLocked(bool locked);
  // MSHV Multi Answering Auto Seq Protocol. 0 off, 1 Standard (Ctrl+Q,
  // callers queue and are worked from the queue), 2 DXpedition (Ctrl+`,
  // several QSOs in one transmission).
  int multiAnswerMode() const { return multiMode_; }
  bool multiAnswer() const;
  bool multiAnswerDx() const { return multiMode_ == 2; }
  QSpinBox* speedBox() const { return speed_; }
  QSpinBox* startBox() const { return start_; }
  QSpinBox* bandwidthBox() const { return bandwidth_; }
  qint64 dialHz() const;
  QString sideband() const;
  // "auto", "1", "2", or "3". Auto reads the callsign. A portable
  // suffix such as N8SDR/VE uses that country. The call text is not rewritten.
  void setRegionOverride(const QString& choice);
  void refreshStation();
  // Fills the Mic, speaker, and volume row from the Speech settings.
  void refreshRadeDevices();
  // RADE scan: every FreeDV Reporter calling dial, low band to high, as
  // (band index, Hz).
  // meters limits it to those bands ("160", "80", "60", "40", "20", "17",
  // "15", "12", "10"); empty means every band.
  QList<QPair<int, qint64>> radeScanDials(const QStringList& meters = QStringList()) const;
  // The bands Scan can use, in order.
  static QStringList radeScanBands();
  // Moves to that band and dial and emits tuneRequested, as a band click does.
  void selectDial(int band, qint64 hz);

signals:
  void tuneRequested(qint64 dialHz, const QString& sideband);
  void displayChanged(int startHz, int bandwidthHz);
  void speedChanged(int speed);
  void houndChanged(bool on);
  void multiAnswerChanged(bool on);
  void filtersChanged(bool on);
  void filterEditRequested();
  void radeVolumeChanged(int percent);
  void radeCaptureChanged(const QString& micId);
  void radeOutputChanged(const QString& speakerId);

private:
  void applySpan();
  void remember() const;
  void refreshBands();
  void reloadExtras();
  void saveExtras() const;
  void advanceCycle();
  void insertExtra(qint64 hz);
  QList<qint64> storedExtras(int index) const;
  QList<qint64> cycleDials(int index) const;
  bool stockDial(int index, qint64 hz) const;
  int effectiveRegion() const;
  qint64 tableHz(int index) const;

  QWidget* bar_ = nullptr;
  QWidget* radeAudio_ = nullptr;
  QWidget* keyboard_ = nullptr;
  QComboBox* radeMicBox_ = nullptr;
  QComboBox* radeSpeakerBox_ = nullptr;
  QSlider* radeVolume_ = nullptr;
  QLabel* radeVolumeValue_ = nullptr;
  QSpinBox* speed_ = nullptr;
  QSpinBox* start_ = nullptr;
  QSpinBox* bandwidth_ = nullptr;
  QComboBox* region_ = nullptr;
  QSpinBox* extra_ = nullptr;
  QString regionChoice_ = QStringLiteral("auto");
  int band_ = 0;
  bool family_ = true;
  QString mode_ = QStringLiteral("FT8");
  bool hound_ = false;
  bool fox_ = false;
  bool txFirst_ = false;
  int multiMode_ = 0;
  QList<qint64> extras_;
  int cycle_ = 0;
};
