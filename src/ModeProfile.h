// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QComboBox;
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
  bool multiAnswer() const;
  qint64 dialHz() const;
  QString sideband() const;
  // "auto", "1", "2", or "3". Auto reads the callsign. A portable
  // suffix such as N8SDR/VE uses that country. The call text is not rewritten.
  void setRegionOverride(const QString& choice);
  void refreshStation();

signals:
  void tuneRequested(qint64 dialHz, const QString& sideband);
  void displayChanged(int startHz, int bandwidthHz);
  void speedChanged(int speed);
  void houndChanged(bool on);
  void multiAnswerChanged(bool on);

private:
  void applySpan();
  void remember() const;
  void refreshBands();
  void reloadExtras();
  void saveExtras() const;
  void advanceCycle();
  void insertExtra(qint64 hz);
  QList<qint64> storedExtras(int index) const;
  int effectiveRegion() const;
  qint64 tableHz(int index) const;

  QWidget* bar_ = nullptr;
  QWidget* keyboard_ = nullptr;
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
  bool multiAnswer_ = false;
  QList<qint64> extras_;
  int cycle_ = 0;
};
