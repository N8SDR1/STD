// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;

// RADE operating screen. It is not the Hell tape and not the FT8 sequence.
// Mic and speaker are the devices already chosen for Speech. Identity stays
// in Options, User. This widget does not key and does not send START.
class RadeScreen : public QWidget {
  Q_OBJECT

public:
  explicit RadeScreen(QWidget* parent = nullptr);

  // Match the Mode menu. Does not emit variantChosen.
  void showMode(const QString& modeName);
  void setLinkReady(bool ready);
  void setSync(bool on, float snrDb);
  // Callsign read from the last end-of-over, and when.
  void setHeard(const QString& call, const QString& utc);

signals:
  void variantChosen(const QString& modeName);
  void optionsChanged();

private:
  void loadBpf(bool v2);
  void rememberBpf(bool on);

  QComboBox* variant_ = nullptr;
  QCheckBox* bpf_ = nullptr;
  QLabel* sync_ = nullptr;
  QLabel* snr_ = nullptr;
  QLabel* heard_ = nullptr;
  QLabel* link_ = nullptr;
  bool showingV2_ = false;
};
