#include "Core/DomainFeeds.h"
#include "Core/DomainHistory.h"
#include "Core/DomainNetwork.h"
#include "Core/DomainProto.h"
#include "Core/DomainSocket.h"
#include "Core/Missions.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <bit>
using namespace helios;
namespace {
QByteArray varint(quint64 n) {
  QByteArray out;
  do {
    auto byte = quint8(n & 127);
    n >>= 7;
    out.append(char(n ? byte | 128 : byte));
  } while (n);
  return out;
}
QByteArray message(int n, const QByteArray &bytes) {
  return varint(quint64(n * 8 + 2)) + varint(quint64(bytes.size())) + bytes;
}
QByteArray integer(int n, quint64 value) {
  return varint(quint64(n * 8)) + varint(value);
}
QByteArray fixed(int n, float value) {
  char bytes[4];
  qToLittleEndian<quint32>(std::bit_cast<quint32>(value), bytes);
  return varint(quint64(n * 8 + 5)) + QByteArray(bytes, 4);
}
QByteArray transitFix(std::optional<quint64> at, float latitude = 0) {
  auto header = message(1, "2.0") + integer(3, 1700000000);
  auto position = fixed(1, latitude) + fixed(2, 0) + fixed(5, 0);
  auto descriptor = message(1, "bus-1") + message(2, "Actual operator label");
  auto vehicle = message(1, message(5, "Red")) + message(2, position) +
                 (at ? integer(5, *at) : QByteArray{}) + message(8, descriptor);
  return message(1, header) + message(2, message(1, "entity-1") + message(4, vehicle));
}
QByteArray json(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
DomainSnapshot parse(QString feed, const QJsonObject &object) {
  return DomainParsers::parse(feed, json(object), {0, 0}, {-1, -1, 1, 1}, {});
}
} // namespace
class DomainTests : public QObject {
  Q_OBJECT
private slots:
  void catalogExclusionsAndIntervals() {
    QCOMPARE(domainFeeds().size(), 15);
    QSet<QString> ids;
    for (const auto &feed : domainFeeds()) {
      QVERIFY(!feed.credit.isEmpty());
      QVERIFY(feed.intervalSeconds >= 15);
      QVERIFY(!ids.contains(feed.id));
      ids.insert(feed.id);
    }
    for (const auto &id : {"bikeshare", "radio", "cockpit", "voice", "neighborhoods"})
      QVERIFY(!domainFeed(id));
  }
  void strictNumbersGeometryDatesAndCameraAdmission() {
    QVERIFY(!DomainParsers::number(true));
    QVERIFY(!DomainParsers::number("nan"));
    QCOMPARE(*DomainParsers::number("0"), 0.0);
    QVERIFY(!DomainParsers::number("2", false));
    QVERIFY(!DomainParsers::utcDate("2026-10-15T12:00:00").isValid());
    QVERIFY(DomainParsers::utcDate("2026-10-15T12:00:00Z").isValid());
    const auto valid =
        QJsonObject{{"type", "LineString"},
                    {"coordinates", QJsonArray{QJsonArray{179, 0}, QJsonArray{-179, 1}}}};
    auto paths = DomainParsers::geometry(valid);
    QCOMPARE(paths.size(), 1);
    QCOMPARE(paths[0][0].longitude, 179.0);
    QVERIFY(DomainParsers::geometry(valid, 1).isEmpty());
    QVERIFY(!DomainParsers::cameraURL("https://weathercam.digitraffic.fi/C0150201.jpg").isEmpty());
    for (const auto &url :
         {"http://weathercam.digitraffic.fi/C0150201.jpg", "https://evil.invalid/C0150201.jpg",
          "https://weathercam.digitraffic.fi/../C0150201.jpg",
          "https://weathercam.digitraffic.fi/C0150201.jpg?x=1",
          "https://weathercam.digitraffic.fi/%43%30.jpg"})
      QVERIFY(DomainParsers::cameraURL(url).isEmpty());
  }
  void earthquakesRejectMalformedAndKeepZero() {
    QJsonObject event{
        {"id", "fixture-zero"},
        {"geometry", QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{0, 0, 0}}}},
        {"properties",
         QJsonObject{{"mag", 2.5}, {"place", "Generated fixture"}, {"time", 1700000000000.0}}}};
    auto snapshot = parse("earthquakes", {{"features", QJsonArray{event}}});
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.records[0].coordinate.longitude, 0.0);
    QVERIFY(snapshot.records[0].observedAt.isValid());
    QVERIFY(!parse("earthquakes", {{"features", QJsonArray{event, event}}}).ok());
    event["properties"] = QJsonObject{{"mag", true}};
    QVERIFY(!parse("earthquakes", {{"features", QJsonArray{event}}}).ok());
  }
  void aircraftKnownAndUnknownTimes() {
    QJsonObject row{{"hex", "A00001"},  {"lat", 0}, {"lon", 0},
                    {"alt_geom", 1000}, {"gs", 0},  {"seen_pos", 2}};
    auto snapshot = parse("military", {{"now", 1700000000000.0}, {"ac", QJsonArray{row, row}}});
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.rejected, 1);
    QCOMPARE(*snapshot.records[0].altitude, 304.8);
    QCOMPARE(snapshot.records[0].observedAt.toSecsSinceEpoch(), 1699999998LL);
    QVERIFY(!parse("military", {{"ac", QJsonArray{}}}).records.size());
    row.remove("seen_pos");
    snapshot = parse("military", {{"now", 1700000000}, {"ac", QJsonArray{row}}});
    QVERIFY(!snapshot.records[0].observedAt.isValid());
  }
  void disastersRejectEntitiesAndPublicationTime() {
    const QByteArray xml =
        "<rss xmlns:geo='http://www.w3.org/2003/01/geo/wgs84_pos#'><channel><item><title>Red "
        "generated "
        "fixture</title><link>https://www.gdacs.org/report.aspx?eventid=1</link><geo:lat>0</"
        "geo:lat><geo:long>0</geo:long><pubDate>Thu, 15 Oct 2026 10:00:00 "
        "+0000</pubDate></item></channel></rss>";
    auto snapshot = DomainParsers::parse("disasters", xml, {0, 0}, {-1, -1, 1, 1}, {});
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QVERIFY(snapshot.records[0].observedAt.isValid());
    QCOMPARE(snapshot.records[0].details["Timestamp meaning"].toString(), "Alert publication time");
    QVERIFY(!DomainParsers::parse("disasters",
                                  "<!DOCTYPE rss [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>" + xml,
                                  {0, 0}, {-1, -1, 1, 1}, {})
                 .ok());
  }
  void mappedSitesLimitsAndIncompleteResponses() {
    QString error;
    QVERIFY(DomainParsers::mappedQuery("energy", {-2, -2, 2, 2}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(DomainParsers::mappedQuery("installations", {-1, -1, 1, 1})
                .contains("out center tags geom 700"));
    QJsonObject row{{"type", "way"},
                    {"id", 23},
                    {"center", QJsonObject{{"lat", 0}, {"lon", 0}}},
                    {"tags", QJsonObject{{"power", "plant"}, {"name", "Generated fixture"}}}};
    auto snapshot = parse("energy", {{"elements", QJsonArray{row}}});
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QVERIFY(!snapshot.records[0].observedAt.isValid());
    QCOMPARE(snapshot.records[0].id, "energy:osm:way:23");
    QVERIFY(snapshot.records[0].details["Location method"].toString().contains("representative"));
    QVERIFY(!parse("energy", {{"elements", QJsonArray{row}}, {"remark", "timeout"}}).ok());
  }
  void firesFilterAcquisitionNotRetrieval() {
    const auto now = DomainParsers::utcDate("2026-10-15T10:00:00Z");
    auto snapshot =
        DomainParsers::parse("fires",
                             "latitude,longitude,acq_date,acq_time,confidence,frp,satellite\n0,0,"
                             "2026-10-15,900,n,0,NPP\n0,0,2026-10-13,900,n,7,NPP\n",
                             {0, 0}, {-1, -1, 1, 1}, {}, now);
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.records[0].observedAt.toUTC().toString(Qt::ISODate), "2026-10-15T09:00:00Z");
    QCOMPARE(snapshot.records[0].details["Radiative power"].toString(), "0 MW");
    QVERIFY(snapshot.records[0].details["Evidence"].toString().contains("not a confirmed"));
  }
  void catalogAndLaunchMetadataRemainDistinct() {
    QJsonObject scene{
        {"Id", "12345678-1234-4234-8234-123456789012"},
        {"Name", "Generated catalogue fixture"},
        {"ContentDate", QJsonObject{{"Start", "2026-10-15T10:00:00Z"}}},
        {"GeoFootprint",
         QJsonObject{{"type", "Polygon"},
                     {"coordinates", QJsonArray{QJsonArray{QJsonArray{0, 0}, QJsonArray{1, 0},
                                                           QJsonArray{1, 1}, QJsonArray{0, 0}}}}}}};
    auto snapshot = parse("sentinel", {{"value", QJsonArray{scene}}});
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.records[0].footprint.size(), 4);
    QVERIFY(snapshot.records[0].details["Evidence"].toString().contains("not been decoded"));
    QJsonObject launch{{"id", "fixture-launch"},
                       {"name", "Generated launch fixture"},
                       {"net", "2026-10-14T01:00:00Z"},
                       {"pad", QJsonObject{{"latitude", 0}, {"longitude", 0}}}};
    snapshot = parse("launches", {{"results", QJsonArray{launch}}});
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QVERIFY(snapshot.records[0].details["Position basis"].toString().contains("not the rocket"));
  }
  void cameraCatalogNoGeneratedStillsOrTimes() {
    QJsonObject row{
        {"id", "fixture"},
        {"lat", 0},
        {"lon", 0},
        {"commonName", "Generated camera fixture"},
        {"additionalProperties",
         QJsonArray{
             QJsonObject{{"key", "available"}, {"value", "true"}},
             QJsonObject{
                 {"key", "imageUrl"},
                 {"value",
                  "https://s3-eu-west-1.amazonaws.com/jamcams.tfl.gov.uk/00001.00101.jpg"}}}}};
    auto bytes = QJsonDocument(QJsonArray{row}).toJson();
    auto snapshot = DomainParsers::parse("cameras", bytes, {0, 0}, {-1, -1, 1, 1}, {});
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QVERIFY(!snapshot.records[0].observedAt.isValid());
  }
  void referencesBundledCountsAndDateline() {
    const QString root = HELIOS_DOMAIN_SOURCE_DIR;
    const auto datacenters = DomainParsers::reference("datacenters", root),
               dams = DomainParsers::reference("dams", root),
               cables = DomainParsers::reference("cables", root);
    QVERIFY(datacenters.ok());
    QVERIFY(dams.ok());
    QVERIFY(cables.ok());
    QCOMPARE(datacenters.records.size(), 4351);
    QCOMPARE(dams.records.size(), 703);
    QCOMPARE(dams.rejected, 1);
    QCOMPARE(cables.records.size(), 2611);
    QCOMPARE(cables.rejected, 18);
    QVERIFY(!cables.records[0].observedAt.isValid());
    QVERIFY(!cables.retrievedAt.isValid());
    QVERIFY(cables.records[0].id.startsWith("cables:system:"));
    bool landing = false;
    for (const auto &r : cables.records) {
      landing |= r.id.startsWith("cables:landing:");
      QCOMPARE(r.coordinate.longitude, r.paths[0][0].longitude);
    }
    QVERIFY(landing);
  }
  void transitWireZeroAndStaleAdmission() {
    const auto providers =
        transitProviders(QString(HELIOS_DOMAIN_SOURCE_DIR) + "/transit-feeds.json");
    QCOMPARE(providers.size(), 7);
    const auto now = QDateTime::fromSecsSinceEpoch(1700000001, QTimeZone::UTC);
    auto snapshot = DomainParsers::transit(transitFix(1700000001), providers[0], now);
    QVERIFY(snapshot.ok());
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.records[0].coordinate.latitude, 0.0);
    QCOMPARE(snapshot.records[0].details["Speed"].toString(), "0.0 km/h");
    snapshot = DomainParsers::transit(transitFix({}), providers[0], now);
    QVERIFY(!snapshot.records[0].observedAt.isValid());
    QVERIFY(DomainParsers::transit(transitFix(1699999820), providers[0], now).records.isEmpty());
    QVERIFY(!DomainParsers::transit(QByteArray::fromHex("0aff"), providers[0], now).ok());
    QVERIFY(!DomainProto::read(QByteArray::fromHex("80808080808080808002")));
  }
  void aisActualReportsAndWebSocketBounds() {
    QJsonObject row{
        {"MessageType", "PositionReport"},
        {"MetaData", QJsonObject{{"MMSI", 123456789},
                                 {"ShipName", "Generated AIS fixture"},
                                 {"time_utc", "2026-10-15 10:00:00.000000000 +0000 UTC"}}},
        {"Message",
         QJsonObject{{"PositionReport",
                      QJsonObject{{"Latitude", 0}, {"Longitude", 0}, {"Sog", 0}, {"Cog", 360}}}}}};
    auto record = DomainParsers::ais(json(row));
    QVERIFY(record);
    QVERIFY(record->observedAt.isValid());
    QCOMPARE(record->details["Speed"].toString(), "0 kn");
    QVERIFY(!record->details.contains("Course"));
    QString error;
    QVERIFY(!DomainParsers::ais("{\"error\":\"SECRET_RESPONSE\"}", &error));
    QVERIFY(!error.contains("SECRET_RESPONSE"));
    QByteArray frame = QByteArray::fromHex("8102") + "{}", payload;
    quint8 opcode;
    bool final;
    auto parsed = DomainSocket::serverFrame(frame, opcode, final, payload);
    QVERIFY(parsed && *parsed);
    QCOMPARE(opcode, quint8(1));
    QVERIFY(final);
    QCOMPARE(payload, "{}");
    QVERIFY(frame.isEmpty());
    frame = QByteArray::fromHex("817f0000000000040001");
    parsed = DomainSocket::serverFrame(frame, opcode, final, payload);
    QVERIFY(parsed && !*parsed);
    frame = QByteArray::fromHex("8105") + "abc";
    QVERIFY(!DomainSocket::serverFrame(frame, opcode, final, payload));
    auto client = DomainSocket::clientFrame(1, "{}");
    QVERIFY(quint8(client[1]) & 128);
    QCOMPARE(client.size(), 8);
  }
  void nativeStateRetainsSuppliedAnnotationsAndCancellation() {
    DomainNetwork network;
    DomainSnapshot supplied;
    supplied.source = "User authored";
    DomainRecord record;
    record.feed = "annotations";
    record.id = "annotation:fixture";
    record.label = "Generated annotation fixture";
    supplied.records.append(record);
    network.setAnnotations(supplied);
    network.setEnabled("annotations", true);
    QCOMPARE(network.records().size(), 1);
    network.cancel();
    QVERIFY(!network.busy());
    QCOMPARE(network.records().size(), 1);
    network.setEnabled("annotations", false);
    QVERIFY(network.records().isEmpty());
    auto canceled = std::make_shared<std::atomic_bool>(true);
    auto response = domainFetch(QUrl("https://example.invalid"), 1024, canceled);
    QCOMPARE(response.error, "Canceled.");
    canceled->store(false);
    QVERIFY(!domainFetch(QUrl("http://example.invalid"), 1024, canceled).error.isEmpty());
  }
  void missionSnapshotIsBoundedAndKeepsSourceIdentity() {
    DomainRecord record;
    record.id = "fixture:source-id";
    record.feed = "cameras";
    record.label = "Generated evidence fixture";
    record.source = "Provider fixture";
    record.coordinate = {0, 0};
    record.details = {{"Frame URL", "https://weathercam.digitraffic.fi/C0150201.jpg"},
                      {"Video URL", "temporary delivery URL"},
                      {"Operator", "Generated operator label"}};
    record.raw = {{"sensitiveTransientResponse", "not retained in mission"}};
    const auto saved = record.missionJSON();
    QVERIFY(saved);
    QCOMPARE((*saved)["id"].toString(), record.id);
    QVERIFY(!saved->contains("raw"));
    QVERIFY(!(*saved)["details"].toObject().contains("Frame URL"));
    QVERIFY(!(*saved)["details"].toObject().contains("Video URL"));
    QCOMPARE((*saved)["details"].toObject()["Operator"].toString(), "Generated operator label");
    QVERIFY(!(*saved)["observedAt"].isString());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Missions missions;
    missions.create("Generated domain compatibility fixture");
    missions.addEvidence({{"id", newID()},
                          {"kind", "record"},
                          {"referenceID", record.id},
                          {"title", record.label},
                          {"savedAt", foundationNow()},
                          {"record", *saved}});
    validateMissions(missions.plans());
    missions.save(directory.filePath("plans.json"));
    Missions reopened;
    reopened.load(directory.filePath("plans.json"));
    const auto frozen = reopened.selected()["items"].toArray()[0].toObject()["record"].toObject();
    QCOMPARE(frozen["id"].toString(), record.id);
    QCOMPARE(frozen["details"].toObject()["Operator"].toString(), "Generated operator label");
    QVERIFY(frozen["observedAt"].isNull());
    QString error;
    record.footprint = QVector<Coordinate>(2001, Coordinate{0, 0});
    QVERIFY(!record.missionJSON(&error));
    QVERIFY(error.contains("not been simplified"));
    record.footprint.clear();
    record.paths = QVector<QVector<Coordinate>>(65, QVector<Coordinate>{{0, 0}, {1, 1}});
    QVERIFY(!record.missionJSON());
  }
  void retainedHistoryUsesOnlyObservedFixesWithoutInterpolation() {
    DomainHistory history;
    const auto now = DomainParsers::utcDate("2026-10-15T10:00:00Z");
    DomainRecord r;
    r.id = "flight:fixture";
    r.feed = "military";
    r.coordinate = {0, 0};
    r.observedAt = now.addSecs(-60);
    history.ingest({r}, now);
    r.coordinate = {1, 1};
    r.observedAt = now;
    history.ingest({r}, now);
    QCOMPARE(history.fixes(r.id).size(), 2);
    auto prior = history.delayed(r, now.addSecs(-30));
    QVERIFY(prior);
    QCOMPARE(prior->coordinate.latitude, 0.0);
    QCOMPARE(prior->observedAt, now.addSecs(-60));
    QVERIFY(prior->details["Playback"].toString().contains("no interpolation"));
    QVERIFY(!history.delayed(r, now.addSecs(-61)));
    r.feed = "military";
    r.id = "unknown-time";
    r.observedAt = {};
    history.ingest({r}, now);
    QVERIFY(history.fixes(r.id).isEmpty());
    r.id = "many-fixes";
    for (int i = 0; i < 300; ++i) {
      r.observedAt = now.addSecs(-300 + i);
      history.ingest({r}, now);
    }
    QCOMPARE(history.fixes(r.id).size(), 240);
    history.ingest({}, now.addSecs(3601));
    QCOMPARE(history.count(), 0);
    QVector<DomainRecord> many;
    for (int i = 0; i < 5001; ++i) {
      r.id = "object:" + QString::number(i);
      r.observedAt = now;
      many.append(r);
    }
    history.ingest(many, now);
    QCOMPARE(history.count(), 5000);
  }
  void offlineModeRejectsRemoteRequestsBeforeCredentialOrTransportWork() {
    DomainNetwork network(nullptr, false);
    QVERIFY(!network.networking());
    for (const auto &feed : domainFeeds())
      if (!QStringList{"annotations", "datacenters", "dams", "cables"}.contains(feed.id)) {
        network.setEnabled(feed.id, true);
        network.refresh(feed.id);
        QVERIFY(!network.busy(feed.id));
        QVERIFY(network.snapshot(feed.id).error.contains("Offline mode"));
      }
  }
  void degenerateViewportDoesNotReuseAnOlderArea() {
    DomainNetwork network;
    network.setView({9, 45, 9, 45});
    for (const QString feed : {"installations", "energy"})
      network.setEnabled(feed, true);
    QTRY_VERIFY_WITH_TIMEOUT(!network.busy(), 3000);
    for (const QString feed : {"installations", "energy"})
      QVERIFY(network.snapshot(feed).error.contains("Zoom in"));
  }
};
QTEST_GUILESS_MAIN(DomainTests)
#include "DomainTests.moc"
