#include "DomainFeeds.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUuid>
#include <QXmlStreamReader>
#include <cmath>
namespace helios::DomainParsers {
namespace {
QJsonArray prefix(const QJsonArray &array, int limit) {
  QJsonArray out;
  for (int i = 0; i < std::min(limit, int(array.size())); ++i)
    out.append(array[i]);
  return out;
}
QJsonValue fallback(const QJsonObject &object, const QString &key, const QJsonValue &other) {
  return object.contains(key) ? object[key] : other;
}
DomainSnapshot malformed() {
  DomainSnapshot s;
  s.error = "Source response is malformed. Previous records are retained.";
  return s;
}
std::optional<Coordinate> coordinate(const QJsonValue &lat, const QJsonValue &lon,
                                     bool strings = true) {
  auto a = number(lat, strings), b = number(lon, strings);
  if (!a || !b)
    return {};
  Coordinate point{*a, *b};
  if (!point.valid())
    return {};
  return point;
}
bool contains(Bounds bounds, Coordinate p) {
  return bounds.valid() && p.latitude >= bounds.south && p.latitude <= bounds.north &&
         p.longitude >= bounds.west && p.longitude <= bounds.east;
}
DomainRecord record(QString id, QString feed, QString label, Coordinate p, QString source,
                    const QJsonObject &raw = {}) {
  DomainRecord r;
  r.id = id;
  r.feed = feed;
  r.label = label;
  r.coordinate = p;
  r.source = source;
  r.raw = raw;
  return r;
}
QJsonArray array(const QJsonObject &root, const QString &key, bool *ok) {
  *ok = root[key].isArray();
  return root[key].toArray();
}
QString textID(const QJsonValue &value) {
  if (value.isString())
    return clean(value, 256);
  auto n = number(value, false);
  return n && *n == std::floor(*n) && std::abs(*n) <= 9007199254740991.0
             ? QString::number(*n, 'f', 0)
             : QString{};
}
DomainSnapshot earthquakes(const QJsonObject &root) {
  bool ok;
  auto rows = array(root, "features", &ok);
  if (!ok || rows.size() > 100000)
    return malformed();
  DomainSnapshot out;
  out.source = "USGS";
  QSet<QString> seen;
  for (const auto value : rows) {
    auto row = value.toObject(), geo = row["geometry"].toObject(),
         props = row["properties"].toObject();
    auto coords = geo["coordinates"].toArray();
    if (geo["type"] != "Point" || coords.size() < 2)
      return malformed();
    auto point = coordinate(coords[1], coords[0], false);
    auto mag = number(props["mag"], false);
    if (!point || (!props["mag"].isNull() && !mag) || (mag && *mag > 10))
      return malformed();
    if (!mag || *mag < 2.5)
      continue;
    auto id = textID(row["id"]);
    if (id.isEmpty() || seen.contains(id))
      return malformed();
    seen.insert(id);
    QString place = clean(props["place"], 400);
    if (place.isEmpty())
      place = "Location name unavailable";
    auto r = record("usgs:" + id, "earthquakes",
                    "M" + QString::number(*mag, 'f', 1) + " · " + place, *point, "USGS", row);
    if (auto epoch = number(props["time"], false); epoch && std::abs(*epoch) < 8640000000000000.0)
      r.observedAt = QDateTime::fromMSecsSinceEpoch(qint64(*epoch), QTimeZone::UTC);
    r.details = {{"Magnitude", QString::number(*mag, 'f', 1)}, {"Location", place}};
    if (coords.size() > 2 && !coords[2].isNull()) {
      auto depth = number(coords[2], false);
      if (!depth)
        return malformed();
      r.details["Depth"] = QString::number(*depth, 'f', 1) + " km";
    }
    QUrl source(props["url"].toString());
    if (source.scheme() == "https")
      r.details["Source URL"] = source.toString();
    out.records.append(r);
  }
  return out;
}
DomainSnapshot aircraft(const QJsonObject &root, QString feed) {
  DomainSnapshot out;
  out.source = "adsb.lol";
  bool ok;
  auto rows = array(root, "ac", &ok);
  if (!ok || rows.size() > 100000)
    return malformed();
  auto now = number(root["now"]);
  if (now && *now > 10000000000.0)
    *now /= 1000;
  QSet<QString> seen;
  for (const auto value : rows) {
    const auto row = value.toObject();
    auto id = clean(row["hex"], 32).toLower();
    auto point = coordinate(row["lat"], row["lon"]);
    if (id.isEmpty() || !point || seen.contains(id)) {
      ++out.rejected;
      continue;
    }
    seen.insert(id);
    auto label = clean(row["flight"], 160);
    if (label.isEmpty())
      label = clean(row["r"], 160);
    if (label.isEmpty())
      label = id.toUpper();
    auto r = record(feed + ":" + id, feed, label, *point, out.source, row);
    r.details = {{"ICAO", id.toUpper()}};
    if (auto alt = number(row["alt_geom"]))
      r.altitude = *alt * .3048;
    if (now)
      if (auto age = number(row["seen_pos"])) {
        const auto epoch = *now - std::max(0.0, *age);
        if (std::abs(epoch) < 8640000000000.0)
          r.observedAt = QDateTime::fromMSecsSinceEpoch(qint64(epoch * 1000), QTimeZone::UTC);
      }
    if (auto speed = number(row["gs"]))
      r.details["Ground speed"] = QString::number(*speed, 'f', 0) + " kt";
    if (auto baro = number(row["alt_baro"]))
      r.details["Barometric altitude"] = QString::number(*baro / .3048, 'f', 0) + " ft";
    r.details["Type"] = clean(row["t"]);
    r.details["Registration"] = clean(row["r"]);
    r.details["Ground state"] =
        row["alt_baro"] == "ground" ? "Reported on ground" : "Not reported on ground";
    out.records.append(r);
  }
  return out;
}
DomainSnapshot disasters(const QByteArray &bytes) {
  if (bytes.toUpper().contains("<!DOCTYPE") || bytes.toUpper().contains("<!ENTITY"))
    return malformed();
  QXmlStreamReader xml(bytes);
  QMap<QString, QString> fields;
  QString current;
  bool item = false, channel = false;
  DomainSnapshot out;
  out.source = "GDACS";
  QSet<QString> seen;
  int count = 0;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isDTD() || xml.isEntityReference())
      return malformed();
    if (xml.isStartElement()) {
      auto name = xml.qualifiedName().toString();
      if (name == "channel")
        channel = true;
      if (name == "item") {
        item = true;
        fields.clear();
      } else if (item)
        current = name;
    } else if (xml.isCharacters() && item && !current.isEmpty()) {
      fields[current] += xml.text().toString().left(4096 - fields[current].size());
    } else if (xml.isEndElement()) {
      if (xml.qualifiedName() == "item") {
        item = false;
        if (++count > 2000)
          return malformed();
        auto lat = number(fields.value("geo:lat")), lon = number(fields.value("geo:long"));
        if (!lat || !lon) {
          auto pair =
              fields.value("georss:point").split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
          if (pair.size() == 2) {
            lat = number(pair[0]);
            lon = number(pair[1]);
          }
        }
        auto point = lat && lon ? coordinate(*lat, *lon) : std::optional<Coordinate>{};
        const auto title = clean(fields.value("title"), 180), link = fields.value("link").trimmed();
        QUrl url(link);
        const auto id = fields.value("guid", link).trimmed();
        if (!point || title.isEmpty() || id.isEmpty() || seen.contains(id) ||
            url.host() != "www.gdacs.org" || (url.scheme() != "http" && url.scheme() != "https")) {
          ++out.rejected;
          current.clear();
          continue;
        }
        seen.insert(id);
        auto r = record("gdacs:" + id, "disasters", title, *point, "GDACS");
        r.observedAt = QDateTime::fromString(fields.value("pubDate"), Qt::RFC2822Date).toUTC();
        r.details = {{"Source URL", link},
                     {"Timestamp meaning", "Alert publication time"},
                     {"Severity", "Unknown"}};
        const QString severity = title.section(' ', 0, 0).toLower();
        if (QStringList{"red", "orange", "green"}.contains(severity))
          r.details["Severity"] = severity;
        for (auto it = fields.begin(); it != fields.end(); ++it)
          r.raw[it.key()] = it.value();
        out.records.append(r);
      }
      current.clear();
    }
  }
  if (xml.hasError() || !channel)
    return malformed();
  return out;
}
DomainSnapshot fires(const QByteArray &bytes, QDateTime now) {
  if (bytes.contains('\0'))
    return malformed();
  auto lines = QString::fromUtf8(bytes).split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
  if (lines.isEmpty() || lines.size() > 300000)
    return malformed();
  auto columns = lines.takeFirst().trimmed().toLower().split(',');
  QSet<QString> names(columns.begin(), columns.end());
  if (names.size() != columns.size())
    return malformed();
  for (const QString key : {"latitude", "longitude", "acq_date", "acq_time", "confidence", "frp"})
    if (!names.contains(key))
      return malformed();
  DomainSnapshot out;
  out.source = "NASA FIRMS";
  out.retrievedAt = now;
  QSet<QString> seen;
  for (const auto &line : lines) {
    auto cells = line.trimmed().split(',');
    if (cells.size() != columns.size()) {
      ++out.rejected;
      continue;
    }
    QJsonObject row;
    for (int i = 0; i < columns.size(); ++i)
      row[columns[i]] = cells[i].trimmed();
    const QString time = row["acq_time"].toString();
    auto point = coordinate(row["latitude"], row["longitude"]);
    auto at = QDateTime::fromString(row["acq_date"].toString() + " " + time.rightJustified(4, '0'),
                                    "yyyy-MM-dd HHmm");
    at.setTimeZone(QTimeZone::UTC);
    if (!point || !QRegularExpression("^[0-9]{1,4}$").match(time).hasMatch() || !at.isValid()) {
      ++out.rejected;
      continue;
    }
    if (at < now.addSecs(-86400) || at > now.addSecs(7200))
      continue;
    auto id = "firms:" + QString::number(point->latitude, 'g', 15) + ":" +
              QString::number(point->longitude, 'g', 15) + ":" +
              QString::number(at.toSecsSinceEpoch()) + ":" + row["satellite"].toString();
    if (seen.contains(id)) {
      ++out.rejected;
      continue;
    }
    seen.insert(id);
    auto r = record(id, "fires", "Thermal detection · " + row["satellite"].toString("VIIRS"),
                    *point, "NASA FIRMS", row);
    r.observedAt = at;
    r.details = {{"Confidence", row["confidence"]},
                 {"Instrument", row["instrument"]},
                 {"Satellite", row["satellite"]},
                 {"Evidence", "Satellite thermal detection; not a confirmed fire perimeter"},
                 {"Source URL", "https://firms.modaps.eosdis.nasa.gov/"}};
    auto frp = number(row["frp"]);
    r.details["Radiative power"] = frp && *frp >= 0 ? QString::number(*frp) + " MW" : "Unknown";
    out.records.append(r);
  }
  return out;
}
DomainSnapshot mapped(const QJsonObject &root, QString feed, Bounds bounds) {
  bool ok;
  auto rows = array(root, "elements", &ok);
  if (!ok)
    return malformed();
  if (!root["remark"].toString().isEmpty()) {
    auto out = malformed();
    out.error = "Map provider returned an incomplete response. Try a smaller view.";
    return out;
  }
  DomainSnapshot out;
  out.source = "OpenStreetMap / Overpass";
  out.retrievedAt = QDateTime::currentDateTimeUtc();
  QSet<QString> seen;
  const int cap = feed == "installations" ? 700 : 400;
  for (const auto value : prefix(rows, cap)) {
    const auto row = value.toObject(), tags = row["tags"].toObject();
    const QString type = row["type"].toString();
    auto n = number(row["id"], false);
    if (!QStringList{"node", "way", "relation"}.contains(type) || !n || *n <= 0 ||
        *n > 9007199254740991.0 || *n != std::floor(*n) || tags.isEmpty()) {
      ++out.rejected;
      continue;
    }
    QString kind;
    if (feed == "installations") {
      static const QMap<QString, QString> kinds{{"airfield", "Military airfield"},
                                                {"naval_base", "Naval base"},
                                                {"range", "Firing range"},
                                                {"barracks", "Barracks"},
                                                {"base", "Military base"}};
      kind = kinds.value(tags["military"].toString());
      if (kind.isEmpty() && tags["landuse"] == "military")
        kind = "Military land";
    } else if (type != "relation") {
      if (tags["power"] == "plant")
        kind = "Power plant";
      else if (tags["power"] == "substation")
        kind = "Substation";
      else if (tags["industrial"] == "refinery")
        kind = "Refinery";
    }
    if (kind.isEmpty()) {
      ++out.rejected;
      continue;
    }
    QVector<Coordinate> points;
    const auto geometry = row["geometry"].toArray();
    if (geometry.size() <= 400)
      for (const auto v : geometry) {
        auto p = coordinate(v.toObject()["lat"], v.toObject()["lon"], false);
        if (!p) {
          points.clear();
          break;
        }
        points.append(*p);
      }
    auto point = coordinate(row["lat"], row["lon"], false);
    QString method = "Reported OSM point";
    if (!point) {
      const auto center = row["center"].toObject();
      point = coordinate(center["lat"], center["lon"], false);
      method = "OSM representative center";
    }
    if (!point) {
      const auto b = row["bounds"].toObject();
      auto s = number(b["minlat"], false), w = number(b["minlon"], false),
           n = number(b["maxlat"], false), e = number(b["maxlon"], false);
      if (s && w && n && e) {
        Bounds box{*w, *s, *e, *n};
        if (box.valid() && box.north - box.south <= 10 && box.east - box.west <= 10) {
          point = box.center();
          method = "Mapped bounds midpoint; not an entrance";
        }
      }
    }
    if (!point && !points.isEmpty()) {
      Bounds b{points[0].longitude, points[0].latitude, points[0].longitude, points[0].latitude};
      for (auto p : points) {
        b.west = std::min(b.west, p.longitude);
        b.east = std::max(b.east, p.longitude);
        b.south = std::min(b.south, p.latitude);
        b.north = std::max(b.north, p.latitude);
      }
      if (b.east - b.west <= 10 && b.north - b.south <= 10) {
        point = b.center();
        method = "Mapped geometry midpoint; not an entrance";
      }
    }
    if (!point) {
      ++out.rejected;
      continue;
    }
    bool intersects = contains(bounds, *point);
    for (auto p : points)
      intersects |= contains(bounds, p);
    if (!intersects)
      continue;
    auto id = "osm:" + type + ":" + QString::number(*n, 'f', 0);
    if (seen.contains(id)) {
      ++out.rejected;
      continue;
    }
    seen.insert(id);
    QString name = clean(tags["name"], 200);
    if (name.isEmpty())
      name = clean(tags["name:en"], 200);
    if (name.isEmpty())
      name = kind;
    auto r = record(feed + ":" + id, feed, name, *point, "OpenStreetMap", row);
    if (points.size() >= 4 && points.first().latitude == points.last().latitude &&
        points.first().longitude == points.last().longitude)
      r.footprint = points;
    r.details = {
        {"Type", kind},
        {"Location method", method},
        {"Source URL", "https://www.openstreetmap.org/" + type + "/" + QString::number(*n, 'f', 0)},
        {"Evidence", "Community-mapped context; operational state unknown"},
        {"Footprint", r.footprint.isEmpty() ? "Unavailable or unsupported geometry"
                                            : "Selected mapped outline; reference ellipsoid"}};
    for (const QString key : {"operator", "power", "plant:source", "plant:output:electricity",
                              "military", "landuse", "industrial"}) {
      auto v = clean(tags[key]);
      if (!v.isEmpty())
        r.details[key] = v;
    }
    out.records.append(r);
  }
  out.notices = {"Mapped context is incomplete. Representative points are not entrances or "
                 "confirmed operational assets."};
  if (rows.size() >= cap)
    out.notices.append(
        QString("Result limit %1 reached. Coverage may be incomplete; zoom in.").arg(cap));
  return out;
}
} // namespace
QString clean(const QJsonValue &value, int maximum) {
  if (!value.isString())
    return {};
  QString result;
  for (const auto c : value.toString().trimmed())
    if (!c.isNull() && (c.unicode() >= 32 || c == '\n' || c == '\t')) {
      result += c;
      if (result.size() >= maximum)
        break;
    }
  return result;
}
std::optional<double> number(const QJsonValue &value, bool strings) {
  bool ok = value.isDouble();
  double n = value.toDouble();
  if (strings && value.isString())
    n = value.toString().trimmed().toDouble(&ok);
  if (!ok || !std::isfinite(n))
    return {};
  return n;
}
QDateTime utcDate(const QJsonValue &value) {
  if (!value.isString())
    return {};
  const auto text = value.toString();
  if (text.size() > 80 || !QRegularExpression("(Z|[+-][0-9]{2}:[0-9]{2})$").match(text).hasMatch())
    return {};
  auto at = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (!at.isValid())
    at = QDateTime::fromString(text, Qt::ISODate);
  return at.toUTC();
}
QVector<QVector<Coordinate>> geometry(const QJsonValue &value, int maximum) {
  const auto object = value.toObject();
  auto coordinates = object["coordinates"].toArray();
  const QString type = object["type"].toString();
  QVector<QVector<Coordinate>> out;
  int total = 0;
  auto line = [&](const QJsonArray &values) {
    QVector<Coordinate> points;
    if (values.size() > maximum - total)
      return points;
    for (const auto v : values) {
      const auto pair = v.toArray();
      if (pair.size() < 2)
        return QVector<Coordinate>{};
      auto p = coordinate(pair[1], pair[0], false);
      if (!p)
        return QVector<Coordinate>{};
      points.append(*p);
    }
    total += points.size();
    return points;
  };
  if (type == "Point") {
    auto points = line(QJsonArray{coordinates});
    if (!points.isEmpty())
      out.append(points);
  } else if (type == "LineString") {
    auto points = line(coordinates);
    if (points.size() >= 2)
      out.append(points);
  } else if (type == "Polygon" || type == "MultiLineString") {
    for (const auto v : coordinates) {
      auto points = line(v.toArray());
      if (points.size() >= 2)
        out.append(points);
    }
  } else if (type == "MultiPolygon") {
    for (const auto polygon : coordinates)
      for (const auto v : polygon.toArray()) {
        auto points = line(v.toArray());
        if (points.size() >= 2)
          out.append(points);
      }
  }
  return out;
}
QString mappedQuery(const QString &feed, Bounds bounds, QString *error) {
  const double limit = feed == "energy" ? 2 : 10;
  if (!QStringList{"energy", "installations"}.contains(feed) || !bounds.validArea() ||
      bounds.north - bounds.south > limit || bounds.east - bounds.west > limit) {
    if (error)
      *error = QString("Zoom in: maximum query span is %1°.").arg(limit);
    return {};
  }
  const QStringList filters =
      feed == "installations"
          ? QStringList{"nwr[\"military\"=\"airfield\"]", "nwr[\"military\"=\"naval_base\"]",
                        "nwr[\"military\"=\"range\"]",    "nwr[\"military\"=\"barracks\"]",
                        "nwr[\"military\"=\"base\"]",     "nwr[\"landuse\"=\"military\"]"}
          : QStringList{"node[\"power\"=\"plant\"]",         "way[\"power\"=\"plant\"]",
                        "node[\"power\"=\"substation\"]",    "way[\"power\"=\"substation\"]",
                        "node[\"industrial\"=\"refinery\"]", "way[\"industrial\"=\"refinery\"]"};
  QString query = "[out:json][timeout:20];(";
  const QString region = QString("(%1,%2,%3,%4);")
                             .arg(bounds.south, 0, 'g', 15)
                             .arg(bounds.west, 0, 'g', 15)
                             .arg(bounds.north, 0, 'g', 15)
                             .arg(bounds.east, 0, 'g', 15);
  for (const auto &filter : filters)
    query += filter + region;
  return query + QString(");out center tags geom %1;").arg(feed == "installations" ? 700 : 400);
}
DomainSnapshot parse(const QString &feed, const QByteArray &bytes, Coordinate center, Bounds bounds,
                     const DomainConfiguration &configuration, QDateTime now) {
  if (bytes.size() > 24 * 1024 * 1024)
    return malformed();
  if (feed == "disasters")
    return disasters(bytes);
  if (feed == "fires")
    return fires(bytes, now);
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError)
    return malformed();
  const auto root = document.object();
  if (feed == "earthquakes")
    return earthquakes(root);
  if (feed == "military")
    return aircraft(root, feed);
  if (feed == "energy" || feed == "installations")
    return mapped(root, feed, bounds);
  DomainSnapshot out;
  out.retrievedAt = now;
  QSet<QString> seen;
  if (feed == "launches") {
    if (!root["results"].isArray() || root["results"].toArray().size() > 100)
      return malformed();
    out.source = "Launch Library 2";
    for (const auto v : root["results"].toArray()) {
      auto row = v.toObject(), pad = row["pad"].toObject(), location = pad["location"].toObject();
      auto point = coordinate(fallback(pad, "latitude", location["latitude"]),
                              fallback(pad, "longitude", location["longitude"]));
      auto id = clean(row["id"], 128), name = clean(row["name"], 200);
      if (!point || id.isEmpty() || name.isEmpty() || seen.contains(id)) {
        ++out.rejected;
        continue;
      }
      seen.insert(id);
      auto r = record("launch:" + id, feed, name, *point, out.source, row);
      r.observedAt = utcDate(row["net"]);
      r.details = {{"Status", clean(row["status"].toObject()["name"])},
                   {"Launch pad", clean(pad["name"])},
                   {"Mission", clean(row["mission"].toObject()["description"], 1500)},
                   {"Position basis", "Launch-site coordinate, not the rocket's position"},
                   {"Source URL", "https://thespacedevs.com/llapi"}};
      out.records.append(r);
    }
    out.notices = {
        "Previous 30 days. Times are scheduled launch times; orbit matching is not inferred."};
  } else if (feed == "sentinel") {
    if (!root["value"].isArray())
      return malformed();
    out.source = "Copernicus Data Space";
    for (const auto v : prefix(root["value"].toArray(), 10)) {
      const auto row = v.toObject();
      auto id = row["Id"].toString(), name = clean(row["Name"], 200);
      if (QUuid(id).isNull() || name.isEmpty() || seen.contains(id)) {
        ++out.rejected;
        continue;
      }
      seen.insert(id);
      auto r = record("sentinel:" + id, feed, name, center, out.source, row);
      r.observedAt = utcDate(row["ContentDate"].toObject()["Start"]);
      r.paths = geometry(row["GeoFootprint"], 10000);
      if (!r.paths.isEmpty())
        r.footprint = r.paths[0];
      r.details = {{"Product ID", id},
                   {"Collection", configuration.sentinelKind},
                   {"Evidence", "Catalog metadata only; product pixels have not been decoded"},
                   {"Location method", "Search point; footprint shown when supplied"},
                   {"Source URL", "https://browser.dataspace.copernicus.eu/"}};
      for (const auto a : prefix(row["Attributes"].toArray(), 100)) {
        const auto attr = a.toObject();
        auto cloud = number(attr["Value"]);
        if (attr["Name"].toString().toLower() == "cloudcover" && cloud && *cloud >= 0 &&
            *cloud <= 100)
          r.details["Cloud cover"] = QString::number(*cloud) + "%";
      }
      out.records.append(r);
    }
    out.notices = {"Newest catalog products intersecting the view center. Metadata and footprints "
                   "are not a raster overlay or SAR decoder."};
  } else if (feed == "cameras") {
    bool finland = configuration.cameraProvider == "finland";
    if ((!finland && !document.isArray()) || (finland && !root["features"].isArray()))
      return malformed();
    auto rows = finland ? root["features"].toArray() : document.array();
    if (rows.size() > 20000)
      return malformed();
    out.source = finland ? "Fintraffic Finland" : "TfL London";
    for (const auto v : rows) {
      auto row = v.toObject(),
           properties = row["properties"].isObject() ? row["properties"].toObject() : row;
      auto paths = geometry(row["geometry"]);
      auto point = paths.isEmpty() ? coordinate(row["lat"], row["lon"])
                                   : std::optional<Coordinate>(paths[0][0]);
      auto id = textID(fallback(row, "id", properties["id"]));
      auto name = clean(fallback(row, "commonName", properties["name"]), 200);
      if (name.isEmpty())
        name = id;
      if (!point || id.isEmpty()) {
        ++out.rejected;
        continue;
      }
      QVector<std::pair<QString, QString>> presets;
      QString video;
      if (!finland) {
        QMap<QString, QString> props;
        for (const auto a : row["additionalProperties"].toArray())
          props[a.toObject()["key"].toString()] = a.toObject()["value"].toString();
        if (props["available"] == "false" || props["imageUrl"].isEmpty())
          continue;
        presets.append({id, props["imageUrl"]});
        video = props["videoUrl"];
      } else {
        if (properties["collectionStatus"] != "GATHERING")
          continue;
        for (const auto a : prefix(properties["presets"].toArray(), 100)) {
          const auto p = a.toObject();
          const auto preset = p["id"].toString();
          if (p["inCollection"].toBool() &&
              QRegularExpression("^C[0-9]{7}$").match(preset).hasMatch() && preset.startsWith(id))
            presets.append({preset, "https://weathercam.digitraffic.fi/" + preset + ".jpg"});
        }
      }
      for (const auto &preset : presets) {
        if (cameraURL(preset.second).isEmpty() || seen.contains(preset.first)) {
          ++out.rejected;
          continue;
        }
        seen.insert(preset.first);
        if (out.records.size() >= 2000)
          continue;
        auto r = record("camera:" + configuration.cameraProvider + ":" + preset.first, feed, name,
                        *point, out.source, row);
        r.details = {
            {"Frame URL", preset.second},
            {"Evidence", "Public operator camera; frame acquisition time may be unknown"},
            {"Source URL",
             finland ? "https://www.digitraffic.fi/en/road-traffic/"
                     : "https://tfl.gov.uk/corporate/terms-and-conditions/transport-data-service"},
            {"Data license", finland ? "Fintraffic / Digitraffic · CC BY 4.0"
                                     : "TfL Transport Data Service terms"}};
        if (!cameraURL(video).isEmpty())
          r.details["Video URL"] = video;
        out.records.append(r);
      }
    }
    if (seen.size() > 2000)
      out.notices.append("Catalog capped at 2,000 cameras.");
  } else
    return malformed();
  return out;
}
DomainSnapshot reference(const QString &feed, const QString &root) {
  QStringList files;
  if (feed == "datacenters")
    files = {"datacenters/datacenters.geojsonl"};
  else if (feed == "dams")
    files = {"dams/dams.geojsonl"};
  else if (feed == "cables")
    files = {"telegeography_submarine_cables/cable-geo.json",
             "telegeography_submarine_cables/landing-point-geo.json"};
  else
    return malformed();
  DomainSnapshot out;
  out.source = "Bundled " + feed;
  out.notices = {"Reference snapshot; operational state and acquisition date are unknown."};
  QSet<QString> seen;
  int points = 0;
  for (int f = 0; f < files.size(); ++f) {
    QFile file(root.isEmpty() ? domainResourcePath(files[f]) : root + "/" + files[f]);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
      out.error = "Bundled reference data is missing or exceeds its limit.";
      return out;
    }
    QJsonArray rows;
    if (files[f].endsWith("geojsonl"))
      for (const auto &line : file.readAll().split('\n')) {
        if (line.trimmed().isEmpty())
          continue;
        const auto document = QJsonDocument::fromJson(line);
        if (!document.isObject())
          return malformed();
        rows.append(document.object());
        if (rows.size() > 20000)
          return malformed();
      }
    else {
      const auto doc = QJsonDocument::fromJson(file.readAll());
      if (!doc.object()["features"].isArray())
        return malformed();
      rows = doc.object()["features"].toArray();
    }
    if (rows.size() > 20000)
      return malformed();
    for (const auto v : rows) {
      const auto row = v.toObject(), p = row["properties"].toObject();
      auto paths = geometry(row["geometry"]);
      auto tags = p["tags"].isObject() ? p["tags"].toObject() : p;
      if (paths.isEmpty() || p.isEmpty()) {
        ++out.rejected;
        continue;
      }
      for (const auto &path : paths)
        points += path.size();
      if (points > 500000) {
        out.notices.append("Reference geometry budget reached.");
        return out;
      }
      auto name = clean(fallback(tags, "name", p["name"]), 200);
      if (name.isEmpty())
        name = "Unnamed mapped feature";
      auto id = textID(fallback(row, "id", p["id"]));
      if (id.isEmpty())
        id = name;
      if (feed == "cables")
        id = (f == 0 ? "system:" : "landing:") + id;
      id = feed + ":" + id;
      if (seen.contains(id)) {
        ++out.rejected;
        continue;
      }
      seen.insert(id);
      auto r = record(id, feed, name, paths[0][0],
                      feed == "cables" ? "TeleGeography" : "OpenStreetMap", row);
      r.paths = paths;
      if (paths[0].size() > 2)
        r.footprint = paths[0];
      r.details = {
          {"Evidence",
           "Bundled reference snapshot; operational state and acquisition date unknown"},
          {"Location method",
           paths[0].size() == 1 ? "Reported point" : "First reported geometry vertex"},
          {"Data license", feed == "cables" ? "CC BY-NC-SA 3.0 (non-commercial)" : "ODbL 1.0"}};
      for (const QString key : {"operator", "capacity", "power", "waterway", "telecom",
                                "start_date", "plant:output:electricity"}) {
        auto text = clean(tags[key]);
        if (!text.isEmpty())
          r.details[key] = text;
      }
      if (feed == "cables") {
        r.details["Snapshot date"] = "2026-05-24";
        r.details["Source URL"] = "https://www.submarinecablemap.com/";
      }
      out.records.append(r);
    }
  }
  return out;
}
QUrl cameraURL(const QString &raw) {
  QUrl url(raw);
  if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() || url.port() != -1 ||
      url.hasFragment() || url.hasQuery() || raw.contains('%') || url.path().contains(".."))
    return {};
  if ((url.host() == "weathercam.digitraffic.fi" &&
       QRegularExpression("^/C[0-9]{7}\\.jpg$").match(url.path()).hasMatch()) ||
      (url.host() == "s3-eu-west-1.amazonaws.com" &&
       url.path().startsWith("/jamcams.tfl.gov.uk/") &&
       (url.path().endsWith(".jpg") || url.path().endsWith(".mp4"))))
    return url;
  return {};
}
std::optional<DomainRecord> ais(const QByteArray &bytes, QString *error) {
  if (bytes.size() > 256 * 1024) {
    if (error)
      *error = "AIS message exceeded its size limit.";
    return {};
  }
  const auto document = QJsonDocument::fromJson(bytes);
  if (!document.isObject()) {
    if (error)
      *error = "AIS returned malformed JSON.";
    return {};
  }
  const auto root = document.object();
  if (root.contains("error") || root.contains("Error")) {
    if (error)
      *error = "AISStream rejected the subscription. Check the key in Provider Settings.";
    return {};
  }
  const QString type = root["MessageType"].toString();
  if (!QStringList{"PositionReport", "StandardClassBPositionReport", "ExtendedClassBPositionReport"}
           .contains(type))
    return {};
  const auto body = root["Message"].toObject()[type].toObject(), meta = root["MetaData"].toObject();
  const auto mmsi = number(fallback(meta, "MMSI", body["UserID"]));
  auto point = coordinate(body["Latitude"], body["Longitude"]);
  if (!mmsi || *mmsi != std::floor(*mmsi) || *mmsi < 10000 || *mmsi > 999999999 || !point)
    return {};
  const QString id = QString::number(*mmsi, 'f', 0);
  auto name = clean(meta["ShipName"], 160);
  if (name.isEmpty())
    name = "Vessel " + id;
  auto r = record("ais:" + id, "vessels", name, *point, "AISStream", root);
  r.observedAt = utcDate(meta["time_utc"]);
  if (!r.observedAt.isValid()) {
    auto text = meta["time_utc"].toString();
    auto match =
        QRegularExpression(
            "^([0-9]{4}-[0-9]{2}-[0-9]{2}) ([0-9]{2}:[0-9]{2}:[0-9]{2})\\.[0-9]+ \\+0000 UTC$")
            .match(text);
    if (match.hasMatch())
      r.observedAt = utcDate(match.captured(1) + "T" + match.captured(2) + "Z");
  }
  r.details = {{"MMSI", id},
               {"Position basis", "AIS-reported position; reception coverage is incomplete"}};
  if (auto speed = number(body["Sog"]); speed && *speed >= 0 && *speed < 102.3)
    r.details["Speed"] = QString::number(*speed) + " kn";
  if (auto course = number(body["Cog"]); course && *course >= 0 && *course < 360)
    r.details["Course"] = QString::number(*course) + "°";
  return r;
}
} // namespace helios::DomainParsers
