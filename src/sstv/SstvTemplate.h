// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QRectF>
#include <QString>

#include <vector>

// An SSTV picture template: a background with layers drawn over it. Layer
// positions are fractions of the frame (0 to 1), so one template fits every
// mode. Text may hold fill-in fields such as {MYCALL} and {RST}, filled when
// the picture is built. Templates are small JSON files in the Templates
// folder, so they can be copied and shared.

struct SstvLayer {
  enum class Kind { Text, RxPicture, Box, Photo };
  Kind kind = Kind::Text;
  // Where the layer sits, as fractions of the frame width and height.
  QRectF rect{0.05, 0.05, 0.9, 0.2};
  // Text
  QString text;
  QString font = QStringLiteral("Arial");
  bool bold = true;
  bool italic = false;
  // Letter height as a fraction of the frame height.
  double size = 0.14;
  QColor fill = Qt::white;
  QColor outline = Qt::black;
  // Outline width as a fraction of the frame height (0 for none).
  double outlineWidth = 0.012;
  bool shadow = true;
  // Letters upright, one under another, down the box (for a vertical call
  // that reads without tilting your head).
  bool stacked = false;
  Qt::Alignment align = Qt::AlignCenter;
  // Box and the RX picture's frame
  QColor color{0, 0, 0, 140};
  // RX picture and photo: draw a border of the outline colour around it.
  bool border = true;
  // Photo: a picture from the Images folder (file name).
  QString image;
  // Turn, in degrees, about the box's centre (positive is clockwise). Text
  // at 90 or -90 runs along a tall, narrow box.
  double angle = 0.0;
};

struct SstvTemplate {
  QString name;
  // CQ, Reply, 73, or Custom.
  QString category = QStringLiteral("Custom");
  // A picture from the Images folder (file name), or empty for a plain colour.
  QString background;
  QColor backgroundColor{20, 40, 90};
  std::vector<SstvLayer> layers;

  QJsonObject toJson() const;
  static SstvTemplate fromJson(const QJsonObject& json);
};

// What the fill-in fields are filled with.
using SstvFields = QMap<QString, QString>;

// The fields a template can use, in the order they are offered, with a hint.
const std::vector<std::pair<QString, QString>>& sstvFieldList();
QString sstvFillFields(const QString& text, const SstvFields& fields);

// Draws the template at the given size. The RX picture goes in any RX picture
// layer; with none, that layer shows a placeholder.
QImage sstvRenderTemplate(const SstvTemplate& tpl, const QSize& size, const SstvFields& fields,
                          const QImage& rxPicture = QImage(), bool placeholders = false);

// Reads a picture the right way up: phones often store a photo sideways with
// a flag saying how to turn it.
QImage sstvLoadPicture(const QString& path);

// A new template for one of your pictures, with the usual text for the
// category already placed (CQ, Reply, 73, or Custom).
SstvTemplate sstvTemplateFromPicture(const QString& name, const QString& category, const QString& pictureFile);

QString sstvTemplatesDirectory();
// Every template in the folder, by name. Writes the starter templates the
// first time the folder is empty.
std::vector<SstvTemplate> sstvLoadTemplates();
bool sstvSaveTemplate(const SstvTemplate& tpl);
bool sstvDeleteTemplate(const QString& name);
std::vector<SstvTemplate> sstvStarterTemplates();
