// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

class QPainter;
class QWindow;

// Standby waterfall.
//
// Phase 1 paints a graticule and an empty state. A later renderer can pass a
// QWindow — including a QVulkanWindow — to setRenderSurface(). This widget
// embeds that surface with QWidget::createWindowContainer and stops painting
// the placeholder. Vulkan is not linked here.
class WaterfallWidget : public QWidget {
  Q_OBJECT
public:
  explicit WaterfallWidget(QWidget* parent = nullptr);

  // Takes ownership of surface. Pass nullptr to drop it and paint the
  // placeholder again. The container deletes the window when it is destroyed.
  void setRenderSurface(QWindow* surface);

  // dial is the VFO text. connected changes the standby line under "No signal".
  void setRadio(bool connected, const QString& dial);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void paintPlaceholder(QPainter& painter) const;

  QWidget* surfaceContainer_ = nullptr;
  bool radioConnected_ = false;
  QString dialText_ = QString::fromUtf8("—");
};
