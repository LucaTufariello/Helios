#pragma once
#include "DomainFeeds.h"
#include "GodEyeSGP4.h"
#include <array>
namespace godeye {
struct DomainSatellite {
  QString name, norad;
  QDateTime epoch;
  GEElements elements{};
  std::optional<std::array<double, 3>> position(double minutes) const;
  std::optional<std::pair<Coordinate, double>> geographic(QDateTime time) const;
};
class DomainOrbit {
public:
  QVector<DomainSatellite> satellites;
  int rejected = 0;
  QDateTime retrievedAt;
  static std::optional<DomainOrbit> parse(const QByteArray &bytes,
                                          QDateTime retrievedAt = QDateTime::currentDateTimeUtc());
  DomainSnapshot snapshot(QDateTime at = QDateTime::currentDateTimeUtc()) const;
  bool orbit(const QString &id, QVector<Coordinate> &points, QVector<double> &altitudes,
             QDateTime at = QDateTime::currentDateTimeUtc()) const;
};
} // namespace godeye
