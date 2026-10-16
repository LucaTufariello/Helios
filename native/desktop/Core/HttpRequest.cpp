#include "HttpRequest.h"
#include <QNetworkReply>
#include <QTimer>
#include <memory>
namespace godeye {
HttpRequest::HttpRequest(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), network_(network ? network : new QNetworkAccessManager(this)) {}
HttpRequest::~HttpRequest() {
  ++generation_;
  if (reply_)
    reply_->abort();
}
void HttpRequest::cancel() {
  ++generation_;
  if (reply_)
    reply_->abort();
  reply_.clear();
  busy_ = false;
  emit changed();
}
void HttpRequest::start(const QUrl &url, qsizetype maximum, const QByteArray &form) {
  cancel();
  if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
      !url.userInfo().isEmpty() || (url.port() != -1 && url.port() != 443) || url.hasFragment() ||
      maximum < 1 || maximum > 16 * 1024 * 1024 || form.size() > 65536) {
    emit completed(0, {}, "Provider request is outside its supported URL or size contract.");
    return;
  }
  QNetworkRequest request(url);
  request.setRawHeader("User-Agent", "GodEyeNative/1.0");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  request.setTransferTimeout(30000);
  if (!form.isEmpty())
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  auto *reply = form.isEmpty() ? network_->get(request) : network_->post(request, form);
  reply_ = reply;
  busy_ = true;
  const auto generation = generation_;
  reply->setReadBufferSize(128 * 1024);
  auto bytes = std::make_shared<QByteArray>();
  auto exceeded = std::make_shared<bool>(false);
  const auto read = [reply, bytes, maximum, exceeded] {
    const auto chunk = reply->read(maximum - bytes->size() + 1);
    if (chunk.size() > maximum - bytes->size()) {
      *exceeded = true;
      reply->abort();
    } else
      bytes->append(chunk);
  };
  connect(reply, &QNetworkReply::readyRead, this, read);
  connect(reply, &QNetworkReply::metaDataChanged, this, [reply, maximum, exceeded] {
    if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maximum) {
      *exceeded = true;
      reply->abort();
    }
  });
  QTimer::singleShot(45000, reply, [reply] {
    if (!reply->isFinished())
      reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, exceeded, generation, read] {
    reply->deleteLater();
    if (generation != generation_)
      return;
    read();
    reply_.clear();
    busy_ = false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString error;
    if (*exceeded)
      error = "Provider response exceeded its configured byte limit.";
    else if (status >= 300 && status < 400)
      error = "Provider redirected the request; no credentials or request body were forwarded.";
    else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
      error = status ? QString("Provider request failed (HTTP %1).").arg(status)
                     : "Provider request failed or timed out. TLS verification remains enabled.";
    emit changed();
    emit completed(status, error.isEmpty() ? *bytes : QByteArray{}, error);
  });
  emit changed();
}
} // namespace godeye
