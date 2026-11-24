// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sstv/SstvTxPanel.h"

#include "sstv/SstvTemplateEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QProgressBar>
#include <QToolButton>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kThumbWidth = 112;

QFont paneFont(int pixelSize) {
  QFont font("Inter");
  font.setPixelSize(pixelSize);
  return font;
}

QString sstvRoot() {
  // The RX folder's parent, so RX, TX and Images sit side by side.
  QDir rx(sstvRxDirectory());
  if (rx.dirName().compare(QLatin1String("RX"), Qt::CaseInsensitive) == 0 && rx.cdUp())
    return rx.absolutePath();
  const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  return QDir(documents).filePath(QStringLiteral("STD/SSTV"));
}

const QStringList kPictureFilters = {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                     QStringLiteral("*.bmp"), QStringLiteral("*.gif")};

QString seconds(double s) {
  const int total = int(s + 0.5);
  return total >= 60 ? QStringLiteral("%1 min %2 s").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1 s").arg(total);
}

}  // namespace

QString sstvImagesDirectory() {
  return QDir(sstvRoot()).filePath(QStringLiteral("Images"));
}

QString sstvTxDirectory() {
  return QDir(sstvRoot()).filePath(QStringLiteral("TX"));
}

SstvTxPanel::SstvTxPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("sstvTxPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 2, 0, 2);
  layout->setSpacing(6);

  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  auto* label = new QLabel(QStringLiteral("Send as"), this);
  label->setFont(paneFont(12));
  mode_ = new QComboBox(this);
  mode_->setObjectName(QStringLiteral("sstvTxMode"));
  mode_->setFont(paneFont(12));
  for (SstvMode m : sstvModes())
    mode_->addItem(sstvModeName(m), sstvModeName(m));
  {
    const QString saved =
        QSettings().value(QStringLiteral("sstv/txMode"), sstvModeName(SstvMode::MartinM1)).toString();
    const int at = mode_->findData(saved);
    mode_->setCurrentIndex(at < 0 ? 0 : at);
  }
  mode_->setToolTip(QStringLiteral(
      "The mode the picture is sent in. Martin M1 and Scottie S1 are the usual choice on 14.230; PD-120 "
      "gives a sharper picture in about two minutes; Robot 36 is the quickest. This does not key."));
  shape_ = new QComboBox(this);
  shape_->setObjectName(QStringLiteral("sstvTxShape"));
  shape_->setFont(paneFont(12));
  shape_->addItem(QStringLiteral("Crop"), QStringLiteral("crop"));
  shape_->addItem(QStringLiteral("Fit"), QStringLiteral("fit"));
  shape_->addItem(QStringLiteral("Stretch"), QStringLiteral("stretch"));
  {
    const int at = shape_->findData(QSettings().value(QStringLiteral("sstv/txShape"), QStringLiteral("crop")));
    shape_->setCurrentIndex(at < 0 ? 0 : at);
  }
  shape_->setToolTip(QStringLiteral(
      "How a picture that is not the mode's shape is made to fit. Crop fills the frame and cuts off the "
      "rest; drag the preview to choose what is kept, double-click to center it. Fit shows all of it with "
      "black bars. Stretch squeezes it to fit. This does not key."));
  auto* add = new QPushButton(QStringLiteral("Add…"), this);
  add->setObjectName(QStringLiteral("sstvTxAdd"));
  add->setFont(paneFont(11));
  add->setFocusPolicy(Qt::NoFocus);
  add->setToolTip(QStringLiteral(
      "Copy pictures into the Images folder so they are listed here. This does not key."));
  auto* saveAs = new QPushButton(QStringLiteral("Save as…"), this);
  saveAs->setObjectName(QStringLiteral("sstvTxSaveAs"));
  saveAs->setFont(paneFont(11));
  saveAs->setFocusPolicy(Qt::NoFocus);
  saveAs->setToolTip(QStringLiteral(
      "Save the picture as it is framed now (the crop, fit, or stretch, at the mode's shape) as a new "
      "picture in the Images folder, under a name you choose. Next time just click it. The original is "
      "kept. This does not key."));
  auto* open = new QPushButton(QStringLiteral("Open"), this);
  open->setObjectName(QStringLiteral("sstvTxOpen"));
  open->setFont(paneFont(11));
  open->setFocusPolicy(Qt::NoFocus);
  open->setToolTip(QStringLiteral("Open the Images folder. This does not key."));
  info_ = new QLabel(this);
  info_->setObjectName(QStringLiteral("sstvTxInfo"));
  info_->setFont(paneFont(12));
  info_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  row->addWidget(label);
  row->addWidget(mode_);
  row->addWidget(shape_);
  row->addWidget(add);
  row->addWidget(saveAs);
  row->addWidget(open);
  row->addWidget(info_, 1);
  layout->addLayout(row);

  auto* qso = new QHBoxLayout();
  qso->setSpacing(6);
  auto field = [this, qso](const QString& caption, const QString& name, const QString& tip, int width) {
    auto* l = new QLabel(caption, this);
    l->setFont(paneFont(12));
    auto* e = new QLineEdit(this);
    e->setObjectName(name);
    e->setFont(paneFont(12));
    e->setFixedWidth(width);
    e->setToolTip(tip);
    l->setToolTip(tip);
    qso->addWidget(l);
    qso->addWidget(e);
    return e;
  };
  call_ = field(QStringLiteral("Call"), QStringLiteral("sstvTxCall"),
                QStringLiteral("The station you are working. Fills {CALL} in a template. This does not key."), 110);
  rst_ = field(QStringLiteral("RST"), QStringLiteral("sstvTxRst"),
               QStringLiteral("The report you give them; SSTV uses RSV, where the last digit rates the picture "
                              "(595 is perfect). Fills {RST}. This does not key."),
               54);
  rstIn_ = field(QStringLiteral("RST rcvd"), QStringLiteral("sstvTxRstIn"),
                 QStringLiteral("The report they gave you, for the log. This does not key."), 54);
  name_ = field(QStringLiteral("Name"), QStringLiteral("sstvTxName"),
                QStringLiteral("Their name. Fills {NAME}. This does not key."), 110);
  rst_->setText(QSettings().value(QStringLiteral("sstv/rst"), QStringLiteral("595")).toString());
  auto* replyLabel = new QLabel(QStringLiteral("Their picture"), this);
  replyLabel->setFont(paneFont(12));
  replyThumb_ = new QLabel(this);
  replyThumb_->setObjectName(QStringLiteral("sstvTxReply"));
  replyThumb_->setFixedSize(60, 45);
  replyThumb_->setAlignment(Qt::AlignCenter);
  replyThumb_->setStyleSheet(QStringLiteral("background: #111216; border: 1px solid #3a4352; color: #8a94a6;"));
  replyThumb_->setText(QStringLiteral("none"));
  replyThumb_->setToolTip(QStringLiteral(
      "Their picture, for a reply. Click here to choose one from the RX folder, or in the receive pane pick a "
      "picture and press Reply with it (or right-click it). It goes in a template's their-picture box, or in "
      "a corner of one of your own pictures. This does not key."));
  replyThumb_->setCursor(Qt::PointingHandCursor);
  replyThumb_->installEventFilter(this);
  inset_ = new QComboBox(this);
  inset_->setObjectName(QStringLiteral("sstvTxInset"));
  inset_->setFont(paneFont(12));
  inset_->addItem(QStringLiteral("Inset off"), QString());
  inset_->addItem(QStringLiteral("Bottom left"), QStringLiteral("bl"));
  inset_->addItem(QStringLiteral("Bottom right"), QStringLiteral("br"));
  inset_->addItem(QStringLiteral("Top left"), QStringLiteral("tl"));
  inset_->addItem(QStringLiteral("Top right"), QStringLiteral("tr"));
  {
    const int at = inset_->findData(QSettings().value(QStringLiteral("sstv/inset"), QStringLiteral("bl")));
    inset_->setCurrentIndex(at < 0 ? 1 : at);
  }
  inset_->setToolTip(QStringLiteral(
      "When you send one of your own pictures with their picture set, put theirs in this corner with a "
      "white border, the usual SSTV reply. Templates use their own their-picture box instead. "
      "This does not key."));
  auto* replyClear = new QPushButton(QStringLiteral("×"), this);
  replyClear->setObjectName(QStringLiteral("sstvTxReplyClear"));
  replyClear->setFixedWidth(24);
  replyClear->setFocusPolicy(Qt::NoFocus);
  replyClear->setToolTip(QStringLiteral("Take their picture out of the template. This does not key."));
  auto* templates = new QPushButton(QStringLiteral("Templates…"), this);
  templates->setObjectName(QStringLiteral("sstvTxTemplates"));
  templates->setFont(paneFont(11));
  templates->setFocusPolicy(Qt::NoFocus);
  templates->setToolTip(QStringLiteral(
      "Make and change templates: text with your call, their call and report, fonts, colours, outlines, "
      "and a box for their picture. This does not key."));
  cwId_ = new QCheckBox(QStringLiteral("CW ID"), this);
  cwId_->setObjectName(QStringLiteral("sstvTxCwId"));
  cwId_->setFont(paneFont(12));
  cwId_->setChecked(QSettings().value(QStringLiteral("sstv/cwId"), true).toBool());
  cwId_->setToolTip(QStringLiteral(
      "Half a second after the picture, send your callsign in Morse as part of the same transmission "
      "(%1 WPM, %2 Hz tone, soft edges). The call comes from Options → User. It goes out only with a picture "
      "you send with TX.")
                        .arg(cwWpm())
                        .arg(cwToneHz()));
  qso->addWidget(replyLabel);
  qso->addWidget(replyThumb_);
  qso->addWidget(replyClear);
  qso->addWidget(inset_);
  qso->addWidget(templates);
  qso->addWidget(cwId_);
  auto* log = new QPushButton(QStringLiteral("Log"), this);
  log->setObjectName(QStringLiteral("sstvTxLog"));
  log->setFont(paneFont(11));
  log->setFocusPolicy(Qt::NoFocus);
  log->setToolTip(QStringLiteral(
      "Log this QSO: Call, RST sent and received, Name, band and frequency, mode SSTV with the picture mode "
      "in the comment. It goes to STD's ADIF log and, when Options → Log links a logger, to SDRLogger+ like "
      "the other modes. Then Call, Name, RST rcvd and their picture clear for the next one. This does not key."));
  qso->addWidget(log);
  connect(log, &QPushButton::clicked, this, [this] { emit logRequested(); });
  qso->addStretch(1);
  layout->addLayout(qso);
  for (QLineEdit* e : {call_, rst_, name_}) {
    connect(e, &QLineEdit::textChanged, this, [this] {
      QSettings().setValue(QStringLiteral("sstv/rst"), rst_->text().trimmed());
      if (usingTemplate())
        showPreview();
    });
  }
  connect(replyClear, &QPushButton::clicked, this, [this] { setReplyPicture(QImage(), QString()); });
  connect(inset_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    QSettings().setValue(QStringLiteral("sstv/inset"), inset_->currentData());
    showPreview();
  });
  connect(templates, &QPushButton::clicked, this, [this] { openEditor(); });
  connect(cwId_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("sstv/cwId"), on); });

  auto* split = new QSplitter(Qt::Horizontal, this);
  split->setChildrenCollapsible(false);
  list_ = new QListWidget(split);
  list_->setObjectName(QStringLiteral("sstvTxList"));
  list_->setIconSize(QSize(kThumbWidth, kThumbWidth * 3 / 4));
  list_->setFont(paneFont(11));
  list_->setMinimumWidth(kThumbWidth + 60);
  list_->setToolTip(QStringLiteral(
      "Templates, then the pictures in the Images folder. Click one to pick it; TX sends it. Double-click a "
      "template to change it. This does not key."));
  preview_ = new QLabel(split);
  preview_->setObjectName(QStringLiteral("sstvTxPreview"));
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setMinimumSize(160, 120);
  preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  preview_->setStyleSheet(QStringLiteral("background: #111216;"));
  preview_->setToolTip(QStringLiteral(
      "The picture as it will go out, at the mode's shape. With Crop, drag to move what is kept and "
      "double-click to center it. This does not key."));
  preview_->installEventFilter(this);
  preview_->setCursor(Qt::OpenHandCursor);
  split->addWidget(list_);
  split->addWidget(preview_);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  layout->addWidget(split, 1);
  progress_ = new QProgressBar(this);
  progress_->setObjectName(QStringLiteral("sstvTxProgress"));
  progress_->setFont(paneFont(11));
  progress_->setTextVisible(true);
  progress_->setMaximumHeight(18);
  progress_->setToolTip(QStringLiteral("How much of the picture has gone out. This does not key."));
  progress_->hide();
  layout->addWidget(progress_);

  connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    QSettings().setValue(QStringLiteral("sstv/txMode"), mode_->currentData());
    showPreview();
  });
  connect(shape_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    QSettings().setValue(QStringLiteral("sstv/txShape"), shape_->currentData());
    showPreview();
  });
  connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (!item)
      return;
    const QString kind = item->data(Qt::UserRole + 1).toString();
    if (kind == QLatin1String("tpl"))
      pickTemplate(item->data(Qt::UserRole).toString());
    else if (kind == QLatin1String("img"))
      pick(item->data(Qt::UserRole).toString());
  });
  connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    if (item && item->data(Qt::UserRole + 1).toString() == QLatin1String("tpl"))
      openEditor();
  });
  connect(saveAs, &QPushButton::clicked, this, [this] {
    const QImage shaped = framed();
    if (shaped.isNull()) {
      emit status(QStringLiteral("Pick a picture first. Nothing was saved or keyed."));
      return;
    }
    const QString suggest = QStringLiteral("%1 %2").arg(QFileInfo(path_).completeBaseName(),
                                                         shape_->currentText().toLower());
    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("Save framed picture"),
                                         QStringLiteral("Name for the framed picture:"), QLineEdit::Normal,
                                         suggest, &ok)
                       .trimmed();
    if (!ok || name.isEmpty())
      return;
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    QDir().mkpath(sstvImagesDirectory());
    QString target = QDir(sstvImagesDirectory()).filePath(name + QStringLiteral(".png"));
    for (int n = 2; QFileInfo::exists(target); ++n)
      target = QDir(sstvImagesDirectory()).filePath(QStringLiteral("%1-%2.png").arg(name).arg(n));
    if (!shaped.save(target, "PNG")) {
      emit status(QStringLiteral("Could not save %1. Nothing was keyed.").arg(target));
      return;
    }
    // Already the right shape, so it sends as it is.
    QSettings().setValue(QStringLiteral("sstv/crop/%1").arg(QFileInfo(target).fileName()), 0.5);
    refresh();
    pick(target);
    emit status(QStringLiteral("Saved %1. Click it next time to send it as framed. Nothing was keyed. "
                               "START was not sent.")
                    .arg(QFileInfo(target).fileName()));
  });
  connect(open, &QPushButton::clicked, this, [] {
    QDir().mkpath(sstvImagesDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(sstvImagesDirectory()));
  });
  connect(add, &QPushButton::clicked, this, [this] {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add pictures to send"), QString(),
        QStringLiteral("Pictures (%1)").arg(kPictureFilters.join(QLatin1Char(' '))));
    if (files.isEmpty())
      return;
    QDir().mkpath(sstvImagesDirectory());
    QString last;
    int copied = 0;
    for (const QString& file : files) {
      const QFileInfo info(file);
      QString target = QDir(sstvImagesDirectory()).filePath(info.fileName());
      if (QFileInfo(target).absoluteFilePath() == info.absoluteFilePath()) {
        last = target;
        continue;
      }
      for (int n = 2; QFileInfo::exists(target); ++n)
        target = QDir(sstvImagesDirectory())
                     .filePath(QStringLiteral("%1-%2.%3").arg(info.completeBaseName()).arg(n).arg(info.suffix()));
      if (QFile::copy(file, target)) {
        last = target;
        ++copied;
      }
    }
    refresh();
    if (!last.isEmpty())
      pick(last);
    emit status(QStringLiteral("%1 picture(s) added to %2. Nothing was keyed. START was not sent.")
                    .arg(copied)
                    .arg(sstvImagesDirectory()));
  });

  refresh();
  const QString savedTemplate = QSettings().value(QStringLiteral("sstv/txTemplate")).toString();
  const QString saved = QSettings().value(QStringLiteral("sstv/txPicture")).toString();
  if (!savedTemplate.isEmpty() && currentTemplate() == nullptr) {
    template_ = savedTemplate;
    if (!currentTemplate())
      template_.clear();
  }
  if (!template_.isEmpty())
    pickTemplate(template_);
  else if (QFileInfo::exists(saved))
    pick(saved);
  else
    showPreview();
}

void SstvTxPanel::setFieldsProvider(std::function<SstvFields()> provider) {
  provider_ = std::move(provider);
  showPreview();
}

void SstvTxPanel::setReplyPicture(const QImage& picture, const QString& from) {
  reply_ = picture;
  replyFrom_ = from;
  if (picture.isNull()) {
    replyThumb_->setPixmap(QPixmap());
    replyThumb_->setText(QStringLiteral("none"));
  } else {
    replyThumb_->setText(QString());
    replyThumb_->setPixmap(
        QPixmap::fromImage(picture).scaled(replyThumb_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  replyThumb_->setToolTip(from.isEmpty()
                              ? QStringLiteral("No picture. In the receive pane, pick one and press Reply with it.")
                              : QStringLiteral("Their picture: %1").arg(from));
  showPreview();
}

SstvFields SstvTxPanel::fields() const {
  SstvFields f = provider_ ? provider_() : SstvFields();
  f.insert(QStringLiteral("{CALL}"), theirCall());
  f.insert(QStringLiteral("{RST}"), rstSent());
  f.insert(QStringLiteral("{NAME}"), theirName());
  f.insert(QStringLiteral("{MODE}"), sstvModeName(mode()));
  return f;
}

bool SstvTxPanel::cwId() const {
  return cwId_ && cwId_->isChecked();
}

int SstvTxPanel::cwWpm() const {
  return std::clamp(QSettings().value(QStringLiteral("sstv/cwWpm"), 20).toInt(), 10, 40);
}

int SstvTxPanel::cwToneHz() const {
  return std::clamp(QSettings().value(QStringLiteral("sstv/cwToneHz"), 800).toInt(), 400, 1200);
}

QString SstvTxPanel::theirCall() const {
  return call_ ? call_->text().trimmed().toUpper() : QString();
}

QString SstvTxPanel::rstSent() const {
  return rst_ ? rst_->text().trimmed() : QString();
}

QString SstvTxPanel::rstRcvd() const {
  return rstIn_ ? rstIn_->text().trimmed() : QString();
}

void SstvTxPanel::clearQso() {
  for (QLineEdit* e : {call_, name_, rstIn_})
    e->clear();
  setReplyPicture(QImage(), QString());
}

QString SstvTxPanel::theirName() const {
  return name_ ? name_->text().trimmed() : QString();
}

const SstvTemplate* SstvTxPanel::currentTemplate() const {
  for (const SstvTemplate& t : templates_)
    if (t.name.compare(template_, Qt::CaseInsensitive) == 0)
      return &t;
  return nullptr;
}

void SstvTxPanel::pickTemplate(const QString& name) {
  template_ = name;
  QSettings().setValue(QStringLiteral("sstv/txTemplate"), name);
  shape_->setEnabled(false);
  for (int i = 0; i < list_->count(); ++i) {
    auto* item = list_->item(i);
    if (item->data(Qt::UserRole + 1).toString() == QLatin1String("tpl") &&
        item->data(Qt::UserRole).toString() == name)
      list_->setCurrentRow(i);
  }
  showPreview();
}

void SstvTxPanel::openEditor() {
  SstvTemplateEditor editor(fields(), reply_, template_, this);
  editor.exec();
  const QString chosen = editor.currentName();
  refresh();
  if (!chosen.isEmpty())
    pickTemplate(chosen);
  else if (!currentTemplate())
    template_.clear();
}

SstvMode SstvTxPanel::mode() const {
  return sstvModeFromName(mode_->currentData().toString());
}

QRect SstvTxPanel::cropRect(SstvMode m, double pos) const {
  const QSize src = source_.size();
  const double want = double(sstvWidth(m)) / double(std::max(1, sstvHeight(m)));
  const double have = double(src.width()) / double(std::max(1, src.height()));
  pos = std::clamp(pos, 0.0, 1.0);
  if (have > want) {
    // Wider than the mode: keep the full height, cut the sides.
    const int w = std::max(1, int(src.height() * want + 0.5));
    return QRect(int((src.width() - w) * pos + 0.5), 0, w, src.height());
  }
  const int h = std::max(1, int(src.width() / want + 0.5));
  return QRect(0, int((src.height() - h) * pos + 0.5), src.width(), h);
}

QImage SstvTxPanel::framed() const {
  const SstvMode m = mode();
  if (currentTemplate())
    return picture();
  if (source_.isNull() || m == SstvMode::None)
    return QImage();
  const QImage rgb = source_.convertToFormat(QImage::Format_RGB32);
  const QString shape = shape_->currentData().toString();
  const double want = double(sstvWidth(m)) / double(std::max(1, sstvHeight(m)));
  if (shape == QLatin1String("crop"))
    return rgb.copy(cropRect(m, cropPos_));
  // Fit and Stretch: a canvas of the mode's shape, as big as the source.
  QSize size(rgb.width(), int(rgb.width() / want + 0.5));
  if (size.height() < rgb.height())
    size = QSize(int(rgb.height() * want + 0.5), rgb.height());
  if (shape == QLatin1String("stretch"))
    return rgb.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QImage out(size, QImage::Format_RGB32);
  out.fill(Qt::black);
  QPainter p(&out);
  p.drawImage((size.width() - rgb.width()) / 2, (size.height() - rgb.height()) / 2, rgb);
  return out;
}

QImage SstvTxPanel::picture() const {
  const SstvMode m = mode();
  if (const SstvTemplate* t = currentTemplate())
    return sstvRenderTemplate(*t, QSize(sstvWidth(m), sstvHeight(m)), fields(), reply_);
  const QImage shaped = framed();
  if (shaped.isNull())
    return QImage();
  QImage out = shaped.scaled(sstvWidth(m), sstvHeight(m), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const QString corner = inset_ ? inset_->currentData().toString() : QString();
  if (!reply_.isNull() && !corner.isEmpty()) {
    // Their picture in a corner, about a third of the width, at the frame's
    // shape, with a white border and a soft shadow.
    const double w = out.width() * 0.36;
    const double h = w * out.height() / out.width();
    const double margin = out.width() * 0.03;
    const double x = corner.endsWith(QLatin1Char('l')) ? margin : out.width() - margin - w;
    const double y = corner.startsWith(QLatin1Char('t')) ? margin : out.height() - margin - h;
    const QRectF box(x, y, w, h);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    const double edge = std::max(2.0, out.height() * 0.01);
    p.fillRect(box.translated(edge, edge), QColor(0, 0, 0, 150));
    p.drawImage(box, reply_.scaled(box.size().toSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                         .copy(QRect(QPoint(0, 0), box.size().toSize())));
    p.setPen(QPen(Qt::white, edge));
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
  }
  return out;
}

bool SstvTxPanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched == replyThumb_ && event->type() == QEvent::MouseButtonPress) {
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Their picture"), sstvRxDirectory(),
        QStringLiteral("Pictures (%1)").arg(kPictureFilters.join(QLatin1Char(' '))));
    if (!file.isEmpty()) {
      const QImage img(file);
      if (img.isNull())
        emit status(QStringLiteral("%1 is not a picture STD can read. Nothing was keyed.").arg(file));
      else
        setReplyPicture(img, QFileInfo(file).fileName());
    }
    return true;
  }
  if (watched != preview_ || source_.isNull() || usingTemplate() ||
      shape_->currentData().toString() != QLatin1String("crop"))
    return QWidget::eventFilter(watched, event);
  const SstvMode m = mode();
  const QRect keep = cropRect(m, 0.5);
  const bool sideways = keep.width() < source_.width();
  const int span = sideways ? source_.width() - keep.width() : source_.height() - keep.height();
  if (span <= 0)
    return QWidget::eventFilter(watched, event);
  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      auto* e = static_cast<QMouseEvent*>(event);
      if (e->button() != Qt::LeftButton)
        break;
      dragFrom_ = sideways ? int(e->position().x()) : int(e->position().y());
      dragStartPos_ = cropPos_;
      preview_->setCursor(Qt::ClosedHandCursor);
      return true;
    }
    case QEvent::MouseMove: {
      auto* e = static_cast<QMouseEvent*>(event);
      if (dragFrom_ < 0)
        break;
      const QPixmap shown = preview_->pixmap();
      const int shownSpan = sideways ? shown.width() : shown.height();
      if (shownSpan <= 0)
        break;
      // Dragging the picture right shows more of its left side.
      const int at = sideways ? int(e->position().x()) : int(e->position().y());
      const double kept = sideways ? keep.width() : keep.height();
      const double moved = double(at - dragFrom_) / shownSpan * kept / span;
      cropPos_ = std::clamp(dragStartPos_ - moved, 0.0, 1.0);
      showPreview();
      return true;
    }
    case QEvent::MouseButtonRelease:
      if (dragFrom_ >= 0) {
        dragFrom_ = -1;
        preview_->setCursor(Qt::OpenHandCursor);
        QSettings().setValue(QStringLiteral("sstv/crop/%1").arg(QFileInfo(path_).fileName()), cropPos_);
        return true;
      }
      break;
    case QEvent::MouseButtonDblClick:
      cropPos_ = 0.5;
      QSettings().setValue(QStringLiteral("sstv/crop/%1").arg(QFileInfo(path_).fileName()), cropPos_);
      showPreview();
      return true;
    default:
      break;
  }
  return QWidget::eventFilter(watched, event);
}

void SstvTxPanel::refresh() {
  list_->clear();
  auto header = [this](const QString& text) {
    auto* item = new QListWidgetItem(text, list_);
    item->setFlags(Qt::NoItemFlags);
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
  };
  templates_ = sstvLoadTemplates();
  header(QStringLiteral("Templates"));
  const SstvFields f = fields();
  for (const SstvTemplate& t : templates_) {
    const QImage thumb = sstvRenderTemplate(t, QSize(kThumbWidth, kThumbWidth * 4 / 5), f, reply_, true);
    auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumb)),
                                     QStringLiteral("%1  (%2)").arg(t.name, t.category), list_);
    item->setData(Qt::UserRole, t.name);
    item->setData(Qt::UserRole + 1, QStringLiteral("tpl"));
    item->setToolTip(QStringLiteral("Template %1. Double-click to change it.").arg(t.name));
    if (t.name == template_)
      list_->setCurrentItem(item);
  }
  header(QStringLiteral("Pictures"));
  const QDir dir(sstvImagesDirectory());
  for (const QFileInfo& info : dir.entryInfoList(kPictureFilters, QDir::Files, QDir::Name)) {
    const QPixmap thumb = QPixmap(info.filePath()).scaledToWidth(kThumbWidth, Qt::SmoothTransformation);
    auto* item = new QListWidgetItem(QIcon(thumb), info.completeBaseName(), list_);
    item->setData(Qt::UserRole, info.filePath());
    item->setData(Qt::UserRole + 1, QStringLiteral("img"));
    item->setToolTip(info.filePath());
    if (template_.isEmpty() && info.filePath() == path_)
      list_->setCurrentItem(item);
  }
}

void SstvTxPanel::pick(const QString& path) {
  QImage img(path);
  if (img.isNull()) {
    emit status(QStringLiteral("%1 is not a picture STD can read. Nothing was keyed.").arg(path));
    return;
  }
  path_ = path;
  source_ = img;
  template_.clear();
  QSettings().setValue(QStringLiteral("sstv/txTemplate"), QString());
  shape_->setEnabled(true);
  cropPos_ = QSettings().value(QStringLiteral("sstv/crop/%1").arg(QFileInfo(path).fileName()), 0.5).toDouble();
  QSettings().setValue(QStringLiteral("sstv/txPicture"), path);
  for (int i = 0; i < list_->count(); ++i) {
    if (list_->item(i)->data(Qt::UserRole + 1).toString() == QLatin1String("img") &&
        list_->item(i)->data(Qt::UserRole).toString() == path)
      list_->setCurrentRow(i);
  }
  showPreview();
}

void SstvTxPanel::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  showPreview();
}

void SstvTxPanel::showPreview() {
  const SstvMode m = sentRows_ >= 0 ? sendingMode_ : mode();
  QImage shown = sentRows_ >= 0 ? sending_ : picture();
  if (sentRows_ >= 0 && !shown.isNull()) {
    // Sent rows bright, the rest dimmed, and a line where the transmitter is.
    QPainter p(&shown);
    p.fillRect(QRect(0, sentRows_, shown.width(), shown.height() - sentRows_), QColor(0, 0, 0, 150));
    if (sentRows_ < shown.height()) {
      p.setPen(QPen(QColor(255, 65, 54), 2));
      p.drawLine(0, sentRows_, shown.width(), sentRows_);
    }
  }
  if (shown.isNull()) {
    preview_->setPixmap(QPixmap());
    preview_->setText(QStringLiteral("Pick a picture on the left, or Add… one."));
    info_->setText(QStringLiteral("%1, %2. No picture picked.").arg(sstvModeName(m), seconds(sstvSeconds(m))));
    return;
  }
  preview_->setText(QString());
  // Shown at the mode's own shape, so what you see is what is sent.
  const QSize box = preview_->size();
  QSize fit(sstvWidth(m), sstvHeight(m));
  fit.scale(box, Qt::KeepAspectRatio);
  preview_->setPixmap(QPixmap::fromImage(shown).scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
  if (sentRows_ >= 0)
    return;
  info_->setText(QStringLiteral("%1, %2 by %3, %4 on the air%5. TX sends %6.")
                     .arg(sstvModeName(m))
                     .arg(sstvWidth(m))
                     .arg(sstvHeight(m))
                     .arg(seconds(sstvSeconds(m)), cwId() ? QStringLiteral(" plus CW ID") : QString(),
                          usingTemplate() ? QStringLiteral("template %1").arg(template_)
                                          : QFileInfo(path_).fileName()));
}

void SstvTxPanel::setTxProgress(double seconds) {
  if (sentRows_ < 0) {
    // First call of this transmission: freeze what is going out.
    sendingMode_ = mode();
    sending_ = picture();
    if (sending_.isNull())
      return;
    sentRows_ = 0;
    progress_->setRange(0, sstvHeight(sendingMode_));
    progress_->show();
  }
  const double total = sstvSeconds(sendingMode_);
  const int rows = sstvRowsSentAt(sendingMode_, seconds);
  const int left = std::max(0, int(total - seconds + 0.5));
  if (rows == sentRows_ && progress_->value() == rows && !progress_->format().isEmpty() &&
      progress_->property("left").toInt() == left)
    return;
  sentRows_ = rows;
  progress_->setValue(rows);
  progress_->setProperty("left", left);
  progress_->setFormat(seconds < 0.91 ? QStringLiteral("Sending the VIS header")
                       : seconds >= total ? QStringLiteral("Sending the CW ID")
                                      : QStringLiteral("Line %1 of %2, %3:%4 left")
                                            .arg(rows)
                                            .arg(sstvHeight(sendingMode_))
                                            .arg(left / 60)
                                            .arg(left % 60, 2, 10, QLatin1Char('0')));
  info_->setText(QStringLiteral("Sending %1 in %2. TRX drops when the picture ends; Halt stops now.")
                     .arg(usingTemplate() ? QStringLiteral("template %1").arg(template_) : QFileInfo(path_).fileName(),
                          sstvModeName(sendingMode_)));
  showPreview();
}

void SstvTxPanel::setTxDone(bool complete) {
  if (sentRows_ < 0)
    return;
  const QString mode = sstvModeName(sendingMode_);
  const int rows = sentRows_;
  sentRows_ = -1;
  sending_ = QImage();
  progress_->hide();
  showPreview();
  const QString utc = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm"));
  info_->setText(complete ? QStringLiteral("Sent %1 at %2 UTC. Web RX shows whether other stations copied it.")
                                .arg(mode, utc)
                          : QStringLiteral("Stopped at line %1 of %2 at %3 UTC.")
                                .arg(rows)
                                .arg(sstvHeight(sendingMode_))
                                .arg(utc));
}
