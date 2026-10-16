#pragma once
#include "Geography.h"
#include <QDate>
#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QSet>
#include <memory>
class QNetworkReply;
namespace godeye {
enum class DailySource { Off, TrueColor, Terra, NightLights, Thermal };
struct DailyTile {
  TileID id;
  QImage image;
  quint64 revision = 0, accessed = 0;
};
// Public NASA visualization tiles; date identifies the requested daily product,
// not the acquisition time of every pixel. Default Off sends no requests.
class DailyImageryCache : public QObject {
  Q_OBJECT
public:
  explicit DailyImageryCache(const QString &cacheDirectory, bool networking,
                             QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
  ~DailyImageryCache() override;
  bool configure(DailySource source, QDate utcDate);
  void request(const QVector<TileID> &wanted);
  // Returns the closest retained parent if the requested tile is unavailable.
  std::shared_ptr<DailyTile> image(TileID tile);
  bool busy() const {
    return !active_.isEmpty() || !queue_.isEmpty() || decoding_ > 0;
  }
  int pending() const {
    return int(active_.size() + queue_.size()) + decoding_;
  }
  int failures() const {
    return failures_;
  }
  int cached() const {
    return int(cache_.size());
  }
  DailySource source() const {
    return source_;
  }
  QDate date() const {
    return date_;
  }
  quint64 generation() const {
    return generation_;
  }
  static QUrl url(DailySource source, QDate utcDate, TileID tile);
  static QImage decode(const QByteArray &bytes);
  static QString sourceName(DailySource source);
  static QString resolution(DailySource source);
  static int maximumZoom(DailySource source);
  static QString sourceID(DailySource source);
  static std::optional<DailySource> readSource(const QString &source);
signals:
  void changed();

private:
  struct Request {
    TileID id;
    QString key;
    quint64 generation;
  };
  QNetworkAccessManager *network_;
  bool networking_;
  DailySource source_ = DailySource::Off;
  QDate date_;
  quint64 generation_ = 0, clock_ = 0;
  int decoding_ = 0, failures_ = 0;
  QHash<QString, std::shared_ptr<DailyTile>> cache_;
  QHash<QString, QNetworkReply *> active_;
  QHash<QString, qint64> failedUntil_;
  QVector<Request> queue_;
  QSet<QString> wanted_, pending_;
  QString key(TileID id) const;
  void cancelRequests();
  void pump();
  void evict();
};
} // namespace godeye
