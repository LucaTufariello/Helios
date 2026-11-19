#include "Core/Missions.h"
#include "Core/Products.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace helios;
class CoreTests : public QObject {
  Q_OBJECT
private slots:
  void cameraAndTiles() {
    Camera camera;
    QVERIFY(camera.valid());
    auto encoded = Camera::read(camera.json());
    QVERIFY(encoded);
    QCOMPARE(encoded->range, camera.range);
    for (double longitude : {9.0, 179.99, -179.99}) {
      camera.center = {45, longitude};
      camera.range = 3000;
      camera.heading = 73;
      camera.tilt = 65;
      const auto tiles = visibleTiles(camera, 2.4);
      QVERIFY(!tiles.isEmpty());
      QVERIFY(tiles.size() <= 81);
      QSet<QString> keys;
      for (auto tile : tiles) {
        QVERIFY(tile.y >= 0 && tile.y < (1 << tile.zoom));
        keys.insert(tile.key());
      }
      QCOMPARE(keys.size(), tiles.size());
    }
    QVERIFY(!Coordinate::read({{"latitude", 91}, {"longitude", 0}}));
    QVERIFY(!Bounds::read(QJsonArray{9, 45, 9, 46}));
    QCOMPARE(TileID::containing({0, 180}, 4).x, 0);
  }
  void missionRoundTrip() {
    QTemporaryDir directory;
    Missions m;
    const auto id = m.create("  Generated QA  ");
    QCOMPARE(m.selected()["name"].toString(), QString("Generated QA"));
    m.addArea("Area", "Monitor fixture", {9, 45, 9.01, 45.01});
    m.addView("View", Camera{});
    m.addNote("Note", "Body", "Reference");
    m.addTask("Task");
    m.addObservation("Object", "Fixture", {45, 9});
    const auto file = directory.filePath("missions.json");
    m.save(file);
    Missions restored;
    restored.load(file);
    QCOMPARE(restored.selectedID(), id);
    QCOMPARE(restored.plans(), m.plans());
    const auto task = restored.selected()["brief"]
                          .toObject()["tasks"]
                          .toArray()
                          .first()
                          .toObject()["id"]
                          .toString();
    restored.setTaskDone(task, true);
    QVERIFY(restored.selected()["brief"]
                .toObject()["tasks"]
                .toArray()
                .first()
                .toObject()["done"]
                .toBool());
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, restored.setField("name", " "));
    QCOMPARE(restored.selected()["name"].toString(), QString("Generated QA"));
  }
  void observationModelProvenanceIsOptionalCompleteAndBounded() {
    Missions m;
    m.create("Generated provenance review");
    m.addObservation("Object", "Fixture", {45, 9});
    const auto base = m.plans();
    auto observation =
        m.selected()["brief"].toObject()["observations"].toArray().first().toObject();
    const auto withObservation = [&](const QJsonObject &value) {
      auto plans = base;
      auto plan = plans.first().toObject();
      auto brief = plan["brief"].toObject();
      brief["observations"] = QJsonArray{value};
      plan["brief"] = brief;
      plans[0] = plan;
      return plans;
    };
    validateMissions(withObservation(observation));
    // Existing Core ML observations have source provenance without an ONNX field group.
    observation["inputSHA256"] = QString(64, 'a');
    observation["modelName"] = "GENERATED-QA-detector.mlmodel";
    observation["inputTransform"] = "Vision scaleFill";
    validateMissions(withObservation(observation));
    for (const auto key :
         {"modelSHA256", "modelContractSHA256", "modelBackend", "imageBox", "imagePixelBox"})
      observation[key] = QJsonValue::Null;
    validateMissions(withObservation(observation));
    observation["modelSHA256"] = QString(64, 'b');
    observation["modelContractSHA256"] = QString(64, 'c');
    observation["modelBackend"] = "onnx-cpu";
    observation["imageBox"] = QJsonArray{0, 0, 1, 1};
    observation["imagePixelBox"] = QJsonArray{0, 0, 2048, 2048};
    observation["futureModelExtension"] = QJsonObject{{"preserve", "unchanged"}};
    validateMissions(withObservation(observation));
    observation["modelBackend"] = "onnx-tinyyolov2-voc-cpu";
    validateMissions(withObservation(observation));
    for (const auto key :
         {"modelSHA256", "modelContractSHA256", "modelBackend", "imageBox", "imagePixelBox"}) {
      auto incomplete = observation;
      incomplete.remove(key);
      QVERIFY_EXCEPTION_THROWN(validateMissions(withObservation(incomplete)), std::runtime_error);
      incomplete[key] = QJsonValue::Null;
      QVERIFY_EXCEPTION_THROWN(validateMissions(withObservation(incomplete)), std::runtime_error);
    }
    const QList<QPair<QString, QJsonValue>> invalidFields{
        {"inputSHA256", QString(64, 'A')},
        {"inputSHA256", QString(64, 'a') + "\n"},
        {"inputSHA256", 12},
        {"modelSHA256", QString(63, 'b')},
        {"modelContractSHA256", QString(64, 'G')},
        {"modelBackend", "coreml"},
        {"imageBox", QJsonArray{0, 0, 1}},
        {"imageBox", QJsonArray{0, 0, 1.001, 1}},
        {"imageBox", QJsonArray{.5, 0, .5, 1}},
        {"imageBox", QJsonArray{0, 0, "1", 1}},
        {"imagePixelBox", QJsonArray{0, 0, 2049, 2048}},
        {"imagePixelBox", QJsonArray{-1, 0, 200, 200}},
        {"imagePixelBox", QJsonArray{0, 20, 200, 20}},
        {"imagePixelBox", QJsonArray{0, 0, QJsonValue::Null, 20}}};
    for (const auto &field : invalidFields) {
      auto invalid = observation;
      invalid[field.first] = field.second;
      QVERIFY_EXCEPTION_THROWN(validateMissions(withObservation(invalid)), std::runtime_error);
    }
    QTemporaryDir directory;
    const auto filename = directory.filePath("missions.json");
    QFile file(filename);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(withObservation(observation)).toJson());
    file.close();
    m.load(filename);
    m.addTask("Retain provenance extensions");
    m.save(filename);
    Missions reopened;
    reopened.load(filename);
    QCOMPARE(reopened.selected()["brief"].toObject()["observations"].toArray().first(),
             QJsonValue(observation));
  }
  void corruptFileAndConflictPreserved() {
    QTemporaryDir directory;
    const auto file = directory.filePath("missions.json");
    Missions m;
    m.create("Generated QA");
    m.save(file);
    QFile changed(file);
    QVERIFY(changed.open(QIODevice::Append));
    changed.write(" ");
    changed.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, m.save(file));
    QFile bad(directory.filePath("bad.json"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("invalid");
    bad.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, m.save(bad.fileName()));
    QVERIFY(bad.open(QIODevice::ReadOnly));
    QCOMPARE(bad.readAll(), QByteArray("invalid"));
  }
  void unknownFieldsPreserved() {
    QTemporaryDir directory;
    Missions m;
    m.create("Generated QA");
    auto array = m.plans();
    auto plan = array.first().toObject();
    plan["futureExtension"] = QJsonObject{{"retain", "unchanged"}};
    array[0] = plan;
    const auto file = directory.filePath("missions.json");
    QFile output(file);
    QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(QJsonDocument(array).toJson());
    output.close();
    m.load(file);
    m.addTask("Keep extension");
    m.save(file);
    Missions restored;
    restored.load(file);
    QCOMPARE(restored.selected()["futureExtension"], plan["futureExtension"]);
  }
  void unicodeAndLinks() {
    Missions m;
    m.create(QString::fromUtf8("Generated QA — 港口 🛰️"));
    m.addArea("Area", "", {9, 45, 9.01, 45.01});
    const auto area =
        m.selected()["brief"].toObject()["areas"].toArray().first().toObject()["id"].toString();
    m.addView("View", Camera{});
    const auto evidence = m.selected()["items"].toArray().first().toObject()["id"].toString();
    m.addNote("Note", "Body", "", evidence, area);
    m.removeEntry("items", evidence);
    QVERIFY(!m.selected()["brief"].toObject()["notes"].toArray().first().toObject().contains(
        "evidenceID"));
    m.removeEntry("areas", area);
    QVERIFY(
        !m.selected()["brief"].toObject()["notes"].toArray().first().toObject().contains("areaID"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, m.addNote("Invalid", "", "", newID()));
  }
  void refinementMasksOnlyLoadedChildren() {
    const TileID parent{1, 1, 2};
    auto cuts = tileCutouts(parent, {{2, 2, 3}, {3, 2, 3}});
    QCOMPARE(cuts.size(), 1);
    QCOMPARE(cuts.first(), QRectF(0, 0, 1, 0.5));
    cuts = tileCutouts(parent, {{2, 2, 3}, {3, 3, 3}});
    QCOMPARE(cuts.size(), 2);
    QVERIFY(!cuts.first().contains(QPointF(0.75, 0.25)));
    cuts = tileCutouts(parent, {{2, 2, 3}, {3, 2, 3}, {2, 3, 3}, {3, 3, 3}, {0, 0, 3}});
    QCOMPARE(cuts.size(), 1);
    QCOMPARE(cuts.first(), QRectF(0, 0, 1, 1));
    QVERIFY(tileCutouts(parent, {{1, 1, 2}, {0, 0, 3}}).isEmpty());
  }
  void foundationDatesKeepUtcSemantics() {
    QCOMPARE(dateLabel(0), QString("2001-01-01 00:00:00 UTC"));
    QCOMPARE(dateLabel("2026-10-13T12:34:56.123Z"), QString("2026-10-13 12:34:56 UTC"));
    QCOMPARE(dateLabel(QJsonValue()), QString("Time unavailable"));
    QTemporaryDir directory;
    Missions m;
    m.create("UTC compatibility");
    auto plans = m.plans();
    auto p = plans[0].toObject();
    p["createdAt"] = "2026-10-13T12:34:56.123Z";
    p["updatedAt"] = "2026-10-13T14:34:56.123+02:00";
    plans[0] = p;
    validateMissions(plans);
    const auto path = directory.filePath("iso-plans.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(plans).toJson());
    file.close();
    m.load(path);
    m.save(path);
    Missions reopened;
    reopened.load(path);
    QCOMPARE(reopened.selected()["createdAt"], p["createdAt"]);
    QCOMPARE(reopened.selected()["updatedAt"], p["updatedAt"]);
    p["createdAt"] = "2026-10-13T12:34:56";
    plans[0] = p;
    QVERIFY_EXCEPTION_THROWN(validateMissions(plans), std::runtime_error);
  }
  void opticalProductAndExportPreserveBytes() {
    QTemporaryDir d;
    QImage image(32, 24, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QVERIFY(image.save(d.filePath("preview.png")));
    QFile science(d.filePath("source-values.tif"));
    QVERIFY(science.open(QIODevice::WriteOnly));
    const QByteArray bytes(3 * 1024 * 1024, 'q');
    QCOMPARE(science.write(bytes), bytes.size());
    science.close();
    QJsonObject root{{"version", 1},
                     {"state", "completed"},
                     {"crs", "EPSG:4326"},
                     {"width", 32},
                     {"height", 24},
                     {"bounds", QJsonArray{9, 45, 9.01, 45.01}},
                     {"preview", "preview.png"},
                     {"scientific", "source-values.tif"},
                     {"sourceResolutionMetres", QJsonArray{10, 10}},
                     {"scene", QJsonObject{{"id", "GENERATED_QA_NOT_A_REAL_ACQUISITION"},
                                           {"platform", "GENERATED QA"},
                                           {"acquiredAt", "2026-10-13T12:00:00Z"}}}};
    QFile manifest(d.filePath("result.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(QJsonDocument(root).toJson());
    manifest.close();
    const auto products = readProducts(manifest.fileName());
    QCOMPARE(products.size(), 1);
    QCOMPARE(products.first().image.size(), image.size());
    QCOMPARE(products.first().bounds.west, 9.0);
    const auto descriptors = readProducts(manifest.fileName(), false);
    QVERIFY(descriptors.first().image.isNull());
    QCOMPARE(descriptors.first().manifestHash, products.first().manifestHash);
    Missions m;
    m.create("QA product links");
    m.addEvidence(products.first().missionEvidence());
    validateMissions(m.plans());
    const auto exportFile = d.filePath("export.tif");
    exportProductFile(science.fileName(), exportFile);
    QFile exported(exportFile);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QCOMPARE(exported.readAll(), bytes);
    exported.close();
    auto cancel = std::make_shared<std::atomic_bool>(true);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             exportProductFile(science.fileName(), exportFile, cancel));
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QCOMPARE(exported.readAll(), bytes);
    exported.close();
    root["crs"] = "EPSG:3857";
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(QJsonDocument(root).toJson());
    manifest.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, readProducts(manifest.fileName()));
  }
  void unsafeProductPathsRejected() {
    QTemporaryDir root, outside;
    QFile file(outside.filePath("preview.png"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not an image");
    file.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, safeProductFile(root.path(), "../preview.png"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, safeProductFile(root.path(), "file:preview.png"));
    QVERIFY(QFile::link(file.fileName(), root.filePath("linked.png")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, safeProductFile(root.path(), "linked.png"));
    QFile manifest(root.filePath("result.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write("{\"version\":1,\"state\":\"processing\"}");
    manifest.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, readProducts(manifest.fileName()));
  }
  void radarPreviewDoesNotReplaceScientificDimensions() {
    QTemporaryDir d;
    QImage image(32, 16, QImage::Format_RGBA8888);
    image.fill(Qt::gray);
    QVERIFY(image.save(d.filePath("VV-preview.png")));
    QJsonObject root{{"version", 1},
                     {"state", "completed"},
                     {"metadata", QJsonObject{{"satellite", "Sentinel-1D"},
                                              {"inputName", "GENERATED_QA_NOT_A_REAL_PRODUCT.SAFE"},
                                              {"acquisitionStart", "2026-10-13T05:00:00Z"}}},
                     {"options", QJsonObject{{"pixelSpacing", 20}}},
                     {"outputs", QJsonArray{QJsonObject{{"id", "VV"},
                                                        {"preview", "VV-preview.png"},
                                                        {"width", 20560},
                                                        {"height", 10613},
                                                        {"bounds", QJsonArray{8, 44, 10, 46}}}}}};
    QFile file(d.filePath("result.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();
    const auto products = readProducts(file.fileName());
    QCOMPARE(products.first().pixelSize, QSize(32, 16));
    QCOMPARE(products.first().rasterSize, QSize(20560, 10613));
    QCOMPARE(products.first().band, QString("VV"));
    Missions m;
    m.create("QA radar link");
    m.addEvidence(products.first().missionEvidence());
    validateMissions(m.plans());
  }
  void sharedLegacyFixture() {
    const auto filename = QStringLiteral(HELIOS_FIXTURE_DIR) + "/cross-platform-mission.json";
    if (!QFileInfo::exists(filename))
      QSKIP("Cross-client fixture is generated after native GPU validation.");
    Missions m;
    m.load(filename);
    QVERIFY(!m.selected().isEmpty());
    QVERIFY(m.selected()["items"].toArray().size() >= 2);
    QTemporaryDir d;
    m.save(d.filePath("copy.json"));
    Missions restored;
    restored.load(d.filePath("copy.json"));
    QCOMPARE(restored.plans(), m.plans());
  }
  void sharedDossierFixturePreservesPortableMetadata() {
    Missions m;
    m.load(QStringLiteral(HELIOS_FIXTURE_DIR) + "/cross-platform-dossier.json");
    const auto brief = m.selected()["brief"].toObject();
    const auto dossier = brief["areas"].toArray()[0].toObject()["dossier"].toObject();
    QCOMPARE(dossier["linkedEvidenceIDs"].toArray().size(), 2);
    QCOMPARE(dossier["baselineEvidenceIDs"].toArray().size(), 1);
    QVERIFY(dossier["sourceSiteEvidenceID"].isString());
    QVERIFY(brief["tasks"].toArray()[0].toObject()["observationID"].isString());
    QTemporaryDir root;
    m.save(root.filePath("plans.json"));
    Missions copy;
    copy.load(root.filePath("plans.json"));
    QCOMPARE(copy.plans(), m.plans());
  }
  void sharedMonitoringFixturePreservesConfigurationInboxAndTaskOrigin() {
    Missions m;
    m.load(QStringLiteral(HELIOS_FIXTURE_DIR) + "/cross-platform-monitoring.json");
    auto b = m.selected()["brief"].toObject();
    QCOMPARE(b["monitoringInbox"].toArray().size(), 1);
    QCOMPARE(b["areas"]
                 .toArray()[0]
                 .toObject()["dossier"]
                 .toObject()["monitoring"]
                 .toObject()["controls"]
                 .toArray()
                 .size(),
             6);
    QTemporaryDir d;
    m.save(d.filePath("copy.json"));
    Missions copy;
    copy.load(d.filePath("copy.json"));
    QCOMPARE(copy.plans(), m.plans());
  }
  void existingMissionLibraryReadOnlyCheck() {
    const auto filename = qEnvironmentVariable("HELIOS_MISSION_CHECK");
    if (filename.isEmpty())
      QSKIP("Opt-in check of an existing mission library.");
    QFile source(filename);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const auto before = source.readAll();
    source.close();
    Missions m;
    m.load(filename);
    QTemporaryDir d;
    m.save(d.filePath("copy.json"));
    Missions restored;
    restored.load(d.filePath("copy.json"));
    QCOMPARE(restored.plans(), m.plans());
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), before);
  }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
