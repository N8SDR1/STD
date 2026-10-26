// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaterfallWidget.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWindow>

WaterfallWidget::WaterfallWidget(QWidget* parent) : QWidget(parent) {
  setObjectName("waterfall");
  setAttribute(Qt::WA_StyledBackground, false);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::NoFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAccessibleName("Waterfall");
  setAccessibleDescription("No signal. Radio not connected.");
}

void WaterfallWidget::setRadio(bool connected, const QString& dial) {
  radioConnected_ = connected;
  dialText_ = dial.isEmpty() ? QString::fromUtf8("—") : dial;
  setAccessibleDescription(connected ? QStringLiteral("No signal. Connected.")
                                      : QStringLiteral("No signal. Radio not connected."));
  update();
}

void WaterfallWidget::setRenderSurface(QWindow* surface) {
  if (surfaceContainer_) {
    surfaceContainer_->hide();
    surfaceContainer_->deleteLater();
    surfaceContainer_ = nullptr;
  }
  if (!surface) {
    update();
    return;
  }

  surfaceContainer_ = QWidget::createWindowContainer(surface, this);
  surfaceContainer_->setFocusPolicy(Qt::NoFocus);
  surfaceContainer_->setGeometry(rect());
  surfaceContainer_->show();
  surfaceContainer_->raise();
}

QSize WaterfallWidget::sizeHint() const {
  return QSize(800, 380);
}

QSize WaterfallWidget::minimumSizeHint() const {
  return QSize(280, 180);
}

void WaterfallWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (surfaceContainer_)
    return;
  QPainter painter(this);
  paintPlaceholder(painter);
}

void WaterfallWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (surfaceContainer_)
    surfaceContainer_->setGeometry(rect());
}

void WaterfallWidget::paintPlaceholder(QPainter& painter) const {
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRect area = rect();

  QLinearGradient ground(0, 0, 0, area.height());
  ground.setColorAt(0.0, QColor("#070c12"));
  ground.setColorAt(1.0, QColor("#05080c"));
  painter.fillRect(area, ground);

  QLinearGradient sheen(0, 0, 0, 22);
  sheen.setColorAt(0.0, QColor(0, 229, 255, 26));
  sheen.setColorAt(1.0, QColor(0, 229, 255, 0));
  painter.fillRect(0, 0, area.width(), 22, sheen);

  painter.setPen(QColor(39, 65, 80, 120));
  constexpr int kColumns = 8;
  for (int i = 1; i < kColumns; ++i) {
    const int x = area.width() * i / kColumns;
    painter.drawLine(x, 28, x, area.height() - 26);
  }
  for (int y = 32; y < area.height() - 26; y += 16)
    painter.drawLine(10, y, area.width() - 10, y);

  const int center = area.width() / 2;
  painter.setPen(QPen(QColor(0, 229, 255, 170), 1));
  painter.drawLine(center, 28, center, area.height() - 26);

  QFont mono("JetBrains Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(12);
  painter.setFont(mono);

  painter.setPen(QColor("#6d8496"));
  painter.drawText(QRect(12, 8, 42, 16), Qt::AlignLeft | Qt::AlignVCenter, "DIAL");
  painter.setPen(QColor("#00e5ff"));
  painter.drawText(QRect(54, 8, 180, 16), Qt::AlignLeft | Qt::AlignVCenter, dialText_);

  painter.setPen(QColor("#8399ac"));
  const QStringList ticks = {
    QString::fromUtf8("−2"),
    QString::fromUtf8("−1"),
    QStringLiteral("0"),
    QStringLiteral("+1"),
    QStringLiteral("+2"),
  };
  for (int i = 0; i < ticks.size(); ++i) {
    const int x = area.width() * (i + 1) / (ticks.size() + 1);
    painter.drawText(QRect(x - 20, area.height() - 20, 40, 16), Qt::AlignCenter, ticks[i]);
  }
  painter.setPen(QColor("#5a7080"));
  painter.drawText(QRect(area.width() - 52, area.height() - 20, 42, 16),
                   Qt::AlignRight | Qt::AlignVCenter, "kHz");

  const QRect plate(area.center().x() - 148, area.center().y() - 34, 296, 68);
  painter.setPen(QPen(QColor("#274150"), 1));
  painter.setBrush(QColor(8, 12, 18, 220));
  painter.drawRoundedRect(plate, 8, 8);

  QFont title = mono;
  title.setPixelSize(16);
  title.setBold(true);
  painter.setFont(title);
  painter.setPen(QColor("#cdd9e5"));
  painter.drawText(plate.adjusted(0, 10, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "No signal");

  painter.setFont(mono);
  painter.setPen(QColor("#e08a3d"));
  painter.drawText(plate.adjusted(0, 36, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                   radioConnected_ ? QStringLiteral("Connected")
                                   : QStringLiteral("Radio not connected"));
}
