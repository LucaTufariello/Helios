#pragma once
#include "Core/Geography.h"
#include <QHash>
#include <QImage>
#include <QMap>
#include <QNetworkAccessManager>
#include <QSet>
#include <memory>
#include <vector>

namespace godeye {
struct TileData {
  TileID id;
  QImage image;
  std::vector<float> elevations;
  quint64 revision = 0, accessed = 0;
};
class TileCache : public QObject {
  Q_OBJECT
public:
  explicit TileCache(const QString &cacheDirectory, bool networking, QObject *parent = nullptr);
  void request(const QVector<TileID> &tiles, bool streets, bool terrain);
  std::shared_ptr<TileData> image(TileID tile, bool streets);
  std::optional<double> height(Coordinate point) const;
  void injectTerrain(TileID tile, const std::vector<float> &heights);
  void injectImage(TileID tile, const QImage &image, bool streets = false);
  int failures() const {
    return failures_;
  }
  int pending() const {
    return int(active_.size() + queue_.size()) + decoding_;
  }
  int cached() const {
    return int(cache_.size());
  }
  static std::shared_ptr<TileData> decode(TileID tile, const QByteArray &bytes, bool terrain);
signals:
  void changed();
  void terrainChanged();

private:
  struct Request {
    TileID id;
    bool streets = false, terrain = false;
    QString key;
  };
  QNetworkAccessManager network_;
  QMap<QString, std::shared_ptr<TileData>> cache_;
  QHash<QString, QNetworkReply *> active_;
  QHash<QString, qint64> failedUntil_;
  QVector<Request> queue_;
  QSet<QString> wanted_;
  bool networking_;
  quint64 clock_ = 0;
  int failures_ = 0;
  int decoding_ = 0;
  void pump();
  void evict();
};
} // namespace godeye
