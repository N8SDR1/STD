// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

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

  int speed() const;
  int startHz() const;
  int bandwidthHz() const;
  bool autoDecode() const;
  qint64 dialHz() const;
  QString sideband() const;

signals:
  void tuneRequested(qint64 dialHz, const QString& sideband);
  void displayChanged(int startHz, int bandwidthHz);
  void speedChanged(int speed);

private:
  void applySpan();
  void remember() const;

  QWidget* bar_ = nullptr;
  QWidget* keyboard_ = nullptr;
  QSpinBox* speed_ = nullptr;
  QSpinBox* start_ = nullptr;
  QSpinBox* bandwidth_ = nullptr;
  int band_ = 0;
  bool family_ = true;
  QString mode_ = QStringLiteral("FT8");
};
