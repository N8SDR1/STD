// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sstv/SstvTemplate.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class SstvTemplateCanvas;

// Make and change SSTV templates: text with fill-in fields, a box for the
// picture you received, and coloured panels over a background. Nothing here
// keys the radio.
class SstvTemplateEditor : public QDialog {
  Q_OBJECT

public:
  // `fields` fills the preview; `rxPicture` shows in their-picture boxes.
  SstvTemplateEditor(const SstvFields& fields, const QImage& rxPicture, const QString& select,
                     QWidget* parent = nullptr);

  // The template open when the dialog closed, if any.
  QString currentName() const { return current_.name; }

private:
  void loadList(const QString& select);
  void open(int row);
  void showLayers();
  void showLayer();
  void layerChanged();
  void saveCurrent();
  bool confirmLeave();
  void setDirty(bool dirty);

  std::vector<SstvTemplate> templates_;
  SstvTemplate current_;
  int layer_ = -1;
  bool dirty_ = false;
  bool loading_ = false;
  SstvFields fields_;
  QImage rx_;

  QListWidget* list_ = nullptr;
  QLineEdit* name_ = nullptr;
  QComboBox* category_ = nullptr;
  QComboBox* background_ = nullptr;
  QPushButton* bgColor_ = nullptr;
  SstvTemplateCanvas* canvas_ = nullptr;
  QListWidget* layers_ = nullptr;
  QStackedWidget* props_ = nullptr;
  // Text layer
  QPlainTextEdit* text_ = nullptr;
  QComboBox* field_ = nullptr;
  QFontComboBox* font_ = nullptr;
  QCheckBox* bold_ = nullptr;
  QCheckBox* italic_ = nullptr;
  QCheckBox* stacked_ = nullptr;
  QDoubleSpinBox* size_ = nullptr;
  QPushButton* fill_ = nullptr;
  QPushButton* outline_ = nullptr;
  QDoubleSpinBox* outlineWidth_ = nullptr;
  QCheckBox* shadow_ = nullptr;
  QComboBox* align_ = nullptr;
  // Box and their-picture layers
  QPushButton* boxColor_ = nullptr;
  QDoubleSpinBox* boxOpacity_ = nullptr;
  QCheckBox* border_ = nullptr;
  QPushButton* borderColor_ = nullptr;
  QComboBox* photo_ = nullptr;
  // Any layer
  QDoubleSpinBox* angle_ = nullptr;
  QPushButton* save_ = nullptr;
};
