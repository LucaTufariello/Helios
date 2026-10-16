#include "DomainNetwork.h"
#include "Credentials.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QtConcurrent>
#include <algorithm>
namespace godeye {
DomainHttpResult domainFetch(const QUrl &initial, qsizetype maximum,
                             const std::shared_ptr<std::atomic_bool> &cancel,
                             const QMap<QByteArray, QByteArray> &headers, const QByteArray &form,
                             bool redirects) {
  DomainHttpResult result;
  if (maximum < 1 || maximum > 24 * 1024 * 1024 || form.size() > 65536) {
    result.error = "Provider request exceeds its contract.";
    return result;
  }
  QNetworkAccessManager manager;
  QUrl url = initial;
  for (int redirect = 0; redirect <= 5; ++redirect) {
    if (cancel->load()) {
      result.error = "Canceled.";
      return result;
    }
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || (url.port() != -1 && url.port() != 443) || url.hasFragment()) {
      result.error = "Provider request requires an admitted HTTPS URL.";
      return result;
    }
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "GodEyeNative/1.0 (native public-data client)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(20000);
    for (auto i = headers.begin(); i != headers.end(); ++i)
      request.setRawHeader(i.key(), i.value());
    if (!form.isEmpty())
      request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = form.isEmpty() ? manager.get(request) : manager.post(request, form);
    reply->setReadBufferSize(128 * 1024);
    QByteArray bytes;
    QEventLoop loop;
    QTimer deadline, poll;
    bool exceeded = false;
    auto read = [&] {
      const auto chunk = reply->read(maximum - bytes.size() + 1);
      if (chunk.size() > maximum - bytes.size()) {
        exceeded = true;
        reply->abort();
      } else
        bytes += chunk;
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, read);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&] {
      if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maximum) {
        exceeded = true;
        reply->abort();
      }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { reply->abort(); });
    deadline.setSingleShot(true);
    deadline.start(30000);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
      if (cancel->load())
        reply->abort();
    });
    poll.start(20);
    loop.exec();
    read();
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    const auto issue = reply->error();
    delete reply;
    if (cancel->load()) {
      result.error = "Canceled.";
      return result;
    }
    if (exceeded) {
      result.error = "Provider response exceeded its byte limit.";
      return result;
    }
    if (result.status >= 300 && result.status < 400) {
      if (!redirects || !headers.isEmpty() || !form.isEmpty() || redirect == 5 ||
          target.isEmpty()) {
        result.error = "Provider redirect was rejected; credentials were not forwarded.";
        return result;
      }
      url = url.resolved(target);
      continue;
    }
    if (issue != QNetworkReply::NoError || result.status < 200 || result.status >= 300) {
      result.error =
          result.status
              ? QString("Provider request failed (HTTP %1). Previous records are retained.")
                    .arg(result.status)
              : "Provider request failed or timed out. TLS verification remains enabled.";
      return result;
    }
    result.bytes = bytes;
    return result;
  }
  result.error = "Provider redirect limit reached.";
  return result;
}
struct DomainNetwork::TrafficCache {
  QMap<QString, DomainSnapshot> snapshots;
  QString credential, day;
  int requests = 0;
};
namespace {
DomainSnapshot failure(QString error) {
  DomainSnapshot out;
  out.error = error;
  return out;
}
QUrl queryURL(QString base, const QList<QPair<QString, QString>> &items) {
  QUrl url(base);
  QUrlQuery query;
  for (const auto &item : items)
    query.addQueryItem(item.first, item.second);
  url.setQuery(query);
  return url;
}
QString number(double value) {
  return QString::number(value, 'g', 15);
}
DomainSnapshot trafficRequest(Bounds bounds,
                              const std::shared_ptr<DomainNetwork::TrafficCache> &cache,
                              const std::shared_ptr<std::atomic_bool> &cancel) {
  if (!bounds.validArea() || bounds.north - bounds.south > .05 || bounds.east - bounds.west > .05 ||
      std::abs(bounds.south) >= 85 || std::abs(bounds.north) >= 85)
    return failure("Zoom in: traffic requires a bounded ground view of at most 0.05°, below the "
                   "horizon and away from the poles.");
  auto credential = Credentials::read("tomtom");
  if (!credential.ok)
    return failure(credential.error);
  const QString key = credential.value;
  if (!QRegularExpression("^[A-Za-z0-9_-]{16,128}$").match(key).hasMatch())
    return failure("Add a TomTom API key in Provider Settings to load actual traffic flow.");
  if (cache->credential != key) {
    cache->snapshots.clear();
    cache->credential = key;
  }
  const QString day = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate);
  if (day != cache->day) {
    cache->day = day;
    cache->requests = 0;
  }
  const auto nw = TileID::containing({bounds.north, bounds.west}, 12),
             se = TileID::containing({bounds.south, bounds.east}, 12);
  if (nw.x > se.x || nw.y > se.y || (se.x - nw.x + 1) * (se.y - nw.y + 1) > 9)
    return failure("Zoom in for a bounded traffic request of at most nine tiles.");
  DomainSnapshot out;
  out.source = "© TomTom";
  int failures = 0, loaded = 0;
  for (int y = nw.y; y <= se.y; ++y)
    for (int x = nw.x; x <= se.x; ++x) {
      if (cancel->load())
        return failure("Canceled.");
      const TileID tile{x, y, 12};
      auto snapshot = cache->snapshots.value(tile.key());
      if (!snapshot.retrievedAt.isValid() ||
          snapshot.retrievedAt.secsTo(QDateTime::currentDateTimeUtc()) >= 120) {
        if (cache->requests >= 1000)
          return failure("The 1,000-tile daily budget for this app session has been reached. Check "
                         "your provider quota before restarting.");
        ++cache->requests;
        auto bytes = domainFetch(
            queryURL(QString("https://api.tomtom.com/traffic/map/4/tile/flow/relative/12/%1/%2.pbf")
                         .arg(x)
                         .arg(y),
                     {{"key", key}, {"margin", "0"}}),
            2 * 1024 * 1024, cancel, {}, {}, false);
        if (!bytes.error.isEmpty()) {
          ++failures;
          continue;
        }
        snapshot = DomainParsers::traffic(bytes.bytes, tile);
        if (!snapshot.ok()) {
          ++failures;
          continue;
        }
        if (cache->snapshots.size() >= 64) {
          auto oldest = cache->snapshots.begin();
          for (auto entry = cache->snapshots.begin(); entry != cache->snapshots.end(); ++entry)
            if (entry->retrievedAt < oldest->retrievedAt)
              oldest = entry;
          cache->snapshots.erase(oldest);
        }
        cache->snapshots[tile.key()] = snapshot;
      }
      ++loaded;
      out.records += snapshot.records;
      out.rejected += snapshot.rejected;
      if (!out.retrievedAt.isValid() || snapshot.retrievedAt < out.retrievedAt)
        out.retrievedAt = snapshot.retrievedAt;
    }
  if (!loaded)
    return failure(
        "TomTom traffic tiles could not be loaded. Check the key, quota and connection.");
  out.notices = {
      QString("Bounded local view: at most nine tiles. %1 failed. %2/1,000 requests used today in "
              "this session. No synthetic traffic or absolute-speed inference.")
          .arg(failures)
          .arg(cache->requests)};
  return out;
}
DomainSnapshot request(QString feed, Bounds view, DomainConfiguration config,
                       const std::shared_ptr<std::atomic_bool> &cancel) {
  const auto center = view.center();
  QUrl url;
  qsizetype maximum = 16 * 1024 * 1024;
  QMap<QByteArray, QByteArray> headers;
  QByteArray form;
  bool redirects = true;
  if (feed == "datacenters" || feed == "dams" || feed == "cables")
    return DomainParsers::reference(feed);
  if (feed == "earthquakes")
    url = QUrl("https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_day.geojson");
  else if (feed == "military")
    url = QUrl("https://api.adsb.lol/v2/mil");
  else if (feed == "flights") {
    auto data =
        domainFetch(QUrl("https://opensky-network.org/api/states/all?extended=1"), maximum, cancel);
    if (data.error.isEmpty()) {
      auto snapshot = DomainParsers::parse(feed, data.bytes, center, view, config);
      if (snapshot.ok())
        return snapshot;
    }
    if (cancel->load())
      return failure("Canceled.");
    url = QUrl(QString("https://api.adsb.lol/v2/lat/%1/lon/%2/dist/125")
                   .arg(number(center.latitude), number(center.longitude)));
  } else if (feed == "disasters") {
    url = QUrl("https://www.gdacs.org/xml/rss.xml");
    maximum = 2 * 1024 * 1024;
  } else if (feed == "atmosphere") {
    auto weather = domainFetch(
        queryURL("https://api.open-meteo.com/v1/forecast",
                 {{"latitude", number(center.latitude)},
                  {"longitude", number(center.longitude)},
                  {"timezone", "UTC"},
                  {"current", "temperature_2m,relative_humidity_2m,apparent_temperature,weather_"
                              "code,wind_speed_10m,wind_direction_10m,surface_pressure"},
                  {"daily", "temperature_2m_max,temperature_2m_min,precipitation_probability_max"},
                  {"forecast_days", "2"}}),
        2 * 1024 * 1024, cancel);
    if (!weather.error.isEmpty())
      return failure(weather.error);
    auto air = domainFetch(
        queryURL("https://air-quality-api.open-meteo.com/v1/air-quality",
                 {{"latitude", number(center.latitude)},
                  {"longitude", number(center.longitude)},
                  {"timezone", "UTC"},
                  {"current", "european_aqi,us_aqi,pm2_5,pm10,nitrogen_dioxide,ozone"}}),
        2 * 1024 * 1024, cancel);
    return DomainParsers::atmosphere(weather.bytes, air.bytes, center, air.error);
  } else if (feed == "installations" || feed == "energy") {
    QString error;
    auto query = DomainParsers::mappedQuery(feed, view, &error);
    if (!error.isEmpty())
      return failure(error);
    static QMutex mutex;
    static QMap<QString, DomainSnapshot> cache;
    {
      QMutexLocker lock(&mutex);
      const auto cached = cache.value(query);
      if (cached.retrievedAt.isValid() &&
          cached.retrievedAt.secsTo(QDateTime::currentDateTimeUtc()) < 300)
        return cached;
    }
    for (const QString host : {"overpass-api.de", "overpass.kumi.systems", "lz4.overpass-api.de",
                               "overpass.private.coffee"}) {
      auto data = domainFetch(QUrl("https://" + host + "/api/interpreter"), 16 * 1024 * 1024,
                              cancel, {}, "data=" + QUrl::toPercentEncoding(query), false);
      if (cancel->load())
        return failure("Canceled.");
      if (data.error.isEmpty()) {
        auto snapshot = DomainParsers::parse(feed, data.bytes, center, view, config);
        if (snapshot.ok()) {
          QMutexLocker lock(&mutex);
          if (cache.size() >= 32) {
            auto oldest = cache.begin();
            for (auto i = cache.begin(); i != cache.end(); ++i)
              if (i->retrievedAt < oldest->retrievedAt)
                oldest = i;
            cache.erase(oldest);
          }
          cache[query] = snapshot;
          return snapshot;
        }
        error = snapshot.error;
      } else
        error = data.error;
    }
    return failure(error);
  } else if (feed == "launches") {
    const auto now = QDateTime::currentDateTimeUtc();
    maximum = 12 * 1024 * 1024;
    url = queryURL("https://ll.thespacedevs.com/2.3.0/launches/",
                   {{"net__gte", now.addDays(-30).toString(Qt::ISODate)},
                    {"net__lte", now.toString(Qt::ISODate)},
                    {"limit", "100"},
                    {"mode", "detailed"}});
  } else if (feed == "cameras") {
    if (!QStringList{"london", "finland"}.contains(config.cameraProvider))
      return failure("Choose a registered camera provider.");
    url = QUrl(config.cameraProvider == "london"
                   ? "https://api.tfl.gov.uk/Place/Type/JamCam"
                   : "https://tie.digitraffic.fi/api/weathercam/v1/stations");
    maximum = 8 * 1024 * 1024;
    redirects = false;
    headers = {{"Accept", "application/json"}, {"Digitraffic-User", "GodEyeNative/1.0"}};
  } else if (feed == "sentinel") {
    if (!QStringList{"SENTINEL-1", "SENTINEL-2"}.contains(config.sentinelKind))
      return failure("Choose Sentinel-1 or Sentinel-2.");
    maximum = 4 * 1024 * 1024;
    url =
        queryURL("https://catalogue.dataspace.copernicus.eu/odata/v1/Products",
                 {{"$filter", "Collection/Name eq '" + config.sentinelKind +
                                  "' and OData.CSC.Intersects(area=geography'SRID=4326;POINT(" +
                                  number(center.longitude) + " " + number(center.latitude) + ")')"},
                  {"$top", "10"},
                  {"$orderby", "ContentDate/Start desc"},
                  {"$expand", "Attributes"}});
  } else if (feed == "fires") {
    auto credential = Credentials::read("firms");
    if (!credential.ok)
      return failure(credential.error);
    if (!QRegularExpression("^[A-Za-z0-9-]{8,128}$").match(credential.value).hasMatch())
      return failure("Add a free NASA FIRMS MAP_KEY in Provider Settings. No detections are "
                     "generated without provider data.");
    maximum = 24 * 1024 * 1024;
    redirects = false;
    url = QUrl("https://firms.modaps.eosdis.nasa.gov/api/area/csv/" + credential.value +
               "/VIIRS_SNPP_NRT/world/2");
  } else if (feed == "transit") {
    for (const auto &provider : transitProviders())
      if (provider.id == config.transitID) {
        auto data = domainFetch(provider.url, 16 * 1024 * 1024, cancel, provider.headers);
        if (!data.error.isEmpty())
          return failure(data.error);
        return DomainParsers::transit(data.bytes, provider);
      }
    return failure(
        "Choose a registered public transit operator; the provider catalog must be installed.");
  } else
    return failure("Source requires its dedicated native context.");
  auto data = domainFetch(url, maximum, cancel, headers, form, redirects);
  if (!data.error.isEmpty())
    return failure(data.error);
  return DomainParsers::parse(feed, data.bytes, center, view, config);
}
} // namespace
DomainNetwork::DomainNetwork(QObject *parent, bool networking)
    : QObject(parent), traffic_(std::make_shared<TrafficCache>()), ais_(this),
      networking_(networking) {
  for (const auto &feed : domainFeeds())
    entries_[feed.id] = {};
  connect(&ais_, &DomainSocket::changed, this, [this] {
    if (enabled("vessels")) {
      auto fresh = ais_.snapshot();
      entries_["vessels"].snapshot = fresh;
      emit changed("vessels");
    }
  });
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, [this] {
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto &feed : domainFeeds()) {
      auto &entry = entries_[feed.id];
      if (entry.enabled && !entry.watcher && !entry.queued &&
          (!entry.attempted.isValid() || entry.attempted.secsTo(now) >= feed.intervalSeconds))
        refresh(feed.id);
    }
    pump();
  });
  timer_.start();
}
DomainNetwork::~DomainNetwork() {
  timer_.stop();
  ais_.stop();
  for (auto &entry : entries_)
    if (entry.cancel)
      entry.cancel->store(true);
  for (auto &entry : entries_)
    if (entry.watcher) {
      disconnect(entry.watcher, nullptr, this, nullptr);
      entry.watcher->waitForFinished();
    }
}
void DomainNetwork::setView(Bounds bounds) {
  if (!Coordinate{bounds.south, bounds.west}.valid() ||
      !Coordinate{bounds.north, bounds.east}.valid() || !bounds.center().valid())
    return;
  if (view_.west == bounds.west && view_.south == bounds.south && view_.east == bounds.east &&
      view_.north == bounds.north)
    return;
  view_ = bounds;
  for (const QString feed : {"installations", "energy", "atmosphere", "sentinel", "traffic"})
    if (enabled(feed)) {
      cancel(feed);
      refresh(feed);
    }
}
void DomainNetwork::setConfiguration(DomainConfiguration value) {
  const auto old = configuration_;
  configuration_ = value;
  for (const auto &feed :
       {QString("satellites"), QString("transit"), QString("cameras"), QString("sentinel")}) {
    bool changed = feed == "satellites" ? old.satelliteGroup != value.satelliteGroup
                   : feed == "transit"  ? old.transitID != value.transitID
                   : feed == "cameras"  ? old.cameraProvider != value.cameraProvider
                                        : old.sentinelKind != value.sentinelKind;
    if (changed && enabled(feed)) {
      cancel(feed);
      refresh(feed);
    }
  }
}
void DomainNetwork::setEnabled(const QString &feed, bool value) {
  if (!entries_.contains(feed) || entries_[feed].enabled == value)
    return;
  entries_[feed].enabled = value;
  if (value)
    refresh(feed);
  else {
    cancel(feed);
    if (feed == "vessels")
      ais_.stop();
  }
  emit changed(feed);
}
bool DomainNetwork::enabled(const QString &feed) const {
  return entries_.value(feed).enabled;
}
void DomainNetwork::refresh(const QString &feed) {
  if (feed.isEmpty()) {
    for (const auto &descriptor : domainFeeds())
      if (enabled(descriptor.id))
        refresh(descriptor.id);
    return;
  }
  if (!entries_.contains(feed) || !enabled(feed))
    return;
  auto &entry = entries_[feed];
  if (entry.watcher) {
    if (entry.cancel && entry.cancel->load())
      entry.queued = true;
    return;
  }
  if (entry.queued)
    return;
  entry.attempted = QDateTime::currentDateTimeUtc();
  if (!networking_ && !QStringList{"annotations", "datacenters", "dams", "cables"}.contains(feed)) {
    entry.snapshot.error =
        "Offline mode: source network requests are disabled. Retained records may be stale.";
    emit changed(feed);
    return;
  }
  if (feed == "vessels" && ais_.running()) {
    entry.snapshot = ais_.snapshot();
    emit changed(feed);
    return;
  }
  if (feed == "annotations") {
    emit changed(feed);
    return;
  }
  if (feed == "satellites" && catalogs_.contains(configuration_.satelliteGroup) &&
      catalogs_[configuration_.satelliteGroup].retrievedAt.secsTo(entry.attempted) < 6 * 3600) {
    entry.snapshot = catalogs_[configuration_.satelliteGroup].snapshot();
    emit changed(feed);
    return;
  }
  entry.queued = true;
  emit changed(feed);
  pump();
}
void DomainNetwork::cancel(const QString &feed) {
  for (auto i = entries_.begin(); i != entries_.end(); ++i)
    if (feed.isEmpty() || feed == i.key()) {
      i->queued = false;
      ++i->generation;
      if (i->cancel)
        i->cancel->store(true);
    }
  if (feed.isEmpty() || feed == "vessels")
    ais_.stop();
  emit changed(feed);
}
bool DomainNetwork::busy(const QString &feed) const {
  for (auto i = entries_.begin(); i != entries_.end(); ++i)
    if ((feed.isEmpty() || feed == i.key()) && (i->watcher || i->queued))
      return true;
  return false;
}
DomainSnapshot DomainNetwork::snapshot(const QString &feed) const {
  return entries_.value(feed).snapshot;
}
QVector<DomainRecord> DomainNetwork::records() const {
  QVector<DomainRecord> out;
  for (auto i = entries_.begin(); i != entries_.end(); ++i)
    if (i->enabled)
      out += i->snapshot.records;
  return out;
}
void DomainNetwork::setAnnotations(DomainSnapshot snapshot) {
  entries_["annotations"].snapshot = snapshot;
  emit changed("annotations");
}
bool DomainNetwork::orbit(const QString &record, QVector<Coordinate> &points,
                          QVector<double> &altitudes, QDateTime at) const {
  auto i = catalogs_.find(configuration_.satelliteGroup);
  return i != catalogs_.end() && i->orbit(record, points, altitudes, at);
}
void DomainNetwork::pump() {
  if (active_ >= 3)
    return;
  for (auto i = entries_.begin(); i != entries_.end() && active_ < 3; ++i)
    if (i->queued && !i->watcher)
      start(i.key());
}
void DomainNetwork::start(const QString &feed) {
  auto &entry = entries_[feed];
  entry.queued = false;
  ++active_;
  const auto generation = ++entry.generation;
  const auto view = view_;
  const auto config = configuration_;
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  entry.cancel = cancel;
  auto *watcher = new QFutureWatcher<DomainSnapshot>(this);
  entry.watcher = watcher;
  auto orbit = std::make_shared<std::optional<DomainOrbit>>();
  auto aisKey = std::make_shared<CredentialResult>();
  connect(watcher, &QFutureWatcher<DomainSnapshot>::finished, this,
          [this, watcher, feed, generation, orbit, aisKey] {
            --active_;
            auto &entry = entries_[feed];
            entry.watcher = nullptr;
            entry.cancel.reset();
            auto snapshot = watcher->result();
            watcher->deleteLater();
            if (entry.generation == generation && entry.enabled) {
              if (*orbit)
                catalogs_[configuration_.satelliteGroup] = **orbit;
              if (snapshot.ok()) {
                if (!snapshot.retrievedAt.isValid() && feed != "vessels" && feed != "datacenters" &&
                    feed != "dams" && feed != "cables")
                  snapshot.retrievedAt = QDateTime::currentDateTimeUtc();
                entry.snapshot = snapshot;
              } else
                entry.snapshot.error = snapshot.error;
              if (feed == "vessels" && snapshot.ok()) {
                ais_.start(aisKey->value);
                entry.snapshot = ais_.snapshot();
              }
            }
            emit changed(feed);
            pump();
          });
  watcher->setFuture(
      QtConcurrent::run([feed, view, config, cancel, orbit, aisKey, cache = traffic_] {
        if (feed == "traffic")
          return trafficRequest(view, cache, cancel);
        if (feed == "vessels") {
          *aisKey = Credentials::read("ais");
          if (!aisKey->ok)
            return failure(aisKey->error);
          DomainSnapshot out;
          out.source = "AISStream";
          return out;
        }
        if (feed == "satellites") {
          if (!QStringList{"stations", "active", "starlink", "weather", "gps-ops"}.contains(
                  config.satelliteGroup))
            return failure("Choose a registered CelesTrak satellite group.");
          auto data = domainFetch(queryURL("https://celestrak.org/NORAD/elements/gp.php",
                                           {{"GROUP", config.satelliteGroup}, {"FORMAT", "tle"}}),
                                  8 * 1024 * 1024, cancel);
          if (!data.error.isEmpty())
            return failure(data.error);
          *orbit = DomainOrbit::parse(data.bytes);
          if (!*orbit)
            return failure("CelesTrak returned no valid TLE elements.");
          return (*orbit)->snapshot();
        }
        return request(feed, view, config, cancel);
      }));
}
} // namespace godeye
