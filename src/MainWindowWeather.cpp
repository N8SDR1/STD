// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Options → Weather: where the <WX> macro tags get the local weather.

#include "MainWindow.h"

#include "WxFeed.h"

#include <algorithm>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

void MainWindow::openWeatherDialog() {
  QSettings s;
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("weatherDialog"));
  dialog.setWindowTitle(QStringLiteral("Weather"));
  dialog.setMinimumWidth(520);

  auto* intro = new QLabel(
      QStringLiteral("The local weather for your macros. <WX> sends it in one line, such as "
                     "54F, partly cloudy, wind SW 8 mph, humidity 72%. <WX_TEMP>, <WX_COND>, <WX_WIND>, "
                     "and <WX_HUM> send one part each. STD fetches it every 10 minutes while a source is on. "
                     "Nothing is fetched while the source is Off. This does not key."),
      &dialog);
  intro->setWordWrap(true);

  auto* source = new QComboBox(&dialog);
  source->setObjectName(QStringLiteral("wxSource"));
  source->addItem(QStringLiteral("Off"), QStringLiteral("off"));
  source->addItem(QStringLiteral("Open-Meteo, from my locator (free, no account)"), QStringLiteral("openmeteo"));
  source->addItem(QStringLiteral("NWS station (US)"), QStringLiteral("nws"));
  source->addItem(QStringLiteral("Ambient Weather station"), QStringLiteral("ambient"));
  source->addItem(QStringLiteral("Ecowitt station"), QStringLiteral("ecowitt"));
  source->setCurrentIndex(std::max(0, source->findData(s.value(QStringLiteral("wx/source"), QStringLiteral("off")))));

  auto line = [&dialog, &s](const char* object, const char* key, bool secret, const QString& hint) {
    auto* edit = new QLineEdit(s.value(QLatin1String(key)).toString(), &dialog);
    edit->setObjectName(QLatin1String(object));
    edit->setPlaceholderText(hint);
    if (secret)
      edit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    return edit;
  };
  auto* nws = line("wxNwsStation", "wx/nwsStation", false, QStringLiteral("KHAO"));
  nws->setToolTip(QStringLiteral(
      "The four-letter NWS observation station nearest you, usually an airport, such as KHAO for Hamilton OH. "
      "weather.gov lists them under the station's observations."));
  auto* ambientApi = line("wxAmbientApi", "wx/ambientApiKey", true, QStringLiteral("API key"));
  auto* ambientApp = line("wxAmbientApp", "wx/ambientAppKey", true, QStringLiteral("Application key"));
  auto* ecoApp = line("wxEcowittApp", "wx/ecowittAppKey", true, QStringLiteral("Application key"));
  auto* ecoApi = line("wxEcowittApi", "wx/ecowittApiKey", true, QStringLiteral("API key"));
  auto* ecoMac = line("wxEcowittMac", "wx/ecowittMac", false, QStringLiteral("AA:BB:CC:DD:EE:FF"));

  auto* units = new QComboBox(&dialog);
  units->setObjectName(QStringLiteral("wxUnits"));
  units->addItem(QStringLiteral("°F and mph"), false);
  units->addItem(QStringLiteral("°C and km/h"), true);
  units->setCurrentIndex(s.value(QStringLiteral("wx/metric"), false).toBool() ? 1 : 0);

  auto* form = new QFormLayout;
  form->addRow(QStringLiteral("Source"), source);
  form->addRow(QStringLiteral("NWS station"), nws);
  form->addRow(QStringLiteral("Ambient API key"), ambientApi);
  form->addRow(QStringLiteral("Ambient app key"), ambientApp);
  form->addRow(QStringLiteral("Ecowitt app key"), ecoApp);
  form->addRow(QStringLiteral("Ecowitt API key"), ecoApi);
  form->addRow(QStringLiteral("Ecowitt MAC"), ecoMac);
  form->addRow(QStringLiteral("Units"), units);

  auto* where = new QLabel(&dialog);
  where->setWordWrap(true);
  auto showRows = [&] {
    const QString pick = source->currentData().toString();
    form->setRowVisible(nws, pick == QLatin1String("nws"));
    form->setRowVisible(ambientApi, pick == QLatin1String("ambient"));
    form->setRowVisible(ambientApp, pick == QLatin1String("ambient"));
    form->setRowVisible(ecoApp, pick == QLatin1String("ecowitt"));
    form->setRowVisible(ecoApi, pick == QLatin1String("ecowitt"));
    form->setRowVisible(ecoMac, pick == QLatin1String("ecowitt"));
    if (pick == QLatin1String("openmeteo"))
      where->setText(QStringLiteral("Uses the middle of your locator from Options → User, about 5 km on a "
                                    "six-character locator. Works anywhere."));
    else if (pick == QLatin1String("nws"))
      where->setText(QStringLiteral("The latest observation from that station, from weather.gov. US only."));
    else if (pick == QLatin1String("ambient"))
      where->setText(QStringLiteral("Your station's last reading. Make both keys at ambientweather.net, "
                                    "Account → API Keys. Ambient sends no sky condition."));
    else if (pick == QLatin1String("ecowitt"))
      where->setText(QStringLiteral("Your station's live reading. The keys are at ecowitt.net, User Profile; "
                                    "the MAC is on the device list. Ecowitt sends no sky condition."));
    else
      where->setText(QStringLiteral("Off: <WX> stays empty, and a macro that sends it waits for weather."));
  };
  showRows();
  connect(source, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { showRows(); });

  auto save = [&] {
    QSettings out;
    out.setValue(QStringLiteral("wx/source"), source->currentData());
    out.setValue(QStringLiteral("wx/nwsStation"), nws->text().trimmed().toUpper());
    out.setValue(QStringLiteral("wx/ambientApiKey"), ambientApi->text().trimmed());
    out.setValue(QStringLiteral("wx/ambientAppKey"), ambientApp->text().trimmed());
    out.setValue(QStringLiteral("wx/ecowittAppKey"), ecoApp->text().trimmed());
    out.setValue(QStringLiteral("wx/ecowittApiKey"), ecoApi->text().trimmed());
    out.setValue(QStringLiteral("wx/ecowittMac"), ecoMac->text().trimmed().toUpper());
    out.setValue(QStringLiteral("wx/metric"), units->currentData());
  };

  auto* result = new QLabel(&dialog);
  result->setObjectName(QStringLiteral("wxResult"));
  result->setWordWrap(true);
  result->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto showResult = [this, result, units] {
    const WxReading& r = wx_->reading();
    QString text;
    if (r.valid)
      text = QStringLiteral("<WX> now: %1  (fetched %2 UTC)")
                 .arg(WxFeed::describe(r, units->currentData().toBool()), r.when.toString(QStringLiteral("HH:mm")));
    else
      text = QStringLiteral("No weather yet.");
    if (!wx_->lastError().isEmpty())
      text += QLatin1Char('\n') + wx_->lastError();
    result->setText(text);
  };
  showResult();
  connect(wx_, &WxFeed::updated, &dialog, showResult);
  connect(units, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [showResult](int) { showResult(); });

  auto* fetch = new QPushButton(QStringLiteral("Save and fetch now"), &dialog);
  fetch->setObjectName(QStringLiteral("wxFetch"));
  fetch->setToolTip(QStringLiteral("Saves these settings and fetches the weather once. Does not key."));
  connect(fetch, &QPushButton::clicked, &dialog, [&] {
    save();
    result->setText(source->currentData().toString() == QLatin1String("off") ? QStringLiteral("Source is Off.")
                                                                            : QStringLiteral("Fetching ..."));
    wx_->reload();
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    save();
    wx_->reload();
    dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(intro);
  layout->addLayout(form);
  layout->addWidget(where);
  layout->addWidget(fetch, 0, Qt::AlignLeft);
  layout->addWidget(result);
  layout->addWidget(buttons);
  if (qEnvironmentVariableIsSet("STD_SELFTEST"))
    return;
  dialog.exec();
}
