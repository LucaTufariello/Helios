#include "Core/Monitors.h"
#include "Core/Timeline.h"
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace helios;
class MissionOperationsTests : public QObject {
  Q_OBJECT
  QJsonObject monitor(const Missions &m, const QString &id) {
    for (const auto value : m.selected()["brief"].toObject()["monitors"].toArray())
      if (value.toObject()["id"] == id)
        return value.toObject();
    return {};
  }
private slots:
  void timelineUnknownTimesFiltersAndSpreadsheetSafeExports() {
    Missions m;
    m.create("Generated");
    m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto area = m.selected()["brief"].toObject()["areas"].toArray()[0].toObject();
    m.addObservation("Undated object", "Building", {45.05, 9.05});
    m.addNote("  =HYPERLINK(\"bad\")", "Body", "@citation");
    auto entries = timelineEntries(m.selected(), {});
    QCOMPARE(entries.size(), 2);
    QVERIFY(!entries[0].time.isValid());
    QVERIFY(entries[1].time.isValid());
    TimelineFilter filter;
    filter.includeUndated = false;
    QCOMPARE(sortedTimeline(entries, filter).size(), 1);
    filter.includeUndated = true;
    filter.area = area;
    QCOMPARE(sortedTimeline(entries, filter).size(), 1);
    const auto csv = timelineCSV(entries);
    QVERIFY(csv.contains("\"'=HYPERLINK(\"\"bad\"\")\""));
    auto untrimmed = entries.first();
    untrimmed.title = " \t@formula";
    QVERIFY(timelineCSV({untrimmed}).contains("\"' \t@formula\""));
    QVERIFY(csv.contains("Note authored"));
    QCOMPARE(missionTime(0).toString(Qt::ISODate), QString("2001-01-01T00:00:00Z"));
    const auto assessment =
        assessTimeline(entries, area, QDateTime::currentDateTimeUtc().addDays(-30),
                       QDateTime::currentDateTimeUtc());
    QCOMPARE(assessment["opticalCount"].toInt(), 0);
    QVERIFY(assessment["gaps"].toArray().size() >= 3);
  }
  void duplicateAcquisitionsDoNotBecomeNewObservations() {
    TimelineEntry a, b;
    a.id = "A";
    b.id = "B";
    a.kind = b.kind = "Radar acquisition";
    a.available = b.available = true;
    a.rasterID = "radar:one:VV";
    b.rasterID = "radar:two:VV";
    a.sourceID = b.sourceID = "same-source";
    a.source = b.source = "S1A";
    a.time = missionTime("2026-09-01T00:00:00Z");
    b.time = missionTime("2026-09-13T00:00:00Z");
    a.bounds = b.bounds = Bounds{9, 45, 9.1, 45.1};
    QVERIFY(!timelineComparisonIssue(a, b).isEmpty());
    b.sourceID = "later-source";
    QVERIFY(timelineComparisonIssue(a, b).isEmpty());
    b.available = false;
    QVERIFY(!timelineComparisonIssue(a, b).isEmpty());
    b.available = true;
    b.bounds = Bounds{10, 45, 10.1, 45.1};
    QVERIFY(!timelineComparisonIssue(a, b).isEmpty());
    b.bounds = a.bounds;
    b.time = {};
    QVERIFY(!timelineComparisonIssue(a, b).isEmpty());
  }
  void editingAndDeletionRetainSourceDataAndLastGoodBackup() {
    QTemporaryDir root;
    Missions m;
    const auto id = m.create("Generated");
    m.addTask("Task");
    m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto task =
        m.selected()["brief"].toObject()["tasks"].toArray()[0].toObject()["id"].toString();
    m.editEntry("tasks", task, {{"title", "Updated"}, {"due", 1000000.0}});
    const auto path = root.filePath("plans.json");
    m.save(path);
    const auto previous = m.plans();
    m.editEntry("tasks", task, {{"due", QJsonValue::Null}, {"done", true}});
    m.save(path);
    QFile backup(path + ".backup");
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(backup.readAll()).array(), previous);
    const auto before = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.editEntry("tasks", task, {{"modelScore", .9}}), std::runtime_error);
    QCOMPARE(m.plans(), before);
    QFile source(root.filePath("source.bin"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("ORIGINAL");
    source.close();
    m.removeMission(id);
    m.save(path);
    QVERIFY(m.plans().isEmpty());
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), QByteArray("ORIGINAL"));
  }
  void localProposalRequiresLinkedSourceAndAtomicSave() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Generated model review");
    const auto evidence = newID();
    m.addEvidence({{"id", evidence},
                   {"kind", "optical"},
                   {"referenceID", newID()},
                   {"title", "Generated source"},
                   {"savedAt", foundationNow()},
                   {"acquisition", QJsonObject{{"sourceID", "GENERATED"},
                                               {"sensor", "QA"},
                                               {"bounds", Bounds{10, 20, 12, 24}.json()}}}});
    const auto file = root.filePath("missions.json");
    m.save(file);
    QJsonObject proposal{{"id", newID()},
                         {"title", "Generated proposal"},
                         {"category", "QA"},
                         {"note", ""},
                         {"method", "Generated QA"},
                         {"coordinate", Coordinate{22, 11}.json()},
                         {"bounds", Bounds{10.5, 21, 11.5, 23}.json()},
                         {"imageBox", QJsonArray{.25, .25, .75, .75}},
                         {"imagePixelBox", QJsonArray{50, 50, 150, 150}},
                         {"createdAt", foundationNow()},
                         {"state", "Unreviewed"},
                         {"evidenceID", evidence},
                         {"inputSHA256", QString(64, 'a')},
                         {"modelSHA256", QString(64, 'b')},
                         {"modelContractSHA256", QString(64, 'c')},
                         {"modelBackend", "onnx-cpu"},
                         {"futureModelExtension", QJsonObject{{"preserve", true}}}};
    const auto original = m.plans();
    for (const auto key : {"inputSHA256", "modelSHA256", "modelContractSHA256", "modelBackend",
                           "imageBox", "imagePixelBox"}) {
      auto incomplete = proposal;
      incomplete.remove(key);
      QVERIFY_EXCEPTION_THROWN(m.addObservationProposal(mission, incomplete), std::runtime_error);
      QCOMPARE(m.plans(), original);
      QVERIFY(!m.dirty());
    }
    const QList<QPair<QString, QJsonValue>> invalidFields{
        {"imageBox", QJsonArray{0, 0, 1.1, 1}},
        {"imagePixelBox", QJsonArray{0, 0, 2049, 200}},
        {"imagePixelBox", QJsonArray{100, 0, 50, 100}},
        {"modelBackend", "onnx-gpu"},
        {"modelSHA256", QString(64, 'B')},
        {"modelContractSHA256", "invalid"}};
    for (const auto &field : invalidFields) {
      auto invalid = proposal;
      invalid[field.first] = field.second;
      QVERIFY_EXCEPTION_THROWN(m.addObservationProposal(mission, invalid), std::runtime_error);
      QCOMPARE(m.plans(), original);
      QVERIFY(!m.dirty());
    }
    m.addObservationProposal(mission, proposal);
    QVERIFY(!m.dirty());
    Missions reopened;
    reopened.load(file);
    QCOMPARE(reopened.selected()["brief"].toObject()["observations"].toArray().size(), 1);
    QCOMPARE(reopened.selected()["brief"].toObject()["observations"].toArray().first(),
             QJsonValue(proposal));
    const auto before = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.addObservationProposal(mission, proposal), std::runtime_error);
    QCOMPARE(m.plans(), before);
    proposal["id"] = newID();
    proposal["bounds"] = Bounds{9, 20, 12, 24}.json();
    QVERIFY_EXCEPTION_THROWN(m.addObservationProposal(mission, proposal), std::runtime_error);
    QCOMPARE(m.plans(), before);
    proposal["bounds"] = Bounds{10.5, 21, 11.5, 23}.json();
    QFile external(file);
    QVERIFY(external.open(QIODevice::Append));
    external.write("\n");
    external.close();
    QVERIFY_EXCEPTION_THROWN(m.addObservationProposal(mission, proposal), std::runtime_error);
    QCOMPARE(m.plans(), before);
    QVERIFY(!m.dirty());
    m.removeEntry("items", evidence);
    const auto unlinked = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.addObservationProposal(mission, proposal), std::runtime_error);
    QCOMPARE(m.plans(), unlinked);
  }
  void monitorBaselineDeduplicationBoundsAndDueTime() {
    const auto now = QDateTime::currentDateTimeUtc();
    QJsonObject m{{"kind", "New Sentinel-1 acquisitions"},
                  {"enabled", true},
                  {"intervalHours", 24},
                  {"seenIDs", QJsonArray{}}};
    QVERIFY(monitorDue(m, now));
    int count = -1;
    m = receiveMonitor(
        m, {"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"}, now,
        true, &count);
    QCOMPARE(count, 0);
    QCOMPARE(m["seenIDs"].toArray().size(), 1);
    QVERIFY(m["summary"].toString().contains("Baseline"));
    QVERIFY(!monitorDue(m, now.addSecs(23 * 3600)));
    QVERIFY(monitorDue(m, now.addDays(1)));
    m = receiveMonitor(m, {"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "new-id"}, now.addDays(1), false,
                       &count);
    QCOMPARE(count, 1);
    QStringList many;
    for (int i = 0; i < 2500; ++i)
      many.append(QString::number(i));
    m = receiveMonitor(m, many, now.addDays(2), false);
    QCOMPARE(m["seenIDs"].toArray().size(), 2000);
    QVERIFY_EXCEPTION_THROWN(receiveMonitor(m, {QString(201, 'x')}, now, false),
                             std::runtime_error);
  }
  void availabilityCheckCompletesAndPersists() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Generated availability check");
    m.addView("Generated view", Camera{});
    const auto id = m.addMonitor({{"kind", "Evidence availability"}});
    const auto file = root.filePath("plans.json");
    m.save(file);
    MonitorRunner runner(m, [] { return QVector<ProductLayer>{}; });
    QSignalSpy failures(&runner, &MonitorRunner::failed);
    QVERIFY(runner.run(mission, id));
    QTRY_VERIFY_WITH_TIMEOUT(!runner.running(mission, id), 3000);
    QVERIFY2(failures.isEmpty(),
             failures.isEmpty() ? "" : qPrintable(failures.first().first().toString()));
    QVERIFY(monitor(m, id).contains("lastSuccess"));
    QVERIFY(monitor(m, id)["summary"].toString().contains("1/1 linked items"));
    m.load(file);
    QVERIFY(monitor(m, id).contains("lastSuccess"));
  }
  void offlineMonitorsDoNotRequestOrRewriteCheckpoint() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Offline monitor checks");
    m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto area =
        m.selected()["brief"].toObject()["areas"].toArray()[0].toObject()["id"].toString();
    const auto remote =
        m.addMonitor({{"kind", "New Sentinel-2 imagery"}, {"areaID", area}, {"enabled", true}});
    const auto local = m.addMonitor({{"kind", "Evidence availability"}});
    m.save(root.filePath("plans.json"));
    const auto original = m.plans();
    QVector<MonitorRunner::Completion> completions;
    MonitorRunner runner(
        m, [] { return QVector<ProductLayer>{}; }, nullptr,
        [&](const QJsonObject &, Bounds, MonitorRunner::Completion done, QObject *) {
          completions.append(done);
        });
    QSignalSpy failures(&runner, &MonitorRunner::failed);
    runner.setNetworking(false);
    runner.tick();
    QVERIFY(!runner.run(mission, remote));
    QCOMPARE(completions.size(), 0);
    QCOMPARE(runner.activeCount(), 0);
    QCOMPARE(m.plans(), original);
    QVERIFY(!monitor(m, remote).contains("lastAttempt"));
    QCOMPARE(failures.size(), 1);
    QVERIFY(failures.first().first().toString().contains("Offline mode"));
    QVERIFY(runner.run(mission, local));
    QTRY_VERIFY_WITH_TIMEOUT(!runner.running(mission, local), 3000);
    QVERIFY(monitor(m, local).contains("lastSuccess"));
    runner.setNetworking(true);
    QVERIFY(runner.run(mission, remote));
    QCOMPARE(completions.size(), 1);
    runner.setNetworking(false);
    const auto cancelled = m.plans();
    QCOMPARE(runner.activeCount(), 0);
    completions.first()({{"cancelled-source"}, false, {}, {}});
    QCOMPARE(m.plans(), cancelled);
    QVERIFY(!monitor(m, remote).contains("lastSuccess"));
  }
  void runnerLimitsPausePersistenceAndStaleGeometry() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Generated");
    m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto area =
        m.selected()["brief"].toObject()["areas"].toArray()[0].toObject()["id"].toString();
    QStringList ids;
    for (int i = 0; i < 3; ++i)
      ids.append(
          m.addMonitor({{"kind", "New Sentinel-2 imagery"}, {"areaID", area}, {"enabled", true}}));
    m.save(root.filePath("plans.json"));
    QVector<MonitorRunner::Completion> completions;
    MonitorRunner runner(
        m, [] { return QVector<ProductLayer>{}; }, nullptr,
        [&](const QJsonObject &, Bounds, MonitorRunner::Completion done, QObject *) {
          completions.append(done);
        });
    runner.tick();
    QCOMPARE(runner.activeCount(), 2);
    QCOMPARE(completions.size(), 2);
    completions[0]({{"source-one"}, false, {}, {}});
    QCOMPARE(runner.activeCount(), 1);
    QVERIFY(monitor(m, ids[0])["summary"].toString().contains("Baseline"));
    QVERIFY(runner.run(mission, ids[0]));
    m.editEntry("monitors", ids[0], {{"enabled", false}});
    completions[2]({{"source-one", "source-two"}, false, {}, {}});
    QVERIFY(!monitor(m, ids[0])["enabled"].toBool());
    QVERIFY(monitor(m, ids[0])["summary"].toString().contains("1 newly seen"));
    m.editEntry("areas", area, {{"region", Bounds{9, 45, 9.2, 45.2}.json()}});
    completions[1]({{"old-geometry"}, false, {}, {}});
    QVERIFY(monitor(m, ids[1])["seenIDs"].toArray().isEmpty());
    QVERIFY(!monitor(m, ids[1]).contains("lastSuccess"));
    runner.tick();
    QCOMPARE(runner.activeCount(), 2);
    runner.stop();
    const auto saved = m.plans();
    completions.last()({{"cancelled"}, false, {}, {}});
    QCOMPARE(m.plans(), saved);
    QCOMPARE(runner.activeCount(), 0);
  }
};
QTEST_GUILESS_MAIN(MissionOperationsTests)
#include "MissionOperationsTests.moc"
