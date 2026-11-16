// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qrv/QrvClient.h"

#include <QDateTime>
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

}  // namespace

QrvClient::QrvClient(QObject* parent)
    : QObject(parent),
      nam_(new QNetworkAccessManager(this)),
      heartbeatTimer_(new QTimer(this)),
      debounceTimer_(new QTimer(this)),
      rosterTimer_(new QTimer(this)) {
  heartbeatTimer_->setInterval(qrv::kHeartbeatPeriodMs);
  connect(heartbeatTimer_, &QTimer::timeout, this, [this] { maybeHeartbeat(false); });
  debounceTimer_->setSingleShot(true);
  debounceTimer_->setInterval(qrv::kBandModeDebounceMs);
  connect(debounceTimer_, &QTimer::timeout, this, [this] { maybeHeartbeat(true); });
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
  if (!canPublish()) {
    heartbeatTimer_->stop();
    debounceTimer_->stop();
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
  if (!canPublish() || inFlight_ || selfTest())
    return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!qrv::intervalElapsed(lastPostMs_, now, force ? qrv::kMinPostIntervalMs : qrv::kHeartbeatPeriodMs))
    return;
  postHeartbeat();
}

void QrvClient::postHeartbeat() {
  const qrv::HeartbeatRequest request = currentHeartbeat();
  if (request.call.isEmpty() || request.band.isEmpty() || request.mode.isEmpty() || selfTest())
    return;
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
  if (kind == QLatin1String("roster")) {
    qrv::Roster parsed = qrv::parseRosterResponse(body, status);
    if (reply->error() != QNetworkReply::NoError && !parsed.ok && parsed.error.isEmpty())
      parsed.error = QStringLiteral("QRV unavailable");
    else if (!parsed.ok && parsed.error.isEmpty())
      parsed.error = QStringLiteral("QRV unavailable");
    if (parsed.ok)
      setError(QString());
    else if (!parsed.error.isEmpty())
      setError(parsed.error);
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
      setError(parsed.error.isEmpty() ? QStringLiteral("QRV unavailable") : parsed.error);
    }
    return;
  }
  if (kind == QLatin1String("qrt") && !parsed.ok && !parsed.error.isEmpty() &&
      parsed.error != QLatin1String("QRV unavailable"))
    setError(parsed.error);
}
