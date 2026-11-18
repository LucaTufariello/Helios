#pragma once
#include "Geography.h"
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <optional>
namespace helios {
struct FeedDescriptor {
  QString id, name, section, credit;
  int intervalSeconds;
};
const QVector<FeedDescriptor> &domainFeeds();
const FeedDescriptor *domainFeed(const QString &id);
struct DomainRecord {
  QString id, feed, label, source;
  Coordinate coordinate;
  std::optional<double> altitude;
  QDateTime observedAt;
  QJsonObject details, raw;
  QVector<Coordinate> footprint;
  QVector<QVector<Coordinate>> paths;
  QJsonObject json() const;
  std::optional<QJsonObject> missionJSON(QString *error = nullptr) const;
};
struct DomainSnapshot {
  QVector<DomainRecord> records;
  QString source, error;
  QStringList notices;
  QDateTime retrievedAt;
  int rejected = 0;
  bool ok() const {
    return error.isEmpty();
  }
};
struct DomainConfiguration {
  QString satelliteGroup = "stations", transitID = "mbta", cameraProvider = "london",
          sentinelKind = "SENTINEL-2";
};
struct TransitProvider {
  QString id, name, attribution, license, licenseUrl;
  QUrl url;
  Coordinate center;
  double radius = 0;
  QMap<QByteArray, QByteArray> headers;
};
QString domainResourcePath(const QString &relative);
QVector<TransitProvider> transitProviders(const QString &path = {});
namespace DomainParsers {
QString clean(const QJsonValue &value, int maximum = 256);
std::optional<double> number(const QJsonValue &value, bool strings = true);
QDateTime utcDate(const QJsonValue &value);
QVector<QVector<Coordinate>> geometry(const QJsonValue &value, int maximumPoints = 50000);
DomainSnapshot parse(const QString &feed, const QByteArray &bytes, Coordinate center, Bounds bounds,
                     const DomainConfiguration &configuration,
                     QDateTime now = QDateTime::currentDateTimeUtc());
DomainSnapshot atmosphere(const QByteArray &weather, const QByteArray &air, Coordinate center,
                          QString airError = {});
DomainSnapshot reference(const QString &feed, const QString &root = {});
DomainSnapshot transit(const QByteArray &bytes, const TransitProvider &provider,
                       QDateTime now = QDateTime::currentDateTimeUtc());
DomainSnapshot traffic(const QByteArray &bytes, TileID tile,
                       QDateTime now = QDateTime::currentDateTimeUtc());
std::optional<DomainRecord> ais(const QByteArray &bytes, QString *error = nullptr);
QUrl cameraURL(const QString &url);
QString mappedQuery(const QString &feed, Bounds bounds, QString *error = nullptr);
} // namespace DomainParsers
} // namespace helios
Q_DECLARE_METATYPE(helios::DomainRecord)
Q_DECLARE_METATYPE(helios::DomainSnapshot)
