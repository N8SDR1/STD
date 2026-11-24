// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sstv/SstvPanel.h"

#include "sstv/SstvModem.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr int kThumbWidth = 128;
constexpr int kSavedShown = 40;

QFont panelFont(int pixelSize) {
  QFont font("Inter");
  font.setPixelSize(pixelSize);
  return font;
}

QPushButton* smallButton(const QString& text, const QString& name, const QString& tip, QWidget* parent) {
  auto* b = new QPushButton(text, parent);
  b->setObjectName(name);
  b->setFont(panelFont(11));
  b->setFocusPolicy(Qt::NoFocus);
  b->setToolTip(tip);
  b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  return b;
}

}  // namespace

SstvPanel::SstvPanel(SstvEngine* engine, QWidget* parent) : QWidget(parent), engine_(engine) {
  setObjectName(QStringLiteral("sstvPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  auto* modeLabel = new QLabel(QStringLiteral("Mode"), this);
  modeLabel->setFont(panelFont(12));
  mode_ = new QComboBox(this);
  mode_->setObjectName(QStringLiteral("sstvMode"));
  mode_->setFont(panelFont(12));
  mode_->addItem(QStringLiteral("Auto (VIS)"), QString());
  for (SstvMode m : sstvModes())
    mode_->addItem(sstvModeName(m), sstvModeName(m));
  mode_->setToolTip(QStringLiteral(
      "Auto reads the mode from the VIS header each picture starts with. Pick a mode and press Start to "
      "begin a picture you tuned into part way through. This does not key."));
  {
    const int at = mode_->findData(QSettings().value(QStringLiteral("sstv/startMode")).toString());
    mode_->setCurrentIndex(at < 0 ? 0 : at);
  }
  start_ = smallButton(QStringLiteral("Start"), QStringLiteral("sstvStart"),
                       QStringLiteral("Start a picture now in the chosen mode, without waiting for its VIS header. "
                                      "For a picture already under way. This does not key."),
                       this);
  stop_ = smallButton(QStringLiteral("Stop"), QStringLiteral("sstvStop"),
                      QStringLiteral("End the picture now and wait for the next VIS header. Auto save keeps "
                                     "what came in. This does not key."),
                      this);
  auto* clear = smallButton(
      QStringLiteral("Clear"), QStringLiteral("sstvClear"),
      QStringLiteral("Clear the picture and wait for the next VIS header. With Auto save on, a picture "
                     "that has come in is saved first. This does not key."),
      this);
  save_ = smallButton(QStringLiteral("Save"), QStringLiteral("sstvSave"),
                      QStringLiteral("Save the picture so far to the RX folder. This does not key."), this);
  auto* reply = smallButton(
      QStringLiteral("Reply with it"), QStringLiteral("sstvReplyWith"),
      QStringLiteral("Put the picture shown here (the one coming in, or one clicked in the list) into the "
                     "their-picture box of a reply template. This does not key."),
      this);
  auto* open = smallButton(QStringLiteral("Open"), QStringLiteral("sstvOpen"),
                           QStringLiteral("Open the RX folder. This does not key."), this);
  auto* folder = smallButton(QStringLiteral("Folder"), QStringLiteral("sstvFolder"),
                             QStringLiteral("Choose where received pictures are saved. This does not key."), this);
  autoSave_ = new QCheckBox(QStringLiteral("Auto save"), this);
  autoSave_->setObjectName(QStringLiteral("sstvAutoSave"));
  autoSave_->setFont(panelFont(12));
  autoSave_->setChecked(QSettings().value(QStringLiteral("sstv/autoSave"), true).toBool());
  autoSave_->setToolTip(QStringLiteral(
      "Save each picture as a PNG in the RX folder when it ends, named with the UTC time and mode. "
      "This does not key."));
  engine_->setAutoSave(autoSave_->isChecked());
  state_ = new QLabel(this);
  state_->setObjectName(QStringLiteral("sstvState"));
  state_->setFont(panelFont(12));
  state_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  row->addWidget(modeLabel);
  row->addWidget(mode_);
  row->addWidget(start_);
  row->addWidget(stop_);
  row->addWidget(clear);
  row->addWidget(save_);
  row->addWidget(autoSave_);
  row->addWidget(reply);
  row->addWidget(open);
  row->addWidget(folder);
  row->addWidget(state_, 1);
  layout->addLayout(row);

  auto* split = new QSplitter(Qt::Horizontal, this);
  split->setObjectName(QStringLiteral("sstvSplit"));
  split->setChildrenCollapsible(false);
  view_ = new QLabel(split);
  view_->setObjectName(QStringLiteral("sstvView"));
  view_->setAlignment(Qt::AlignCenter);
  view_->setMinimumSize(160, 120);
  view_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  view_->setStyleSheet(QStringLiteral("background: #111216;"));
  view_->setToolTip(QStringLiteral(
      "The picture coming in. Black is 1500 Hz, white 2300 Hz, sync 1200 Hz; the mode comes from the "
      "VIS header. Line sync is followed pulse by pulse, so a fast or slow sound card does not slant "
      "the picture. This does not key and does not send START."));
  saved_ = new QListWidget(split);
  saved_->setObjectName(QStringLiteral("sstvSaved"));
  saved_->setViewMode(QListView::ListMode);
  saved_->setIconSize(QSize(kThumbWidth, kThumbWidth * 3 / 4));
  saved_->setFont(panelFont(11));
  saved_->setMinimumWidth(kThumbWidth + 40);
  saved_->setToolTip(QStringLiteral(
      "Pictures in the RX folder, newest first. Click one to show it; it stays until the next picture "
      "starts. Right-click one to reply with it. This does not key."));
  split->addWidget(view_);
  split->addWidget(saved_);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 0);
  layout->addWidget(split, 1);

  connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    QSettings().setValue(QStringLiteral("sstv/startMode"), mode_->currentData());
    showState();
  });
  connect(start_, &QPushButton::clicked, this, [this] {
    const SstvMode m = sstvModeFromName(mode_->currentData().toString());
    if (m == SstvMode::None) {
      emit status(QStringLiteral("Pick a mode to start by hand; Auto waits for the VIS header. "
                                 "Nothing was keyed. START was not sent."));
      return;
    }
    viewing_.clear();
    engine_->startNow(m);
    showState();
  });
  connect(stop_, &QPushButton::clicked, this, [this] {
    if (engine_->receiving() && autoSave_->isChecked() && engine_->rowCount() >= 16)
      engine_->saveNow();
    engine_->stop();
    showState();
  });
  connect(clear, &QPushButton::clicked, this, [this] {
    if (autoSave_->isChecked() && engine_->rowCount() >= 16 && engine_->receiving())
      engine_->saveNow();
    engine_->clear();
    viewing_.clear();
    showPicture();
    showState();
  });
  connect(reply, &QPushButton::clicked, this, [this] {
    QImage picture;
    QString from;
    if (!viewing_.isEmpty()) {
      picture.load(viewing_);
      from = QFileInfo(viewing_).fileName();
    } else if (!engine_->image().isNull() && engine_->rowCount() > 0) {
      picture = engine_->image().copy(0, 0, engine_->image().width(), engine_->rowCount());
      from = QStringLiteral("the picture on screen");
    }
    if (picture.isNull()) {
      emit status(QStringLiteral("No received picture to reply with yet. Nothing was keyed."));
      return;
    }
    emit replyWith(picture, from);
    emit status(QStringLiteral("%1 is now their picture in reply templates. Nothing was keyed. START was not sent.")
                    .arg(from));
  });
  connect(save_, &QPushButton::clicked, this, [this] {
    const QString path = engine_->saveNow();
    if (path.isEmpty())
      emit status(QStringLiteral("No SSTV picture to save yet. Nothing was keyed. START was not sent."));
  });
  connect(open, &QPushButton::clicked, this, [] {
    QDir().mkpath(sstvRxDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(sstvRxDirectory()));
  });
  connect(folder, &QPushButton::clicked, this, [this] {
    const QString picked = QFileDialog::getExistingDirectory(this, QStringLiteral("SSTV RX folder"),
                                                             sstvRxDirectory());
    if (picked.isEmpty())
      return;
    sstvSetRxDirectory(picked);
    refreshSaved();
    emit status(QStringLiteral("SSTV pictures save to %1. Nothing was keyed. START was not sent.").arg(picked));
  });
  connect(autoSave_, &QCheckBox::toggled, this, [this](bool on) {
    QSettings().setValue(QStringLiteral("sstv/autoSave"), on);
    engine_->setAutoSave(on);
  });
  saved_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(saved_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& at) {
    QListWidgetItem* item = saved_->itemAt(at);
    if (!item)
      return;
    const QString path = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    QAction* reply = menu.addAction(QStringLiteral("Reply with this picture"));
    QAction* show = menu.addAction(QStringLiteral("Show it"));
    QAction* chosen = menu.exec(saved_->viewport()->mapToGlobal(at));
    if (chosen == reply) {
      const QImage picture(path);
      if (picture.isNull())
        return;
      emit replyWith(picture, QFileInfo(path).fileName());
      emit status(QStringLiteral("%1 is now their picture for a reply. Nothing was keyed. START was not sent.")
                      .arg(QFileInfo(path).fileName()));
    } else if (chosen == show) {
      viewing_ = path;
      showPicture();
      showState();
    }
  });
  connect(saved_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    viewing_ = item ? item->data(Qt::UserRole).toString() : QString();
    showPicture();
    showState();
  });

  connect(engine_, &SstvEngine::pictureStarted, this, [this](SstvMode m) {
    viewing_.clear();
    emit status(QStringLiteral("SSTV %1 picture starting. Nothing was keyed. START was not sent.")
                    .arg(sstvModeName(m)));
    showState();
  });
  connect(engine_, &SstvEngine::imageChanged, this, [this] {
    if (viewing_.isEmpty())
      showPicture();
    showState();
  });
  connect(engine_, &SstvEngine::pictureFinished, this, [this] { showState(); });
  connect(engine_, &SstvEngine::imageSaved, this, [this](const QString& path) {
    lastSaved_ = path;
    refreshSaved();
    showState();
    emit status(QStringLiteral("SSTV saved %1. Nothing was keyed. START was not sent.").arg(path));
  });

  refreshSaved();
  showState();
}

void SstvPanel::refreshSaved() {
  saved_->clear();
  const QDir dir(sstvRxDirectory());
  const QFileInfoList files =
      dir.entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.bmp")}, QDir::Files,
                        QDir::Time);
  int shown = 0;
  for (const QFileInfo& info : files) {
    if (shown++ >= kSavedShown)
      break;
    const QPixmap thumb = QPixmap(info.filePath()).scaledToWidth(kThumbWidth, Qt::SmoothTransformation);
    auto* item = new QListWidgetItem(QIcon(thumb), info.completeBaseName(), saved_);
    item->setData(Qt::UserRole, info.filePath());
    item->setToolTip(info.filePath());
  }
}

void SstvPanel::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  showPicture();
}

void SstvPanel::showPicture() {
  QImage picture;
  if (!viewing_.isEmpty())
    picture.load(viewing_);
  else if (!engine_->image().isNull())
    picture = engine_->image();
  if (picture.isNull()) {
    view_->setPixmap(QPixmap());
    view_->setText(QStringLiteral("Waiting for a picture. SSTV is usually on 14.230 MHz USB."));
    return;
  }
  view_->setText(QString());
  view_->setPixmap(QPixmap::fromImage(picture).scaled(view_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SstvPanel::showState() {
  QString text;
  if (!viewing_.isEmpty()) {
    text = QStringLiteral("Showing %1").arg(QFileInfo(viewing_).fileName());
  } else if (engine_->receiving()) {
    const SstvMode m = engine_->mode();
    text = QStringLiteral("Receiving %1, line %2 of %3").arg(sstvModeName(m)).arg(engine_->rowCount()).arg(sstvHeight(m));
    if (std::abs(engine_->offsetHz()) >= 20.0)
      text += QStringLiteral(", %1 Hz off").arg(engine_->offsetHz(), 0, 'f', 0);
  } else {
    text = QStringLiteral("Waiting for a VIS header.");
    if (!engine_->endReason().isEmpty())
      text += QStringLiteral(" Last picture: %1.").arg(engine_->endReason());
  }
  if (!lastSaved_.isEmpty() && viewing_.isEmpty())
    text += QStringLiteral("   Last saved: %1").arg(QFileInfo(lastSaved_).fileName());
  state_->setText(text);
  start_->setEnabled(!mode_->currentData().toString().isEmpty());
  stop_->setEnabled(engine_->receiving());
}
