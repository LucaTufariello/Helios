#include "DailyImagery.h"
#include <QBuffer>
#include <QColorSpace>
#include <QDateTime>
#include <QFutureWatcher>
#include <QImageReader>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>
#include <algorithm>
#include <limits>
namespace helios {
namespace {
constexpr qsizetype maxBytes = 2 * 1024 * 1024;
constexpr int maxTiles = 128, maxWanted = 224;
bool sourceValid(DailySource source) {
  return source >= DailySource::Off && source <= DailySource::Thermal;
}
bool dateValid(QDate date) {
  return date.isValid() && date >= QDate(2000, 1, 1) &&
         date <= QDateTime::currentDateTimeUtc().date();
}
TileID ancestor(TileID tile) {
  return {tile.x / 2, tile.y / 2, tile.zoom - 1};
}
bool tileValid(TileID tile) {
  if (tile.zoom < 0 || tile.zoom > 24)
    return false;
  const int count = 1 << tile.zoom;
  return tile.x >= 0 && tile.x < count && tile.y >= 0 && tile.y < count;
}
} // namespace
QString DailyImageryCache::sourceID(DailySource source) {
  switch (source) {
  case DailySource::Off:
    return "off";
  case DailySource::TrueColor:
    return "trueColor";
  case DailySource::Terra:
    return "terra";
  case DailySource::NightLights:
    return "nightLights";
  case DailySource::Thermal:
    return "thermal";
  }
  return {};
}
std::optional<DailySource> DailyImageryCache::readSource(const QString &source) {
  for (const auto value : {DailySource::Off, DailySource::TrueColor, DailySource::Terra,
                           DailySource::NightLights, DailySource::Thermal})
    if (sourceID(value) == source)
      return value;
  return {};
}
QString DailyImageryCache::sourceName(DailySource source) {
  switch (source) {
  case DailySource::Off:
    return "Off";
  case DailySource::TrueColor:
    return "VIIRS true colour";
  case DailySource::Terra:
    return "Terra true colour";
  case DailySource::NightLights:
    return "Night lights (ENCC archive)";
  case DailySource::Thermal:
    return "Thermal anomalies";
  }
  return {};
}
QString DailyImageryCache::resolution(DailySource source) {
  switch (source) {
  case DailySource::TrueColor:
  case DailySource::Terra:
    return "~250 m · neighbourhood scale";
  case DailySource::NightLights:
    return "~500 m · city lights";
  case DailySource::Thermal:
    return "~1 km · thermal detections";
  case DailySource::Off:
    return {};
  }
  return {};
}
int DailyImageryCache::maximumZoom(DailySource source) {
  return source == DailySource::NightLights ? 8 : source == DailySource::Thermal ? 7 : 9;
}
QUrl DailyImageryCache::url(DailySource source, QDate date, TileID tile) {
  if (!sourceValid(source) || source == DailySource::Off || !dateValid(date) || !tileValid(tile) ||
      tile.zoom > maximumZoom(source))
    return {};
  if (source == DailySource::Thermal) {
    // NASA's thermal WMTS layer is now vector; WMS still renders its styled PNG.
    constexpr double extent = 20037508.342789244;
    const double span = 2 * extent / double(1 << tile.zoom);
    QUrl result("https://gibs.earthdata.nasa.gov/wms/epsg3857/best/wms.cgi");
    QUrlQuery query;
    for (const auto &entry :
         QList<QPair<QString, QString>>{{"SERVICE", "WMS"},
                                        {"VERSION", "1.3.0"},
                                        {"REQUEST", "GetMap"},
                                        {"LAYERS", "MODIS_Terra_Thermal_Anomalies_All"},
                                        {"STYLES", ""},
                                        {"FORMAT", "image/png"},
                                        {"TRANSPARENT", "TRUE"},
                                        {"CRS", "EPSG:3857"},
                                        {"WIDTH", "256"},
                                        {"HEIGHT", "256"},
                                        {"TIME", date.toString(Qt::ISODate)},
                                        {"BBOX", QString("%1,%2,%3,%4")
                                                     .arg(-extent + tile.x * span, 0, 'g', 17)
                                                     .arg(extent - (tile.y + 1) * span, 0, 'g', 17)
                                                     .arg(-extent + (tile.x + 1) * span, 0, 'g', 17)
                                                     .arg(extent - tile.y * span, 0, 'g', 17)}})
      query.addQueryItem(entry.first, entry.second);
    result.setQuery(query);
    return result;
  }
  QString layer, format;
  switch (source) {
  case DailySource::TrueColor:
    layer = "VIIRS_SNPP_CorrectedReflectance_TrueColor";
    format = "jpg";
    break;
  case DailySource::Terra:
    layer = "MODIS_Terra_CorrectedReflectance_TrueColor";
    format = "jpg";
    break;
  case DailySource::NightLights:
    layer = "VIIRS_SNPP_DayNightBand_ENCC";
    format = "png";
    break;
  case DailySource::Thermal:
    layer = "MODIS_Terra_Thermal_Anomalies_All";
    format = "png";
    break;
  case DailySource::Off:
    return {};
  }
  return QUrl(QString("https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/%1/default/%2/"
                      "GoogleMapsCompatible_Level%7/%3/%4/%5.%6")
                  .arg(layer, date.toString(Qt::ISODate))
                  .arg(tile.zoom)
                  .arg(tile.y)
                  .arg(tile.x)
                  .arg(format)
                  .arg(maximumZoom(source)));
}
QImage DailyImageryCache::decode(const QByteArray &bytes) {
  if (bytes.isEmpty() || bytes.size() > maxBytes)
    return {};
  QBuffer buffer;
  buffer.setData(bytes);
  if (!buffer.open(QIODevice::ReadOnly))
    return {};
  QImageReader reader(&buffer);
  if ((reader.format() != "png" && reader.format() != "jpeg") || reader.size() != QSize(256, 256) ||
      reader.supportsAnimation())
    return {};
  auto image = reader.read();
  if (image.isNull())
    return {};
  if (image.colorSpace().isValid())
    image = image.convertedToColorSpace(QColorSpace::SRgb);
  return image.convertToFormat(QImage::Format_RGBA8888);
}
DailyImageryCache::DailyImageryCache(const QString &directory, bool networking, QObject *parent,
                                     QNetworkAccessManager *network)
    : QObject(parent), network_(network ? network : new QNetworkAccessManager(this)),
      networking_(networking) {
  if (!network) {
    auto *disk = new QNetworkDiskCache(network_);
    disk->setCacheDirectory(directory);
    disk->setMaximumCacheSize(128 * 1024 * 1024);
    network_->setCache(disk);
  }
}
DailyImageryCache::~DailyImageryCache() {
  ++generation_;
  cancelRequests();
}
void DailyImageryCache::cancelRequests() {
  queue_.clear();
  pending_.clear();
  wanted_.clear();
  const auto active = active_;
  active_.clear();
  for (auto *reply : active) {
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
}
bool DailyImageryCache::configure(DailySource source, QDate date) {
  if (!sourceValid(source) || (source != DailySource::Off && !dateValid(date)))
    return false;
  if (source == source_ && date == date_)
    return true;
  ++generation_;
  cancelRequests();
  source_ = source;
  date_ = date;
  cache_.clear();
  failedUntil_.clear();
  failures_ = 0;
  emit changed();
  return true;
}
QString DailyImageryCache::key(TileID id) const {
  return sourceID(source_) + ':' + date_.toString(Qt::ISODate) + ':' + id.key();
}
void DailyImageryCache::request(const QVector<TileID> &tiles) {
  for (const auto &queued : queue_)
    pending_.remove(queued.key);
  queue_.clear();
  wanted_.clear();
  if (source_ == DailySource::Off) {
    cancelRequests();
    return;
  }
  QVector<TileID> wanted;
  for (auto tile : tiles.first(std::min(qsizetype(64), tiles.size()))) {
    if (!tileValid(tile))
      continue;
    while (tile.zoom > maximumZoom(source_))
      tile = ancestor(tile);
    // Parents appear first so an overview can render while finer tiles arrive.
    QVector<TileID> chain;
    do {
      chain.prepend(tile);
      if (tile.zoom == 0)
        break;
      tile = ancestor(tile);
    } while (true);
    for (auto item : chain) {
      const auto itemKey = key(item);
      if (wanted_.size() >= maxWanted || wanted_.contains(itemKey))
        continue;
      wanted_.insert(itemKey);
      wanted.append(item);
    }
  }
  const auto now = QDateTime::currentMSecsSinceEpoch();
  for (auto it = failedUntil_.begin(); it != failedUntil_.end();)
    if (it.value() <= now || failedUntil_.size() > 512)
      it = failedUntil_.erase(it);
    else
      ++it;
  for (const auto id : wanted) {
    const auto itemKey = key(id);
    if (auto value = cache_.value(itemKey)) {
      value->accessed = ++clock_;
      continue;
    }
    if (!pending_.contains(itemKey) && failedUntil_.value(itemKey) <= now) {
      queue_.append({id, itemKey, generation_});
      pending_.insert(itemKey);
    }
  }
  const auto active = active_;
  for (auto it = active.begin(); it != active.end(); ++it)
    if (!wanted_.contains(it.key()))
      it.value()->abort();
  evict();
  pump();
}
void DailyImageryCache::pump() {
  if (!networking_ || source_ == DailySource::Off) {
    for (const auto &queued : queue_)
      pending_.remove(queued.key);
    queue_.clear();
    return;
  }
  while (active_.size() + decoding_ < 4 && !queue_.isEmpty()) {
    const auto item = queue_.takeFirst();
    if (!wanted_.contains(item.key) || item.generation != generation_ ||
        cache_.contains(item.key)) {
      pending_.remove(item.key);
      continue;
    }
    QNetworkRequest request(url(source_, date_, item.id));
    request.setRawHeader("User-Agent", "Helios/0.1 (native GIS viewer)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(20000);
    auto *reply = network_->get(request);
    reply->setReadBufferSize(maxBytes + 1);
    active_.insert(item.key, reply);
    const auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
      bytes->append(reply->read(maxBytes + 1 - bytes->size()));
      if (bytes->size() > maxBytes)
        reply->abort();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply] {
      if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maxBytes)
        reply->abort();
    });
    QTimer::singleShot(30000, reply, [reply] {
      if (!reply->isFinished())
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, item] {
      if (reply->isOpen())
        bytes->append(reply->read(maxBytes + 1 - bytes->size()));
      active_.remove(item.key);
      const bool ok = reply->error() == QNetworkReply::NoError &&
                      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                      reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isNull() &&
                      bytes->size() <= maxBytes;
      reply->deleteLater();
      if (ok && item.generation == generation_ && wanted_.contains(item.key)) {
        ++decoding_;
        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, item] {
          const auto image = watcher->result();
          watcher->deleteLater();
          --decoding_;
          if (item.generation == generation_) {
            pending_.remove(item.key);
            if (wanted_.contains(item.key)) {
              if (!image.isNull()) {
                auto value = std::make_shared<DailyTile>();
                value->id = item.id;
                value->image = image;
                value->revision = value->accessed = ++clock_;
                cache_[item.key] = value;
                evict();
              } else {
                ++failures_;
                failedUntil_[item.key] = QDateTime::currentMSecsSinceEpoch() + 30000;
              }
            }
          }
          pump();
          emit changed();
        });
        watcher->setFuture(QtConcurrent::run([bytes] { return decode(*bytes); }));
      } else if (item.generation == generation_) {
        pending_.remove(item.key);
        if (wanted_.contains(item.key)) {
          ++failures_;
          failedUntil_[item.key] = QDateTime::currentMSecsSinceEpoch() + 30000;
        }
      }
      pump();
      emit changed();
    });
  }
}
void DailyImageryCache::evict() {
  while (cache_.size() > maxTiles) {
    auto victim = cache_.end();
    auto oldest = std::numeric_limits<quint64>::max();
    for (auto it = cache_.begin(); it != cache_.end(); ++it)
      if (it.value()->accessed < oldest) {
        oldest = it.value()->accessed;
        victim = it;
      }
    if (victim == cache_.end())
      break;
    cache_.erase(victim);
  }
}
std::shared_ptr<DailyTile> DailyImageryCache::image(TileID tile) {
  if (source_ == DailySource::Off || !tileValid(tile))
    return {};
  while (tile.zoom > maximumZoom(source_))
    tile = ancestor(tile);
  while (true) {
    if (auto value = cache_.value(key(tile))) {
      value->accessed = ++clock_;
      return value;
    }
    if (tile.zoom == 0)
      return {};
    tile = ancestor(tile);
  }
}
} // namespace helios
