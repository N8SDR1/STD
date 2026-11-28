// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qrv/QrvClient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace {

const auto kOptIn = QStringLiteral("qrv/optIn");
const auto kNote = QStringLiteral("qrv/note");
const auto kShareHz = QStringLiteral("qrv/shareHz");
const auto kApiBase = QStringLiteral("qrv/apiBase");
const auto kInstallKey = QStringLiteral("qrv/installKey");
const auto kBoundCall = QStringLiteral("qrv/boundCall");

QNetworkRequest jsonRequest(const QString& url) {
  QNetworkRequest request{QUrl(url)};
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/json; charset=utf-8"));
  request.setRawHeader("Accept", "application/json");
  request.setRawHeader("User-Agent", "STD (QRV)");
  request.setTransferTimeout(qrv::kHttpTimeoutMs);
  return request;
}

bool selfTest() {
  return qEnvironmentVariableIsSet("STD_SELFTEST");
}

// STD_QRV_DEBUG=1 writes each QRV decision to qrv-debug.log in the STD data
// folder, so a silent "not posting" can be traced.
void qrvLog(const QString& line) {
  static const bool on = qEnvironmentVariableIsSet("STD_QRV_DEBUG");
  if (!on)
    return;
  static const QString path = [] {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/qrv-debug.log");
  }();
  QFile file(path);
  if (!file.open(QIODevice::Append | QIODevice::Text))
    return;
  QTextStream(&file) << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ' ' << line << Qt::endl;
}

}  // namespace

QrvClient::QrvClient(QObject* parent)
    : QObject(parent),
      nam_(new QNetworkAccessManager(this)),
      heartbeatTimer_(new QTimer(this)),
      debounceTimer_(new QTimer(this)),
      rosterTimer_(new QTimer(this)),
      retryTimer_(new QTimer(this)) {
  heartbeatTimer_->setInterval(qrv::kHeartbeatPeriodMs);
  connect(heartbeatTimer_, &QTimer::timeout, this, [this] { maybeHeartbeat(false); });
  debounceTimer_->setSingleShot(true);
  debounceTimer_->setInterval(qrv::kBandModeDebounceMs);
  connect(debounceTimer_, &QTimer::timeout, this, [this] { maybeHeartbeat(true); });
  retryTimer_->setSingleShot(true);
  connect(retryTimer_, &QTimer::timeout, this, [this] { maybeHeartbeat(true); });
  rosterTimer_->setInterval(qrv::kRosterPeriodMs);
  connect(rosterTimer_, &QTimer::timeout, this, [this] { fetchRoster(); });
  load();
}

void QrvClient::load() {
  QSettings settings;
  optIn_ = settings.value(kOptIn, false).toBool();
  note_ = qrv::sanitizeNote(settings.value(kNote).toString());
  shareHz_ = settings.value(kShareHz, false).toBool();
  apiBase_ = qrv::normalizeApiBase(settings.value(kApiBase, QString::fromUtf8(qrv::kDefaultApiBase)).toString());
  installKey_ = settings.value(kInstallKey).toString().trimmed();
  boundCall_ = qrv::normalizeCall(settings.value(kBoundCall).toString());
}

void QrvClient::persist() {
  QSettings settings;
  settings.setValue(kOptIn, optIn_);
  settings.setValue(kNote, note_);
  settings.setValue(kShareHz, shareHz_);
  settings.setValue(kApiBase, apiBase_);
  settings.setValue(kInstallKey, installKey_);
  settings.setValue(kBoundCall, boundCall_);
}

void QrvClient::setError(const QString& error) {
  if (lastError_ == error)
    return;
  qrvLog(QStringLiteral("error '%1' -> '%2'").arg(lastError_, error));
  lastError_ = error;
  emit lastErrorChanged();
}

bool QrvClient::canPublish() const {
  return optIn_ && radioConnected_ && !hold_ && !qrv::normalizeCall(stationCall_).isEmpty() &&
         !modeToken_.isEmpty() && !qrv::bandForHz(vfoHz_).isEmpty();
}

void QrvClient::setOptIn(bool on) {
  if (optIn_ == on)
    return;
  optIn_ = on;
  hold_ = false;
  persist();
  reconsider();
}

void QrvClient::setShareHz(bool on) {
  if (shareHz_ == on)
    return;
  shareHz_ = on;
  hold_ = false;
  persist();
  if (canPublish() && onAir_)
    debounceTimer_->start();
}

void QrvClient::setNote(const QString& note) {
  const QString cleaned = qrv::sanitizeNote(note);
  if (cleaned == note_)
    return;
  note_ = cleaned;
  hold_ = false;
  persist();
  if (canPublish() && onAir_)
    debounceTimer_->start();
}

void QrvClient::setApiBase(const QString& url) {
  const QString base = qrv::normalizeApiBase(url);
  if (base == apiBase_)
    return;
  apiBase_ = base;
  persist();
}

void QrvClient::setRadioConnected(bool on) {
  if (radioConnected_ == on)
    return;
  radioConnected_ = on;
  if (on)
    hold_ = false;
  reconsider();
}

void QrvClient::setStationCall(const QString& call) {
  const QString next = qrv::normalizeCall(call);
  if (next == stationCall_)
    return;
  const bool changed = !stationCall_.isEmpty() && stationCall_ != next;
  if (changed && !installKey_.isEmpty()) {
    postQrt(false, 0);
    installKey_.clear();
    boundCall_.clear();
    persist();
    lastPostMs_ = QDateTime::currentMSecsSinceEpoch();
    onAir_ = false;
    emit onAirChanged();
  }
  stationCall_ = next;
  hold_ = false;
  reconsider();
}

void QrvClient::setActivity(qint64 hz, const QString& stdMode) {
  const QString token = qrv::qrvModeForStd(stdMode);
  const QString band = qrv::bandForHz(hz);
  const bool bandChanged = !band.isEmpty() && band != lastBand_;
  const bool modeChanged = !token.isEmpty() && token != lastModeSent_;
  vfoHz_ = hz;
  if (token != modeToken_)
    hold_ = false;
  modeToken_ = token;
  if (canPublish() && onAir_ && (bandChanged || modeChanged)) {
    debounceTimer_->start();
    return;
  }
  reconsider();
}

void QrvClient::setRosterWatching(bool on) {
  if (rosterWatching_ == on)
    return;
  rosterWatching_ = on;
  if (rosterWatching_) {
    fetchRoster();
    rosterTimer_->start();
  } else {
    rosterTimer_->stop();
  }
}

void QrvClient::reconsider() {
  qrvLog(QStringLiteral("reconsider optIn=%1 radio=%2 hold=%3 call=%4 mode=%5 hz=%6 band=%7 onAir=%8 inFlight=%9")
             .arg(optIn_).arg(radioConnected_).arg(hold_).arg(stationCall_, modeToken_)
             .arg(vfoHz_).arg(qrv::bandForHz(vfoHz_)).arg(onAir_).arg(inFlight_));
  if (!canPublish()) {
    heartbeatTimer_->stop();
    debounceTimer_->stop();
    retryTimer_->stop();
    if (onAir_) {
      if (inFlight_)
        qrtAfterFlight_ = true;
      else
        postQrt(false, 0);
      onAir_ = false;
      emit onAirChanged();
    }
    if (optIn_ && radioConnected_ && modeToken_.isEmpty() && !qrv::normalizeCall(stationCall_).isEmpty())
      setError(QStringLiteral("QRV has no token for this mode."));
    else if (!optIn_)
      setError(QString());
    return;
  }
  if (!heartbeatTimer_->isActive())
    heartbeatTimer_->start();
  if (!onAir_)
    maybeHeartbeat(true);
}

qrv::HeartbeatRequest QrvClient::currentHeartbeat() const {
  qrv::HeartbeatRequest request;
  request.call = qrv::normalizeCall(stationCall_);
  request.app = QString::fromLatin1(qrv::kAppStd);
  request.band = qrv::bandForHz(vfoHz_);
  request.mode = modeToken_;
  request.installKey = installKey_.trimmed();
  request.note = qrv::sanitizeNote(note_);
  if (shareHz_ && qrv::hzInBand(vfoHz_, request.band)) {
    request.hasHz = true;
    request.hz = vfoHz_;
  }
  return request;
}

void QrvClient::maybeHeartbeat(bool force) {
  if (!canPublish() || inFlight_ || selfTest()) {
    qrvLog(QStringLiteral("maybeHeartbeat skip canPublish=%1 inFlight=%2 selfTest=%3")
               .arg(canPublish()).arg(inFlight_).arg(selfTest()));
    return;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!qrv::intervalElapsed(lastPostMs_, now, force ? qrv::kMinPostIntervalMs : qrv::kHeartbeatPeriodMs)) {
    qrvLog(QStringLiteral("maybeHeartbeat skip interval force=%1 sinceLast=%2ms").arg(force).arg(now - lastPostMs_));
    // A band or mode change that comes too soon after the last post goes out
    // as soon as the board allows, not at the next 25 s heartbeat.
    if (force)
      retryTimer_->start(int(qrv::kMinPostIntervalMs - (now - lastPostMs_)) + 250);
    return;
  }
  retryTimer_->stop();
  postHeartbeat();
}

void QrvClient::postHeartbeat() {
  const qrv::HeartbeatRequest request = currentHeartbeat();
  if (request.call.isEmpty() || request.band.isEmpty() || request.mode.isEmpty() || selfTest()) {
    qrvLog(QStringLiteral("postHeartbeat skip call=%1 band=%2 mode=%3").arg(request.call, request.band, request.mode));
    return;
  }
  qrvLog(QStringLiteral("POST heartbeat %1").arg(QString::fromUtf8(qrv::encodeHeartbeat(request))
                                                     .replace(request.installKey, QStringLiteral("<key>"))));
  inFlight_ = true;
  lastPostMs_ = QDateTime::currentMSecsSinceEpoch();
  lastBand_ = request.band;
  lastModeSent_ = request.mode;
  if (!onAir_) {
    onAir_ = true;
    emit onAirChanged();
  }
  const QString url = qrv::endpointUrl(apiBase_, QStringLiteral("heartbeat.php"));
  QNetworkReply* reply = nam_->post(jsonRequest(url), qrv::encodeHeartbeat(request));
  connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply, QStringLiteral("heartbeat")); });
}

void QrvClient::postQrt(bool blocking, int timeoutMs) {
  const QString call = qrv::normalizeCall(boundCall_.isEmpty() ? stationCall_ : boundCall_);
  if (call.isEmpty() || installKey_.trimmed().isEmpty() || selfTest())
    return;
  qrv::QrtRequest request;
  request.call = call;
  request.app = QString::fromLatin1(qrv::kAppStd);
  request.installKey = installKey_.trimmed();
  const QString url = qrv::endpointUrl(apiBase_, QStringLiteral("qrt.php"));
  QNetworkReply* reply = nam_->post(jsonRequest(url), qrv::encodeQrt(request));
  if (!blocking) {
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply, QStringLiteral("qrt")); });
    return;
  }
  QEventLoop loop;
  QTimer killer;
  killer.setSingleShot(true);
  connect(&killer, &QTimer::timeout, &loop, &QEventLoop::quit);
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  killer.start(timeoutMs > 0 ? timeoutMs : 2500);
  loop.exec();
  onReplyFinished(reply, QStringLiteral("qrt"));
}

void QrvClient::fetchRoster() {
  if (!rosterWatching_ || selfTest())
    return;
  const QString url = qrv::endpointUrl(apiBase_, QStringLiteral("roster.php"));
  QNetworkRequest request{QUrl(url)};
  request.setRawHeader("Accept", "application/json");
  request.setRawHeader("User-Agent", "STD (QRV)");
  request.setTransferTimeout(qrv::kHttpTimeoutMs);
  QNetworkReply* reply = nam_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply, QStringLiteral("roster")); });
}

void QrvClient::flushQrtBlocking(int timeoutMs) {
  heartbeatTimer_->stop();
  debounceTimer_->stop();
  if (!onAir_ && installKey_.isEmpty())
    return;
  if (onAir_ || lastPostMs_ > 0)
    postQrt(true, timeoutMs);
  if (onAir_) {
    onAir_ = false;
    emit onAirChanged();
  }
}

void QrvClient::onReplyFinished(QNetworkReply* reply, const QString& kind) {
  reply->deleteLater();
  if (kind == QLatin1String("heartbeat"))
    inFlight_ = false;

  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray body = reply->readAll();
  if (kind != QLatin1String("roster"))
    qrvLog(QStringLiteral("reply %1 http=%2 net=%3 body=%4")
               .arg(kind).arg(status).arg(int(reply->error())).arg(QString::fromUtf8(body.left(300))));
  if (kind == QLatin1String("roster")) {
    qrv::Roster parsed = qrv::parseRosterResponse(body, status);
    if (reply->error() != QNetworkReply::NoError && !parsed.ok && parsed.error.isEmpty())
      parsed.error = QStringLiteral("QRV unavailable");
    else if (!parsed.ok && parsed.error.isEmpty())
      parsed.error = QStringLiteral("QRV unavailable");
    if (parsed.ok)
      setError(postError_);
    else if (!parsed.error.isEmpty())
      setError(postError_.isEmpty() ? parsed.error : postError_);
    roster_ = parsed;
    emit rosterChanged();
    return;
  }

  qrv::ApiResult parsed = qrv::parseWriteResponse(body, status);
  if (reply->error() != QNetworkReply::NoError && !parsed.ok)
    parsed.error = QStringLiteral("QRV unavailable");
  if (kind == QLatin1String("heartbeat")) {
    if (parsed.ok) {
      if (!parsed.installKey.isEmpty() && parsed.installKey != installKey_) {
        installKey_ = parsed.installKey;
        boundCall_ = qrv::normalizeCall(stationCall_);
        persist();
      } else if (boundCall_.isEmpty()) {
        boundCall_ = qrv::normalizeCall(stationCall_);
        persist();
      }
      postError_.clear();
      setError(QString());
      if (qrtAfterFlight_ || !canPublish()) {
        qrtAfterFlight_ = false;
        postQrt(false, 0);
        if (onAir_) {
          onAir_ = false;
          emit onAirChanged();
        }
      }
    } else {
      const bool stop = parsed.error == QLatin1String("bad_key") ||
                        parsed.error == QLatin1String("call_mismatch") ||
                        parsed.error == QLatin1String("forbidden") ||
                        parsed.error == QLatin1String("invalid_fields") ||
                        parsed.error == QLatin1String("invalid_hz") || status == 400 || status == 401 ||
                        status == 403;
      if (stop) {
        hold_ = true;
        heartbeatTimer_->stop();
        if (onAir_) {
          onAir_ = false;
          emit onAirChanged();
        }
      }
      if (parsed.error == QLatin1String("rate_limited") || status == 429) {
        // Posted too soon after the last one: try again once the board allows.
        postError_ = QStringLiteral("QRV board said too soon; posting again shortly.");
        if (!stop && canPublish())
          retryTimer_->start(qrv::kMinPostIntervalMs);
      } else {
        postError_ = parsed.error.isEmpty() ? QStringLiteral("QRV unavailable") : parsed.error;
      }
      setError(postError_);
    }
    return;
  }
  if (kind == QLatin1String("qrt") && !parsed.ok && !parsed.error.isEmpty() &&
      parsed.error != QLatin1String("QRV unavailable"))
    setError(parsed.error);
}
