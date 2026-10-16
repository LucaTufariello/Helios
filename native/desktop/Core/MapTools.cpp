#include "MapTools.h"
#include "DomainFeeds.h"
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace godeye {
namespace {
constexpr qsizetype documentLimit = 2 * 1024 * 1024;
constexpr double radians = std::numbers::pi / 180;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
bool number(const QJsonValue &v) {
  return v.isDouble() && std::isfinite(v.toDouble());
}
QString clean(const QJsonValue &value, int maximum = 256) {
  if (!value.isString())
    return {};
  auto text = value.toString();
  text.remove(QRegularExpression("[\\x00-\\x1f\\x7f]"));
  return text.trimmed().left(maximum);
}
QJsonDocument decode(const QByteArray &bytes, qsizetype maximum) {
  require(bytes.size() <= maximum, "The response or document exceeds its size limit.");
  QJsonParseError parse;
  auto document = QJsonDocument::fromJson(bytes, &parse);
  require(parse.error == QJsonParseError::NoError && !document.isNull(),
          "Invalid JSON response or document.");
  return document;
}
std::optional<Coordinate> pair(const QJsonValue &value) {
  const auto array = value.toArray();
  if (array.size() < 2 || !number(array[0]) || !number(array[1]))
    return {};
  Coordinate point{array[1].toDouble(), array[0].toDouble()};
  return point.valid() ? std::optional(point) : std::nullopt;
}
QJsonArray pointsJSON(const QVector<Coordinate> &points) {
  QJsonArray values;
  for (auto point : points)
    values.append(point.json());
  return values;
}
bool uid(const QJsonValue &value) {
  return value.isString() && !QUuid(value.toString()).isNull();
}
double authoredNow() {
  return QDateTime::currentMSecsSinceEpoch() / 1000. - 978307200.;
}
const QStringList publicWorkspaceKeys{
    "satelliteGroup", "transitOperator", "cameraProvider", "sentinelCollection",
    "version",        "camera",          "layers",         "basemap",
    "style",          "imagery",         "opacity",        "solar",
    "terrain",        "trackedID",       "annotations",    "imageryDate"};
} // namespace
QUrl placeSearchURL(const QString &raw) {
  const auto query = raw.trimmed();
  if (query.isEmpty() || query.size() > 200 ||
      query.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")))
    return {};
  QUrl url("https://photon.komoot.io/api/");
  QUrlQuery parameters;
  parameters.addQueryItem("q", query);
  parameters.addQueryItem("limit", "6");
  url.setQuery(parameters);
  return url;
}
QVector<PlaceMatch> parsePlaces(const QByteArray &bytes) {
  const auto document = decode(bytes, 1024 * 1024);
  require(document.isObject() && document.object()["features"].isArray(),
          "The place provider returned an invalid feature collection.");
  const auto features = document.object()["features"].toArray();
  require(features.size() <= 100, "The place provider returned too many features.");
  QVector<PlaceMatch> result;
  for (const auto &feature : features) {
    const auto row = feature.toObject(), geometry = row["geometry"].toObject();
    const auto point = pair(geometry["coordinates"]);
    if (geometry["type"] != "Point" || !point || !row["properties"].isObject())
      continue;
    const auto properties = row["properties"].toObject();
    QStringList names;
    for (const auto &field : {"name", "city", "country"}) {
      const auto text = clean(properties[field], 200);
      if (!text.isEmpty() && !names.contains(text))
        names.append(text);
    }
    const auto label = names.join(", ").left(600);
    if (!label.isEmpty())
      result.append({label, *point});
    if (result.size() == 6)
      break;
  }
  return result;
}
QUrl routeURL(Coordinate start, Coordinate end, const QString &mode) {
  if (!start.valid() || !end.valid() || !QStringList{"car", "foot", "bike"}.contains(mode))
    return {};
  const auto delta = subtract(start.ecef(), end.ecef());
  if (std::sqrt(dot(delta, delta)) > 1000000)
    return {};
  const auto profile = mode == "car" ? "driving" : mode;
  QUrl url(QString("https://routing.openstreetmap.de/routed-%1/route/v1/%2/%3,%4;%5,%6")
               .arg(mode, profile)
               .arg(start.longitude, 0, 'g', 17)
               .arg(start.latitude, 0, 'g', 17)
               .arg(end.longitude, 0, 'g', 17)
               .arg(end.latitude, 0, 'g', 17));
  QUrlQuery parameters;
  parameters.addQueryItem("overview", "full");
  parameters.addQueryItem("geometries", "geojson");
  parameters.addQueryItem("steps", "true");
  url.setQuery(parameters);
  return url;
}
NativeRoute parseRoute(const QByteArray &bytes) {
  const auto document = decode(bytes, 8 * 1024 * 1024);
  const auto root = document.object();
  require(document.isObject() && root["code"] == "Ok" && root["routes"].isArray() &&
              !root["routes"].toArray().isEmpty(),
          "No valid route was returned by the routing provider.");
  const auto route = root["routes"].toArray().first().toObject(),
             geometry = route["geometry"].toObject();
  require(number(route["distance"]) && route["distance"].toDouble() >= 0 &&
              route["distance"].toDouble() <= 3000000 && number(route["duration"]) &&
              route["duration"].toDouble() >= 0 && route["duration"].toDouble() < 2000000,
          "The route distance or duration is outside the supported limits.");
  require(geometry["type"] == "LineString" && geometry["coordinates"].isArray(),
          "The route geometry must be a GeoJSON line.");
  const auto coordinates = geometry["coordinates"].toArray();
  require(coordinates.size() >= 2 && coordinates.size() <= 30000,
          "The route geometry exceeds its point limit.");
  NativeRoute result;
  result.distance = route["distance"].toDouble();
  result.duration = route["duration"].toDouble();
  for (const auto &value : coordinates) {
    const auto point = pair(value);
    require(point.has_value(), "The route contains an invalid coordinate.");
    result.points.append(*point);
  }
  const auto legs = route["legs"].toArray();
  require(legs.size() <= 100, "The route has too many legs.");
  for (const auto &leg : legs) {
    const auto steps = leg.toObject()["steps"].toArray();
    require(steps.size() <= 5000, "A route leg has too many steps.");
    for (const auto &entry : steps) {
      if (result.steps.size() == 2000)
        break;
      const auto step = entry.toObject(), maneuver = step["maneuver"].toObject();
      const auto point = pair(maneuver["location"]);
      if (!point || !number(step["distance"]) || step["distance"].toDouble() < 0 ||
          step["distance"].toDouble() > 3000000)
        continue;
      const auto type = clean(maneuver["type"], 40), modifier = clean(maneuver["modifier"], 40),
                 road = clean(step["name"], 256);
      const QHash<QString, QString> actions{{"depart", "Start"},
                                            {"arrive", "Arrive at your destination"},
                                            {"turn", "Turn"},
                                            {"end of road", "At the end of the road, turn"},
                                            {"fork", "At the fork, keep"},
                                            {"merge", "Merge"},
                                            {"roundabout", "Enter the roundabout"},
                                            {"rotary", "Enter the roundabout"},
                                            {"exit roundabout", "Exit the roundabout"},
                                            {"exit rotary", "Exit the roundabout"}};
      auto instruction = actions.value(type, "Continue");
      if (!modifier.isEmpty() && !QStringList{"arrive", "roundabout", "rotary"}.contains(type))
        instruction += " " + QString(modifier).replace("slight ", "slightly ");
      if ((type == "roundabout" || type == "rotary") && number(maneuver["exit"]) &&
          maneuver["exit"].toDouble() >= 1 && maneuver["exit"].toDouble() < 100)
        instruction += ", take exit " + QString::number(int(maneuver["exit"].toDouble()));
      if (!road.isEmpty() && type != "arrive")
        instruction += " onto " + road;
      result.steps.append({instruction.left(500), step["distance"].toDouble(), *point});
    }
  }
  return result;
}
MapLookup::MapLookup(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), request_(nullptr, network) {
  connect(&request_, &HttpRequest::completed, this,
          [this](int status, QByteArray bytes, QString error) {
            if (!busy_)
              return;
            const auto generation = generation_;
            if (!error.isEmpty() || status != 200) {
              busy_ = false;
              error_ = error.isEmpty() ? QString("The map provider returned HTTP %1.").arg(status)
                                       : error;
              emit changed();
              emit completed(false);
              return;
            }
            struct Parsed {
              QVector<PlaceMatch> places;
              NativeRoute route;
              QString error;
            };
            auto *watcher = new QFutureWatcher<Parsed>(this);
            parsing_ = true;
            const bool routing = routing_;
            connect(watcher, &QFutureWatcher<Parsed>::finished, this,
                    [this, watcher, generation, routing] {
                      const auto result = watcher->result();
                      watcher->deleteLater();
                      parsing_ = false;
                      if (generation != generation_) {
                        emit changed();
                        return;
                      }
                      busy_ = false;
                      error_ = result.error;
                      if (result.error.isEmpty()) {
                        if (routing)
                          route_ = result.route;
                        else
                          places_ = result.places;
                      }
                      emit changed();
                      emit completed(error_.isEmpty());
                    });
            watcher->setFuture(QtConcurrent::run([bytes = std::move(bytes), routing] {
              Parsed result;
              try {
                if (routing)
                  result.route = parseRoute(bytes);
                else
                  result.places = parsePlaces(bytes);
              } catch (const std::exception &e) {
                result.error = QString::fromUtf8(e.what());
              }
              return result;
            }));
          });
}
MapLookup::~MapLookup() {
  ++generation_;
  disconnect(&request_, nullptr, this, nullptr);
  request_.cancel();
}
bool MapLookup::start(const QUrl &url, bool routing) {
  if (parsing_) {
    error_ = "The previous bounded response is still finishing. Retry in a moment.";
    emit changed();
    return false;
  }
  cancel();
  routing_ = routing;
  if (url.isEmpty()) {
    error_ = routing ? "Choose valid endpoints within 1,000 km and car, foot or bike travel."
                     : "Enter a place query of 1–200 characters without control characters.";
    emit changed();
    return false;
  }
  error_.clear();
  busy_ = true;
  request_.start(url, routing ? 8 * 1024 * 1024 : 1024 * 1024);
  emit changed();
  return true;
}
bool MapLookup::search(const QString &query) {
  return start(placeSearchURL(query), false);
}
bool MapLookup::route(Coordinate start, Coordinate end, const QString &mode) {
  return this->start(routeURL(start, end, mode), true);
}
void MapLookup::cancel() {
  if (busy_ || parsing_)
    error_ = "Map lookup cancelled.";
  ++generation_;
  busy_ = false;
  request_.cancel();
  emit changed();
}
void validateAnnotations(const QJsonArray &values) {
  require(values.size() <= 500, "The annotation library exceeds 500 entries.");
  QSet<QString> ids;
  for (const auto &value : values) {
    const auto item = value.toObject();
    require(value.isObject() && uid(item["id"]) && !ids.contains(item["id"].toString().toLower()),
            "The annotation library contains an invalid or duplicate UUID.");
    const auto title = item["title"];
    require(title.isString() && !title.toString().trimmed().isEmpty() &&
                title.toString().size() <= 256 && item["closed"].isBool() &&
                number(item["createdAt"]) && item["points"].isArray(),
            "An annotation has invalid text, timestamp or geometry fields.");
    const auto points = item["points"].toArray();
    require(points.size() >= (item["closed"].toBool() ? 3 : 1) && points.size() <= 2000,
            "An annotation exceeds its vertex limits.");
    for (const auto &point : points)
      require(point.isObject() && Coordinate::read(point.toObject()).has_value(),
              "An annotation contains an invalid coordinate.");
    ids.insert(item["id"].toString().toLower());
  }
}
QJsonObject newAnnotation(const QString &title, const QVector<Coordinate> &points, bool closed) {
  QJsonObject value{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {"title", title},
                    {"points", pointsJSON(points)},
                    {"closed", closed},
                    {"createdAt", authoredNow()}};
  validateAnnotations({value});
  return value;
}
AnnotationLibrary::AnnotationLibrary(QString file)
    : file_(std::move(file), documentLimit, [](const QJsonDocument &document) {
        require(document.isArray(), "Expected an annotation JSON array.");
        validateAnnotations(document.array());
      }) {}
void AnnotationLibrary::load() {
  values_ = file_.load(QJsonDocument(QJsonArray{})).array();
}
void AnnotationLibrary::replace(const QJsonArray &values) {
  file_.save(QJsonDocument(values));
  values_ = values;
}
void AnnotationLibrary::add(const QJsonObject &value) {
  auto next = values_;
  next.append(value);
  replace(next);
}
void AnnotationLibrary::update(const QString &id, const QString &title,
                               const QVector<Coordinate> &points, bool closed) {
  auto next = values_;
  for (qsizetype i = 0; i < next.size(); ++i)
    if (next[i].toObject()["id"].toString() == id) {
      auto item = next[i].toObject();
      item["title"] = title;
      item["points"] = pointsJSON(points);
      item["closed"] = closed;
      next[i] = item;
      replace(next);
      return;
    }
  throw std::runtime_error("The selected annotation no longer exists.");
}
void AnnotationLibrary::remove(const QString &id) {
  auto next = values_;
  for (qsizetype i = 0; i < next.size(); ++i)
    if (next[i].toObject()["id"].toString() == id) {
      next.removeAt(i);
      replace(next);
      return;
    }
  throw std::runtime_error("The selected annotation no longer exists.");
}
QJsonObject defaultWorkspace() {
  return {{"version", 1},
          {"satelliteGroup", "stations"},
          {"transitOperator", "mbta"},
          {"cameraProvider", "london"},
          {"sentinelCollection", "SENTINEL-2"},
          {"camera", Camera{}.json()},
          {"layers", QJsonArray{}},
          {"basemap", "satellite"},
          {"style", "standard"},
          {"imagery", "off"},
          {"opacity", .85},
          {"solar", false},
          {"terrain", true},
          {"annotations", QJsonArray{}}};
}
QJsonObject validatedWorkspace(QJsonObject value) {
  require(value["version"] == 1 && Camera::read(value["camera"].toObject()).has_value(),
          "Unsupported workspace version or camera.");
  const auto oneOf = [&](const char *key, QStringList choices) {
    require(value[key].isString() && choices.contains(value[key].toString()),
            "The workspace contains an unsupported provider or display choice.");
  };
  oneOf("satelliteGroup", {"stations", "active", "starlink", "weather", "gps-ops"});
  oneOf("cameraProvider", {"london", "finland"});
  oneOf("sentinelCollection", {"SENTINEL-1", "SENTINEL-2"});
  oneOf("basemap", {"satellite", "streets"});
  oneOf("style", {"standard", "monochrome", "nightVision", "thermal"});
  oneOf("imagery", {"off", "trueColor", "terra", "nightLights", "thermal"});
  QStringList operators{"mbta",     "capmetro-austin", "metrotransit-msp", "hsl-helsinki",
                        "ovapi-nl", "entur-norway",    "translink-seq"};
  for (const auto &provider : transitProviders())
    if (!operators.contains(provider.id))
      operators.append(provider.id);
  oneOf("transitOperator", operators);
  require(number(value["opacity"]) && value["opacity"].toDouble() >= 0 &&
              value["opacity"].toDouble() <= 1 && value["solar"].isBool() &&
              (!value.contains("terrain") || value["terrain"].isBool()),
          "The workspace has invalid opacity or layer visibility.");
  if (!value.contains("terrain"))
    value["terrain"] = true;
  require(value["layers"].isArray() && value["layers"].toArray().size() <= 32 &&
              value["annotations"].isArray(),
          "The workspace layers or annotations are invalid.");
  QJsonArray layers;
  QSet<QString> seen;
  for (const auto &item : value["layers"].toArray()) {
    require(item.isString(), "The workspace contains a non-text layer identifier.");
    const auto name = item.toString();
    if (name == "neighborhoods")
      continue;
    require(domainFeed(name) != nullptr, "The workspace contains an unknown data source.");
    if (!seen.contains(name)) {
      layers.append(name);
      seen.insert(name);
    }
  }
  value["layers"] = layers;
  validateAnnotations(value["annotations"].toArray());
  if (value.contains("trackedID") && !value["trackedID"].isNull()) {
    require(value["trackedID"].isString() && value["trackedID"].toString().size() <= 200,
            "The tracked record identifier is invalid.");
    if (value["trackedID"].toString().startsWith("neighborhoods:"))
      value.remove("trackedID");
  }
  if (value.contains("imageryDate")) {
    const auto raw = value["imageryDate"].toString();
    const auto date = QDate::fromString(raw, Qt::ISODate);
    require(date.isValid() && raw == date.toString(Qt::ISODate),
            "The selected imagery date is invalid.");
    if (value["imagery"] != "off")
      require(date >= QDate(2000, 1, 1) && date <= QDateTime::currentDateTimeUtc().date(),
              "Active daily imagery requires a UTC date from 2000 through today.");
  }
  return value;
}
QJsonObject loadWorkspace(const QString &filename) {
  const auto document = decode(JsonFile::read(filename, documentLimit), documentLimit);
  require(document.isObject(), "Expected a workspace JSON object.");
  return validatedWorkspace(document.object());
}
void saveWorkspace(const QString &filename, const QJsonObject &workspace) {
  const auto value = validatedWorkspace(workspace);
  JsonFile file(filename, documentLimit, [](const QJsonDocument &document) {
    require(document.isObject(), "Expected a workspace JSON object.");
    validatedWorkspace(document.object());
  });
  file.load(QJsonDocument(defaultWorkspace()));
  file.save(QJsonDocument(value));
}
QUrl cameraShareURL(const Camera &camera) {
  if (!camera.valid())
    return {};
  QUrl url("godeye://view");
  QUrlQuery query;
  for (const auto &entry : QList<QPair<QString, double>>{{"lat", camera.center.latitude},
                                                         {"lon", camera.center.longitude},
                                                         {"alt", camera.range},
                                                         {"heading", camera.heading},
                                                         {"tilt", camera.tilt}})
    query.addQueryItem(entry.first, QString::number(entry.second, 'g', 17));
  url.setQuery(query);
  return url;
}
std::optional<Camera> cameraFromShareURL(const QUrl &url) {
  if (url.toString().size() > 16000 || url.scheme() != "godeye" || url.host() != "view" ||
      !url.userInfo().isEmpty() || url.port(-1) != -1 || !url.fragment().isEmpty() ||
      (!url.path().isEmpty() && url.path() != "/"))
    return {};
  QHash<QString, QString> fields;
  for (const auto &field : QUrlQuery(url).queryItems()) {
    if (fields.contains(field.first) ||
        !QStringList{"lat", "lon", "alt", "heading", "tilt"}.contains(field.first))
      return {};
    fields.insert(field.first, field.second);
  }
  if (!fields.contains("lat") || !fields.contains("lon") || !fields.contains("alt"))
    return {};
  const auto read = [&](const QString &name, double fallback) {
    bool ok = false;
    const auto value = fields.contains(name) ? fields[name].toDouble(&ok) : fallback;
    return (ok || !fields.contains(name)) && std::isfinite(value)
               ? value
               : std::numeric_limits<double>::quiet_NaN();
  };
  Camera camera{
      {read("lat", 0), read("lon", 0)}, read("alt", 0), read("heading", 0), read("tilt", 0)};
  return camera.valid() ? std::optional(camera) : std::nullopt;
}
QUrl workspaceShareURL(const QJsonObject &workspace) {
  auto value = validatedWorkspace(workspace);
  QJsonObject publicValue;
  for (const auto &key : publicWorkspaceKeys)
    if (value.contains(key))
      publicValue[key] = value[key];
  publicValue["annotations"] = QJsonArray{};
  publicValue["camera"] = Camera::read(value["camera"].toObject())->json();
  const auto bytes = QJsonDocument(publicValue).toJson(QJsonDocument::Compact);
  require(bytes.size() < 10000, "The shared workspace exceeds its size limit.");
  QUrl url("godeye://workspace");
  QUrlQuery query;
  query.addQueryItem("state", QString::fromLatin1(bytes.toBase64()));
  url.setQuery(query);
  return url;
}
std::optional<QJsonObject> workspaceFromShareURL(const QUrl &url) {
  if (const auto camera = cameraFromShareURL(url)) {
    auto value = defaultWorkspace();
    value["camera"] = camera->json();
    return value;
  }
  if (url.toString().size() > 16000 || url.scheme() != "godeye" || url.host() != "workspace" ||
      !url.userInfo().isEmpty() || url.port(-1) != -1 || !url.fragment().isEmpty() ||
      (!url.path().isEmpty() && url.path() != "/"))
    return {};
  const auto items = QUrlQuery(url).queryItems();
  if (items.size() != 1 || items[0].first != "state")
    return {};
  const auto raw = items[0].second.toLatin1();
  const auto decoded = QByteArray::fromBase64Encoding(raw, QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || decoded.decoded.size() >= 10000)
    return {};
  try {
    auto document = decode(decoded.decoded, 10000);
    if (!document.isObject())
      return {};
    auto value = validatedWorkspace(document.object());
    if (!value["annotations"].toArray().isEmpty())
      return {};
    return value;
  } catch (const std::exception &) {
    return {};
  }
}
SolarState::SolarState(QDateTime utc) : date(utc.toUTC()) {
  require(date.isValid(), "Select a valid UTC solar time.");
  const auto time = date.time();
  const double minutes = time.hour() * 60. + time.minute() + time.second() / 60.;
  const double day = date.date().dayOfYear() + minutes / 1440.;
  subsolar = {-23.44 * std::cos(2 * std::numbers::pi * (day + 10) / 365.), 180 - minutes * .25};
}
double SolarState::elevation(Coordinate point) const {
  require(point.valid(), "Invalid solar query coordinate.");
  const double lat = point.latitude * radians, decl = subsolar.latitude * radians,
               hour = (point.longitude - subsolar.longitude) * radians;
  return std::asin(std::clamp(std::sin(lat) * std::sin(decl) +
                                  std::cos(lat) * std::cos(decl) * std::cos(hour),
                              -1., 1.)) /
         radians;
}
QVector<Coordinate> SolarState::terminator() const {
  const double lat = subsolar.latitude * radians, lon = subsolar.longitude * radians;
  const GEVector3 n{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
  GEVector3 u{n.y, -n.x, 0};
  u = scale(u, 1 / std::sqrt(dot(u, u)));
  const GEVector3 v{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};
  QVector<Coordinate> values;
  for (int i = 0; i <= 720; ++i) {
    const double angle = i * 2 * std::numbers::pi / 720.;
    const auto p = add(scale(u, std::cos(angle)), scale(v, std::sin(angle)));
    values.append({std::asin(std::clamp(p.z, -1., 1.)) / radians, std::atan2(p.y, p.x) / radians});
  }
  return values;
}
} // namespace godeye
