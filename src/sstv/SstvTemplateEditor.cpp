// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sstv/SstvTemplateEditor.h"

#include "sstv/SstvTxPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

// The preview: the template drawn at the 320 by 256 shape, with the selected
// layer outlined. Drag a layer to move it; drag its corner to resize it.
class SstvTemplateCanvas : public QWidget {
public:
  SstvTemplateCanvas(SstvTemplate* tpl, int* layer, QWidget* parent) : QWidget(parent), tpl_(tpl), layer_(layer) {
    setMinimumSize(400, 320);
    setMouseTracking(true);
    setToolTip(QStringLiteral(
        "Click a layer to select it. Drag it to move it, and drag the small square at its corner to resize it. "
        "This does not key."));
  }

  std::function<void()> onSelect;
  std::function<void()> onMoved;
  SstvFields fields;
  QImage rx;

protected:
  QRectF frame() const {
    QSizeF s(320, 256);
    s.scale(QSizeF(size()) - QSizeF(16, 16), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0), s);
  }

  QRectF layerRect(int i) const {
    const QRectF f = frame();
    const QRectF r = tpl_->layers[size_t(i)].rect;
    return QRectF(f.x() + r.x() * f.width(), f.y() + r.y() * f.height(), r.width() * f.width(),
                  r.height() * f.height());
  }

  // A point turned about the layer's centre, by the layer's angle (or back).
  QPointF turn(int i, const QPointF& pt, double sign) const {
    const QRectF r = layerRect(i);
    const double a = sign * tpl_->layers[size_t(i)].angle * 3.14159265358979323846 / 180.0;
    const QPointF d = pt - r.center();
    return r.center() + QPointF(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
  }

  // The corner square sits on the turned box's corner.
  QRectF handle(int i) const {
    const QPointF corner = turn(i, layerRect(i).bottomRight(), 1.0);
    return QRectF(corner.x() - 6, corner.y() - 6, 12, 12);
  }

  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), QColor(17, 18, 22));
    const QRectF f = frame();
    const QImage img = sstvRenderTemplate(*tpl_, f.size().toSize(), fields, rx, true);
    p.drawImage(f.topLeft(), img);
    if (*layer_ >= 0 && *layer_ < int(tpl_->layers.size())) {
      p.setPen(QPen(QColor(0, 229, 255), 1.5, Qt::DashLine));
      p.setBrush(Qt::NoBrush);
      const QRectF r = layerRect(*layer_);
      const double angle = tpl_->layers[size_t(*layer_)].angle;
      p.save();
      p.translate(r.center());
      p.rotate(angle);
      p.drawRect(QRectF(-r.width() / 2.0, -r.height() / 2.0, r.width(), r.height()));
      p.restore();
      p.fillRect(handle(*layer_), QColor(0, 229, 255));
    }
  }

  void mousePressEvent(QMouseEvent* e) override {
    const QPointF at = e->position();
    if (*layer_ >= 0 && *layer_ < int(tpl_->layers.size()) && handle(*layer_).contains(at)) {
      resizing_ = true;
    } else {
      int hit = -1;
      for (int i = int(tpl_->layers.size()) - 1; i >= 0; --i) {
        // Hit-test in the layer's own, unturned frame.
        if (layerRect(i).contains(turn(i, at, -1.0))) {
          hit = i;
          break;
        }
      }
      *layer_ = hit;
      resizing_ = false;
      if (onSelect)
        onSelect();
    }
    dragFrom_ = at;
    if (*layer_ >= 0)
      startRect_ = tpl_->layers[size_t(*layer_)].rect;
    update();
  }

  void mouseMoveEvent(QMouseEvent* e) override {
    if (!(e->buttons() & Qt::LeftButton) || *layer_ < 0 || *layer_ >= int(tpl_->layers.size()))
      return;
    const QRectF f = frame();
    const double dx = (e->position().x() - dragFrom_.x()) / f.width();
    const double dy = (e->position().y() - dragFrom_.y()) / f.height();
    QRectF r = startRect_;
    if (resizing_) {
      // Resize along the layer's own sides, so a turned box grows the way
      // its corner is dragged.
      const double a = -tpl_->layers[size_t(*layer_)].angle * 3.14159265358979323846 / 180.0;
      const double px = (e->position().x() - dragFrom_.x());
      const double py = (e->position().y() - dragFrom_.y());
      const double lx = (px * std::cos(a) - py * std::sin(a)) / f.width();
      const double ly = (px * std::sin(a) + py * std::cos(a)) / f.height();
      r.setWidth(std::clamp(startRect_.width() + lx, 0.03, 1.2));
      r.setHeight(std::clamp(startRect_.height() + ly, 0.03, 1.2));
    } else {
      r.moveTo(std::clamp(startRect_.x() + dx, -0.5 * r.width(), 1.0 - 0.5 * r.width()),
               std::clamp(startRect_.y() + dy, -0.5 * r.height(), 1.0 - 0.5 * r.height()));
    }
    tpl_->layers[size_t(*layer_)].rect = r;
    update();
    if (onMoved)
      onMoved();
  }

private:
  SstvTemplate* tpl_;
  int* layer_;
  QPointF dragFrom_;
  QRectF startRect_;
  bool resizing_ = false;
};

namespace {

void paintSwatch(QPushButton* b, const QColor& c) {
  b->setProperty("color", c);
  b->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid #3a4352; "
                                  "border-radius: 3px; padding: 2px 10px; }")
                       .arg(c.name(QColor::HexRgb), c.lightness() > 128 ? QStringLiteral("#000")
                                                                        : QStringLiteral("#fff")));
}

QColor swatch(const QPushButton* b) {
  return b->property("color").value<QColor>();
}

QString layerLabel(const SstvLayer& l) {
  switch (l.kind) {
    case SstvLayer::Kind::RxPicture:
      return QStringLiteral("Their picture");
    case SstvLayer::Kind::Box:
      return QStringLiteral("Box");
    case SstvLayer::Kind::Photo:
      return QStringLiteral("Photo: %1").arg(l.image.isEmpty() ? QStringLiteral("(none)") : l.image);
    default: {
      QString t = l.text;
      t.replace(QLatin1Char('\n'), QLatin1Char(' '));
      return QStringLiteral("Text: %1").arg(t.isEmpty() ? QStringLiteral("(empty)") : t.left(30));
    }
  }
}

}  // namespace

SstvTemplateEditor::SstvTemplateEditor(const SstvFields& fields, const QImage& rxPicture, const QString& select,
                                       QWidget* parent)
    : QDialog(parent), fields_(fields), rx_(rxPicture) {
  // Sample values for anything still empty, so the preview shows the layout.
  const std::pair<const char*, const char*> samples[] = {
      {"{MYCALL}", "N0CALL"}, {"{MYGRID}", "EM79"}, {"{MYNAME}", "Pat"},  {"{CALL}", "W1AW"},
      {"{RST}", "595"},       {"{NAME}", "Joe"},    {"{BAND}", "20m"},    {"{FREQ}", "14.230"},
      {"{MODE}", "Martin M1"}, {"{DATE}", "2026-01-01"}, {"{UTC}", "12:00Z"}};
  for (const auto& s : samples) {
    const QString key = QLatin1String(s.first);
    if (fields_.value(key).trimmed().isEmpty())
      fields_.insert(key, QLatin1String(s.second));
  }
  setObjectName(QStringLiteral("sstvTemplateEditor"));
  setWindowTitle(QStringLiteral("SSTV templates"));
  resize(1180, 720);
  auto* outer = new QHBoxLayout(this);

  // Left: the templates.
  auto* left = new QVBoxLayout();
  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("sstvTplList"));
  list_->setMinimumWidth(170);
  left->addWidget(new QLabel(QStringLiteral("Templates"), this));
  left->addWidget(list_, 1);
  auto* newBtn = new QPushButton(QStringLiteral("New"), this);
  auto* dupBtn = new QPushButton(QStringLiteral("Duplicate"), this);
  auto* delBtn = new QPushButton(QStringLiteral("Delete"), this);
  auto* starters = new QPushButton(QStringLiteral("Add starters"), this);
  starters->setToolTip(QStringLiteral("Add the CQ, Reply and 73 starter templates again (existing ones are kept)."));
  for (auto* b : {newBtn, dupBtn, delBtn, starters})
    left->addWidget(b);
  outer->addLayout(left);

  // Middle: name, background, the preview, and the layers.
  auto* mid = new QVBoxLayout();
  auto* head = new QHBoxLayout();
  name_ = new QLineEdit(this);
  name_->setObjectName(QStringLiteral("sstvTplName"));
  category_ = new QComboBox(this);
  category_->addItems({QStringLiteral("CQ"), QStringLiteral("Reply"), QStringLiteral("73"), QStringLiteral("Custom")});
  background_ = new QComboBox(this);
  background_->setObjectName(QStringLiteral("sstvTplBackground"));
  background_->setToolTip(QStringLiteral("A picture from the Images folder behind everything, or a plain colour."));
  bgColor_ = new QPushButton(QStringLiteral("Colour"), this);
  head->addWidget(new QLabel(QStringLiteral("Name"), this));
  head->addWidget(name_, 1);
  head->addWidget(category_);
  head->addWidget(new QLabel(QStringLiteral("Background"), this));
  head->addWidget(background_, 1);
  head->addWidget(bgColor_);
  mid->addLayout(head);
  canvas_ = new SstvTemplateCanvas(&current_, &layer_, this);
  canvas_->setObjectName(QStringLiteral("sstvTplCanvas"));
  canvas_->fields = fields_;
  canvas_->rx = rx_;
  mid->addWidget(canvas_, 1);
  auto* note = new QLabel(QStringLiteral(
                              "The preview fills the fields with sample values; when you send, they come from "
                              "Options → User and the Call, RST and Name boxes. Positions follow the frame, so the "
                              "template fits every mode."),
                          this);
  note->setWordWrap(true);
  mid->addWidget(note);
  outer->addLayout(mid, 1);

  // Right: layers and the selected layer's settings.
  auto* right = new QVBoxLayout();
  right->addWidget(new QLabel(QStringLiteral("Layers (top is drawn last)"), this));
  layers_ = new QListWidget(this);
  layers_->setObjectName(QStringLiteral("sstvTplLayers"));
  layers_->setMaximumHeight(160);
  right->addWidget(layers_);
  auto* layerBtns = new QHBoxLayout();
  auto* addText = new QPushButton(QStringLiteral("+ Text"), this);
  addText->setObjectName(QStringLiteral("sstvTplAddText"));
  auto* addRx = new QPushButton(QStringLiteral("+ Their picture"), this);
  auto* addBox = new QPushButton(QStringLiteral("+ Box"), this);
  auto* addPhoto = new QPushButton(QStringLiteral("+ Photo"), this);
  addPhoto->setToolTip(QStringLiteral("One of your pictures as a layer, for example your shack or station photo."));
  for (auto* b : {addText, addRx, addBox, addPhoto})
    layerBtns->addWidget(b);
  right->addLayout(layerBtns);
  auto* orderBtns = new QHBoxLayout();
  auto* up = new QPushButton(QStringLiteral("Forward"), this);
  auto* down = new QPushButton(QStringLiteral("Back"), this);
  auto* remove = new QPushButton(QStringLiteral("Remove"), this);
  for (auto* b : {up, down, remove})
    orderBtns->addWidget(b);
  right->addLayout(orderBtns);

  props_ = new QStackedWidget(this);
  // Page 0: nothing selected.
  props_->addWidget(new QLabel(QStringLiteral("Click a layer in the preview or the list."), this));
  // Page 1: text.
  auto* textPage = new QWidget(this);
  auto* tf = new QFormLayout(textPage);
  text_ = new QPlainTextEdit(textPage);
  text_->setObjectName(QStringLiteral("sstvTplText"));
  text_->setMaximumHeight(70);
  field_ = new QComboBox(textPage);
  field_->addItem(QStringLiteral("Insert a field…"));
  for (const auto& f : sstvFieldList())
    field_->addItem(QStringLiteral("%1  %2").arg(f.first, f.second), f.first);
  font_ = new QFontComboBox(textPage);
  bold_ = new QCheckBox(QStringLiteral("Bold"), textPage);
  italic_ = new QCheckBox(QStringLiteral("Italic"), textPage);
  auto* style = new QHBoxLayout();
  style->addWidget(bold_);
  style->addWidget(italic_);
  stacked_ = new QCheckBox(QStringLiteral("Stacked"), textPage);
  stacked_->setObjectName(QStringLiteral("sstvTplStacked"));
  stacked_->setToolTip(QStringLiteral(
      "Letters upright, one under another, down the box: a vertical call that reads without tilting your head. "
      "Make the box tall and narrow. A space leaves a gap."));
  style->addWidget(stacked_);
  size_ = new QDoubleSpinBox(textPage);
  size_->setRange(2.0, 80.0);
  size_->setSuffix(QStringLiteral(" % of height"));
  size_->setDecimals(1);
  fill_ = new QPushButton(QStringLiteral("Fill"), textPage);
  outline_ = new QPushButton(QStringLiteral("Outline"), textPage);
  outlineWidth_ = new QDoubleSpinBox(textPage);
  outlineWidth_->setRange(0.0, 5.0);
  outlineWidth_->setDecimals(1);
  outlineWidth_->setSingleStep(0.2);
  outlineWidth_->setSuffix(QStringLiteral(" % of height"));
  shadow_ = new QCheckBox(QStringLiteral("Shadow"), textPage);
  align_ = new QComboBox(textPage);
  align_->addItems({QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
  auto* colors = new QHBoxLayout();
  colors->addWidget(fill_);
  colors->addWidget(outline_);
  tf->addRow(QStringLiteral("Text"), text_);
  tf->addRow(QString(), field_);
  tf->addRow(QStringLiteral("Font"), font_);
  tf->addRow(QString(), style);
  tf->addRow(QStringLiteral("Size"), size_);
  tf->addRow(QStringLiteral("Colours"), colors);
  tf->addRow(QStringLiteral("Outline"), outlineWidth_);
  tf->addRow(QString(), shadow_);
  tf->addRow(QStringLiteral("Align"), align_);
  props_->addWidget(textPage);
  // Page 2: box or their picture.
  auto* boxPage = new QWidget(this);
  auto* bf = new QFormLayout(boxPage);
  boxColor_ = new QPushButton(QStringLiteral("Colour"), boxPage);
  boxOpacity_ = new QDoubleSpinBox(boxPage);
  boxOpacity_->setRange(0, 100);
  boxOpacity_->setSuffix(QStringLiteral(" %"));
  border_ = new QCheckBox(QStringLiteral("Border"), boxPage);
  borderColor_ = new QPushButton(QStringLiteral("Border colour"), boxPage);
  photo_ = new QComboBox(boxPage);
  photo_->setObjectName(QStringLiteral("sstvTplPhoto"));
  bf->addRow(QStringLiteral("Box colour"), boxColor_);
  bf->addRow(QStringLiteral("Opacity"), boxOpacity_);
  bf->addRow(QString(), border_);
  bf->addRow(QString(), borderColor_);
  bf->addRow(QStringLiteral("Picture"), photo_);
  props_->addWidget(boxPage);
  right->addWidget(props_, 1);
  auto* angleRow = new QHBoxLayout();
  angle_ = new QDoubleSpinBox(this);
  angle_->setObjectName(QStringLiteral("sstvTplAngle"));
  angle_->setRange(-180.0, 180.0);
  angle_->setDecimals(0);
  angle_->setSingleStep(5.0);
  angle_->setSuffix(QStringLiteral(" °"));
  angle_->setToolTip(QStringLiteral(
      "Turn the selected layer about its centre: positive is clockwise. Text at 90 or -90 runs up or down a tall, "
      "narrow box; an angle between gives a slanted banner."));
  angleRow->addWidget(new QLabel(QStringLiteral("Angle"), this));
  angleRow->addWidget(angle_, 1);
  for (const int preset : {0, 90, -90}) {
    auto* b = new QPushButton(preset == 0 ? QStringLiteral("Level") : QStringLiteral("%1°").arg(preset), this);
    b->setToolTip(preset == 0 ? QStringLiteral("Straight across.")
                              : preset > 0 ? QStringLiteral("Vertical, reading downward.")
                                           : QStringLiteral("Vertical, reading upward."));
    angleRow->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, preset] { angle_->setValue(preset); });
  }
  right->addLayout(angleRow);
  connect(angle_, &QDoubleSpinBox::valueChanged, this, [this] { layerChanged(); });

  auto* bottom = new QHBoxLayout();
  save_ = new QPushButton(QStringLiteral("Save"), this);
  save_->setObjectName(QStringLiteral("sstvTplSave"));
  auto* close = new QPushButton(QStringLiteral("Close"), this);
  bottom->addStretch(1);
  bottom->addWidget(save_);
  bottom->addWidget(close);
  right->addLayout(bottom);
  outer->addLayout(right);

  // Backgrounds: plain colour, or a picture from the Images folder.
  background_->addItem(QStringLiteral("Plain colour"), QString());
  photo_->addItem(QStringLiteral("(pick a picture)"), QString());
  for (const QFileInfo& info : QDir(sstvImagesDirectory())
                                   .entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                                   QStringLiteral("*.jpeg"), QStringLiteral("*.bmp")},
                                                  QDir::Files, QDir::Name)) {
    background_->addItem(info.fileName(), info.fileName());
    photo_->addItem(info.fileName(), info.fileName());
  }

  auto pickColor = [this](QPushButton* b, bool alpha) {
    const QColor c = QColorDialog::getColor(swatch(b), this, QStringLiteral("Colour"),
                                            alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
    if (!c.isValid())
      return false;
    paintSwatch(b, c);
    return true;
  };

  canvas_->onSelect = [this] {
    const QSignalBlocker block(layers_);
    layers_->setCurrentRow(layer_);
    showLayer();
  };
  canvas_->onMoved = [this] { setDirty(true); };

  connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (loading_)
      return;
    if (!confirmLeave()) {
      const QSignalBlocker block(list_);
      for (int i = 0; i < int(templates_.size()); ++i)
        if (templates_[size_t(i)].name == current_.name)
          list_->setCurrentRow(i);
      return;
    }
    open(row);
  });
  connect(name_, &QLineEdit::textEdited, this, [this](const QString& t) {
    current_.name = t.trimmed();
    setDirty(true);
  });
  connect(category_, &QComboBox::currentTextChanged, this, [this](const QString& t) {
    if (loading_)
      return;
    current_.category = t;
    setDirty(true);
  });
  connect(background_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (loading_)
      return;
    current_.background = background_->currentData().toString();
    setDirty(true);
    canvas_->update();
  });
  connect(bgColor_, &QPushButton::clicked, this, [this, pickColor] {
    if (!pickColor(bgColor_, false))
      return;
    current_.backgroundColor = swatch(bgColor_);
    setDirty(true);
    canvas_->update();
  });
  connect(layers_, &QListWidget::currentRowChanged, this, [this](int row) {
    layer_ = row;
    showLayer();
    canvas_->update();
  });
  auto addLayer = [this](SstvLayer l) {
    current_.layers.push_back(l);
    layer_ = int(current_.layers.size()) - 1;
    showLayers();
    setDirty(true);
  };
  connect(addText, &QPushButton::clicked, this, [addLayer] {
    SstvLayer l;
    l.text = QStringLiteral("{MYCALL}");
    l.rect = QRectF(0.1, 0.4, 0.8, 0.2);
    addLayer(l);
  });
  connect(addRx, &QPushButton::clicked, this, [addLayer] {
    SstvLayer l;
    l.kind = SstvLayer::Kind::RxPicture;
    l.rect = QRectF(0.05, 0.2, 0.5, 0.6);
    l.outline = Qt::white;
    addLayer(l);
  });
  connect(addBox, &QPushButton::clicked, this, [addLayer] {
    SstvLayer l;
    l.kind = SstvLayer::Kind::Box;
    l.rect = QRectF(0.0, 0.0, 1.0, 0.2);
    l.color = QColor(0, 0, 0, 140);
    addLayer(l);
  });
  connect(addPhoto, &QPushButton::clicked, this, [addLayer] {
    SstvLayer l;
    l.kind = SstvLayer::Kind::Photo;
    l.rect = QRectF(0.58, 0.05, 0.38, 0.38);
    l.outline = Qt::white;
    addLayer(l);
  });
  connect(up, &QPushButton::clicked, this, [this] {
    if (layer_ < 0 || layer_ + 1 >= int(current_.layers.size()))
      return;
    std::swap(current_.layers[size_t(layer_)], current_.layers[size_t(layer_ + 1)]);
    ++layer_;
    showLayers();
    setDirty(true);
  });
  connect(down, &QPushButton::clicked, this, [this] {
    if (layer_ <= 0 || layer_ >= int(current_.layers.size()))
      return;
    std::swap(current_.layers[size_t(layer_)], current_.layers[size_t(layer_ - 1)]);
    --layer_;
    showLayers();
    setDirty(true);
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    if (layer_ < 0 || layer_ >= int(current_.layers.size()))
      return;
    current_.layers.erase(current_.layers.begin() + layer_);
    layer_ = std::min(layer_, int(current_.layers.size()) - 1);
    showLayers();
    setDirty(true);
  });

  for (QWidget* w : std::initializer_list<QWidget*>{font_, size_, outlineWidth_, align_, boxOpacity_}) {
    if (auto* spin = qobject_cast<QDoubleSpinBox*>(w))
      connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { layerChanged(); });
    else if (auto* combo = qobject_cast<QComboBox*>(w))
      connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { layerChanged(); });
  }
  connect(text_, &QPlainTextEdit::textChanged, this, [this] { layerChanged(); });
  for (QCheckBox* c : {bold_, italic_, stacked_, shadow_, border_})
    connect(c, &QCheckBox::toggled, this, [this] { layerChanged(); });
  for (QPushButton* b : {fill_, outline_, borderColor_}) {
    connect(b, &QPushButton::clicked, this, [this, b, pickColor] {
      if (pickColor(b, false))
        layerChanged();
    });
  }
  connect(photo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { layerChanged(); });
  connect(boxColor_, &QPushButton::clicked, this, [this, pickColor] {
    if (pickColor(boxColor_, false))
      layerChanged();
  });
  connect(field_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
    if (i <= 0)
      return;
    text_->insertPlainText(field_->itemData(i).toString());
    const QSignalBlocker block(field_);
    field_->setCurrentIndex(0);
  });

  connect(newBtn, &QPushButton::clicked, this, [this] {
    if (!confirmLeave())
      return;
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("New template"), QStringLiteral("Name:"), QLineEdit::Normal,
                              QStringLiteral("My template"), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
      return;
    SstvTemplate t;
    t.name = name;
    SstvLayer l;
    l.text = QStringLiteral("{MYCALL}");
    l.rect = QRectF(0.05, 0.35, 0.9, 0.3);
    l.size = 0.25;
    t.layers.push_back(l);
    sstvSaveTemplate(t);
    dirty_ = false;
    loadList(name);
  });
  connect(dupBtn, &QPushButton::clicked, this, [this] {
    if (current_.name.isEmpty())
      return;
    SstvTemplate t = current_;
    t.name = current_.name + QStringLiteral(" copy");
    sstvSaveTemplate(t);
    dirty_ = false;
    loadList(t.name);
  });
  connect(delBtn, &QPushButton::clicked, this, [this] {
    if (current_.name.isEmpty())
      return;
    if (QMessageBox::question(this, QStringLiteral("Delete template"),
                              QStringLiteral("Delete the template %1?").arg(current_.name)) != QMessageBox::Yes)
      return;
    sstvDeleteTemplate(current_.name);
    dirty_ = false;
    loadList(QString());
  });
  connect(starters, &QPushButton::clicked, this, [this] {
    for (const SstvTemplate& t : sstvStarterTemplates()) {
      bool have = false;
      for (const SstvTemplate& mine : templates_)
        have = have || mine.name.compare(t.name, Qt::CaseInsensitive) == 0;
      if (!have)
        sstvSaveTemplate(t);
    }
    loadList(current_.name);
  });
  connect(save_, &QPushButton::clicked, this, [this] { saveCurrent(); });
  connect(close, &QPushButton::clicked, this, [this] {
    if (confirmLeave())
      accept();
  });

  loadList(select);
}

void SstvTemplateEditor::setDirty(bool dirty) {
  dirty_ = dirty;
  save_->setText(dirty ? QStringLiteral("Save *") : QStringLiteral("Save"));
}

bool SstvTemplateEditor::confirmLeave() {
  if (!dirty_)
    return true;
  const auto answer = QMessageBox::question(
      this, QStringLiteral("SSTV templates"), QStringLiteral("Save the changes to %1?").arg(current_.name),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (answer == QMessageBox::Cancel)
    return false;
  if (answer == QMessageBox::Save)
    saveCurrent();
  setDirty(false);
  return true;
}

void SstvTemplateEditor::saveCurrent() {
  if (current_.name.isEmpty()) {
    QMessageBox::information(this, QStringLiteral("SSTV templates"), QStringLiteral("Give the template a name first."));
    return;
  }
  // A rename leaves the old file behind; remove it.
  const int row = list_->currentRow();
  if (row >= 0 && row < int(templates_.size()) && templates_[size_t(row)].name != current_.name)
    sstvDeleteTemplate(templates_[size_t(row)].name);
  sstvSaveTemplate(current_);
  setDirty(false);
  loadList(current_.name);
}

void SstvTemplateEditor::loadList(const QString& select) {
  loading_ = true;
  templates_ = sstvLoadTemplates();
  list_->clear();
  int at = templates_.empty() ? -1 : 0;
  for (int i = 0; i < int(templates_.size()); ++i) {
    const SstvTemplate& t = templates_[size_t(i)];
    const QImage thumb = sstvRenderTemplate(t, QSize(96, 77), fields_, rx_, true);
    list_->addItem(new QListWidgetItem(QIcon(QPixmap::fromImage(thumb)),
                                       QStringLiteral("%1\n%2").arg(t.name, t.category)));
    if (t.name.compare(select, Qt::CaseInsensitive) == 0)
      at = i;
  }
  list_->setIconSize(QSize(96, 77));
  loading_ = false;
  const QSignalBlocker block(list_);
  list_->setCurrentRow(at);
  open(at);
}

void SstvTemplateEditor::open(int row) {
  loading_ = true;
  current_ = (row >= 0 && row < int(templates_.size())) ? templates_[size_t(row)] : SstvTemplate();
  layer_ = current_.layers.empty() ? -1 : 0;
  name_->setText(current_.name);
  category_->setCurrentText(current_.category);
  const int bg = background_->findData(current_.background);
  background_->setCurrentIndex(bg < 0 ? 0 : bg);
  paintSwatch(bgColor_, current_.backgroundColor);
  loading_ = false;
  setDirty(false);
  showLayers();
}

void SstvTemplateEditor::showLayers() {
  const QSignalBlocker block(layers_);
  layers_->clear();
  // Listed top first, the way they stack.
  for (const SstvLayer& l : current_.layers)
    layers_->addItem(layerLabel(l));
  layers_->setCurrentRow(layer_);
  showLayer();
  canvas_->update();
}

void SstvTemplateEditor::showLayer() {
  loading_ = true;
  if (layer_ < 0 || layer_ >= int(current_.layers.size())) {
    props_->setCurrentIndex(0);
    angle_->setEnabled(false);
    loading_ = false;
    return;
  }
  const SstvLayer& l = current_.layers[size_t(layer_)];
  angle_->setEnabled(true);
  angle_->setValue(l.angle);
  if (l.kind == SstvLayer::Kind::Text) {
    props_->setCurrentIndex(1);
    text_->setPlainText(l.text);
    font_->setCurrentFont(QFont(l.font));
    bold_->setChecked(l.bold);
    italic_->setChecked(l.italic);
    stacked_->setChecked(l.stacked);
    size_->setValue(l.size * 100.0);
    paintSwatch(fill_, l.fill);
    paintSwatch(outline_, l.outline);
    outlineWidth_->setValue(l.outlineWidth * 100.0);
    shadow_->setChecked(l.shadow);
    align_->setCurrentIndex((l.align & Qt::AlignLeft) ? 0 : (l.align & Qt::AlignRight) ? 2 : 1);
  } else {
    props_->setCurrentIndex(2);
    const bool box = l.kind == SstvLayer::Kind::Box;
    boxColor_->setEnabled(box);
    boxOpacity_->setEnabled(box);
    paintSwatch(boxColor_, QColor(l.color.red(), l.color.green(), l.color.blue()));
    boxOpacity_->setValue(l.color.alphaF() * 100.0);
    border_->setEnabled(!box);
    borderColor_->setEnabled(!box);
    border_->setChecked(l.border);
    paintSwatch(borderColor_, l.outline);
    photo_->setEnabled(l.kind == SstvLayer::Kind::Photo);
    const int at = photo_->findData(l.image);
    photo_->setCurrentIndex(at < 0 ? 0 : at);
  }
  loading_ = false;
}

void SstvTemplateEditor::layerChanged() {
  if (loading_ || layer_ < 0 || layer_ >= int(current_.layers.size()))
    return;
  SstvLayer& l = current_.layers[size_t(layer_)];
  l.angle = angle_->value();
  if (l.kind == SstvLayer::Kind::Text) {
    l.text = text_->toPlainText();
    l.font = font_->currentFont().family();
    l.bold = bold_->isChecked();
    l.italic = italic_->isChecked();
    l.stacked = stacked_->isChecked();
    l.size = size_->value() / 100.0;
    l.fill = swatch(fill_);
    l.outline = swatch(outline_);
    l.outlineWidth = outlineWidth_->value() / 100.0;
    l.shadow = shadow_->isChecked();
    const int a = align_->currentIndex();
    l.align = a == 0 ? (Qt::AlignLeft | Qt::AlignVCenter) : a == 2 ? (Qt::AlignRight | Qt::AlignVCenter)
                                                                  : Qt::Alignment(Qt::AlignCenter);
    if (auto* item = layers_->item(layer_))
      item->setText(layerLabel(l));
  } else if (l.kind == SstvLayer::Kind::Box) {
    QColor c = swatch(boxColor_);
    c.setAlphaF(boxOpacity_->value() / 100.0);
    l.color = c;
  } else {
    l.border = border_->isChecked();
    l.outline = swatch(borderColor_);
    if (l.kind == SstvLayer::Kind::Photo) {
      l.image = photo_->currentData().toString();
      if (auto* item = layers_->item(layer_))
        item->setText(layerLabel(l));
    }
  }
  setDirty(true);
  canvas_->update();
}
