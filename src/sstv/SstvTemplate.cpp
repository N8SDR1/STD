// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sstv/SstvTemplate.h"

#include "sstv/SstvTxPanel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>

namespace {

QString colorText(const QColor& c) {
  return c.name(QColor::HexArgb);
}

QColor colorFrom(const QJsonValue& v, const QColor& fallback) {
  const QColor c(v.toString());
  return c.isValid() ? c : fallback;
}

QString alignText(Qt::Alignment a) {
  if (a & Qt::AlignLeft)
    return QStringLiteral("left");
  if (a & Qt::AlignRight)
    return QStringLiteral("right");
  return QStringLiteral("center");
}

Qt::Alignment alignFrom(const QString& s) {
  if (s == QLatin1String("left"))
    return Qt::AlignLeft | Qt::AlignVCenter;
  if (s == QLatin1String("right"))
    return Qt::AlignRight | Qt::AlignVCenter;
  return Qt::AlignCenter;
}

QString kindText(SstvLayer::Kind k) {
  switch (k) {
    case SstvLayer::Kind::RxPicture:
      return QStringLiteral("rx");
    case SstvLayer::Kind::Box:
      return QStringLiteral("box");
    default:
      return QStringLiteral("text");
  }
}

SstvLayer::Kind kindFrom(const QString& s) {
  if (s == QLatin1String("rx"))
    return SstvLayer::Kind::RxPicture;
  if (s == QLatin1String("box"))
    return SstvLayer::Kind::Box;
  return SstvLayer::Kind::Text;
}

QString fileNameFor(const QString& name) {
  QString safe = name.trimmed();
  safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
  return safe.isEmpty() ? QStringLiteral("template") : safe;
}

// Draw `picture` to fill `target`, cropping what does not fit.
void drawFilled(QPainter& p, const QImage& picture, const QRectF& target) {
  if (picture.isNull() || target.width() < 1 || target.height() < 1)
    return;
  const QSizeF src = picture.size();
  const double scale = std::max(target.width() / src.width(), target.height() / src.height());
  const QSizeF used(target.width() / scale, target.height() / scale);
  const QRectF from((src.width() - used.width()) / 2.0, (src.height() - used.height()) / 2.0, used.width(),
                    used.height());
  p.drawImage(target, picture, from);
}

void drawText(QPainter& p, const SstvLayer& layer, const QRectF& box, double frameHeight, const QString& text) {
  if (text.trimmed().isEmpty())
    return;
  QFont font(layer.font);
  font.setBold(layer.bold);
  font.setItalic(layer.italic);
  double px = std::max(4.0, layer.size * frameHeight);
  const QStringList lines = text.split(QLatin1Char('\n'));
  // Shrink to fit the box, so a long call or name never runs off the edge.
  for (int guard = 0; guard < 30; ++guard) {
    font.setPixelSize(int(px));
    const QFontMetricsF fm(font);
    double widest = 0.0;
    for (const QString& line : lines)
      widest = std::max(widest, fm.horizontalAdvance(line));
    const double tall = fm.lineSpacing() * lines.size();
    if ((widest <= box.width() && tall <= box.height() * 1.15) || px <= 6.0)
      break;
    px *= 0.92;
  }
  font.setPixelSize(int(px));
  const QFontMetricsF fm(font);
  const double lineHeight = fm.lineSpacing();
  const double total = lineHeight * lines.size();
  double y = box.top() + (box.height() - total) / 2.0 + fm.ascent();
  QPainterPath path;
  for (const QString& line : lines) {
    const double w = fm.horizontalAdvance(line);
    double x = box.left() + (box.width() - w) / 2.0;
    if (layer.align & Qt::AlignLeft)
      x = box.left();
    else if (layer.align & Qt::AlignRight)
      x = box.right() - w;
    path.addText(QPointF(x, y), font, line);
    y += lineHeight;
  }
  const double stroke = layer.outlineWidth * frameHeight;
  if (layer.shadow) {
    const double off = std::max(1.0, frameHeight * 0.008);
    QPainterPath shadow = path.translated(off, off);
    if (stroke > 0.0)
      p.strokePath(shadow, QPen(QColor(0, 0, 0, 170), stroke * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(shadow, QColor(0, 0, 0, 170));
  }
  // The outline is drawn first at twice its width, so the fill covers its
  // inner half and the letters keep their shape.
  if (stroke > 0.0 && layer.outline.alpha() > 0)
    p.strokePath(path, QPen(layer.outline, stroke * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.fillPath(path, layer.fill);
}

}  // namespace

QJsonObject SstvTemplate::toJson() const {
  QJsonObject o;
  o.insert(QStringLiteral("name"), name);
  o.insert(QStringLiteral("category"), category);
  o.insert(QStringLiteral("background"), background);
  o.insert(QStringLiteral("backgroundColor"), colorText(backgroundColor));
  QJsonArray list;
  for (const SstvLayer& l : layers) {
    QJsonObject j;
    j.insert(QStringLiteral("kind"), kindText(l.kind));
    j.insert(QStringLiteral("x"), l.rect.x());
    j.insert(QStringLiteral("y"), l.rect.y());
    j.insert(QStringLiteral("w"), l.rect.width());
    j.insert(QStringLiteral("h"), l.rect.height());
    if (l.kind == SstvLayer::Kind::Text) {
      j.insert(QStringLiteral("text"), l.text);
      j.insert(QStringLiteral("font"), l.font);
      j.insert(QStringLiteral("bold"), l.bold);
      j.insert(QStringLiteral("italic"), l.italic);
      j.insert(QStringLiteral("size"), l.size);
      j.insert(QStringLiteral("fill"), colorText(l.fill));
      j.insert(QStringLiteral("outline"), colorText(l.outline));
      j.insert(QStringLiteral("outlineWidth"), l.outlineWidth);
      j.insert(QStringLiteral("shadow"), l.shadow);
      j.insert(QStringLiteral("align"), alignText(l.align));
    } else {
      j.insert(QStringLiteral("color"), colorText(l.color));
      j.insert(QStringLiteral("outline"), colorText(l.outline));
      j.insert(QStringLiteral("border"), l.border);
    }
    list.append(j);
  }
  o.insert(QStringLiteral("layers"), list);
  return o;
}

SstvTemplate SstvTemplate::fromJson(const QJsonObject& o) {
  SstvTemplate t;
  t.name = o.value(QStringLiteral("name")).toString();
  t.category = o.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
  t.background = o.value(QStringLiteral("background")).toString();
  t.backgroundColor = colorFrom(o.value(QStringLiteral("backgroundColor")), t.backgroundColor);
  for (const QJsonValue& v : o.value(QStringLiteral("layers")).toArray()) {
    const QJsonObject j = v.toObject();
    SstvLayer l;
    l.kind = kindFrom(j.value(QStringLiteral("kind")).toString());
    l.rect = QRectF(j.value(QStringLiteral("x")).toDouble(0.05), j.value(QStringLiteral("y")).toDouble(0.05),
                    j.value(QStringLiteral("w")).toDouble(0.9), j.value(QStringLiteral("h")).toDouble(0.2));
    l.text = j.value(QStringLiteral("text")).toString();
    l.font = j.value(QStringLiteral("font")).toString(l.font);
    l.bold = j.value(QStringLiteral("bold")).toBool(l.bold);
    l.italic = j.value(QStringLiteral("italic")).toBool(l.italic);
    l.size = j.value(QStringLiteral("size")).toDouble(l.size);
    l.fill = colorFrom(j.value(QStringLiteral("fill")), l.fill);
    l.outline = colorFrom(j.value(QStringLiteral("outline")), l.outline);
    l.outlineWidth = j.value(QStringLiteral("outlineWidth")).toDouble(l.outlineWidth);
    l.shadow = j.value(QStringLiteral("shadow")).toBool(l.shadow);
    l.align = alignFrom(j.value(QStringLiteral("align")).toString());
    l.color = colorFrom(j.value(QStringLiteral("color")), l.color);
    l.border = j.value(QStringLiteral("border")).toBool(l.border);
    t.layers.push_back(l);
  }
  return t;
}

const std::vector<std::pair<QString, QString>>& sstvFieldList() {
  static const std::vector<std::pair<QString, QString>> list = {
      {QStringLiteral("{MYCALL}"), QStringLiteral("your callsign, from Options → User")},
      {QStringLiteral("{MYGRID}"), QStringLiteral("your locator")},
      {QStringLiteral("{MYNAME}"), QStringLiteral("your name")},
      {QStringLiteral("{CALL}"), QStringLiteral("their callsign")},
      {QStringLiteral("{RST}"), QStringLiteral("the report you give them")},
      {QStringLiteral("{NAME}"), QStringLiteral("their name")},
      {QStringLiteral("{DATE}"), QStringLiteral("UTC date")},
      {QStringLiteral("{UTC}"), QStringLiteral("UTC time")},
      {QStringLiteral("{BAND}"), QStringLiteral("band, such as 20m")},
      {QStringLiteral("{FREQ}"), QStringLiteral("dial frequency in MHz")},
      {QStringLiteral("{MODE}"), QStringLiteral("the SSTV mode being sent")},
  };
  return list;
}

QString sstvFillFields(const QString& text, const SstvFields& fields) {
  QString out = text;
  for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
    out.replace(it.key(), it.value(), Qt::CaseInsensitive);
  // A field with nothing to fill it prints as nothing, not as {FIELD}.
  for (const auto& f : sstvFieldList())
    out.remove(f.first, Qt::CaseInsensitive);
  return out.trimmed();
}

QImage sstvRenderTemplate(const SstvTemplate& tpl, const QSize& size, const SstvFields& fields,
                          const QImage& rxPicture, bool placeholders) {
  QImage out(size, QImage::Format_RGB32);
  out.fill(tpl.backgroundColor);
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  if (!tpl.background.isEmpty()) {
    const QImage bg(QDir(sstvImagesDirectory()).filePath(tpl.background));
    drawFilled(p, bg, QRectF(QPointF(0, 0), QSizeF(size)));
  }
  const double w = size.width();
  const double h = size.height();
  for (const SstvLayer& l : tpl.layers) {
    const QRectF box(l.rect.x() * w, l.rect.y() * h, l.rect.width() * w, l.rect.height() * h);
    switch (l.kind) {
      case SstvLayer::Kind::Box:
        p.fillRect(box, l.color);
        break;
      case SstvLayer::Kind::RxPicture:
        if (!rxPicture.isNull()) {
          drawFilled(p, rxPicture, box);
        } else if (placeholders) {
          p.fillRect(box, QColor(60, 60, 70));
          p.setPen(QColor(200, 200, 210));
          QFont f(QStringLiteral("Arial"));
          f.setPixelSize(std::max(8, int(h * 0.06)));
          p.setFont(f);
          p.drawText(box, Qt::AlignCenter, QStringLiteral("Their picture"));
        }
        if (l.border && (!rxPicture.isNull() || placeholders)) {
          p.setPen(QPen(l.outline, std::max(1.0, h * 0.008)));
          p.setBrush(Qt::NoBrush);
          p.drawRect(box);
        }
        break;
      case SstvLayer::Kind::Text:
        drawText(p, l, box, h, sstvFillFields(l.text, fields));
        break;
    }
  }
  return out;
}

QString sstvTemplatesDirectory() {
  QDir root(sstvImagesDirectory());
  root.cdUp();
  return root.filePath(QStringLiteral("Templates"));
}

std::vector<SstvTemplate> sstvStarterTemplates() {
  std::vector<SstvTemplate> list;
  auto text = [](const QString& t, QRectF r, double size, QColor fill) {
    SstvLayer l;
    l.kind = SstvLayer::Kind::Text;
    l.text = t;
    l.rect = r;
    l.size = size;
    l.fill = fill;
    return l;
  };
  {
    SstvTemplate t;
    t.name = QStringLiteral("CQ");
    t.category = QStringLiteral("CQ");
    t.backgroundColor = QColor(18, 42, 96);
    SstvLayer band;
    band.kind = SstvLayer::Kind::Box;
    band.rect = QRectF(0.0, 0.0, 1.0, 0.24);
    band.color = QColor(200, 30, 30, 230);
    t.layers.push_back(band);
    t.layers.push_back(text(QStringLiteral("CQ SSTV"), QRectF(0.02, 0.01, 0.96, 0.22), 0.18, QColor(255, 220, 40)));
    t.layers.push_back(text(QStringLiteral("{MYCALL}"), QRectF(0.03, 0.30, 0.94, 0.38), 0.30, Qt::white));
    t.layers.push_back(
        text(QStringLiteral("{MYGRID}   {BAND}"), QRectF(0.03, 0.74, 0.94, 0.2), 0.12, QColor(140, 220, 255)));
    list.push_back(t);
  }
  {
    SstvTemplate t;
    t.name = QStringLiteral("Reply");
    t.category = QStringLiteral("Reply");
    t.backgroundColor = QColor(12, 60, 40);
    t.layers.push_back(text(QStringLiteral("{CALL}"), QRectF(0.02, 0.01, 0.96, 0.18), 0.15, QColor(255, 220, 40)));
    SstvLayer rx;
    rx.kind = SstvLayer::Kind::RxPicture;
    rx.rect = QRectF(0.04, 0.22, 0.50, 0.58);
    rx.outline = Qt::white;
    t.layers.push_back(rx);
    t.layers.push_back(text(QStringLiteral("UR\n{RST}"), QRectF(0.57, 0.22, 0.40, 0.34), 0.13, Qt::white));
    t.layers.push_back(
        text(QStringLiteral("TNX FER\nPIC"), QRectF(0.57, 0.56, 0.40, 0.24), 0.08, QColor(140, 220, 255)));
    t.layers.push_back(text(QStringLiteral("DE {MYCALL}"), QRectF(0.02, 0.82, 0.96, 0.17), 0.13, Qt::white));
    list.push_back(t);
  }
  {
    SstvTemplate t;
    t.name = QStringLiteral("73");
    t.category = QStringLiteral("73");
    t.backgroundColor = QColor(70, 20, 90);
    t.layers.push_back(text(QStringLiteral("{CALL}"), QRectF(0.02, 0.02, 0.96, 0.2), 0.15, Qt::white));
    t.layers.push_back(text(QStringLiteral("73"), QRectF(0.02, 0.22, 0.96, 0.5), 0.45, QColor(255, 220, 40)));
    t.layers.push_back(text(QStringLiteral("DE {MYCALL}"), QRectF(0.02, 0.78, 0.96, 0.2), 0.13, Qt::white));
    list.push_back(t);
  }
  return list;
}

std::vector<SstvTemplate> sstvLoadTemplates() {
  const QDir dir(sstvTemplatesDirectory());
  if (!dir.exists() || dir.entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty()) {
    for (const SstvTemplate& t : sstvStarterTemplates())
      sstvSaveTemplate(t);
  }
  std::vector<SstvTemplate> list;
  for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
    QFile f(info.filePath());
    if (!f.open(QIODevice::ReadOnly))
      continue;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
      continue;
    SstvTemplate t = SstvTemplate::fromJson(doc.object());
    if (t.name.isEmpty())
      t.name = info.completeBaseName();
    list.push_back(t);
  }
  return list;
}

bool sstvSaveTemplate(const SstvTemplate& tpl) {
  if (!QDir().mkpath(sstvTemplatesDirectory()))
    return false;
  QFile f(QDir(sstvTemplatesDirectory()).filePath(fileNameFor(tpl.name) + QStringLiteral(".json")));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(QJsonDocument(tpl.toJson()).toJson(QJsonDocument::Indented));
  return true;
}

bool sstvDeleteTemplate(const QString& name) {
  return QFile::remove(QDir(sstvTemplatesDirectory()).filePath(fileNameFor(name) + QStringLiteral(".json")));
}
