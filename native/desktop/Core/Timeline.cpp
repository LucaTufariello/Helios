#include "Timeline.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
namespace godeye {
namespace {
bool overlap(Bounds a, Bounds b) {
  return a.west < b.east && a.east > b.west && a.south < b.north && a.north > b.south;
}
bool contains(Bounds b, Coordinate c) {
  return c.longitude >= b.west && c.longitude <= b.east && c.latitude >= b.south &&
         c.latitude <= b.north;
}
} // namespace
QDateTime missionTime(const QJsonValue &v) {
  if (v.isDouble() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) < 1e11)
    return QDateTime::fromMSecsSinceEpoch(qRound64((v.toDouble() + 978307200.0) * 1000),
                                          QTimeZone::UTC);
  return v.isString() ? QDateTime::fromString(v.toString(), Qt::ISODateWithMs).toUTC()
                      : QDateTime{};
}
QString rasterReference(const ProductLayer &p) {
  return p.kind == "optical" ? "optical:" + p.jobID : p.id;
}
bool TimelineEntry::relates(const QJsonObject &area) const {
  if (areaIDs.contains(area["id"].toString()))
    return true;
  const auto region = Bounds::read(area["region"]);
  return region &&
         ((bounds && overlap(*region, *bounds)) || (coordinate && contains(*region, *coordinate)));
}
QJsonObject TimelineEntry::json() const {
  QJsonArray areas;
  for (const auto &id : areaIDs)
    areas.append(id);
  QJsonObject result{{"id", id},
                     {"kind", kind},
                     {"title", title},
                     {"source", source},
                     {"sourceID", sourceID},
                     {"areaIDs", areas},
                     {"available", available},
                     {"detail", detail}};
  if (time.isValid())
    result["time"] = time.toUTC().toString(Qt::ISODateWithMs);
  if (bounds)
    result["bounds"] = QJsonArray{bounds->west, bounds->south, bounds->east, bounds->north};
  if (coordinate)
    result["coordinate"] = coordinate->json();
  if (!evidenceID.isEmpty())
    result["evidenceID"] = evidenceID;
  if (!rasterID.isEmpty())
    result["rasterID"] = rasterID;
  if (!band.isEmpty())
    result["displayBand"] = band;
  if (resolution > 0)
    result["resolutionMetres"] = resolution;
  return result;
}
bool TimelineFilter::accepts(const TimelineEntry &e) const {
  if (!area.isEmpty() && !e.relates(area))
    return false;
  if (!kind.isEmpty() && e.kind != kind)
    return false;
  if (!query.isEmpty() &&
      !(e.title + " " + e.source + " " + e.detail).contains(query, Qt::CaseInsensitive))
    return false;
  if (!e.time.isValid())
    return includeUndated;
  return (!start.isValid() || e.time >= start) &&
         (!endExclusive.isValid() || e.time < endExclusive);
}
QVector<TimelineEntry> timelineEntries(const QJsonObject &plan,
                                       const QVector<ProductLayer> &library) {
  QVector<TimelineEntry> entries;
  for (const auto value : plan["items"].toArray()) {
    const auto item = value.toObject();
    const auto kind = item["kind"].toString();
    if (kind != "optical" && kind != "radar" && kind != "record")
      continue;
    TimelineEntry e;
    e.id = item["id"].toString();
    e.title = item["title"].toString();
    e.evidenceID = e.id;
    if (kind == "record") {
      const auto record = item["record"].toObject();
      e.kind = "Source observation";
      e.time = missionTime(record["observedAt"]);
      e.source = record["source"].toString();
      e.sourceID = record["id"].toString();
      e.coordinate = Coordinate::read(record["coordinate"].toObject());
      e.detail = "Saved source snapshot; live updates do not change its observation time.";
    } else {
      e.kind = kind == "optical" ? "Optical acquisition" : "Radar acquisition";
      auto acquisition = item["acquisition"].toObject();
      const bool retained = !acquisition.isEmpty();
      const ProductLayer *found = nullptr;
      for (const auto &p : library)
        if (p.kind == kind &&
            p.jobID.compare(item["referenceID"].toString(), Qt::CaseInsensitive) == 0 &&
            (acquisition["displayBand"].toString().isEmpty() ||
             acquisition["displayBand"] == p.band)) {
          found = &p;
          break;
        }
      if (found && acquisition.isEmpty())
        acquisition = found->missionEvidence()["acquisition"].toObject();
      e.available = found && QFileInfo(found->preview).isFile();
      e.source = acquisition["sensor"].toString("Unknown sensor");
      e.sourceID = acquisition["sourceID"].toString();
      e.time = missionTime(acquisition["acquiredAt"]);
      e.bounds = Bounds::read(acquisition["bounds"]);
      e.band = acquisition["displayBand"].toString();
      e.resolution = acquisition["resolutionMetres"].toDouble();
      if (found)
        e.rasterID = rasterReference(*found);
      e.detail = retained ? "Acquisition metadata retained in mission."
                          : "Metadata read from local library; save acquisition metadata to retain "
                            "it after raster removal.";
    }
    for (const auto value : plan["brief"].toObject()["areas"].toArray()) {
      const auto area = value.toObject();
      for (const auto linked : area["dossier"].toObject()["linkedEvidenceIDs"].toArray())
        if (linked.toString().compare(e.evidenceID, Qt::CaseInsensitive) == 0) {
          e.areaIDs.append(area["id"].toString());
          break;
        }
    }
    entries.append(e);
  }
  const auto brief = plan["brief"].toObject();
  for (const auto value : brief["observations"].toArray()) {
    const auto o = value.toObject();
    TimelineEntry e;
    e.id = o["id"].toString();
    e.kind = "Object observation";
    e.title = o["title"].toString();
    e.time = missionTime(o["observedAt"]);
    e.source = o["method"].toString();
    e.bounds = Bounds::read(o["bounds"]);
    e.coordinate = Coordinate::read(o["coordinate"].toObject());
    e.evidenceID = o["evidenceID"].toString();
    if (o["areaID"].isString())
      e.areaIDs.append(o["areaID"].toString());
    e.detail =
        o["state"].toString() + " · " + o["category"].toString() + " · " + o["note"].toString();
    entries.append(e);
  }
  for (const auto value : brief["notes"].toArray()) {
    const auto n = value.toObject();
    TimelineEntry e;
    e.id = n["id"].toString();
    e.kind = "Review note";
    e.title = n["title"].toString();
    e.time = missionTime(n["createdAt"]);
    e.source = "Analyst note";
    e.evidenceID = n["evidenceID"].toString();
    if (n["areaID"].isString())
      e.areaIDs.append(n["areaID"].toString());
    for (const auto &parent : entries)
      if (parent.id == e.evidenceID) {
        e.bounds = parent.bounds;
        e.coordinate = parent.coordinate;
        break;
      }
    e.detail =
        n["body"].toString() +
        (n["reference"].toString().isEmpty() ? QString{} : " · " + n["reference"].toString());
    entries.append(e);
  }
  return entries;
}
QVector<TimelineEntry> sortedTimeline(QVector<TimelineEntry> entries,
                                      const TimelineFilter &filter) {
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&](const auto &e) { return !filter.accepts(e); }),
                entries.end());
  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    if (a.time != b.time)
      return a.time.isValid() && (!b.time.isValid() || a.time > b.time);
    return a.id < b.id;
  });
  return entries;
}
QJsonObject assessTimeline(const QVector<TimelineEntry> &entries, const QJsonObject &area,
                           const QDateTime &since, const QDateTime &now) {
  QSet<QString> optical, radar;
  int missing = 0, undated = 0;
  QDateTime newest;
  for (const auto &e : entries)
    if (e.raster() && e.relates(area)) {
      if (!e.available)
        ++missing;
      if (!e.time.isValid())
        ++undated;
      if (e.available && e.time.isValid() && e.time >= since && e.time <= now) {
        (e.kind == "Optical acquisition" ? optical : radar)
            .insert(e.sourceID.isEmpty() ? e.id : e.sourceID);
        if (!newest.isValid() || e.time > newest)
          newest = e.time;
      }
    }
  QJsonArray gaps;
  if (optical.isEmpty())
    gaps.append("No available dated optical acquisition in the lookback window.");
  if (radar.isEmpty())
    gaps.append("No available dated radar acquisition in the lookback window.");
  if (missing)
    gaps.append(QString("%1 linked raster(s) unavailable locally.").arg(missing));
  if (undated)
    gaps.append(QString("%1 acquisition(s) have unknown observation time.").arg(undated));
  if (std::max(optical.size(), radar.size()) < 2)
    gaps.append("Fewer than two distinct recent acquisitions of either modality; a before/after "
                "pair may be missing.");
  QJsonObject result{{"opticalCount", optical.size()},
                     {"radarCount", radar.size()},
                     {"unavailableCount", missing},
                     {"undatedCount", undated},
                     {"gaps", gaps}};
  if (newest.isValid())
    result["newest"] = newest.toString(Qt::ISODateWithMs);
  return result;
}
QString timelineComparisonIssue(const TimelineEntry &a, const TimelineEntry &b) {
  if (!a.raster() || !b.raster() || !a.available || !b.available || a.rasterID.isEmpty() ||
      b.rasterID.isEmpty())
    return "Both acquisitions need an available local raster preview.";
  if (a.id == b.id || a.rasterID == b.rasterID ||
      (!a.sourceID.isEmpty() && a.sourceID == b.sourceID && a.source == b.source))
    return "Choose two distinct acquisitions; duplicate crops or processing runs are not a new "
           "observation.";
  if (!a.time.isValid() || !b.time.isValid() || a.time >= b.time)
    return "The baseline must have a known acquisition time earlier than the comparison.";
  if (!a.bounds || !b.bounds || !overlap(*a.bounds, *b.bounds))
    return "The acquisition bounds do not overlap, or their location is unknown.";
  return {};
}
QStringList timelineComparisonWarnings(const TimelineEntry &a, const TimelineEntry &b) {
  QStringList result{"Visual comparison only. Bounds overlap does not establish image registration "
                     "or usable pixel coverage."};
  if (a.source != b.source || a.kind != b.kind)
    result.append("Different sensors or modalities can produce apparent changes.");
  if (a.band != b.band)
    result.append("The selected display bands differ.");
  if (a.resolution <= 0 || b.resolution <= 0 || a.resolution != b.resolution)
    result.append("Source pixel spacing differs or is unknown.");
  return result;
}
QByteArray timelineCSV(const QVector<TimelineEntry> &entries) {
  auto cell = [](QString value) {
    if (QString("=+-@").contains(value.trimmed().left(1)) && !value.trimmed().isEmpty())
      value.prepend('\'');
    value.replace('"', "\"\"");
    return '"' + value + '"';
  };
  QString csv = "entry_id,kind,time_utc,time_basis,title,source,source_id,available,evidence_id,"
                "bounds_wsen,detail\r\n";
  for (const auto &e : entries) {
    QString bounds;
    if (e.bounds) {
      QStringList parts;
      for (double x : {e.bounds->west, e.bounds->south, e.bounds->east, e.bounds->north})
        parts.append(QString::number(x, 'g', 15));
      bounds = parts.join(' ');
    }
    QStringList values{e.id,
                       e.kind,
                       e.time.isValid() ? e.time.toUTC().toString(Qt::ISODateWithMs) : QString{},
                       e.kind == "Review note" ? "Note authored" : "Acquired/observed",
                       e.title,
                       e.source,
                       e.sourceID,
                       e.available ? "yes" : "no",
                       e.evidenceID,
                       bounds,
                       e.detail};
    for (auto &v : values)
      v = cell(v);
    csv += values.join(',') + "\r\n";
  }
  return csv.toUtf8();
}
} // namespace godeye
