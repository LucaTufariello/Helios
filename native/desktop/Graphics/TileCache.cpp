#include "TileCache.h"
#include <QBuffer>
#include <QColorSpace>
#include <QDateTime>
#include <QFutureWatcher>
#include <QImageReader>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace godeye {
namespace {
QString key(TileID id, bool streets, bool terrain) {
  return (terrain ? "t:" : streets ? "s:" : "b:") + id.key();
}
} // namespace
TileCache::TileCache(const QString &directory, bool networking, QObject *parent)
    : QObject(parent), network_(this), networking_(networking) {
  auto *disk = new QNetworkDiskCache(this);
  disk->setCacheDirectory(directory);
  disk->setMaximumCacheSize(128 * 1024 * 1024);
  network_.setCache(disk);
}
std::shared_ptr<TileData> TileCache::decode(TileID id, const QByteArray &bytes, bool terrain) {
  if (bytes.size() > (terrain ? 1024 * 1024 : 2 * 1024 * 1024))
    throw std::runtime_error("Oversized map tile");
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  if ((reader.format() != "png" && reader.format() != "jpeg") || reader.size() != QSize(256, 256))
    throw std::runtime_error("Invalid map tile dimensions or encoding");
  auto image = reader.read();
  if (image.isNull())
    throw std::runtime_error("Map tile decoding failed");
  auto result = std::make_shared<TileData>();
  result->id = id;
  if (terrain) {
    result->elevations.resize(256 * 256);
    for (int y = 0; y < 256; ++y)
      for (int x = 0; x < 256; ++x) {
        // Terrarium channels are numeric elevation bytes; never color-convert them.
        const auto p = image.pixelColor(x, y);
        result->elevations[size_t(y * 256 + x)] =
            float(p.red() * 256 + p.green() + p.blue() / 256.0 - 32768);
      }
  } else {
    if (image.colorSpace().isValid())
      image = image.convertedToColorSpace(QColorSpace::SRgb);
    result->image = image.convertToFormat(QImage::Format_RGBA8888);
  }
  return result;
}
void TileCache::request(const QVector<TileID> &tiles, bool streets, bool terrain) {
  QVector<Request> requests;
  for (const auto id : tiles)
    requests.append({id, streets, false, key(id, streets, false)});
  if (terrain)
    for (auto id : tiles) {
      while (id.zoom > 14) {
        id = {id.x / 2, id.y / 2, id.zoom - 1};
      }
      requests.append({id, false, true, key(id, false, true)});
    }
  wanted_.clear();
  queue_.clear();
  const auto now = QDateTime::currentMSecsSinceEpoch();
  for (auto it = failedUntil_.begin(); it != failedUntil_.end();) {
    if (!wanted_.contains(it.key()) && (it.value() <= now || failedUntil_.size() > 512))
      it = failedUntil_.erase(it);
    else
      ++it;
  }
  for (const auto &r : requests) {
    if (wanted_.contains(r.key))
      continue;
    wanted_.insert(r.key);
    if (cache_.contains(r.key)) {
      cache_[r.key]->accessed = ++clock_;
      continue;
    }
    if (!active_.contains(r.key) && failedUntil_.value(r.key) <= now)
      queue_.append(r);
  }
  const auto active = active_;
  for (auto it = active.begin(); it != active.end(); ++it)
    if (!wanted_.contains(it.key()))
      it.value()->abort();
  evict();
  pump();
}
void TileCache::pump() {
  if (!networking_) {
    queue_.clear();
    return;
  }
  while (active_.size() + decoding_ < 4 && !queue_.isEmpty()) {
    const auto r = queue_.takeFirst();
    if (!wanted_.contains(r.key) || active_.contains(r.key) || cache_.contains(r.key))
      continue;
    const QString base =
        r.terrain ? "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/"
        : r.streets
            ? "https://tile.openstreetmap.org/"
            : "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/";
    const QString tail = r.terrain || r.streets
                             ? r.id.key() + ".png"
                             : QString("%1/%2/%3").arg(r.id.zoom).arg(r.id.y).arg(r.id.x);
    QNetworkRequest request{QUrl(base + tail)};
    request.setRawHeader("User-Agent", "GodEyeDesktop/0.1 (native GIS viewer)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setTransferTimeout(20000);
    auto *reply = network_.get(request);
    active_.insert(r.key, reply);
    auto bytes = std::make_shared<QByteArray>();
    const int maximum = r.terrain ? 1024 * 1024 : 2 * 1024 * 1024;
    reply->setReadBufferSize(maximum + 1);
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, maximum] {
      if (reply->isOpen())
        bytes->append(reply->readAll());
      if (bytes->size() > maximum)
        reply->abort();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, maximum] {
      if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maximum)
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, r, bytes, maximum] {
      if (reply->isOpen())
        bytes->append(reply->readAll());
      active_.remove(r.key);
      const bool ok = reply->error() == QNetworkReply::NoError && bytes->size() <= maximum;
      reply->deleteLater();
      if (ok && wanted_.contains(r.key)) {
        ++decoding_;
        using Result = std::pair<std::shared_ptr<TileData>, QString>;
        auto *watcher = new QFutureWatcher<Result>(this);
        connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, r] {
          const auto result = watcher->result();
          watcher->deleteLater();
          --decoding_;
          if (result.first) {
            result.first->revision = ++clock_;
            result.first->accessed = clock_;
            cache_[r.key] = result.first;
            evict();
            if (r.terrain)
              emit terrainChanged();
          } else {
            ++failures_;
            failedUntil_[r.key] = QDateTime::currentMSecsSinceEpoch() + 30000;
          }
          pump();
          emit changed();
        });
        watcher->setFuture(QtConcurrent::run([r, bytes]() -> Result {
          try {
            return {decode(r.id, *bytes, r.terrain), {}};
          } catch (const std::exception &e) {
            return {{}, QString::fromUtf8(e.what())};
          }
        }));
      } else if (wanted_.contains(r.key)) {
        ++failures_;
        failedUntil_[r.key] = QDateTime::currentMSecsSinceEpoch() + 30000;
      }
      pump();
      emit changed();
    });
  }
}
void TileCache::evict() {
  while (cache_.size() > 224) {
    auto candidate = cache_.end();
    quint64 oldest = std::numeric_limits<quint64>::max();
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
      if (!wanted_.contains(it.key()) && it.value()->accessed < oldest) {
        oldest = it.value()->accessed;
        candidate = it;
      }
    }
    if (candidate == cache_.end())
      break;
    cache_.erase(candidate);
  }
}
std::shared_ptr<TileData> TileCache::image(TileID id, bool streets) {
  auto value = cache_.value(key(id, streets, false));
  if (value)
    value->accessed = ++clock_;
  return value;
}
std::optional<double> TileCache::height(Coordinate p) const {
  for (int z = 14; z >= 0; --z) {
    const auto id = TileID::containing(p, z);
    const auto data = cache_.value(key(id, false, true));
    if (!data || data->elevations.size() != 256 * 256)
      continue;
    const double n = 1 << z,
                 lat = std::clamp(p.latitude, -85.05112878, 85.05112878) * std::numbers::pi / 180;
    double tx = (p.longitude + 180) / 360 * n - id.x;
    if (tx > 1)
      tx -= n;
    const double ty = (1 - std::asinh(std::tan(lat)) / std::numbers::pi) / 2 * n - id.y;
    const double x = std::clamp(tx * 256 - 0.5, 0.0, 255.0),
                 y = std::clamp(ty * 256 - 0.5, 0.0, 255.0);
    const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, 255), y1 = std::min(y0 + 1, 255);
    auto h = [&](int px, int py) { return double(data->elevations[size_t(py * 256 + px)]); };
    return std::lerp(std::lerp(h(x0, y0), h(x1, y0), x - x0),
                     std::lerp(h(x0, y1), h(x1, y1), x - x0), y - y0);
  }
  return {};
}
void TileCache::injectTerrain(TileID tile, const std::vector<float> &heights) {
  if (heights.size() != 256 * 256 ||
      !std::all_of(heights.begin(), heights.end(), [](float x) { return std::isfinite(x); }))
    throw std::runtime_error("Invalid terrain samples");
  auto data = std::make_shared<TileData>();
  data->id = tile;
  data->elevations = heights;
  data->revision = ++clock_;
  data->accessed = clock_;
  cache_[key(tile, false, true)] = data;
  emit terrainChanged();
  emit changed();
}
void TileCache::injectImage(TileID tile, const QImage &image, bool streets) {
  if (image.size() != QSize(256, 256))
    throw std::runtime_error("Invalid map tile dimensions");
  auto data = std::make_shared<TileData>();
  data->id = tile;
  data->image = image.convertToFormat(QImage::Format_RGBA8888);
  data->revision = ++clock_;
  data->accessed = clock_;
  cache_[key(tile, streets, false)] = data;
  evict();
  emit changed();
}
} // namespace godeye
