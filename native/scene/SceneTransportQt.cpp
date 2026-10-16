#include "SceneTransport.h"
#include <CesiumAsync/AsyncSystem.h>
#include <CesiumAsync/IAssetRequest.h>
#include <CesiumAsync/IAssetResponse.h>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <cstring>
#include <stdexcept>

namespace godeye::scene {
namespace {
constexpr qint64 maximumBody = 32 * 1024 * 1024;
class Jobs final : public CesiumAsync::ITaskProcessor {
  QThreadPool pool_;

public:
  Jobs() {
    pool_.setMaxThreadCount(4);
  }
  void startTask(std::function<void()> task) override {
    pool_.start(std::move(task));
  }
};
class Response final : public CesiumAsync::IAssetRequest, public CesiumAsync::IAssetResponse {
public:
  std::string address, type, verb = "GET";
  CesiumAsync::HttpHeaders fields;
  std::vector<std::byte> body;
  uint16_t status = 200;
  const std::string &method() const override {
    return verb;
  }
  const std::string &url() const override {
    return address;
  }
  const CesiumAsync::HttpHeaders &headers() const override {
    return fields;
  }
  const CesiumAsync::IAssetResponse *response() const override {
    return this;
  }
  uint16_t statusCode() const override {
    return status;
  }
  std::string contentType() const override {
    return type;
  }
  std::span<const std::byte> data() const override {
    return body;
  }
};
class QtAssets final : public Assets, public std::enable_shared_from_this<QtAssets> {
  QUrl root_;
  QString directory_;
  std::atomic_bool cancelled_{false};

public:
  explicit QtAssets(QUrl root) : root_(std::move(root)) {
    directory_ = QFileInfo(root_.toLocalFile()).dir().canonicalPath() + QDir::separator();
  }
  bool allows(const QUrl &url) const {
    if (!url.isValid() || !url.userInfo().isEmpty())
      return false;
    if (root_.isLocalFile()) {
      const auto path = QFileInfo(url.toLocalFile()).canonicalFilePath();
      return url.isLocalFile() && url.host().isEmpty() && url.query().isEmpty() &&
             url.fragment().isEmpty() && !path.isEmpty() && path.startsWith(directory_);
    }
    return url.scheme() == "https" && url.host().compare(root_.host(), Qt::CaseInsensitive) == 0 &&
           url.port(443) == root_.port(443);
  }
  void cancel() override {
    cancelled_ = true;
  }
  CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
  get(const CesiumAsync::AsyncSystem &async, const std::string &address,
      const std::vector<THeader> &headers) override {
    auto self = shared_from_this();
    return async.runInWorkerThread([self, address,
                                    headers]() -> std::shared_ptr<CesiumAsync::IAssetRequest> {
      try {
        QUrl url(QString::fromStdString(address), QUrl::StrictMode);
        if (self->cancelled_ || !self->allows(url))
          throw std::runtime_error(
              "3D tile URL is missing or outside the selected origin/directory");
        auto result = std::make_shared<Response>();
        result->address = address;
        QByteArray bytes;
        if (url.isLocalFile()) {
          QFile file(url.toLocalFile());
          if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBody)
            throw std::runtime_error("Local 3D tile exceeds 32 MB or cannot be read");
          bytes = file.read(maximumBody + 1);
          if (bytes.size() > maximumBody || file.error() != QFileDevice::NoError)
            throw std::runtime_error("Local 3D tile could not be read within its limit");
        } else {
          QNetworkAccessManager network;
          for (int redirects = 0;; ++redirects) {
            if (self->cancelled_ || !self->allows(url) || redirects > 5)
              throw std::runtime_error("3D tile redirect is outside the selected origin or limit");
            QNetworkRequest request(url);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
            request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                                 QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                                 QNetworkRequest::Manual);
            request.setTransferTimeout(30000);
            request.setRawHeader("User-Agent", "GodEyeNative/1.0 (3D Tiles)");
            for (const auto &[key, value] : headers)
              request.setRawHeader(QByteArray::fromStdString(key),
                                   QByteArray::fromStdString(value));
            std::unique_ptr<QNetworkReply> reply(network.get(request));
            reply->setReadBufferSize(256 * 1024);
            QEventLoop loop;
            QTimer cancellation, deadline;
            cancellation.setInterval(20);
            deadline.setSingleShot(true);
            bool exceeded = false, expired = false;
            bytes.clear();
            auto read = [&] {
              const auto chunk = reply->read(maximumBody - bytes.size() + 1);
              if (chunk.size() > maximumBody - bytes.size()) {
                exceeded = true;
                reply->abort();
              } else
                bytes.append(chunk);
            };
            QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, read);
            QObject::connect(reply.get(), &QNetworkReply::metaDataChanged, &loop, [&] {
              if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maximumBody) {
                exceeded = true;
                reply->abort();
              }
            });
            QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
              if (self->cancelled_)
                reply->abort();
            });
            QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
              expired = true;
              reply->abort();
            });
            cancellation.start();
            deadline.start(45000);
            if (!reply->isFinished())
              loop.exec();
            read();
            if (self->cancelled_ || exceeded || expired || reply->error() != QNetworkReply::NoError)
              throw std::runtime_error(
                  "3D tile transport failed, was cancelled or exceeded its limit");
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto redirect =
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            if (status >= 300 && status < 400 && !redirect.isEmpty()) {
              url = url.resolved(redirect);
              continue;
            }
            if (status < 200 || status >= 300)
              throw std::runtime_error("3D tile server returned an unsuccessful status");
            result->status = uint16_t(status);
            result->type =
                reply->header(QNetworkRequest::ContentTypeHeader).toString().toStdString();
            for (const auto &name : reply->rawHeaderList())
              result->fields[name.toStdString()] = reply->rawHeader(name).toStdString();
            break;
          }
        }
        if (self->cancelled_)
          throw std::runtime_error("3D tile session stopped");
        result->body.resize(size_t(bytes.size()));
        if (!bytes.isEmpty())
          std::memcpy(result->body.data(), bytes.constData(), size_t(bytes.size()));
        return result;
      } catch (...) {
        ++self->failures;
        throw;
      }
    });
  }
  CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
  request(const CesiumAsync::AsyncSystem &async, const std::string &, const std::string &,
          const std::vector<THeader> &, const std::span<const std::byte> &) override {
    auto promise = async.createPromise<std::shared_ptr<CesiumAsync::IAssetRequest>>();
    promise.reject(std::runtime_error("3D Tiles transport is read-only"));
    return promise.getFuture();
  }
  void tick() noexcept override {}
};
} // namespace
std::shared_ptr<CesiumAsync::ITaskProcessor> makeJobs() {
  return std::make_shared<Jobs>();
}
std::shared_ptr<Assets> makeAssets(const char *address) {
  if (!address)
    return {};
  QUrl url(QString::fromUtf8(address), QUrl::StrictMode);
  if (!url.isValid() || !url.userInfo().isEmpty() || url.isEmpty())
    return {};
  if (url.isLocalFile()) {
    if (!url.host().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty() ||
        QFileInfo(url.toLocalFile()).dir().canonicalPath().isEmpty())
      return {};
  } else if (url.scheme() != "https" || url.host().isEmpty())
    return {};
  return std::make_shared<QtAssets>(std::move(url));
}
} // namespace godeye::scene
