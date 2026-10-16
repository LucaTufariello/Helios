#include "RadarDownload.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace godeye {
namespace {
struct Lease {
  int fd = -1;
  explicit Lease(const QString &directory) {
    fd = ::open(QFile::encodeName(directory + "/.download.lock").constData(),
                O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) != 0) {
      ::close(fd);
      fd = -1;
    }
  }
  ~Lease() {
    if (fd >= 0)
      ::close(fd);
  }
};
bool save(const QString &path, const QJsonObject &object) {
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  const auto bytes = QJsonDocument(object).toJson();
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QString statusError(int status) {
  if (status == 401 || status == 403)
    return "Copernicus rejected download authorization. Sign in again and check account access.";
  if (status == 429)
    return "Copernicus download quota reached. Wait before trying again.";
  if (status == 404 || status == 410)
    return "This acquisition is no longer available; search again.";
  return QString("Copernicus download failed (HTTP %1).").arg(status);
}
} // namespace
RadarDownload::RadarDownload(QObject *parent, NetworkFactory factory)
    : QObject(parent), factory_(std::move(factory)) {
  connect(&watcher_, &QFutureWatcherBase::finished, this, [this] {
    const auto result = watcher_.result();
    busy_ = false;
    control_.reset();
    emit changed();
    emit finished(result.file, result.error);
  });
}
RadarDownload::~RadarDownload() {
  cancel();
  watcher_.waitForFinished();
}
void RadarDownload::cancel() {
  if (control_)
    control_->cancelled = true;
}
bool RadarDownload::trusted(const QUrl &url) {
  const auto host = url.host().toLower();
  return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() &&
         !url.hasFragment() && (url.port() == -1 || url.port() == 443) &&
         (host == "download.dataspace.copernicus.eu" ||
          host == "catalogue.dataspace.copernicus.eu");
}
bool RadarDownload::validArchive(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() < 22 ||
      file.read(4) != QByteArray::fromHex("504b0304"))
    return false;
  file.seek(std::max<qint64>(0, file.size() - 65557));
  return file.read(65557).contains(QByteArray::fromHex("504b0506"));
}
QString RadarDownload::savedArchive(const QString &directory, const RadarScene &scene) {
  if (QUuid(scene.id).isNull() || QUuid(QFileInfo(directory).fileName()) != QUuid(scene.id) ||
      QFileInfo(directory).isSymLink() || !RadarScene::supportedName(scene.name))
    return {};
  for (const auto &name : QStringList{scene.archiveFilename(), "product.zip"}) {
    QFileInfo file(QDir(directory).filePath(name));
    if (file.isFile() && !file.isSymLink() &&
        QFileInfo(file.canonicalFilePath()).absolutePath() == QDir(directory).canonicalPath() &&
        validArchive(file.filePath()))
      return file.filePath();
  }
  return {};
}
bool RadarDownload::start(const RadarScene &scene, const RadarQuery &query, const QString &token,
                          const QString &library, qint64 maximum) {
  if (busy_)
    return false;
  if (QUuid(scene.id).isNull() || !RadarScene::supportedName(scene.name) || !query.valid() ||
      !scene.online || scene.contentLength <= 0 || scene.contentLength > maximumBytes ||
      maximum < 1 || maximum > maximumBytes || token.isEmpty() || token.size() > 32768 ||
      !QRegularExpression("^[A-Za-z0-9._~+/-]+=*$").match(token).hasMatch()) {
    emit finished({}, "Choose an online supported GRD acquisition and sign in before downloading.");
    return false;
  }
  busy_ = true;
  control_ = std::make_shared<Control>();
  const auto control = control_;
  const auto factory = factory_;
  const QPointer<RadarDownload> guard(this);
  auto report = [guard, control](qint64 received, qint64 expected) {
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [guard, control, received, expected] {
          if (guard && guard->control_ == control)
            emit guard->progress(received, expected);
        },
        Qt::QueuedConnection);
  };
  watcher_.setFuture(QtConcurrent::run([scene, query, token, library, maximum, control, factory,
                                        report]() -> DownloadResult {
    QString partial, directory;
    auto failure = [&](const QString &error) -> DownloadResult {
      if (!partial.isEmpty())
        QFile::remove(partial);
      return {{}, error};
    };
    if (!QDir().mkpath(library) || QFileInfo(library).isSymLink())
      return failure("The download library cannot be created safely.");
    directory =
        QDir(library).canonicalPath() + "/" + QUuid(scene.id).toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(directory) || QFileInfo(directory).isSymLink())
      return failure("The acquisition directory is unavailable or unsafe.");
    Lease lease(directory);
    if (lease.fd < 0)
      return failure(
          "Another operation owns this acquisition directory; existing data were preserved.");
    const auto destination = directory + "/" + scene.archiveFilename();
    if (QFileInfo::exists(destination))
      return failure(
          "This acquisition already exists. Use its saved archive; it was not replaced.");
    QStorageInfo storage(directory);
    storage.refresh();
    if (storage.isReadOnly() ||
        (storage.bytesAvailable() >= 0 &&
         storage.bytesAvailable() <
             std::max(2ll * 1024 * 1024 * 1024, scene.contentLength + 1024 * 1024 * 1024)))
      return failure("Not enough writable free space for the source archive and safety margin.");
    partial = directory + "/.partial-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QFile output(partial);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
      return failure("The partial download file could not be created.");
    QFile::setPermissions(partial, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto network = factory ? factory() : std::make_unique<QNetworkAccessManager>();
    QUrl url = scene.downloadURL();
    qint64 received = 0, expected = -1;
    QElapsedTimer elapsed, lastProgress;
    elapsed.start();
    lastProgress.start();
    for (int redirects = 0;; ++redirects) {
      if (control->cancelled) {
        output.close();
        return failure("Download cancelled. Partial bytes were removed.");
      }
      if (redirects > 5 || !trusted(url)) {
        output.close();
        return failure("Download redirected outside the trusted Copernicus services; authorization "
                       "was not forwarded.");
      }
      QNetworkRequest request(url);
      request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
      request.setRawHeader("User-Agent", "GodEyeNative/1.0");
      request.setRawHeader("Accept-Encoding", "identity");
      request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                           QNetworkRequest::ManualRedirectPolicy);
      request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
      request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
      request.setTransferTimeout(120000);
      std::unique_ptr<QNetworkReply> reply(network->get(request));
      reply->setReadBufferSize(128 * 1024);
      QEventLoop loop;
      QTimer cancellation;
      cancellation.setInterval(20);
      QString issue;
      const auto read = [&] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
          reply->read(128 * 1024);
          return;
        }
        const auto bytes = reply->read(std::min<qint64>(128 * 1024, maximum - received + 1));
        if (bytes.size() > maximum - received) {
          issue = "Download exceeded its supported byte limit.";
          reply->abort();
          return;
        }
        if (output.write(bytes) != bytes.size()) {
          issue = "Download destination became full or unwritable.";
          reply->abort();
          return;
        }
        received += bytes.size();
        if (lastProgress.elapsed() >= 250) {
          report(received, expected);
          lastProgress.restart();
        }
      };
      QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, read);
      QObject::connect(reply.get(), &QNetworkReply::metaDataChanged, &loop, [&] {
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader);
        expected = length.isValid() ? length.toLongLong() : -1;
        if (expected > maximum) {
          issue = "Download exceeded its supported byte limit.";
          reply->abort();
        }
      });
      QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
      QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
        if (control->cancelled || elapsed.elapsed() > 6ll * 3600 * 1000)
          reply->abort();
      });
      cancellation.start();
      if (!reply->isFinished())
        loop.exec();
      while (reply->bytesAvailable() > 0 && issue.isEmpty())
        read();
      if (control->cancelled) {
        output.close();
        return failure("Download cancelled. Partial bytes were removed.");
      }
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
      if (status >= 300 && status < 400 && !redirect.isEmpty() && issue.isEmpty()) {
        url = url.resolved(redirect);
        continue;
      }
      if (!issue.isEmpty()) {
        output.close();
        return failure(issue);
      }
      if (status != 200) {
        output.close();
        return failure(statusError(status));
      }
      if (reply->error() != QNetworkReply::NoError) {
        output.close();
        return failure("Download failed or timed out. Partial bytes were removed; retry later.");
      }
      if (expected >= 0 && expected != received) {
        output.close();
        return failure(
            "Download length did not match the HTTP response; partial bytes were removed.");
      }
      break;
    }
    if (!output.flush()) {
      output.close();
      return failure("Download could not be flushed to storage.");
    }
    output.close();
    if (!validArchive(partial))
      return failure("Download is incomplete or is not a ZIP archive; partial bytes were removed.");
    if (control->cancelled)
      return failure("Download cancelled. Partial bytes were removed.");
    if (!QFile::rename(partial, destination))
      return failure(
          "The completed archive could not be finalized without replacing existing data.");
    partial.clear();
    const QJsonObject record{
        {"scene", scene.json()},
        {"query", query.json()},
        {"downloadedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (!save(directory + "/download.json", record)) {
      QFile::remove(destination);
      return failure("Download provenance could not be saved atomically; the unpublished archive "
                     "was removed.");
    }
    report(received, received);
    return {destination, {}};
  }));
  emit changed();
  return true;
}
} // namespace godeye
