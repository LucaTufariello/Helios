#pragma once
#include "Products.h"
#include <QDateTime>
namespace helios {
QDateTime missionTime(const QJsonValue &value);
struct TimelineEntry {
  QString id, kind, title, source, sourceID, evidenceID, rasterID, band, detail;
  QDateTime time;
  std::optional<Bounds> bounds;
  std::optional<Coordinate> coordinate;
  QStringList areaIDs;
  bool available = true;
  double resolution = 0;
  bool raster() const {
    return kind == "Optical acquisition" || kind == "Radar acquisition";
  }
  bool relates(const QJsonObject &area) const;
  QJsonObject json() const;
};
struct TimelineFilter {
  QJsonObject area;
  QString kind, query;
  QDateTime start, endExclusive;
  bool includeUndated = true;
  bool accepts(const TimelineEntry &entry) const;
};
QVector<TimelineEntry> timelineEntries(const QJsonObject &plan,
                                       const QVector<ProductLayer> &library);
QVector<TimelineEntry> sortedTimeline(QVector<TimelineEntry> entries, const TimelineFilter &filter);
QJsonObject assessTimeline(const QVector<TimelineEntry> &entries, const QJsonObject &area,
                           const QDateTime &since, const QDateTime &now);
QString timelineComparisonIssue(const TimelineEntry &before, const TimelineEntry &after);
QStringList timelineComparisonWarnings(const TimelineEntry &before, const TimelineEntry &after);
QByteArray timelineCSV(const QVector<TimelineEntry> &entries);
QString rasterReference(const ProductLayer &product);
} // namespace helios
