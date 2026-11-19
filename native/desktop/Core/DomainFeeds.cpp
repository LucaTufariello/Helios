#include "DomainFeeds.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
namespace helios {
const QVector<FeedDescriptor> &domainFeeds() {
  static const QVector<FeedDescriptor> feeds{
      {"earthquakes", "Earthquakes (24h)", "Ground", "Data courtesy of the U.S. Geological Survey",
       60},
      {"military", "Military Flights", "Sky", "adsb.lol · ODbL 1.0", 30},
      {"disasters", "Disaster Alerts", "Ground", "GDACS · UN / EU JRC · public-domain alerts",
       1800},
      {"installations", "Mapped Installations", "Ground",
       "© OpenStreetMap contributors · ODbL · community mapped, incomplete", 300},
      {"energy", "Energy & Refining", "Ground",
       "© OpenStreetMap contributors · ODbL · community mapped, incomplete", 300},
      {"launches", "Space Missions", "Sky",
       "Launch Library 2 / The Space Devs · launch site, not vehicle position", 900},
      {"vessels", "AIS Vessels", "Marine", "AISStream · reported AIS positions · key required", 30},
      {"fires", "Thermal Detections (24h)", "Ground",
       "NASA FIRMS · satellite thermal detections · free MAP_KEY required", 900},
      {"transit", "Public Transit", "Ground",
       "Public operator GTFS-realtime · operator attribution shown per record", 60},
      {"cameras", "Traffic Cameras", "Ground",
       "TfL / Fintraffic · public operator cameras · stills may be delayed", 1800},
      {"datacenters", "Datacenters", "Ground",
       "© OpenStreetMap contributors · ODbL · bundled reference, incomplete", 86400},
      {"dams", "Dams", "Ground",
       "© OpenStreetMap contributors / Open Infrastructure Map · ODbL · bundled reference, "
       "incomplete",
       86400},
      {"cables", "Submarine Cables", "Marine",
       "© TeleGeography · CC BY-NC-SA 3.0 · non-commercial data", 86400},
      {"sentinel", "Sentinel Product Catalog", "Satellite",
       "Copernicus Data Space · dated product metadata, not decoded imagery", 900},
      {"annotations", "Local Annotations", "Ground",
       "User-authored notes and geometry · saved locally", 86400}};
  return feeds;
}
const FeedDescriptor *domainFeed(const QString &id) {
  for (const auto &feed : domainFeeds())
    if (feed.id == id)
      return &feed;
  return nullptr;
}
QJsonObject DomainRecord::json() const {
  QJsonObject value{{"id", id},
                    {"feed", feed},
                    {"label", label},
                    {"source", source},
                    {"coordinate", coordinate.json()},
                    {"details", details},
                    {"raw", raw}};
  value["observedAt"] = observedAt.isValid()
                            ? QJsonValue(observedAt.toUTC().toString(Qt::ISODateWithMs))
                            : QJsonValue();
  value["altitude"] = altitude ? QJsonValue(*altitude) : QJsonValue();
  QJsonArray outline, lines;
  for (auto p : footprint)
    outline.append(p.json());
  for (const auto &path : paths) {
    QJsonArray line;
    for (auto p : path)
      line.append(p.json());
    lines.append(line);
  }
  value["footprint"] = outline;
  value["paths"] = lines;
  return value;
}
std::optional<QJsonObject> DomainRecord::missionJSON(QString *error) const {
  const auto fail = [&](const QString &message) -> std::optional<QJsonObject> {
    if (error)
      *error = message;
    return {};
  };
  if (!domainFeed(feed) || id.isEmpty() || id.size() > 512 || label.trimmed().isEmpty() ||
      label.size() > 1024 || source.size() > 1024 || !coordinate.valid() ||
      (altitude && !std::isfinite(*altitude)))
    return fail("The source record is outside the saved-evidence text or coordinate contract.");
  if (footprint.size() > 2000 || paths.size() > 64)
    return fail("This source geometry exceeds the mission snapshot limits. Export the full record "
                "separately; it has not been simplified for evidence.");
  int total = 0;
  for (const auto &path : paths) {
    total += path.size();
    if (path.size() > 2000 || total > 20000)
      return fail("This source geometry exceeds the mission snapshot limits. Export the full "
                  "record separately; it has not been simplified for evidence.");
    for (auto point : path)
      if (!point.valid())
        return fail("The source contains an invalid geometry point.");
  }
  for (auto point : footprint)
    if (!point.valid())
      return fail("The source contains an invalid outline point.");
  QJsonObject savedDetails;
  for (auto i = details.begin(); i != details.end(); ++i) {
    if (i.key() == "Frame URL" || i.key() == "Video URL")
      continue;
    QString value;
    if (i.value().isString())
      value = i.value().toString();
    else if (i.value().isDouble())
      value = QString::number(i.value().toDouble(), 'g', 17);
    else if (i.value().isBool())
      value = i.value().toBool() ? "true" : "false";
    else if (i.value().isNull() || i.value().isUndefined())
      value = "Unknown";
    else
      return fail("A source detail uses an unsupported saved-evidence type.");
    if (i.key().size() > 256 || value.size() > 8192 || savedDetails.size() >= 100)
      return fail("Source details exceed the mission snapshot limits.");
    savedDetails[i.key()] = value;
  }
  auto value = json();
  value.remove("raw");
  value["details"] = savedDetails;
  return value;
}
QString domainResourcePath(const QString &relative) {
#ifdef Q_OS_MACOS
  const QString root = QCoreApplication::applicationDirPath() + "/../Resources/sources/";
#else
  const QString root = QCoreApplication::applicationDirPath() + "/../share/helios/sources/";
#endif
  return QDir::cleanPath(root + relative);
}
QVector<TransitProvider> transitProviders(const QString &path) {
  QFile file(path.isEmpty() ? domainResourcePath("transit-feeds.json") : path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
    return {};
  auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isArray() || document.array().size() > 100)
    return {};
  QVector<TransitProvider> providers;
  for (const auto value : document.array()) {
    const auto item = value.toObject();
    TransitProvider p;
    p.id = item["id"].toString();
    p.name = item["name"].toString();
    p.url = QUrl(item["url"].toString());
    p.attribution = item["attribution"].toString();
    p.license = item["license"].toString();
    p.licenseUrl = item["licenseUrl"].toString();
    p.center = {item["lat"].toDouble(999), item["lon"].toDouble(999)};
    p.radius = item["radius"].toDouble();
    if (!p.center.valid() || p.url.scheme() != "https" || !p.url.userInfo().isEmpty() ||
        p.id.isEmpty() || p.id.size() > 80 || p.radius <= 0)
      continue;
    const auto headers = item["headers"].toObject();
    for (auto i = headers.begin(); i != headers.end(); ++i)
      if (i.key().size() < 128 && i.value().toString().size() < 256 && !i.key().contains('\n') &&
          !i.value().toString().contains('\n'))
        p.headers[i.key().toLatin1()] = i.value().toString().toLatin1();
    providers.append(p);
  }
  return providers;
}
} // namespace helios
