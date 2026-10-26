// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "StatusStrip.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>

namespace {

QFont monoFont(int pixelSize) {
  QFont font("JetBrains Mono");
  font.setStyleHint(QFont::Monospace);
  font.setPixelSize(pixelSize);
  return font;
}

class PttLamp : public QPushButton {
public:
  explicit PttLamp(QWidget* parent = nullptr) : QPushButton(parent) {
    setObjectName("ptt");
    setCheckable(true);
    setChecked(false);
    setEnabled(false);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setToolTip("Off. The radio is not connected.");
    setAccessibleName("PTT");
    setAccessibleDescription("Off. The radio is not connected.");
    setFixedSize(112, 28);
  }

  QSize sizeHint() const override { return QSize(112, 28); }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool onAir = isChecked();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(onAir ? QColor("#ffc48a") : QColor("#6a4a2a"), 1));
    painter.setBrush(onAir ? QColor("#e08a3d") : QColor("#14120f"));
    painter.drawRoundedRect(box, 13, 13);

    const QPointF lamp(16.0, height() / 2.0);
    if (onAir) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(255, 196, 138, 90));
      painter.drawEllipse(lamp, 9.0, 9.0);
    }
    painter.setPen(QPen(onAir ? QColor("#fff1df") : QColor("#5a4030"), 1));
    painter.setBrush(onAir ? QColor("#ffd7a8") : QColor("#2a211c"));
    painter.drawEllipse(lamp, 4.5, 4.5);

    painter.setFont(monoFont(11));
    painter.setPen(onAir ? QColor("#2a1404") : QColor("#e7c7a4"));
    painter.drawText(rect().adjusted(28, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     onAir ? QStringLiteral("ON AIR") : QStringLiteral("PTT"));
  }
};

QWidget* readout(const QString& label, const QString& valueName, const QString& value) {
  auto* group = new QWidget;
  auto* layout = new QHBoxLayout(group);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  auto* name = new QLabel(label);
  name->setObjectName("readoutLabel");
  QFont small("Inter");
  small.setPixelSize(10);
  small.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
  name->setFont(small);

  auto* reading = new QLabel(value);
  reading->setObjectName(valueName);
  reading->setFont(monoFont(13));

  layout->addWidget(name);
  layout->addWidget(reading);
  return group;
}

QFrame* separator() {
  auto* line = new QFrame;
  line->setObjectName("statusSep");
  line->setFrameShape(QFrame::NoFrame);
  line->setFixedWidth(1);
  line->setMinimumHeight(18);
  return line;
}

}  // namespace

StatusStrip::StatusStrip(QWidget* parent) : QWidget(parent) {
  setObjectName("statusStrip");
  setAttribute(Qt::WA_StyledBackground, true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(40);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(12, 0, 12, 0);
  layout->setSpacing(12);

  layout->addWidget(readout("MODE", "modeValue", QString::fromUtf8("—")));
  layout->addWidget(separator());
  layout->addWidget(readout("AFC", "afcValue", "Off"));
  layout->addWidget(separator());
  layout->addWidget(readout("SQUELCH", "squelchValue", QString::fromUtf8("—")));
  layout->addWidget(separator());
  layout->addWidget(new PttLamp(this));
  layout->addStretch(1);

  auto* dot = new QLabel(QString::fromUtf8("●"));
  dot->setObjectName("radioDot");
  dot->setFont(monoFont(10));

  auto* radio = new QLabel("Radio not connected");
  radio->setObjectName("radioState");
  radio->setFont(monoFont(12));

  layout->addWidget(dot);
  layout->addWidget(radio);
}
