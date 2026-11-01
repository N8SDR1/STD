// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// WEFAX stations pane, shown only in WEFAX mode where the transmit pane
// would be. Pick a region, then a station frequency: STD sets the dial
// 1.9 kHz below the listed carrier (USB) and puts the receive marker on
// 1900 Hz, so black lands at 1500 Hz and white at 2300 Hz.

#include "MainWindow.h"

#include "TciClient.h"
#include "WaterfallWidget.h"
#include "wefax/WefaxModem.h"

#include <cmath>
#include <functional>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QDateTime>
#include <QTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace {

// A frequency some stations use only part of the day: on from..to UTC as
// HHMM, wrapping past midnight when to is smaller. -1 means no stated limit.
struct FaxHours {
  double khz;
  int from;
  int to;
};

struct FaxStation {
  int region;  // ITU region
  const char* place;
  const char* call;
  double khz[6];  // assigned frequencies, 0 ends the list
};

// Per-frequency hours from the NWS "U.S. Coast Guard broadcasts" page
// (weather.gov/marine/uscg_broadcasts). Frequencies it gives no hours for
// are not limited here. Charts go out at set times with silence between.
const FaxHours kFaxHours[] = {
    {4235.0, 230, 1039},    // NMF
    {12750.0, 1400, 2239},  // NMF
    {17146.4, 1200, 2045},  // NMG
    {4346.0, 140, 1608},    // NMC
    {22527.0, 1840, 2356},  // NMC
    {9982.5, 519, 1556},    // KVM70
    {16135.0, 1719, 356},   // KVM70
};

// Returns true when the frequency has stated hours, and sets *now when
// the current UTC time is inside them.
bool faxHours(const char* call, double khz, bool* now, QString* text) {
  for (const FaxHours& h : kFaxHours) {
    if (std::fabs(h.khz - khz) > 0.05)
      continue;
    // 17146.4 is both NMG and CBV; the hours are NMG's.
    if (khz == 17146.4 && QLatin1String(call) != QLatin1String("NMG"))
      return false;
    const QTime utc = QDateTime::currentDateTimeUtc().time();
    const int hhmm = utc.hour() * 100 + utc.minute();
    *now = h.from <= h.to ? (hhmm >= h.from && hhmm <= h.to) : (hhmm >= h.from || hhmm <= h.to);
    *text = QString::asprintf("%04d-%04dZ", h.from, h.to);
    return true;
  }
  return false;
}

// Assigned frequencies from the NOAA "Worldwide Marine Radiofacsimile
// Broadcast Schedules". Stations change frequencies and hours, so check
// the current schedule when one stays silent.
const FaxStation kFaxStations[] = {
    {2, "Boston, MA", "NMF", {4235.0, 6340.5, 9110.0, 12750.0}},
    {2, "New Orleans, LA", "NMG", {4317.9, 8503.9, 12789.9, 17146.4}},
    {2, "Pt. Reyes, CA", "NMC", {4346.0, 8682.0, 12786.0, 17151.2, 22527.0}},
    {2, "Kodiak, AK", "NOJ", {2054.0, 4298.0, 8459.0, 12410.6}},
    {2, "Honolulu, HI", "KVM70", {9982.5, 11090.0, 16135.0, 23331.5}},
    {2, "Halifax, NS", "CFH", {4271.0, 6496.4, 10536.0, 13510.0}},
    {2, "Valparaiso, Chile", "CBV", {4228.0, 8677.0, 17146.4}},
    {1, "Hamburg, Germany", "DDH/DDK", {3855.0, 7880.0, 13882.5}},
    {1, "Northwood, UK", "GYA", {2618.5, 4610.0, 8040.0, 11086.5}},
    {1, "Athens, Greece", "SVJ4", {4481.0, 8105.0}},
    {1, "Cape Town, South Africa", "ZSJ", {4014.0, 7508.0, 13538.0, 18238.0}},
    {3, "Tokyo, Japan", "JMH", {3622.5, 7795.0, 13988.5}},
    {3, "Charleville, Australia", "VMC", {2628.0, 5100.0, 11030.0, 13920.0, 20469.0}},
    {3, "Wiluna, Australia", "VMW", {5755.0, 7535.0, 10555.0, 15615.0, 18060.0}},
    {3, "Wellington, New Zealand", "ZKLF", {3247.4, 5807.0, 9459.0, 13550.5, 16340.1}},
    {3, "Taipei, Taiwan", "BMF", {4616.0, 8140.0, 13900.0}},
    {3, "Bangkok, Thailand", "HSW64", {7395.0}},
};

constexpr int kFaxMarkerHz = 1900;

// Clicks on the chart: left sets the left edge there, right puts it back.
class FaxClick : public QObject {
public:
  FaxClick(QObject* parent, std::function<void(QLabel*, QMouseEvent*)> hit)
      : QObject(parent), hit_(std::move(hit)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonPress) {
      if (auto* label = qobject_cast<QLabel*>(watched)) {
        hit_(label, static_cast<QMouseEvent*>(event));
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void(QLabel*, QMouseEvent*)> hit_;
};

QFont faxUi(int pixelSize) {
  QFont font("Inter");
  font.setPixelSize(pixelSize);
  return font;
}

// Auto region from the locator's longitude and latitude.
int regionFromGrid(const QString& grid) {
  const QString g = grid.trimmed().toUpper();
  if (g.size() < 4 || g.at(0) < QLatin1Char('A') || g.at(0) > QLatin1Char('R') || g.at(1) < QLatin1Char('A') ||
      g.at(1) > QLatin1Char('R') || !g.at(2).isDigit() || !g.at(3).isDigit())
    return 2;
  const double lon = -180.0 + 20.0 * (g.at(0).unicode() - 'A') + 2.0 * g.at(2).digitValue() + 1.0;
  const double lat = -90.0 + 10.0 * (g.at(1).unicode() - 'A') + g.at(3).digitValue() + 0.5;
  if (lon < -30.0)
    return 2;
  if (lon > 60.0 && (lat < 40.0 || lon > 100.0))
    return 3;
  return 1;
}

}  // namespace

int MainWindow::wefaxRegion() const {
  const QString choice = QSettings().value(QStringLiteral("wefax/region"), QStringLiteral("auto")).toString();
  const int fixed = choice.toInt();
  return fixed >= 1 && fixed <= 3 ? fixed : regionFromGrid(stationGrid());
}

QWidget* MainWindow::makeWefaxStations(QWidget* parent) {
  auto* panel = new QWidget(parent);
  panel->setObjectName(QStringLiteral("wefaxStations"));
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(0, 2, 0, 2);
  layout->setSpacing(6);

  auto* top = new QHBoxLayout();
  top->setSpacing(8);
  auto* label = new QLabel(QStringLiteral("Region"), panel);
  label->setFont(faxUi(12));
  auto* region = new QComboBox(panel);
  region->setObjectName(QStringLiteral("wefaxRegion"));
  region->setFont(faxUi(12));
  region->addItem(QStringLiteral("Auto (from locator)"), QStringLiteral("auto"));
  region->addItem(QStringLiteral("Region 1 - Europe, Africa"), QStringLiteral("1"));
  region->addItem(QStringLiteral("Region 2 - Americas"), QStringLiteral("2"));
  region->addItem(QStringLiteral("Region 3 - Asia, Pacific"), QStringLiteral("3"));
  region->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  region->setToolTip(QStringLiteral(
      "Which WEFAX stations to list. Auto uses the ITU region of your locator (Options → User). "
      "This does not key."));
  {
    const QSignalBlocker block(region);
    const int at = region->findData(QSettings().value(QStringLiteral("wefax/region"), QStringLiteral("auto")));
    region->setCurrentIndex(at < 0 ? 0 : at);
  }
  auto* note = new QLabel(panel);
  note->setObjectName(QStringLiteral("wefaxStationNote"));
  note->setFont(faxUi(11));
  note->setText(QStringLiteral(
      "Bandwidth: black 1500 Hz, white 2300 Hz (\u00b1400 Hz around the 1900 Hz marker), about 1 kHz "
      "wide. A 2.4 kHz USB filter passes it. Listed frequencies are assigned; a click sets the dial "
      "1.9 kHz lower, USB. Dimmed ones are outside their published hours."));
  note->setWordWrap(true);
  top->addWidget(label);
  top->addWidget(region);
  top->addWidget(note, 1);
  layout->addLayout(top);

  auto* list = new QWidget(panel);
  list->setObjectName(QStringLiteral("wefaxStationList"));
  new QGridLayout(list);
  layout->addWidget(list);
  layout->addStretch(1);

  connect(region, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, region](int) {
    QSettings().setValue(QStringLiteral("wefax/region"), region->currentData());
    paintWefaxStations();
  });
  panel->hide();
  return panel;
}

void MainWindow::paintWefaxStations() {
  auto* list = findChild<QWidget*>(QStringLiteral("wefaxStationList"));
  auto* grid = list ? qobject_cast<QGridLayout*>(list->layout()) : nullptr;
  if (!grid)
    return;
  while (QLayoutItem* item = grid->takeAt(0)) {
    if (QWidget* w = item->widget())
      w->deleteLater();
    delete item;
  }
  grid->setHorizontalSpacing(4);
  grid->setVerticalSpacing(4);
  auto* group = new QButtonGroup(list);
  group->setExclusive(true);
  const int region = wefaxRegion();
  const qint64 dial = liveDialHz_;
  int row = 0;
  for (const FaxStation& station : kFaxStations) {
    if (station.region != region)
      continue;
    auto* name = new QLabel(QStringLiteral("%1  %2").arg(QLatin1String(station.call), QLatin1String(station.place)),
                            list);
    name->setFont(faxUi(12));
    name->setMinimumWidth(240);
    grid->addWidget(name, row, 0);
    for (int i = 0; i < 6 && station.khz[i] > 0.0; ++i) {
      const double khz = station.khz[i];
      const qint64 dialHz = qint64(std::llround(khz * 1000.0)) - kFaxMarkerHz;
      bool onNow = true;
      QString hours;
      const bool limited = faxHours(station.call, khz, &onNow, &hours);
      auto* button = new QPushButton(limited ? QStringLiteral("%1  %2").arg(khz, 0, 'f', 1).arg(hours)
                                             : QString::number(khz, 'f', 1),
                                     list);
      button->setObjectName(QStringLiteral("wefaxFreq"));
      button->setProperty("khz", khz);
      button->setProperty("offAir", limited && !onNow);
      button->setFont(faxUi(11));
      button->setFocusPolicy(Qt::NoFocus);
      button->setCheckable(true);
      button->setChecked(dial == dialHz);
      button->setToolTip(QStringLiteral("%1 %2, assigned %3 kHz%5. Dial %4 Hz USB (1.9 kHz lower), "
                                        "marker 1900 Hz. Charts go out at set times with silence "
                                        "between. This does not key and does not send START.")
                             .arg(QLatin1String(station.call), QLatin1String(station.place))
                             .arg(khz, 0, 'f', 1)
                             .arg(dialHz)
                             .arg(limited ? (onNow ? QStringLiteral(", in use now (%1)").arg(hours)
                                                   : QStringLiteral(", used %1 only, not now").arg(hours))
                                          : QString()));
      group->addButton(button);
      grid->addWidget(button, row, i + 1);
      const QString call = QLatin1String(station.call);
      connect(button, &QPushButton::clicked, this, [this, dialHz, khz, call] {
        const bool open = tci_ && tci_->isOpen();
        qsyDial(dialHz);
        if (waterfall_)
          waterfall_->setRxHz(kFaxMarkerHz);
        setTxStatus(open ? QStringLiteral("%1 %2 kHz. Dial %3 Hz USB, marker 1900 Hz. "
                                          "This does not key. START was not sent.")
                               .arg(call)
                               .arg(khz, 0, 'f', 1)
                               .arg(dialHz)
                         : QStringLiteral("%1 %2 kHz. The radio is not connected, so the VFO was not moved. "
                                          "This does not key. START was not sent.")
                               .arg(call)
                               .arg(khz, 0, 'f', 1));
      });
    }
    ++row;
  }
  grid->setColumnStretch(7, 1);
}

void MainWindow::showWefaxState() {
  auto* state = findChild<QLabel*>(QStringLiteral("wefaxState"));
  if (!state || !wefax_)
    return;
  QString text = wefax_->receiving()
                     ? QStringLiteral("Receiving, %1 lines").arg(wefax_->rowCount())
                     : QStringLiteral("Waiting for a start tone (300 Hz) and phasing. Press Start if the chart "
                                      "is already running.");
  if (!wefaxLastSaved_.isEmpty())
    text += QStringLiteral("   Last saved: %1").arg(QFileInfo(wefaxLastSaved_).fileName());
  state->setText(text);
  state->setToolTip(wefaxLastSaved_.isEmpty() ? QString() : wefaxLastSaved_);
}

// Auto save keeps a picture that Start or CLR RX is about to throw away.
// Fewer than 50 lines is not worth a file.
void MainWindow::wefaxAutoSaveNow() {
  const auto* box = findChild<QCheckBox*>(QStringLiteral("wefaxAutoSave"));
  if (wefax_ && box && box->isChecked() && wefax_->rowCount() >= 50)
    wefax_->saveNow();
}

void MainWindow::installWefaxClick(QLabel* view) {
  view->setCursor(Qt::CrossCursor);
  view->installEventFilter(new FaxClick(view, [this](QLabel* label, QMouseEvent* event) {
    if (!wefax_ || wefax_->picture().isNull())
      return;
    if (event->button() == Qt::RightButton) {
      wefax_->setShift(0);
      setTxStatus(QStringLiteral("WEFAX left edge put back. Nothing was keyed. START was not sent."));
      return;
    }
    const QPixmap shown = label->pixmap();
    if (event->button() != Qt::LeftButton || shown.isNull() || shown.width() < 1)
      return;
    const int width = wefax_->picture().width();
    const int at = int(event->position().x() * double(width) / double(shown.width()));
    wefax_->setShift(wefax_->shift() + at);
    setTxStatus(QStringLiteral("WEFAX left edge moved. Right-click the chart to put it back. "
                               "Nothing was keyed. START was not sent."));
  }));
}
