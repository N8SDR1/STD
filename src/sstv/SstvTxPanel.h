// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sstv/SstvModem.h"
#include "sstv/SstvTemplate.h"

#include <QImage>
#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTabBar;
class QLabel;
class QListWidget;

// The SSTV transmit pane: pick a picture from the Images folder and the mode
// to send it in. The TX button below keys it; nothing in this pane keys the
// radio or sends START.
class SstvTxPanel : public QWidget {
  Q_OBJECT

public:
  explicit SstvTxPanel(QWidget* parent = nullptr);

  SstvMode mode() const;
  // The picture as it will be sent: scaled to the mode's size. Null when
  // nothing is picked.
  QImage picture() const;
  // The picture framed to the mode's shape (crop, fit, or stretch) at the
  // source's own resolution, for Save as.
  QImage framed() const;
  QString picturePath() const { return path_; }
  // Read the Images and Templates folders again.
  void refresh();
  // Station fields ({MYCALL}, {BAND} and so on) for templates, read when the
  // picture is built.
  void setFieldsProvider(std::function<SstvFields()> provider);
  // The picture received from the station being worked, for a template's
  // their-picture box.
  void setReplyPicture(const QImage& picture, const QString& from);
  // Every fill-in field as it stands now.
  SstvFields fields() const;
  bool usingTemplate() const { return !template_.isEmpty(); }
  QString templateName() const { return template_; }
  // CW identification after the picture, and what it sends.
  bool cwId() const;
  int cwWpm() const;
  int cwToneHz() const;
  QString theirCall() const;
  QString rstSent() const;
  QString theirName() const;
  QString rstRcvd() const;
  QString theirGrid() const;
  QString theirQth() const;
  // FSK ID after the picture.
  bool fskId() const;
  // Put a call heard in an FSK ID into Call, unless one is typed already.
  bool offerCall(const QString& call);
  // After a QSO is logged: clear Call, Name, RST rcvd and their picture.
  void clearQso();
  // While transmitting: seconds of the picture gone out so far. The preview
  // shows the sent part bright and the rest dimmed, with a progress line.
  void setTxProgress(double seconds);
  // The picture has finished, or TX stopped early.
  void setTxDone(bool complete);


signals:
  void status(const QString& text);
  // The operator pressed Log.
  void logRequested();

protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void pick(const QString& path);
  void pickTemplate(const QString& name);
  const SstvTemplate* currentTemplate() const;
  void openEditor();
  // A template from the picture shown, with the text for the open tab.
  void makeTemplate();
  void showPreview();

  QComboBox* mode_ = nullptr;
  // How the picture is made to fit the mode: crop, fit with bars, or stretch.
  QComboBox* shape_ = nullptr;
  QListWidget* list_ = nullptr;
  QLabel* preview_ = nullptr;
  QLabel* info_ = nullptr;
  QLineEdit* call_ = nullptr;
  QLineEdit* rst_ = nullptr;
  QLineEdit* name_ = nullptr;
  QLineEdit* rstIn_ = nullptr;
  QLineEdit* grid_ = nullptr;
  QLineEdit* qth_ = nullptr;
  QCheckBox* fskId_ = nullptr;
  // All, CQ, Reply, 73, Custom, Pictures.
  QTabBar* tabs_ = nullptr;
  QLabel* replyThumb_ = nullptr;
  // Where their picture goes on one of your own pictures: off or a corner.
  QComboBox* inset_ = nullptr;
  QCheckBox* cwId_ = nullptr;
  std::vector<SstvTemplate> templates_;
  QString template_;
  QImage reply_;
  QString replyFrom_;
  std::function<SstvFields()> provider_;
  class QProgressBar* progress_ = nullptr;
  // Transmit progress: -1 when not sending.
  int sentRows_ = -1;
  SstvMode sendingMode_ = SstvMode::None;
  QImage sending_;
  QString path_;
  QImage source_;
  // Crop: where the kept part sits along the side that is cut, 0 to 1
  // (0.5 is the middle). Remembered per picture.
  double cropPos_ = 0.5;
  int dragFrom_ = -1;
  double dragStartPos_ = 0.5;
  // The part of the source a crop keeps, for the given mode.
  QRect cropRect(SstvMode mode, double pos) const;
};

// Where pictures to send are kept, and where a copy of each sent picture goes.
QString sstvImagesDirectory();
QString sstvTxDirectory();
