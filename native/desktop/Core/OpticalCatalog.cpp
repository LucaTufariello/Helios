#include "OpticalCatalog.h"
#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace godeye {
namespace {
constexpr qsizetype maximumResponse = 8 * 1024 * 1024;
std::optional<double> number(const QJsonValue &value) {
  bool ok = value.isDouble();
  const double result = value.isString() ? value.toString().toDouble(&ok) : value.toDouble();
  return ok && std::isfinite(result) ? std::optional(result) : std::nullopt;
}
QDateTime acquired(const QString &text) {
  if (!QRegularExpression("(Z|[+-][0-9]{2}:[0-9]{2})$").match(text).hasMatch())
    return {};
  auto date = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (!date.isValid())
    date = QDateTime::fromString(text, Qt::ISODate);
  return date.toUTC();
}
} // namespace
bool OpticalQuery::valid() const {
  return region.validArea() && start.isValid() && end.isValid() && start <= end &&
         start.daysTo(end) <= 366 && std::isfinite(maximumCloud) && maximumCloud >= 0 &&
         maximumCloud <= 100 && (source == "sentinel2" || source == "cbers4a");
}
QUrl OpticalQuery::url() const {
  if (!valid())
    throw std::runtime_error(
        "Choose a nonempty region, up to one year of UTC dates and a cloud limit from 0 to 100.");
  QUrl url(source == "cbers4a" ? "https://data.inpe.br/bdc/stac/v1/search"
                               : "https://earth-search.aws.element84.com/v1/search");
  QUrlQuery query;
  query.addQueryItem("collections", source == "cbers4a" ? "CB4A-WPM-L4-DN-1" : "sentinel-2-c1-l2a");
  QStringList bounds;
  for (double value : {region.west, region.south, region.east, region.north})
    bounds.append(QString::number(value, 'g', 15));
  query.addQueryItem("bbox", bounds.join(','));
  query.addQueryItem("datetime", start.toString(Qt::ISODate) + "T00:00:00.000Z/" +
                                     end.toString(Qt::ISODate) + "T23:59:59.999Z");
  if (source == "sentinel2") {
    query.addQueryItem(
        "query", QString::fromUtf8(QJsonDocument(QJsonObject{{"eo:cloud_cover",
                                                              QJsonObject{{"lte", maximumCloud}}}})
                                       .toJson(QJsonDocument::Compact)));
    query.addQueryItem("sortby", "-properties.datetime");
  }
  query.addQueryItem("limit", "40");
  url.setQuery(query);
  return url;
}
OpticalCatalog::OpticalCatalog(QObject *parent) : QObject(parent) {}
bool OpticalCatalog::validSentinelAsset(const QString &text, const QString &id) {
  const QUrl url(text, QUrl::StrictMode);
  return url.isValid() && url.scheme() == "https" &&
         url.host() == "e84-earth-search-sentinel-data.s3.us-west-2.amazonaws.com" &&
         url.userInfo().isEmpty() && url.port() == -1 && !url.hasQuery() && !url.hasFragment() &&
         !text.contains('%') && !url.path(QUrl::FullyEncoded).contains('%') &&
         !url.path().contains("..") && url.path().startsWith("/sentinel-2-c1-l2a/") &&
         url.path().endsWith("/" + id + "/TCI.tif");
}
QString OpticalCatalog::cbersAsset(const QString &id, const QString &band) {
  if (!QRegularExpression("^CBERS_4A_WPM_[0-9]{8}_[0-9]{3}_[0-9]{3}_L4$").match(id).hasMatch() ||
      !QStringList{"BAND0", "BAND1", "BAND2", "BAND3", "BAND4"}.contains(band))
    return {};
  const auto parts = id.split('_');
  return "https://brazil-eosats.s3.us-west-2.amazonaws.com/CBERS4A/WPM/" + parts[4] + "/" +
         parts[5] + "/" + id + "/" + id + "_" + band + ".tif";
}
QVector<QJsonObject> OpticalCatalog::parse(const QByteArray &bytes, const QString &source) {
  if (bytes.size() > maximumResponse || (source != "sentinel2" && source != "cbers4a"))
    throw std::runtime_error(
        "Optical catalogue response or source exceeds the supported contract.");
  QJsonParseError error;
  const auto root = QJsonDocument::fromJson(bytes, &error).object();
  if (error.error != QJsonParseError::NoError || !root["features"].isArray() ||
      root["features"].toArray().size() > 100)
    throw std::runtime_error("The optical catalogue returned an unsupported response.");
  QVector<QJsonObject> scenes;
  for (const auto value : root["features"].toArray()) {
    const auto feature = value.toObject();
    const auto id = feature["id"].toString();
    const auto properties = feature["properties"].toObject();
    const auto date = properties["datetime"].toString();
    const auto bounds = Bounds::read(feature["bbox"]);
    const auto cloud = number(properties["eo:cloud_cover"]);
    const auto assets = feature["assets"].toObject();
    if (!bounds || !acquired(date).isValid() || (cloud && (*cloud < 0 || *cloud > 100)))
      continue;
    QJsonObject scene{{"id", id},
                      {"acquiredAt", date},
                      {"bounds", feature["bbox"]},
                      {"source", source},
                      {"cloudCover", cloud ? QJsonValue(*cloud) : QJsonValue::Null}};
    if (source == "sentinel2") {
      const auto url = assets["visual"].toObject()["href"].toString();
      const auto platform = properties["platform"].toString("Sentinel-2");
      if (feature["collection"] != "sentinel-2-c1-l2a" || !cloud || platform.size() > 60 ||
          !QRegularExpression("^S2[ABC]_T[A-Z0-9]{5}_[0-9]{8}T[0-9]{6}_L2A$")
               .match(id)
               .hasMatch() ||
          !validSentinelAsset(url, id))
        continue;
      scene["platform"] = platform;
      scene["visualURL"] = url;
    } else {
      if (feature["collection"] != "CB4A-WPM-L4-DN-1" || cbersAsset(id, "BAND0").isEmpty())
        continue;
      bool present = true;
      QJsonObject bands;
      for (int i = 0; i <= 4; ++i) {
        const auto key = "BAND" + QString::number(i);
        if (i < 4 && !assets[key].isObject())
          present = false;
        bands[key] = cbersAsset(id, key);
      }
      if (!present)
        continue;
      scene["platform"] = "CBERS-4A WPM";
      scene["visualURL"] = "";
      scene["bands"] = bands;
    }
    scenes.append(scene);
  }
  return scenes;
}
void OpticalCatalog::cancel() {
  ++generation_;
  if (reply_)
    reply_->abort();
  reply_.clear();
  busy_ = false;
  emit changed();
}
void OpticalCatalog::search(const OpticalQuery &query) {
  cancel();
  QUrl url;
  try {
    url = query.url();
  } catch (const std::exception &e) {
    emit failed(QString::fromUtf8(e.what()));
    return;
  }
  busy_ = true;
  const auto generation = generation_;
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(30000);
  request.setRawHeader("User-Agent", "GodEyeNative/1.0");
  auto *reply = network_.get(request);
  reply_ = reply;
  reply->setReadBufferSize(128 * 1024);
  auto bytes = std::make_shared<QByteArray>();
  auto exceeded = std::make_shared<bool>(false);
  connect(reply, &QNetworkReply::readyRead, this, [reply, bytes, exceeded] {
    const auto chunk = reply->read(maximumResponse - bytes->size() + 1);
    if (chunk.size() > maximumResponse - bytes->size()) {
      *exceeded = true;
      reply->abort();
    } else
      bytes->append(chunk);
  });
  QTimer::singleShot(45000, reply, [reply] {
    if (!reply->isFinished())
      reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, exceeded, query, generation] {
    reply->deleteLater();
    if (generation != generation_)
      return;
    reply_.clear();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (*exceeded || reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
      busy_ = false;
      emit changed();
      emit failed(*exceeded ? "Optical catalogue exceeded 8 MiB."
                            : "Optical catalogue request failed or timed out. Previous saved "
                              "products were preserved.");
      return;
    }
    auto *watcher = new QFutureWatcher<std::pair<QVector<QJsonObject>, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
      const auto result = watcher->result();
      watcher->deleteLater();
      if (generation != generation_)
        return;
      busy_ = false;
      emit changed();
      if (result.second.isEmpty())
        emit results(result.first);
      else
        emit failed(result.second);
    });
    watcher->setFuture(QtConcurrent::run([bytes, query] {
      try {
        auto scenes = parse(*bytes, query.source);
        scenes.erase(std::remove_if(scenes.begin(), scenes.end(),
                                    [&](const auto &scene) {
                                      const auto cloud = number(scene["cloudCover"]);
                                      const auto date =
                                          acquired(scene["acquiredAt"].toString()).date();
                                      return (cloud && *cloud > query.maximumCloud) ||
                                             date < query.start || date > query.end;
                                    }),
                     scenes.end());
        std::sort(scenes.begin(), scenes.end(), [](const auto &a, const auto &b) {
          return acquired(a["acquiredAt"].toString()) > acquired(b["acquiredAt"].toString());
        });
        if (scenes.size() > 40)
          scenes.resize(40);
        return std::make_pair(scenes, QString{});
      } catch (const std::exception &e) {
        return std::make_pair(QVector<QJsonObject>{}, QString::fromUtf8(e.what()));
      }
    }));
  });
  emit changed();
}
} // namespace godeye
