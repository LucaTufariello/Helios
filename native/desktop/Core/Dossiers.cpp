#include "Dossiers.h"
#include "Timeline.h"
#include <QFileInfo>
#include <QSet>
#include <algorithm>
namespace helios {
namespace {
QString id(const QJsonValue &v) {
  return v.toString().toUpper();
}
bool intersects(Bounds a, Bounds b) {
  return a.west < b.east && a.east > b.west && a.south < b.north && a.north > b.south;
}
bool contains(Bounds b, Coordinate p) {
  return p.longitude >= b.west && p.longitude <= b.east && p.latitude >= b.south &&
         p.latitude <= b.north;
}
std::optional<Bounds> evidenceBounds(const QJsonObject &item) {
  if (auto b = Bounds::read(item["acquisition"].toObject()["bounds"]))
    return b;
  if (auto b = Bounds::read(item["site"].toObject()["region"]))
    return b;
  auto points = item["annotation"].toObject()["points"].toArray();
  if (points.isEmpty())
    points = item["record"].toObject()["footprint"].toArray();
  Bounds b{180, 90, -180, -90};
  for (const auto point : points)
    if (auto p = Coordinate::read(point.toObject())) {
      b.west = std::min(b.west, p->longitude);
      b.east = std::max(b.east, p->longitude);
      b.south = std::min(b.south, p->latitude);
      b.north = std::max(b.north, p->latitude);
    }
  return b.valid() ? std::optional<Bounds>(b) : std::nullopt;
}
} // namespace
bool evidenceOverlaps(const QJsonObject &item, Bounds area) {
  if (auto b = evidenceBounds(item)) {
    if (intersects(*b, area))
      return true;
    if (b->west == b->east && b->south == b->north)
      return contains(area, b->center());
  }
  if (auto p = Coordinate::read(item["record"].toObject()["coordinate"].toObject()))
    return contains(area, *p);
  for (const auto point : item["annotation"].toObject()["points"].toArray())
    if (auto p = Coordinate::read(point.toObject()); p && contains(area, *p))
      return true;
  return false;
}
bool sameAcquisition(const QJsonObject &item, const ProductLayer &product) {
  return item["kind"] == product.kind &&
         item["referenceID"].toString().compare(product.jobID, Qt::CaseInsensitive) == 0 &&
         (item["acquisition"].toObject()["displayBand"].toString().isEmpty() ||
          item["acquisition"].toObject()["displayBand"] == product.band);
}
DossierView dossierView(const QJsonObject &plan, const QString &areaID,
                        const QVector<ProductLayer> &library) {
  DossierView result;
  result.missionID = plan["id"].toString();
  result.areaID = areaID;
  const auto brief = plan["brief"].toObject();
  for (const auto v : brief["areas"].toArray())
    if (id(v.toObject()["id"]) == areaID.toUpper())
      result.area = v.toObject();
  if (result.area.isEmpty())
    return result;
  const auto bounds = Bounds::read(result.area["region"]);
  const auto data = result.area["dossier"].toObject();
  QSet<QString> links, pins, represented;
  for (const auto v : data["linkedEvidenceIDs"].toArray())
    links.insert(id(v));
  for (const auto v : data["baselineEvidenceIDs"].toArray())
    pins.insert(id(v));
  int missing = 0, undated = 0, outside = 0;
  auto describe = [&](QJsonObject item, const ProductLayer *p, const QString &key) {
    DossierEvidence e;
    e.item = item;
    e.key = key;
    e.baseline = pins.contains(id(item["id"]));
    const bool raster = item["kind"] == "optical" || item["kind"] == "radar";
    if (p) {
      e.product = *p;
      e.product->image = {};
    }
    e.available =
        !raster || (p && QFileInfo(p->preview).isFile() && QFileInfo(p->manifest).isFile());
    if (raster) {
      e.time = missionTime(item["acquisition"].toObject()["acquiredAt"]);
      if (!e.available)
        e.warnings.append(
            "Local raster unavailable; retained source metadata remains inspectable.");
      if (!e.time.isValid())
        e.warnings.append("Acquisition time unavailable.");
      if (p && !item["acquisition"].toObject()["sourceID"].toString().isEmpty() &&
          item["acquisition"].toObject()["sourceID"] != p->sourceID)
        e.warnings.append("Local product source differs from the retained evidence snapshot.");
      e.warnings.append(
          "Geographic and radiometric quality are not independently established by this link.");
    } else if (item["kind"] == "record")
      e.time = missionTime(item["record"].toObject()["observedAt"]);
    else if (item["kind"] == "annotation")
      e.time = missionTime(item["annotation"].toObject()["createdAt"]);
    if (bounds && item["kind"] != "view" && !evidenceOverlaps(item, *bounds))
      e.warnings.append("Evidence geometry does not overlap the current area boundary, or its "
                        "coverage is unknown.");
    return e;
  };
  for (const auto v : plan["items"].toArray()) {
    const auto item = v.toObject();
    const ProductLayer *product = nullptr;
    for (const auto &p : library)
      if (sameAcquisition(item, p)) {
        product = &p;
        represented.insert(p.id);
        break;
      }
    auto e = describe(item, product, item["id"].toString());
    if (links.contains(id(item["id"]))) {
      if (!e.available)
        ++missing;
      if ((item["kind"] == "optical" || item["kind"] == "radar") && !e.time.isValid())
        ++undated;
      if (bounds && item["kind"] != "view" && !evidenceOverlaps(item, *bounds))
        ++outside;
      result.linked.append(e);
    } else if (bounds && evidenceOverlaps(item, *bounds))
      result.suggestions.append(e);
  }
  for (const auto &p : library) {
    if (represented.contains(p.id))
      continue;
    auto item = p.missionEvidence();
    if (bounds && evidenceOverlaps(item, *bounds)) {
      item.remove("id");
      result.suggestions.append(describe(item, &p, "product:" + p.id));
    }
  }
  auto sort = [](const DossierEvidence &a, const DossierEvidence &b) {
    if (a.time != b.time)
      return a.time.isValid() && (!b.time.isValid() || a.time > b.time);
    return a.key < b.key;
  };
  std::sort(result.linked.begin(), result.linked.end(), sort);
  std::sort(result.suggestions.begin(), result.suggestions.end(), sort);
  if (result.suggestions.size() > 500) {
    result.suggestions.resize(500);
    result.notices.append("Suggestions are limited to the first 500 local matches.");
  }
  if (missing)
    result.notices.append(QString("%1 linked raster(s) are unavailable locally.").arg(missing));
  if (undated)
    result.notices.append(
        QString("%1 linked raster(s) have unknown acquisition times.").arg(undated));
  if (outside)
    result.notices.append(
        QString("%1 linked item(s) have unknown coverage or lie outside the current boundary.")
            .arg(outside));
  if (pins.isEmpty())
    result.notices.append("No reference acquisition has been pinned.");
  for (auto collection : {"observations", "notes", "tasks"}) {
    QJsonArray values;
    for (const auto v : brief[collection].toArray())
      if (id(v.toObject()["areaID"]) == areaID.toUpper())
        values.append(v);
    if (QString(collection) == "observations")
      result.observations = values;
    else if (QString(collection) == "notes")
      result.notes = values;
    else
      result.tasks = values;
  }
  return result;
}
} // namespace helios
