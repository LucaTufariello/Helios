#include "Core/Missions.h"
#include "Core/Monitoring.h"
#include "Core/MonitoringRunner.h"
#include "Core/ScientificQueue.h"
#include "Core/Timeline.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace helios;
class MonitoringTests : public QObject {
  Q_OBJECT
  static QJsonObject area(Missions &m, const QString &id) {
    return monitoringArea(m.selected(), id);
  }
  static QJsonObject radar() {
    return {{"id", newID()},
            {"referenceID", newID()},
            {"kind", "radar"},
            {"title", "Generated reference"},
            {"savedAt", foundationNow()},
            {"acquisition", QJsonObject{{"sourceID", "GENERATED"},
                                        {"sensor", "Sentinel-1A"},
                                        {"displayBand", "VV"},
                                        {"bounds", QJsonArray{9, 45, 9.1, 45.1}},
                                        {"acquiredAt", 0}}}};
  }
  static QJsonObject profile(const QString &baseline) {
    auto p = monitoringDefaults();
    p["baselineEvidenceID"] = baseline;
    QJsonArray controls;
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 3; ++x)
        controls.append(QJsonObject{
            {"id", newID()},
            {"name", QString("Control %1-%2").arg(y).arg(x)},
            {"role", y ? "Verify" : "Fit"},
            {"region", Bounds{9 + x * .02, 45 + y * .03, 9.01 + x * .02, 45.01 + y * .03}.json()}});
    p["controls"] = controls;
    return p;
  }
  static QJsonObject entry(const QString &area, const QString &baseline) {
    return {{"version", 1},
            {"id", newID()},
            {"key", QString(64, 'a')},
            {"signature", QString(64, 'b')},
            {"areaID", area},
            {"baselineEvidenceID", baseline},
            {"areaName", "Generated area"},
            {"title", "Generated analysis"},
            {"sourceID", "GENERATED"},
            {"kind", "Analysis"},
            {"status", "Blocked"},
            {"reviewState", "New"},
            {"rationale", ""},
            {"reason", "Generated missing controls"},
            {"createdAt", foundationNow()},
            {"updatedAt", foundationNow()}};
  }
private slots:
  void importedPairProducesOwnedTimestampAndDeduplicatedWork() {
    QTemporaryDir root;
    QVector<ProductLayer> products;
    for (int i = 0; i < 2; ++i) {
      const auto directory = root.filePath(newID());
      QVERIFY(QDir().mkpath(directory));
      QImage preview(96, 96, QImage::Format_RGB32);
      preview.fill(Qt::gray);
      QVERIFY(preview.save(directory + "/preview.png"));
      QFile scientific(directory + "/power.tif");
      QVERIFY(scientific.open(QIODevice::WriteOnly));
      scientific.write("Generated metadata fixture; no raster analysis");
      scientific.close();
      QJsonObject manifest{
          {"version", 1},
          {"state", "completed"},
          {"metadata",
           QJsonObject{{"mission", "Sentinel-1"},
                       {"satellite", "Sentinel-1A"},
                       {"inputName", "GENERATED.SAFE"},
                       {"mode", "IW"},
                       {"productType", "GRD"},
                       {"relativeOrbit", 66},
                       {"orbitDirection", "DESCENDING"},
                       {"acquisitionStart", i ? "2020-07-01T05:00:00Z" : "2016-07-01T05:00:00Z"}}},
          {"options", QJsonObject{{"applyOrbit", true}, {"speckle", false}, {"pixelSpacing", 20}}},
          {"processingStages", QJsonArray{"Calibration", "Terrain-Correction"}},
          {"outputs", QJsonArray{QJsonObject{{"id", "VV"},
                                             {"scientific", "power.tif"},
                                             {"preview", "preview.png"},
                                             {"width", 96},
                                             {"height", 96},
                                             {"bounds", QJsonArray{9, 45, 9.1, 45.1}}}}}};
      QFile file(directory + "/result.json");
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(QJsonDocument(manifest).toJson());
      file.close();
      products += readProducts(file.fileName(), false);
    }
    Missions m;
    const auto mission = m.create("Generated"), id = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto evidence = m.linkDossierEvidence(mission, id, products.first().missionEvidence());
    m.setDossierBaseline(mission, id, evidence, true);
    m.configureMonitoring(mission, id, profile(evidence));
    QVERIFY(monitoringWork(m.selected(), products).isEmpty());
    const auto work = monitoringWork(m.selected(), products, id);
    QCOMPARE(work.size(), 1);
    const auto event = work.first().event;
    QVERIFY(event["sourceTime"].isDouble());
    QCOMPARE(missionTime(event["sourceTime"]),
             QDateTime::fromString("2020-07-01T05:00:00Z", Qt::ISODate));
    m.upsertMonitoringEvent(mission, event);
    QVERIFY(monitoringWork(m.selected(), products, id).isEmpty());
    auto failed = event;
    failed["status"] = "Interrupted";
    m.upsertMonitoringEvent(mission, failed);
    QCOMPARE(monitoringWork(m.selected(), products, id).size(), 1);
    QFile replaced(products.last().scientific);
    QVERIFY(replaced.open(QIODevice::Append));
    replaced.write("changed");
    replaced.close();
    const auto replacementWork = monitoringWork(m.selected(), products, id);
    QCOMPARE(replacementWork.size(), 1);
    QVERIFY(replacementWork.first().key != work.first().key);
    QFile changed(products.last().manifest);
    QVERIFY(changed.open(QIODevice::Append));
    changed.write(" ");
    changed.close();
    QVERIFY(monitoringWork(m.selected(), products, id).isEmpty());
  }
  void monitoringConfigurationRoundTripCleanupAndAtomicValidation() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Generated"), id = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto evidence = m.linkDossierEvidence(mission, id, radar());
    m.setDossierBaseline(mission, id, evidence, true);
    auto p = profile(evidence);
    m.configureMonitoring(mission, id, p);
    m.configureMonitoring(mission, id, p);
    QCOMPARE(m.selected()["brief"].toObject()["monitors"].toArray().size(), 1);
    QVERIFY(!area(m, id)["dossier"].toObject()["monitoring"].toObject()["enabled"].toBool());
    p = area(m, id)["dossier"].toObject()["monitoring"].toObject();
    p["enabled"] = true;
    m.configureMonitoring(mission, id, p);
    QCOMPARE(m.selected()["brief"].toObject()["monitors"].toArray().size(), 1);
    QVERIFY(
        m.selected()["brief"].toObject()["monitors"].toArray()[0].toObject()["enabled"].toBool());
    auto invalid = p;
    invalid["controls"] = QJsonArray{};
    const auto before = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.configureMonitoring(mission, id, invalid), std::runtime_error);
    QCOMPARE(m.plans(), before);
    m.save(root.filePath("plans.json"));
    Missions copy;
    copy.load(root.filePath("plans.json"));
    QCOMPARE(copy.plans(), m.plans());
    const auto event = m.upsertMonitoringEvent(mission, entry(id, evidence));
    m.addMonitoringTask(event, "Follow up");
    m.setDossierBaseline(mission, id, evidence, false);
    QVERIFY(!area(m, id)["dossier"].toObject()["monitoring"].toObject()["enabled"].toBool());
    m.removeEntry("items", evidence);
    QVERIFY(!m.selected()["brief"].toObject()["monitoringInbox"].toArray()[0].toObject().contains(
        "baselineEvidenceID"));
    m.removeEntry("areas", id);
    QVERIFY(!m.selected()["brief"].toObject()["monitoringInbox"].toArray()[0].toObject().contains(
        "areaID"));
    m.reviewMonitoringEvent(event, "Closed", "Retained source job");
    m.removeMonitoringEvent(event);
    QVERIFY(!m.selected()["brief"].toObject()["tasks"].toArray()[0].toObject().contains(
        "monitoringEventID"));
    validateMissions(m.plans());
  }
  void inboxCaptureIsMissionScopedDeduplicatedAndDoesNotReplaceReview() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Generated"), id = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto evidence = m.linkDossierEvidence(mission, id, radar());
    m.save(root.filePath("plans.json"));
    auto e = entry(id, evidence);
    const auto entryID = m.upsertMonitoringEvent(mission, e);
    m.reviewMonitoringEvent(entryID, "Investigating", "Keep rationale");
    const auto other = m.create("Other");
    e["status"] = "Ready";
    e["reason"] = "Generated completion";
    QCOMPARE(m.upsertMonitoringEvent(mission, e), entryID);
    QCOMPARE(m.selectedID(), other);
    QVERIFY(m.selected()["brief"].toObject()["monitoringInbox"].toArray().isEmpty());
    m.select(mission);
    const auto inbox = m.selected()["brief"].toObject()["monitoringInbox"].toArray();
    QCOMPARE(inbox.size(), 1);
    QCOMPARE(inbox[0].toObject()["reviewState"].toString(), QString("Investigating"));
    QCOMPARE(inbox[0].toObject()["rationale"].toString(), QString("Keep rationale"));
    auto invalid = e;
    invalid["key"] = "invalid";
    const auto before = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.upsertMonitoringEvent(mission, invalid), std::runtime_error);
    QCOMPARE(m.plans(), before);
  }
  void restartRecoveryRetainsBlockedAndStaleResultsWithoutRequeue() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Generated"), id = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto evidence = m.linkDossierEvidence(mission, id, radar());
    m.setDossierBaseline(mission, id, evidence, true);
    m.configureMonitoring(mission, id, profile(evidence));
    m.save(root.filePath("plans.json"));
    auto e = entry(id, evidence);
    e["signature"] = monitoringSignature(area(m, id));
    e["status"] = "Queued";
    m.upsertMonitoringEvent(mission, e);
    ScientificQueue q(root.filePath("jobs"));
    int submissions = 0;
    MonitoringRunner runner(
        root.filePath("jobs"), m, q, [] { return QVector<ProductLayer>{}; },
        [&](WorkerRequest, const QString &) {
          ++submissions;
          return false;
        });
    auto status = [&] {
      return m.selected()["brief"]
          .toObject()["monitoringInbox"]
          .toArray()[0]
          .toObject()["status"]
          .toString();
    };
    runner.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Interrupted"), 3000);
    const auto job = newID().toLower(), directory = root.filePath("jobs/" + job);
    QVERIFY(QDir().mkpath(directory + "/product"));
    auto write = [](const QString &path, const QJsonObject &object) {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly))
        return false;
      return f.write(QJsonDocument(object).toJson()) > 0;
    };
    QVERIFY(write(directory + "/run.json", {{"version", 1}, {"id", job}, {"state", "completed"}}));
    QVERIFY(write(directory + "/product/result.json",
                  {{"analysisStatus", "blocked"},
                   {"quality", QJsonObject{{"version", 1},
                                           {"state", "blocked"},
                                           {"reason", "Generated unavailable controls"},
                                           {"method", "Generated recovery fixture"},
                                           {"controls", QJsonArray{}},
                                           {"absoluteGeolocation", "unknown"},
                                           {"absoluteRadiometry", "unknown"}}}}));
    e["jobID"] = job;
    e["status"] = "Running";
    m.upsertMonitoringEvent(mission, e);
    QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Blocked"), 3000);
    QCOMPARE(submissions, 0);
    auto p = profile(evidence);
    p["thresholdDb"] = 4;
    m.configureMonitoring(mission, id, p);
    e["status"] = "Running";
    m.upsertMonitoringEvent(mission, e);
    QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Stale"), 3000);
    QCOMPARE(submissions, 0);
    runner.stop();
  }
  void controlsAreIndependentAndRegistrationSettingsAreBounded() {
    auto p = profile("11111111-1111-4111-8111-111111111111");
    p.remove("baselineEvidenceID");
    validateMonitoring(p, {}, {}, {});
    auto c = p["controls"].toArray();
    auto v = c[3].toObject();
    v["region"] = c[0].toObject()["region"];
    c[3] = v;
    auto invalid = p;
    invalid["controls"] = c;
    QVERIFY_EXCEPTION_THROWN(validateMonitoring(invalid, {}, {}, {}), std::runtime_error);
    invalid = p;
    invalid["maximumResidualPixels"] = 0;
    QVERIFY_EXCEPTION_THROWN(validateMonitoring(invalid, {}, {}, {}), std::runtime_error);
  }
  void scientificQueuePrioritizesManualWorkAndBoundsPendingJobs() {
    QTemporaryDir root;
    QFile input(root.filePath("input"));
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write("sleep");
    input.close();
    QFile quick(root.filePath("quick"));
    QVERIFY(quick.open(QIODevice::WriteOnly));
    quick.write("complete");
    quick.close();
    WorkerRequest r;
    r.python = QStringLiteral(HELIOS_TEST_PYTHON);
    r.worker = QStringLiteral(HELIOS_TEST_WORKER);
    r.host = QStringLiteral(HELIOS_TEST_HOST);
    r.operation = "render";
    r.optical = true;
    r.gpt = r.python;
    r.input = input.fileName();
    ScientificQueue q(root.filePath("jobs"));
    QSignalSpy started(&q, &ScientificQueue::started), finished(&q, &ScientificQueue::finished);
    QVERIFY(q.submit(r, "automatic", true));
    QTRY_VERIFY(q.activeJob() && q.activeJob()->running());
    r.input = quick.fileName();
    QVERIFY(q.submit(r, "manual", false));
    QTRY_VERIFY_WITH_TIMEOUT(finished.size() >= 2, 10000);
    QCOMPARE(started.size(), 2);
    QCOMPARE(started[1][0].toString(), QString("manual"));
    QVERIFY(finished[1][2].toBool());
    r.input = input.fileName();
    QVERIFY(q.submit(r, "hold", true));
    for (int i = 0; i < 20; ++i)
      QVERIFY(q.submit(r, QString("queued%1").arg(i), true));
    QVERIFY(!q.submit(r, "overflow", true));
    QVERIFY(!q.submit(r, "hold", true));
    QCOMPARE(q.pendingCount(), 20);
    q.cancelAll();
    QTRY_VERIFY_WITH_TIMEOUT(!q.busy(), 10000);
  }
};
QTEST_GUILESS_MAIN(MonitoringTests)
#include "MonitoringTests.moc"
