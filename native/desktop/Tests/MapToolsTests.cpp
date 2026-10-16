#include "Core/MapTools.h"
#include "FakeNetwork.h"
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <cmath>
using namespace godeye;
using namespace godeye::qa;
namespace {
QByteArray places(const QString &name = "Milan") {
  QJsonObject point{{"type", "Point"}, {"coordinates", QJsonArray{9.19, 45.46}}};
  QJsonObject feature{
      {"geometry", point},
      {"properties", QJsonObject{{"name", name}, {"city", "Milan"}, {"country", "Italy"}}}};
  return QJsonDocument(QJsonObject{{"features", QJsonArray{feature}}}).toJson();
}
QByteArray route() {
  QJsonArray steps;
  for (auto type : {"depart", "turn", "roundabout", "arrive"})
    steps.append(QJsonObject{{"distance", 100},
                             {"name", "Via Test"},
                             {"maneuver", QJsonObject{{"type", type},
                                                      {"modifier", "slight right"},
                                                      {"exit", 2},
                                                      {"location", QJsonArray{9.19, 45.46}}}}});
  QJsonObject value{
      {"distance", 1200},
      {"duration", 300},
      {"geometry",
       QJsonObject{{"type", "LineString"},
                   {"coordinates", QJsonArray{QJsonArray{9.18, 45.46}, QJsonArray{9.19, 45.465}}}}},
      {"legs", QJsonArray{QJsonObject{{"steps", steps}}}}};
  return QJsonDocument(QJsonObject{{"code", "Ok"}, {"routes", QJsonArray{value}}}).toJson();
}
QByteArray read(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}
} // namespace
class MapToolsTests : public QObject {
  Q_OBJECT
private slots:
  void searchURLAndBounds() {
    const auto url = placeSearchURL("  Côte d'Azur & railway+city  ");
    QCOMPARE(url.host(), "photon.komoot.io");
    QCOMPARE(QUrlQuery(url).queryItemValue("q"), "Côte d'Azur & railway+city");
    QCOMPARE(QUrlQuery(url).queryItemValue("limit"), "6");
    QVERIFY(placeSearchURL(" ").isEmpty());
    QVERIFY(placeSearchURL(QString(201, 'x')).isEmpty());
    QVERIFY(placeSearchURL("Milan\nsecret").isEmpty());
    const auto results = parsePlaces(places());
    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].label, "Milan, Italy");
    QCOMPARE(results[0].coordinate.latitude, 45.46);
    QVERIFY_EXCEPTION_THROWN(parsePlaces("{}"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(parsePlaces(QByteArray(1024 * 1024 + 1, 'x')), std::runtime_error);
    QJsonObject invalid{
        {"features",
         QJsonArray{QJsonObject{
             {"geometry", QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{9, 999}}}},
             {"properties", QJsonObject{{"name", "Bad"}}}}}}};
    QVERIFY(parsePlaces(QJsonDocument(invalid).toJson()).isEmpty());
  }
  void routeURLParserAndLimits() {
    const auto url = routeURL({45.46, 9.18}, {45.47, 9.20}, "car");
    QCOMPARE(url.host(), "routing.openstreetmap.de");
    QVERIFY(url.path().contains("routed-car/route/v1/driving/"));
    QVERIFY(routeURL({0, 0}, {0, 90}, "car").isEmpty());
    QVERIFY(routeURL({45, 9}, {45, 10}, "shell").isEmpty());
    const auto result = parseRoute(route());
    QCOMPARE(result.points.size(), 2);
    QCOMPARE(result.distance, 1200.);
    QCOMPARE(result.duration, 300.);
    QCOMPARE(result.steps.size(), 4);
    QVERIFY(result.steps[1].instruction.contains("Turn slightly right"));
    QVERIFY(result.steps[2].instruction.contains("take exit 2"));
    QCOMPARE(result.steps.last().instruction, "Arrive at your destination");
    QVERIFY_EXCEPTION_THROWN(parseRoute("{\"code\":\"NoRoute\"}"), std::runtime_error);
    auto root = QJsonDocument::fromJson(route()).object();
    auto value = root["routes"].toArray().first().toObject();
    value["distance"] = -1;
    root["routes"] = QJsonArray{value};
    QVERIFY_EXCEPTION_THROWN(parseRoute(QJsonDocument(root).toJson()), std::runtime_error);
    value["distance"] = 1200;
    QJsonArray many;
    for (int i = 0; i < 30001; ++i)
      many.append(QJsonArray{9, 45});
    value["geometry"] = QJsonObject{{"type", "LineString"}, {"coordinates", many}};
    root["routes"] = QJsonArray{value};
    QVERIFY_EXCEPTION_THROWN(parseRoute(QJsonDocument(root).toJson()), std::runtime_error);
  }
  void asynchronousLookupCancellationAndPreviousResults() {
    FakeNetwork network;
    MapLookup lookup(nullptr, &network);
    QSignalSpy completed(&lookup, &MapLookup::completed);
    network.responses.enqueue({places()});
    QVERIFY(lookup.search("Milan"));
    QTRY_VERIFY(!lookup.busy());
    QCOMPARE(completed.count(), 1);
    QVERIFY(completed.last().first().toBool());
    QCOMPARE(lookup.places().size(), 1);
    network.responses.enqueue({{}, 200, -1, {}, true});
    QVERIFY(lookup.search("Cancelled"));
    lookup.cancel();
    QVERIFY(!lookup.busy());
    QCOMPARE(completed.count(), 1);
    network.responses.enqueue({"{}", 503});
    QVERIFY(lookup.search("Unavailable"));
    QTRY_VERIFY(!lookup.busy());
    QVERIFY(!lookup.error().isEmpty());
    QCOMPARE(lookup.places()[0].label, "Milan, Italy");
    network.responses.enqueue({route()});
    QVERIFY(lookup.route({45.46, 9.18}, {45.47, 9.20}, "foot"));
    QTRY_VERIFY(!lookup.busy());
    QCOMPARE(lookup.result().points.size(), 2);
    QVERIFY(!lookup.search("\n"));
    QVERIFY(!lookup.error().isEmpty());
  }
  void cancellationDuringParseRetainsGeneration() {
    FakeNetwork network;
    MapLookup lookup(nullptr, &network);
    network.responses.enqueue({places("OLD")});
    QVERIFY(lookup.search("old"));
    auto *reply = network.findChild<QNetworkReply *>();
    QVERIFY(reply);
    connect(reply, &QNetworkReply::finished, &lookup, [&] { lookup.cancel(); });
    QTRY_VERIFY(!lookup.busy());
    QVERIFY(lookup.places().isEmpty());
    network.responses.enqueue({places("NEW")});
    QVERIFY(lookup.search("new"));
    QTRY_VERIFY(!lookup.busy());
    QVERIFY(lookup.places().first().label.startsWith("NEW"));
  }
  void annotationLibraryCompatibilityConflictAndRecovery() {
    QTemporaryDir root;
    const auto path = root.path() + "/annotations.json";
    AnnotationLibrary first(path), second(path);
    first.load();
    second.load();
    auto annotation = newAnnotation("Unicode café", {{45, 9}, {45.1, 9.1}, {45.2, 9.2}}, true);
    annotation["futureExtension"] = QJsonObject{{"preserve", true}};
    first.add(annotation);
    const auto original = read(path);
    QVERIFY_EXCEPTION_THROWN(second.add(newAnnotation("Conflict", {{45, 9}}, false)),
                             std::runtime_error);
    QCOMPARE(read(path), original);
    QVERIFY(second.values().isEmpty());
    first.update(annotation["id"].toString(), "Edited", {{45, 9}, {45.1, 9.1}, {45.2, 9.2}}, true);
    QCOMPARE(first.values().first().toObject()["futureExtension"], annotation["futureExtension"]);
    QCOMPARE(first.values().first().toObject()["createdAt"], annotation["createdAt"]);
    QVERIFY(QFile::exists(path + ".backup"));
    AnnotationLibrary reopened(path);
    reopened.load();
    QCOMPARE(reopened.values(), first.values());
    reopened.remove(annotation["id"].toString());
    QVERIFY(reopened.values().isEmpty());
    QFile corrupted(root.path() + "/bad.json");
    QVERIFY(corrupted.open(QIODevice::WriteOnly));
    corrupted.write("not-json");
    corrupted.close();
    AnnotationLibrary bad(corrupted.fileName());
    QVERIFY_EXCEPTION_THROWN(bad.load(), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(bad.add(annotation), std::runtime_error);
    QCOMPARE(read(corrupted.fileName()), QByteArray("not-json"));
    QVERIFY_EXCEPTION_THROWN(newAnnotation("", {{45, 9}}, false), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(newAnnotation("Too few", {{45, 9}}, true), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(validateAnnotations({annotation, annotation}), std::runtime_error);
  }
  void workspaceCompatibilityAndSharedPrivacy() {
    QTemporaryDir root;
    auto value = defaultWorkspace();
    value["unknownFutureField"] = QJsonObject{{"secret", "not-shared"}};
    auto camera = value["camera"].toObject();
    camera["privateExtension"] = "not-shared";
    value["camera"] = camera;
    value["annotations"] = QJsonArray{newAnnotation("Private note", {{45, 9}}, false)};
    value["layers"] = QJsonArray{"earthquakes", "neighborhoods", "traffic"};
    value["trackedID"] = "neighborhoods:old";
    value.remove("terrain");
    value = validatedWorkspace(value);
    QVERIFY(value["terrain"].toBool());
    QVERIFY(!value.contains("trackedID"));
    QCOMPARE(value["layers"].toArray().size(), 2);
    const auto path = root.path() + "/workspace.json";
    saveWorkspace(path, value);
    QCOMPARE(loadWorkspace(path), value);
    const auto link = workspaceShareURL(value);
    const auto reopened = workspaceFromShareURL(link);
    QVERIFY(reopened);
    QVERIFY(reopened->value("annotations").toArray().isEmpty());
    QVERIFY(!reopened->contains("unknownFutureField"));
    QVERIFY(!reopened->value("camera").toObject().contains("privateExtension"));
    auto invalid = value;
    invalid["opacity"] = 2;
    QVERIFY_EXCEPTION_THROWN(validatedWorkspace(invalid), std::runtime_error);
    invalid = value;
    invalid["layers"] = QJsonArray{"not-a-source"};
    QVERIFY_EXCEPTION_THROWN(validatedWorkspace(invalid), std::runtime_error);
    invalid = value;
    invalid["imagery"] = "terra";
    for (const auto &date : {"1999-12-31", "2099-01-01", "2026-02-30"}) {
      invalid["imageryDate"] = date;
      QVERIFY_EXCEPTION_THROWN(validatedWorkspace(invalid), std::runtime_error);
    }
    invalid["imageryDate"] = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate);
    QVERIFY(!validatedWorkspace(invalid).isEmpty());
    auto malicious = value;
    malicious["annotations"] = QJsonArray{newAnnotation("Hidden", {{45, 9}}, false)};
    QUrl bad("godeye://workspace");
    QUrlQuery query;
    query.addQueryItem(
        "state",
        QString::fromLatin1(QJsonDocument(malicious).toJson(QJsonDocument::Compact).toBase64()));
    bad.setQuery(query);
    QVERIFY(!workspaceFromShareURL(bad));
    QVERIFY(!workspaceFromShareURL(QUrl(link.toString() + "&state=bad")));
    QVERIFY(!workspaceFromShareURL(QUrl("godeye://workspace?state=@@@")));
    QFile corrupt(root.path() + "/corrupt.json");
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("{");
    corrupt.close();
    QVERIFY_EXCEPTION_THROWN(saveWorkspace(corrupt.fileName(), value), std::runtime_error);
    QCOMPARE(read(corrupt.fileName()), QByteArray("{"));
  }
  void cameraShareAndSolarApproximation() {
    Camera camera{{45.46, 9.18}, 30000, 123, 55};
    auto url = cameraShareURL(camera);
    const auto parsed = cameraFromShareURL(url);
    QVERIFY(parsed);
    QCOMPARE(parsed->json(), camera.json());
    QVERIFY(!cameraFromShareURL(QUrl(url.toString() + "&lat=10")));
    QVERIFY(!cameraFromShareURL(QUrl("godeye://view?lat=45&lon=9&alt=1")));
    QVERIFY(!cameraFromShareURL(QUrl("godeye://view?lat=45&lon=9&alt=30000&heading=360")));
    QVERIFY(workspaceFromShareURL(url));
    SolarState solar(QDateTime::fromString("2026-06-21T12:00:00Z", Qt::ISODate));
    QVERIFY(solar.subsolar.latitude > 23 && solar.subsolar.latitude < 24);
    QCOMPARE(solar.subsolar.longitude, 0.);
    QVERIFY(std::abs(solar.elevation(solar.subsolar) - 90) < 1e-6);
    const auto line = solar.terminator();
    QCOMPARE(line.size(), 721);
    for (auto point : line) {
      QVERIFY(point.valid());
      QVERIFY(std::abs(solar.elevation(point)) < 1e-6);
    }
  }
  void publicLookupOptIn() {
    if (!qEnvironmentVariableIsSet("GODEYE_MAPTOOLS_LIVE"))
      QSKIP("Set GODEYE_MAPTOOLS_LIVE for Photon and public OSRM checks.");
    MapLookup search;
    QVERIFY(search.search("Milano Italia"));
    QTRY_VERIFY_WITH_TIMEOUT(!search.busy(), 45000);
    QVERIFY2(search.error().isEmpty(), qPrintable(search.error()));
    QVERIFY(!search.places().isEmpty());
    const auto qa = qEnvironmentVariable("GODEYE_MAPTOOLS_QA");
    if (!qa.isEmpty()) {
      QVERIFY(QDir().mkpath(qa));
      QFile file(qa + "/photon-live.json");
      QVERIFY(file.open(QIODevice::WriteOnly));
      const auto bytes =
          QJsonDocument(QJsonObject{{"provider", "Photon / OpenStreetMap"},
                                    {"query", "Milano Italia"},
                                    {"matches", search.places().size()},
                                    {"firstPlace", search.places().first().label},
                                    {"firstCoordinate", search.places().first().coordinate.json()}})
              .toJson();
      QCOMPARE(file.write(bytes), bytes.size());
    }
  }
  void publicRouteOptIn() {
    if (!qEnvironmentVariableIsSet("GODEYE_MAPTOOLS_LIVE"))
      QSKIP("Set GODEYE_MAPTOOLS_LIVE for the public OSRM route check.");
    MapLookup directions;
    QVERIFY(directions.route({45.4641, 9.1919}, {45.4700, 9.2000}, "foot"));
    QTRY_VERIFY_WITH_TIMEOUT(!directions.busy(), 45000);
    QVERIFY2(directions.error().isEmpty(), qPrintable(directions.error()));
    QVERIFY(directions.result().points.size() >= 2);
    const auto qa = qEnvironmentVariable("GODEYE_MAPTOOLS_QA");
    if (!qa.isEmpty()) {
      QVERIFY(QDir().mkpath(qa));
      QFile file(qa + "/osrm-live.json");
      QVERIFY(file.open(QIODevice::WriteOnly));
      QJsonObject report{
          {"routingProvider", "routing.openstreetmap.de / OSRM"},
          {"mode", "foot"},
          {"start", Coordinate{45.4641, 9.1919}.json()},
          {"end", Coordinate{45.4700, 9.2000}.json()},
          {"distanceMetres", directions.result().distance},
          {"durationSeconds", directions.result().duration},
          {"geometryPoints", directions.result().points.size()},
          {"steps", directions.result().steps.size()},
          {"disclosure",
           "Provider route estimate, not observed movement or a navigation guarantee"}};
      const auto bytes = QJsonDocument(report).toJson();
      QCOMPARE(file.write(bytes), bytes.size());
    }
  }
};
QTEST_GUILESS_MAIN(MapToolsTests)
#include "MapToolsTests.moc"
