// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Options → Lookup: where the QSO bar fills from when a call is entered.

#include "MainWindow.h"

#include "Callbook.h"
#include "QsoLog.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

void MainWindow::openLookupDialog() {
  QSettings s;
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("lookupDialog"));
  dialog.setWindowTitle(QStringLiteral("Lookup"));
  dialog.setMinimumWidth(560);

  auto* order = new QLabel(
      QStringLiteral("When a call goes into the QSO bar, STD fills the empty fields first from your log, in blue, "
                     "then from the callbook, in green. Anything you type wins, and what is in the box when you "
                     "log is what is logged. This does not key."),
      &dialog);
  order->setWordWrap(true);

  // Your logs.
  auto* logs = new QGroupBox(QStringLiteral("Your log"), &dialog);
  auto* extra = new QLineEdit(s.value(QStringLiteral("log/memoryAdif")).toString(), logs);
  extra->setObjectName(QStringLiteral("lookupAdif"));
  extra->setPlaceholderText(QStringLiteral("Another ADIF log, such as an export from your logger"));
  extra->setToolTip(QStringLiteral(
      "STD always reads its own log. This adds a second ADIF file, so stations you worked in other programs "
      "are remembered too. A QSO in both counts once. It is read again whenever it changes. "
      "STD keeps a copy in its log folder (imported), refreshed when this file changes, and reads "
      "the copy if this file is moved or deleted."));
  auto* browse = new QPushButton(QStringLiteral("Browse ..."), logs);
  browse->setObjectName(QStringLiteral("lookupAdifBrowse"));
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const QString pick = QFileDialog::getOpenFileName(&dialog, QStringLiteral("ADIF log"), extra->text(),
                                                      QStringLiteral("ADIF (*.adi *.adif);;All files (*)"));
    if (!pick.isEmpty())
      extra->setText(pick);
  });
  auto* logRow = new QHBoxLayout;
  logRow->addWidget(extra, 1);
  logRow->addWidget(browse);
  auto* logLay = new QVBoxLayout(logs);
  logLay->addLayout(logRow);
  auto* kept = new QLabel(QStringLiteral("STD keeps a copy in %1, so moving or deleting the file above "
                                         "does not lose these stations. Options → Log Options → Open log "
                                         "folder shows it.")
                              .arg(QDir::toNativeSeparators(QDir(QsoLog::logFolder()).filePath(
                                  QStringLiteral("imported")))),
                          logs);
  kept->setObjectName(QStringLiteral("lookupAdifKept"));
  kept->setWordWrap(true);
  kept->setTextInteractionFlags(Qt::TextSelectableByMouse);
  logLay->addWidget(kept);

  // The callbook.
  auto* book = new QGroupBox(QStringLiteral("Callbook"), &dialog);
  auto* source = new QComboBox(book);
  source->setObjectName(QStringLiteral("lookupSource"));
  source->addItem(QStringLiteral("Off"), QStringLiteral("off"));
  source->addItem(QStringLiteral("HamQTH (free)"), QStringLiteral("hamqth"));
  source->addItem(QStringLiteral("QRZ.com"), QStringLiteral("qrz"));
  source->setCurrentIndex(
      std::max(0, source->findData(s.value(QStringLiteral("callbook/source"), QStringLiteral("off")))));
  auto* user = new QLineEdit(s.value(QStringLiteral("callbook/user")).toString(), book);
  user->setObjectName(QStringLiteral("lookupUser"));
  auto* password = new QLineEdit(s.value(QStringLiteral("callbook/password")).toString(), book);
  password->setObjectName(QStringLiteral("lookupPassword"));
  password->setEchoMode(QLineEdit::Password);
  password->setToolTip(QStringLiteral(
      "Kept in STD's settings on this computer, as other ham programs keep it. It is sent only to the "
      "callbook you picked, never in the web address."));
  auto* portable = new QCheckBox(QStringLiteral("For a portable call (/P, /M, /4, VE3/...), fill only the name"), book);
  portable->setObjectName(QStringLiteral("lookupPortable"));
  portable->setChecked(s.value(QStringLiteral("callbook/portableNameOnly"), true).toBool());
  portable->setToolTip(QStringLiteral(
      "A station on the road or on vacation is not at the QTH, state, and grid in the callbook. With this on, "
      "only their name is filled for a call with a slash; you fill where they are. Off fills it all."));
  auto* about = new QLabel(book);
  about->setWordWrap(true);
  auto* form = new QFormLayout;
  form->addRow(QStringLiteral("Service"), source);
  form->addRow(QStringLiteral("User"), user);
  form->addRow(QStringLiteral("Password"), password);
  auto* bookLay = new QVBoxLayout(book);
  bookLay->addLayout(form);
  bookLay->addWidget(portable);
  bookLay->addWidget(about);
  auto showAbout = [&] {
    const QString pick = source->currentData().toString();
    const bool on = pick != QLatin1String("off");
    user->setEnabled(on);
    password->setEnabled(on);
    portable->setEnabled(on);
    if (pick == QLatin1String("hamqth"))
      about->setText(QStringLiteral("Free. Use your HamQTH callsign and password; make an account at hamqth.com."));
    else if (pick == QLatin1String("qrz"))
      about->setText(QStringLiteral("Your QRZ.com login. The full record (QTH, state, grid) needs QRZ's XML Data "
                                    "subscription; without it QRZ sends little more than the name."));
    else
      about->setText(QStringLiteral("Off: only your log fills the QSO bar."));
  };
  showAbout();
  connect(source, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { showAbout(); });

  auto save = [&] {
    QSettings out;
    out.setValue(QStringLiteral("log/memoryAdif"), extra->text().trimmed());
    QsoLog::keepAdifCopy(extra->text());
    out.setValue(QStringLiteral("callbook/source"), source->currentData());
    out.setValue(QStringLiteral("callbook/user"), user->text().trimmed());
    out.setValue(QStringLiteral("callbook/password"), password->text());
    out.setValue(QStringLiteral("callbook/portableNameOnly"), portable->isChecked());
    callbook_->reload();
  };

  // A test: look up your own call.
  auto* test = new QPushButton(QStringLiteral("Save and look up my call"), &dialog);
  test->setObjectName(QStringLiteral("lookupTest"));
  auto* result = new QLabel(&dialog);
  result->setObjectName(QStringLiteral("lookupResult"));
  result->setWordWrap(true);
  result->setTextInteractionFlags(Qt::TextSelectableByMouse);
  connect(callbook_, &Callbook::answered, &dialog,
          [result, this](const QString& asked, const CallbookRecord& r, const QString& error) {
            if (asked != Callbook::homeCall(stationCall()))
              return;
            if (r.found)
              result->setText(QStringLiteral("%1: %2, %3 %4, %5")
                                  .arg(r.call, r.name.isEmpty() ? QStringLiteral("(no name)") : r.name, r.qth,
                                       r.state, r.grid.isEmpty() ? QStringLiteral("(no grid)") : r.grid));
            else
              result->setText(error.isEmpty() ? QStringLiteral("%1 was not found.").arg(asked) : error);
          });
  connect(test, &QPushButton::clicked, &dialog, [&] {
    save();
    if (!callbook_->enabled()) {
      result->setText(QStringLiteral("Pick a service and enter the user and password."));
      return;
    }
    if (stationCall().isEmpty()) {
      result->setText(QStringLiteral("Set your callsign in Options → User first."));
      return;
    }
    result->setText(QStringLiteral("Looking up %1 ...").arg(stationCall()));
    callbook_->lookup(Callbook::homeCall(stationCall()));
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    save();
    dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(order);
  layout->addWidget(logs);
  layout->addWidget(book);
  layout->addWidget(test, 0, Qt::AlignLeft);
  layout->addWidget(result);
  layout->addWidget(buttons);
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  dialog.exec();
}
