// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class SstvEngine;

// The SSTV receive pane: the picture coming in, the controls, and the
// pictures already saved in the RX folder. Shown in SSTV mode in place of the
// decode list. Nothing here keys the radio or sends START.
class SstvPanel : public QWidget {
  Q_OBJECT

public:
  SstvPanel(SstvEngine* engine, QWidget* parent = nullptr);

  // Read the RX folder again (after a save, or when the folder changes).
  void refreshSaved();

signals:
  // A line for the status bar, already worded for the operator.
  void status(const QString& text);
  // Use this received picture in a reply template.
  void replyWith(const QImage& picture, const QString& from);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void showPicture();
  void showState();

  SstvEngine* engine_;
  QComboBox* mode_ = nullptr;
  QPushButton* start_ = nullptr;
  QPushButton* stop_ = nullptr;
  QPushButton* save_ = nullptr;
  QCheckBox* autoSave_ = nullptr;
  // When an FSK ID names the sender, make that picture their picture.
  QCheckBox* autoReply_ = nullptr;
  QLabel* state_ = nullptr;
  QLabel* view_ = nullptr;
  QListWidget* saved_ = nullptr;
  QString lastSaved_;
  QString lastFskId_;
  // A saved picture opened from the list; cleared when a new one starts.
  QString viewing_;
};
