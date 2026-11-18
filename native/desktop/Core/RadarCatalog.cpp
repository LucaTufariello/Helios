#include "RadarCatalog.h"
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace helios {
namespace {
constexpr qint64 maximumArchive = 40ll * 1024 * 1024 * 1024;
QVector<QVector<Coordinate>> footprint(const QJsonObject &geometry) {
  QVector<QVector<Coordinate>> paths;
  const auto type = geometry["type"].toString();
  QJsonArray rings;
  if (type == "Polygon")
    rings = geometry["coordinates"].toArray();
  else if (type == "MultiPolygon") {
    const auto polygons = geometry["coordinates"].toArray();
    if (polygons.size() > 100)
      return {};
    for (const auto polygon : polygons)
      for (const auto ring : polygon.toArray())
        rings.append(ring);
  }
  if (rings.size() > 100)
    return {};
  int total = 0;
  for (const auto value : rings) {
    const auto ring = value.toArray();
    if (ring.size() < 3 || ring.size() > 10000 || (total += ring.size()) > 10000)
      return {};
    QVector<Coordinate> points;
    for (const auto coordinate : ring) {
      const auto pair = coordinate.toArray();
      if (pair.size() < 2 || !pair[0].isDouble() || !pair[1].isDouble())
        return {};
      Coordinate point{pair[1].toDouble(), pair[0].toDouble()};
      if (!point.valid())
        return {};
      points.append(point);
    }
    paths.append(points);
  }
  return paths;
}
bool token(const QString &value) {
  return !value.isEmpty() && value.size() <= 32768 &&
         QRegularExpression("^[A-Za-z0-9._~+/-]+=*$").match(value).hasMatch();
}
} // namespace
bool RadarQuery::valid() const {
  return region.validArea() && start.isValid() && end.isValid() && start <= end &&
         start.daysTo(end) <= 366 && QStringList{"VV + VH", "VV", "VH"}.contains(polarization) &&
         QStringList{"IW", "EW", "SM"}.contains(mode);
}
QUrl RadarQuery::url(int skip) const {
  if (!valid() || skip < 0 || skip > 980 || skip % 20)
    throw std::runtime_error(
        "Choose valid bounds, up to one year of UTC dates, polarization and acquisition mode.");
  QStringList modes =
      mode == "SM" ? QStringList{"SM", "S1", "S2", "S3", "S4", "S5", "S6"} : QStringList{mode};
  for (auto &beam : modes)
    beam = "contains(Name,'_" + beam + "_GRD')";
  const auto polarizations = polarization == "VV"
                                 ? "(contains(Name,'_1SDV_') or contains(Name,'_1SSV_'))"
                                 : "contains(Name,'_1SDV_')";
  const auto point = [](double x, double y) {
    return QString::number(x, 'g', 15) + " " + QString::number(y, 'g', 15);
  };
  const auto wkt = "POLYGON((" +
                   QStringList{point(region.west, region.south), point(region.east, region.south),
                               point(region.east, region.north), point(region.west, region.north),
                               point(region.west, region.south)}
                       .join(',') +
                   "))";
  const auto filter = "Collection/Name eq 'SENTINEL-1' and (" + modes.join(" or ") +
                      ") and not contains(Name,'_COG') and " + polarizations +
                      " and ContentDate/Start ge " + start.toString(Qt::ISODate) +
                      "T00:00:00Z and ContentDate/Start lt " +
                      end.addDays(1).toString(Qt::ISODate) +
                      "T00:00:00Z and OData.CSC.Intersects(area=geography'SRID=4326;" + wkt + "')";
  QUrl url("https://catalogue.dataspace.copernicus.eu/odata/v1/Products");
  QUrlQuery query;
  query.addQueryItem("$filter", filter);
  query.addQueryItem("$orderby", "ContentDate/Start desc,Id");
  query.addQueryItem("$top", "20");
  query.addQueryItem("$skip", QString::number(skip));
  query.addQueryItem("$expand", "Attributes");
  url.setQuery(query);
  return url;
}
QJsonObject RadarQuery::json() const {
  return {{"region", region.json()},
          {"start", start.toString(Qt::ISODate) + "T00:00:00Z"},
          {"end", end.toString(Qt::ISODate) + "T00:00:00Z"},
          {"polarization", polarization},
          {"mode", mode}};
}
bool RadarScene::supportedName(const QString &name) {
  return name.size() <= 200 &&
         QRegularExpression("^S1[A-D]_(IW|EW|SM|S[1-6])_GRD[HMF]_1S(DV|SV)_[0-9]{8}T[0-9]{6}_[0-9]{"
                            "8}T[0-9]{6}_[0-9]{6}_[A-Fa-f0-9]{6}_[A-Fa-f0-9]{4}\\.SAFE$")
             .match(name)
             .hasMatch();
}
QString RadarScene::archiveFilename() const {
  return supportedName(name) ? name.chopped(5) + ".zip" : id + ".zip";
}
QUrl RadarScene::downloadURL() const {
  return QUrl("https://download.dataspace.copernicus.eu/odata/v1/Products(" + id.toLower() +
              ")/$value");
}
QJsonObject RadarScene::json() const {
  QJsonArray paths;
  for (const auto &ring : footprint) {
    QJsonArray points;
    for (const auto p : ring)
      points.append(p.json());
    paths.append(points);
  }
  QJsonObject value{{"id", id},
                    {"name", name},
                    {"acquisition", acquisition},
                    {"contentLength", contentLength},
                    {"online", online},
                    {"orbit", orbit},
                    {"footprint", paths}};
  if (relativeOrbit)
    value["relativeOrbit"] = *relativeOrbit;
  return value;
}
RadarCatalog::RadarCatalog(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), request_(this, network) {
  connect(&request_, &HttpRequest::changed, this, &RadarCatalog::changed);
  connect(&request_, &HttpRequest::completed, this,
          [this](int, const QByteArray &bytes, const QString &error) {
            if (!error.isEmpty()) {
              emit failed(error);
              return;
            }
            const auto query = query_;
            const auto generation = generation_;
            parsing_ = true;
            emit changed();
            auto *watcher = new QFutureWatcher<std::pair<RadarPage, QString>>(this);
            connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
              const auto result = watcher->result();
              watcher->deleteLater();
              if (generation != generation_)
                return;
              parsing_ = false;
              emit changed();
              if (result.second.isEmpty())
                emit results(result.first);
              else
                emit failed(result.second);
            });
            watcher->setFuture(QtConcurrent::run([bytes, query] {
              try {
                auto page = parse(bytes);
                if (query) {
                  const auto old = page.scenes.size();
                  page.scenes.erase(std::remove_if(page.scenes.begin(), page.scenes.end(),
                                                   [&](const RadarScene &scene) {
                                                     const auto date =
                                                         QDateTime::fromString(scene.acquisition,
                                                                               Qt::ISODateWithMs)
                                                             .toUTC()
                                                             .date();
                                                     auto mode = scene.name.section('_', 1, 1);
                                                     if (mode.size() == 2 && mode[0] == 'S' &&
                                                         mode[1] >= '1' && mode[1] <= '6')
                                                       mode = "SM";
                                                     return date < query->start ||
                                                            date > query->end ||
                                                            mode != query->mode ||
                                                            (query->polarization != "VV" &&
                                                             !scene.name.contains("_1SDV_"));
                                                   }),
                                    page.scenes.end());
                  page.rejected += int(old - page.scenes.size());
                }
                return std::make_pair(page, QString{});
              } catch (const std::exception &e) {
                return std::make_pair(RadarPage{}, QString::fromUtf8(e.what()));
              }
            }));
          });
}
void RadarCatalog::cancel() {
  ++generation_;
  parsing_ = false;
  request_.cancel();
  emit changed();
}
void RadarCatalog::search(const RadarQuery &query, int skip) {
  cancel();
  try {
    query_ = query;
    request_.start(query.url(skip), 2 * 1024 * 1024);
  } catch (const std::exception &e) {
    emit failed(QString::fromUtf8(e.what()));
  }
}
RadarPage RadarCatalog::parse(const QByteArray &bytes) {
  if (bytes.size() > 2 * 1024 * 1024)
    throw std::runtime_error("Radar catalogue exceeded 2 MiB.");
  QJsonParseError error;
  const auto root = QJsonDocument::fromJson(bytes, &error).object();
  const auto rows = root["value"].toArray();
  if (error.error != QJsonParseError::NoError || !root["value"].isArray() || rows.size() > 20)
    throw std::runtime_error("Copernicus returned an unsupported catalogue response.");
  RadarPage page;
  page.hasMore = rows.size() == 20;
  QSet<QString> seen;
  for (const auto value : rows) {
    const auto row = value.toObject();
    const QUuid uuid(row["Id"].toString());
    const auto name = row["Name"].toString(),
               date = row["ContentDate"].toObject()["Start"].toString();
    const auto length = row["ContentLength"];
    if (uuid.isNull() || seen.contains(uuid.toString()) || !RadarScene::supportedName(name) ||
        !length.isDouble() || length.toDouble() != std::floor(length.toDouble()) ||
        length.toDouble() <= 0 || length.toDouble() > maximumArchive || !row["Online"].isBool() ||
        date.size() > 40 ||
        !QRegularExpression("(Z|[+-][0-9]{2}:[0-9]{2})$").match(date).hasMatch() ||
        !QDateTime::fromString(date, Qt::ISODateWithMs).isValid()) {
      ++page.rejected;
      continue;
    }
    seen.insert(uuid.toString());
    RadarScene scene;
    scene.id = uuid.toString(QUuid::WithoutBraces);
    scene.name = name;
    scene.acquisition = date;
    scene.contentLength = qint64(length.toDouble());
    scene.online = row["Online"].toBool();
    scene.footprint = footprint(row["GeoFootprint"].toObject());
    const auto attributes = row["Attributes"].toArray();
    for (qsizetype i = 0; i < attributes.size() && i < 100; ++i) {
      const auto attribute = attributes[i].toObject();
      const auto key = attribute["Name"].toString(), text = attribute["Value"].toString();
      if (key == "orbitDirection" && (text == "ASCENDING" || text == "DESCENDING"))
        scene.orbit = text;
      const auto orbit = attribute["Value"];
      if (key == "relativeOrbitNumber" && orbit.isDouble() && orbit.toInt() == orbit.toDouble() &&
          orbit.toInt() >= 1 && orbit.toInt() <= 175)
        scene.relativeOrbit = orbit.toInt();
    }
    page.scenes.append(scene);
  }
  return page;
}
CopernicusAccount::CopernicusAccount(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), request_(this, network) {
  connect(&request_, &HttpRequest::changed, this, &CopernicusAccount::changed);
  connect(&request_, &HttpRequest::completed, this,
          [this](int, const QByteArray &bytes, const QString &error) {
            const auto result = QJsonDocument::fromJson(bytes).object();
            const auto access = result["access_token"].toString(),
                       refresh = result["refresh_token"].toString();
            const double seconds = result["expires_in"].toDouble();
            QString failure;
            if (!error.isEmpty() || !token(access) || (!refresh.isEmpty() && !token(refresh)) ||
                !std::isfinite(seconds) || seconds <= 0 || seconds > 7 * 86400)
              failure = "Copernicus sign-in or refresh failed. Check account access and the "
                        "current two-factor code, then sign in again.";
            if (failure.isEmpty()) {
              access_ = access;
              refresh_ = refresh;
              expires_ =
                  QDateTime::currentDateTimeUtc().addSecs(qint64(std::max(0.0, seconds - 30)));
            } else {
              access_.clear();
              refresh_.clear();
            }
            auto completion = std::move(pending_);
            pending_ = {};
            emit changed();
            if (!failure.isEmpty())
              emit failed(failure);
            if (completion)
              completion(failure.isEmpty() ? access_ : QString{}, failure);
          });
}
void CopernicusAccount::signOut() {
  request_.cancel();
  access_.clear();
  refresh_.clear();
  expires_ = {};
  auto completion = std::move(pending_);
  pending_ = {};
  if (completion)
    completion({}, "Copernicus account signed out.");
  emit changed();
}
void CopernicusAccount::exchange(const QMap<QString, QString> &form) {
  QByteArray body;
  for (auto i = form.begin(); i != form.end(); ++i) {
    if (!body.isEmpty())
      body.append('&');
    body.append(QUrl::toPercentEncoding(i.key()) + '=' + QUrl::toPercentEncoding(i.value()));
  }
  request_.start(QUrl("https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/"
                      "openid-connect/token"),
                 64 * 1024, body);
}
void CopernicusAccount::signIn(const QString &username, const QString &password,
                               const QString &totp) {
  signOut();
  if (username.isEmpty() || username.size() > 320 || password.isEmpty() || password.size() > 1024 ||
      (!totp.isEmpty() && !QRegularExpression("^[0-9]{6}$").match(totp).hasMatch())) {
    emit failed("Enter account credentials and a valid optional six-digit two-factor code.");
    return;
  }
  QMap<QString, QString> form{{"client_id", "cdse-public"},
                              {"grant_type", "password"},
                              {"username", username},
                              {"password", password}};
  if (!totp.isEmpty())
    form["totp"] = totp;
  exchange(form);
}
void CopernicusAccount::access(std::function<void(QString, QString)> completion) {
  if (request_.busy()) {
    completion({}, "Copernicus sign-in or refresh is already running.");
    return;
  }
  if (access_.isEmpty()) {
    completion({}, "Sign in to your free Copernicus Data Space account to download a product.");
    return;
  }
  if (QDateTime::currentDateTimeUtc() < expires_) {
    completion(access_, {});
    return;
  }
  if (refresh_.isEmpty()) {
    signOut();
    completion({}, "Copernicus session expired; sign in again.");
    return;
  }
  pending_ = std::move(completion);
  exchange(
      {{"client_id", "cdse-public"}, {"grant_type", "refresh_token"}, {"refresh_token", refresh_}});
}
} // namespace helios
