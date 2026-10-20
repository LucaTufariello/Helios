#include "Core/Briefing.h"
#include "Core/Missions.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace godeye;
class InvestigationTests : public QObject {
  Q_OBJECT
  static Asset asset(QString name = "Generated civilian terminal") {
    Asset a;
    a.id = newID();
    a.name = name;
    a.category = "Civilian airport";
    a.boundary = Bounds{9, 45, 9.1, 45.1};
    a.aliases = {"Terminal QA"};
    return a;
  }
  static QJsonObject evidence() {
    return {{"id", newID()},
            {"kind", "optical"},
            {"referenceID", newID()},
            {"title", "Generated evaluation source - missing raster"},
            {"savedAt", foundationNow()},
            {"acquisition", QJsonObject{{"sensor", "Generated public-source fixture"},
                                        {"sourceID", "CIVIL-QA-20261020"},
                                        {"bounds", Bounds{9, 45, 9.1, 45.1}.json()},
                                        {"acquiredAt", foundationNow()}}}};
  }
  static QJsonObject citation(QString label = "Generated analyst citation") {
    QJsonObject origin;
    origin["label"] = label;
    origin["sourceID"] = "evaluation-document:1";
    origin["kind"] = "Evaluation fixture";
    auto c = citationForOrigin(origin);
    c["reference"] = "https://example.invalid/evaluation-only";
    return c;
  }
  static Finding finding(QString sid, QString cid) {
    Finding f;
    f.claim = "Generated evaluation: terminal boundary is documented";
    f.assetSnapshotIDs = {sid};
    f.citations = {QJsonObject{{"citationID", cid}, {"role", "Supporting"}}};
    f.rationale = "Analyst evaluation only; the cited fixture records the boundary and does not "
                  "demonstrate an operational change.";
    f.state = "Supported";
    return f;
  }
private slots:
  void portableFixturesRemainValid() {
    Missions m;
    m.load(QString(GODEYE_TEST_FIXTURE_DIR) + "/cross-platform-investigation.json");
    QCOMPARE(investigation(m.selected())["snapshots"].toArray().size(), 3);
    QCOMPARE(investigation(m.selected())["findings"]
                 .toArray()[0]
                 .toObject()["revisions"]
                 .toArray()
                 .size(),
             1);
    AssetRegistry registry(QString(GODEYE_TEST_FIXTURE_DIR) + "/cross-platform-registry.json");
    registry.load();
    QCOMPARE(registry.assets().size(), 2);
    QCOMPARE(registry.relationships().size(), 1);
  }
  void legacyAndInvalidExtension() {
    Missions m;
    m.create("Legacy");
    const auto a = m.addArea("Legacy area", "", {9, 45, 9.1, 45.1});
    Q_UNUSED(a);
    QVERIFY(!m.selected()["brief"].toObject().contains("investigation"));
    auto p = m.selected(), b = p["brief"].toObject();
    b["investigation"] = QJsonObject{{"version", 2}};
    p["brief"] = b;
    QVERIFY_EXCEPTION_THROWN(validateMissions({p}), std::runtime_error);
  }
  void registryConflictBackupAndArchive() {
    QTemporaryDir dir;
    const auto path = dir.filePath("assets.json");
    AssetRegistry one(path), two(path);
    one.load();
    two.load();
    auto a = asset();
    one.saveAsset(a, 0);
    QVERIFY_EXCEPTION_THROWN(two.saveAsset(asset("Other"), 0), std::runtime_error);
    two.load();
    const auto original = one.asset(a.id);
    a.name = "Revised terminal";
    one.saveAsset(a, 1);
    QVERIFY(QFile::exists(path + ".backup"));
    QVERIFY_EXCEPTION_THROWN(one.saveAsset(a, 1), std::runtime_error);
    one.archive(a.id, false, 2);
    QVERIFY(one.asset(a.id)["archived"].toBool());
    QCOMPARE(one.asset(a.id)["revision"].toInt(), 3);
    QFile corrupt(dir.filePath("corrupt.json"));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("broken");
    corrupt.close();
    AssetRegistry bad(corrupt.fileName());
    QVERIFY_EXCEPTION_THROWN(bad.load(), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(bad.saveAsset(a, 0), std::runtime_error);
    Q_UNUSED(original);
  }
  void twoMissionsKeepVersionsAndPortableImport() {
    QTemporaryDir dir;
    AssetRegistry registry(dir.filePath("registry.json"));
    registry.load();
    auto a = asset();
    registry.saveAsset(a, 0);
    Missions m;
    const auto first = m.create("First case");
    const auto area = m.addArea("Terminal", "", {9, 45, 9.1, 45.1});
    const auto sid = m.adoptAsset(first, a, area);
    const QJsonArray prior = investigation(m.selected())["snapshots"].toArray();
    const auto second = m.create("Second case");
    m.adoptAsset(second, a);
    a.name = "Terminal amended";
    registry.saveAsset(a, 1);
    m.select(first);
    QCOMPARE(investigation(m.selected())["snapshots"].toArray(), prior);
    const auto adopted = m.adoptAsset(first, Asset::read(registry.asset(a.id)), area);
    QVERIFY(adopted != sid);
    QCOMPARE(investigation(m.selected())["snapshots"].toArray().size(), 2);
    m.select(second);
    QCOMPARE(investigation(m.selected())["snapshots"]
                 .toArray()[0]
                 .toObject()["asset"]
                 .toObject()["name"]
                 .toString(),
             QString("Generated civilian terminal"));
    m.save(dir.filePath("missions.json"));
    Missions portable;
    portable.load(dir.filePath("missions.json"));
    QCOMPARE(portable.plans(), m.plans());
    AssetRegistry destination(dir.filePath("other/registry.json"));
    destination.load();
    const auto preview = destination.importPreview(portable.selected());
    QCOMPARE(preview.size(), 1);
    destination.importSnapshots(portable.selected(), preview);
    QVERIFY_EXCEPTION_THROWN(destination.importSnapshots(portable.selected(), preview),
                             std::runtime_error);
    auto conflict = Asset::read(destination.asset(a.id));
    conflict.name = "Destination version";
    destination.saveAsset(conflict, conflict.revision);
    const auto conflicts = destination.importPreview(portable.selected());
    QVERIFY(conflicts[0].toObject()["action"] != "Identical");
    destination.importSnapshots(portable.selected(), conflicts);
    QCOMPARE(destination.asset(a.id)["name"], preview[0].toObject()["incoming"].toObject()["name"]);
    m.select(first);
    Finding areaFinding;
    areaFinding.claim = "Retain the original area";
    areaFinding.areaIDs = {area};
    m.saveFinding(first, areaFinding, 0);
    m.removeEntry("areas", area);
    QCOMPARE(investigation(m.selected())["snapshots"].toArray().size(), 2);
    QCOMPARE(investigation(m.selected())["findings"]
                 .toArray()[0]
                 .toObject()["areaSnapshots"]
                 .toArray()[0]
                 .toObject()["name"]
                 .toString(),
             QString("Terminal"));
  }
  void relationshipsNeedExplicitEvidence() {
    QTemporaryDir dir;
    AssetRegistry registry(dir.filePath("registry.json"));
    registry.load();
    auto a = asset(), b = asset("Generated civilian power supply");
    registry.saveAsset(a, 0);
    registry.saveAsset(b, 0);
    AssetRelationship r;
    r.id = newID();
    r.fromAssetID = a.id;
    r.toAssetID = b.id;
    r.kind = "Depends on";
    QVERIFY_EXCEPTION_THROWN(registry.saveRelationship(r, 0), std::runtime_error);
    r.citations = {citation()};
    r.rationale = "Explicit supply diagram in evaluation fixture";
    r.reviewState = "Supported";
    registry.saveRelationship(r, 0);
    Missions m;
    const auto mid = m.create("Relationships");
    m.adoptAsset(mid, a);
    QVERIFY_EXCEPTION_THROWN(m.adoptRelationship(mid, r), std::runtime_error);
    m.adoptAsset(mid, b);
    m.adoptRelationship(mid, r);
    const auto old = investigation(m.selected());
    registry.archive(r.id, true, 1);
    QCOMPARE(investigation(m.selected()), old);
  }
  void reviewedSuggestionsAreContextualAndStaleSafe() {
    Missions m;
    const auto mid = m.create("Suggestions");
    const auto a = asset();
    m.adoptAsset(mid, a);
    const auto e = evidence();
    m.addEvidence(e);
    const auto before = m.plans();
    const auto suggestions = suggestAssociations(m.selected());
    QCOMPARE(suggestions.size(), 1);
    QVERIFY(suggestions[0].reason.contains("overlap"));
    QCOMPARE(m.plans(), before);
    m.approveAssociation(mid, suggestions[0]);
    QVERIFY(suggestAssociations(m.selected()).isEmpty());
    QCOMPARE(investigation(m.selected())["associations"].toArray().size(), 1);
    QVERIFY_EXCEPTION_THROWN(m.approveAssociation(mid, suggestions[0]), std::runtime_error);
    QCOMPARE(investigation(m.selected())["snapshots"].toArray().size(), 1);
    m.removeEntry("items", e["id"].toString());
    QCOMPARE(investigation(m.selected())["citations"].toArray().size(), 1);
  }
  void reviewRevisionsAndAtomicCitations() {
    Missions m;
    const auto mid = m.create("Review");
    const auto sid = m.adoptAsset(mid, asset());
    auto c = citation();
    const auto cid = m.retainCitation(mid, c);
    auto f = finding(sid, cid);
    const auto fid = m.saveFinding(mid, f, 0);
    auto saved = Finding::read(investigation(m.selected())["findings"].toArray()[0].toObject());
    QCOMPARE(saved.state, QString("Supported"));
    saved.claim += " - revised";
    m.saveFinding(mid, saved, 1);
    saved = Finding::read(investigation(m.selected())["findings"].toArray()[0].toObject());
    QCOMPARE(saved.state, QString("Unreviewed"));
    QCOMPARE(saved.revisions.size(), 1);
    QCOMPARE(saved.revisions[0].toObject()["state"].toString(), QString("Supported"));
    QVERIFY_EXCEPTION_THROWN(m.saveFinding(mid, saved, 1), std::runtime_error);
    const auto before = m.plans();
    auto bad = saved;
    bad.state = "Supported";
    bad.rationale = "";
    auto extra = citation("New source");
    QVERIFY_EXCEPTION_THROWN(m.saveFinding(mid, bad, 2, {extra}), std::runtime_error);
    QCOMPARE(m.plans(), before);
    saved.state = "Uncertain";
    saved.rationale = "Conflicting source requires review";
    m.saveFinding(mid, saved, 2);
    QCOMPARE(investigation(m.selected())["findings"].toArray()[0].toObject()["id"].toString(), fid);
    auto draft = Finding::read(investigation(m.selected())["findings"].toArray()[0].toObject());
    draft.claim += " / revised again";
    draft.rationale = "A newly typed rationale must remain a draft after the claim changes";
    m.saveFinding(mid, draft, 3);
    const auto retainedDraft = investigation(m.selected())["findings"].toArray()[0].toObject();
    QCOMPARE(retainedDraft["state"].toString(), QString("Unreviewed"));
    QCOMPARE(retainedDraft["rationale"].toString(), draft.rationale);
  }
  void taskCyclesCompletionAndDeletion() {
    Missions m;
    m.create("Tasks");
    const auto a = m.addTask("Verify source"), b = m.addTask("Prepare conclusion");
    QVERIFY_EXCEPTION_THROWN(m.editEntry("tasks", a, {{"findingID", newID()}}), std::runtime_error);
    m.editEntry("tasks", b, {{"owner", "Local analyst"}, {"dependencyIDs", QJsonArray{a}}});
    const auto before = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.setTaskDone(b, true), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(m.editEntry("tasks", a, {{"dependencyIDs", QJsonArray{b}}}),
                             std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(m.removeEntry("tasks", a), std::runtime_error);
    QCOMPARE(m.plans(), before);
    m.setTaskDone(a, true);
    m.setTaskDone(b, true);
    QVERIFY_EXCEPTION_THROWN(m.setTaskDone(a, false), std::runtime_error);
  }
  void closureAndStaleContexts() {
    Missions m;
    const auto mid = m.create("Close"), task = m.addTask("Outstanding");
    Q_UNUSED(task);
    const auto before = investigation(m.selected());
    QVERIFY_EXCEPTION_THROWN(m.setInvestigationOverview(mid, before, {{"status", "Closed"}}),
                             std::runtime_error);
    m.setInvestigationOverview(
        mid, before,
        {{"status", "Closed"},
         {"closureRationale", "Evaluation ended with a documented unresolved task"}});
    QCOMPARE(investigation(m.selected())["status"].toString(), QString("Closed"));
    QVERIFY_EXCEPTION_THROWN(m.setInvestigationOverview(mid, before, {{"status", "Open"}}),
                             std::runtime_error);
    m.create("Other");
    QVERIFY_EXCEPTION_THROWN(m.adoptAsset(mid, asset()), std::runtime_error);
  }
  void sarDraftRetainsOriginalReportSiteAndRejectsChangedContext() {
    QTemporaryDir directory;
    const auto bytes = JsonFile::read(
        QString(GODEYE_TEST_FIXTURE_DIR) + "/change-review-result.json", 1024 * 1024);
    auto report = QJsonDocument::fromJson(bytes).object();
    const auto reportID = newID();
    QDir(directory.path()).mkdir(reportID);
    QFile file(directory.filePath(reportID + "/result.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    Missions m;
    const auto mid = m.create("SAR candidate investigation");
    const auto candidate = report["candidates"].toArray()[0].toObject();
    QJsonObject origin{{"kind", "SAR candidate"},
                       {"sourceID", reportID + ":" + candidate["id"].toString()},
                       {"reportID", reportID},
                       {"label", "Generated SAR candidate"},
                       {"manifest", file.fileName()},
                       {"record", candidate},
                       {"site", report["site"]},
                       {"quality", report["quality"]},
                       {"evidence", report["evidence"]},
                       {"limitations", report["limitations"]},
                       {"qualityLimitations", "SAR candidate; calibrated processing does not by "
                                              "itself establish the cause of a change."}};
    m.draftFinding(mid, origin);
    const auto retained =
        investigation(m.selected())["findings"].toArray()[0].toObject()["origin"].toObject();
    QCOMPARE(retained["site"], report["site"]);
    QCOMPARE(retained["fingerprint"].toString().size(), 64);
    auto site = report["site"].toObject();
    site["name"] = "Changed report site";
    report["site"] = site;
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(report).toJson());
    file.close();
    QVERIFY_EXCEPTION_THROWN(m.draftFinding(mid, origin), std::runtime_error);
    QCOMPARE(investigation(m.selected())["findings"].toArray().size(), 1);
  }
  void monitoringDraftPreservesEntryAndRejectsStaleReview() {
    Missions m;
    m.load(QString(GODEYE_TEST_FIXTURE_DIR) + "/cross-platform-monitoring.json");
    const auto mid = m.selectedID();
    const auto entry = m.selected()["brief"].toObject()["monitoringInbox"].toArray()[0].toObject();
    QJsonObject origin{
        {"kind", "Monitoring entry"},
        {"sourceID", entry["id"]},
        {"label", "Generated monitoring entry"},
        {"record", entry},
        {"qualityLimitations", "Monitoring entry; analysis requires analyst review."}};
    m.draftFinding(mid, origin);
    QCOMPARE(investigation(m.selected())["findings"]
                 .toArray()[0]
                 .toObject()["origin"]
                 .toObject()["record"]
                 .toObject(),
             entry);
    m.reviewMonitoringEvent(entry["id"].toString(), "Investigating",
                            "Entry reviewed after draft capture");
    QVERIFY_EXCEPTION_THROWN(m.draftFinding(mid, origin), std::runtime_error);
  }
  void sourceDraftPreservesIdentityAndLimitations() {
    Missions m;
    const auto mid = m.create("Draft");
    const auto oid = m.addObservation("Evaluation observation", "Infrastructure", {45.05, 9.05});
    const auto o = m.selected()["brief"].toObject()["observations"].toArray()[0].toObject();
    QJsonObject origin{{"kind", "Observation"},
                       {"sourceID", oid},
                       {"label", o["title"]},
                       {"record", o},
                       {"qualityLimitations", "Generated observation; no measured change"}};
    m.draftFinding(mid, origin);
    const auto f = investigation(m.selected())["findings"].toArray()[0].toObject();
    QCOMPARE(f["origin"].toObject(), origin);
    QCOMPARE(f["state"].toString(), QString("Unreviewed"));
    m.editEntry("observations", oid, {{"title", "Updated"}});
    QVERIFY_EXCEPTION_THROWN(m.draftFinding(mid, origin), std::runtime_error);
    m.removeEntry("observations", oid);
    QCOMPARE(investigation(m.selected())["findings"].toArray()[0].toObject()["origin"].toObject(),
             origin);
  }
  void boundedGraphResponsiveness() {
    QJsonArray assets, relationships;
    auto a = asset();
    assets.append(a.json());
    for (int n = 0; n < 300; ++n) {
      auto other = asset(QString("Node %1").arg(n));
      assets.append(other.json());
      AssetRelationship r;
      r.id = newID();
      r.kind = "Connected to";
      r.fromAssetID = a.id;
      r.toAssetID = other.id;
      relationships.append(r.json());
    }
    QElapsedTimer timer;
    timer.start();
    const auto graph = assetNeighborhood(assets, relationships, a.id, 10);
    QCOMPARE(graph.nodes.size(), 100);
    QVERIFY(graph.edges.size() <= 200);
    QVERIFY(graph.truncated);
    QVERIFY(timer.elapsed() < 1000);
    QJsonArray dense = relationships;
    for (int n = 0; n < 300; ++n) {
      AssetRelationship r;
      r.id = newID();
      r.kind = "Connected to";
      r.fromAssetID = assets[1 + n % 20].toObject()["id"].toString();
      r.toAssetID = assets[1 + (n + 1) % 20].toObject()["id"].toString();
      dense.append(r.json());
    }
    const auto capped = assetNeighborhood(assets, dense, a.id, 10);
    QCOMPARE(capped.edges.size(), 200);
    QVERIFY(capped.nodes.size() <= 100);
    QVERIFY(capped.truncated);
  }
  void frozenPreviewsSurviveSourceDeletionAndFlagChangedProducts() {
    QTemporaryDir temp;
    const auto configured = qEnvironmentVariable("GODEYE_INVESTIGATION_QA");
    const auto target = configured.isEmpty() ? temp.path() : configured;
    const auto directory = QDir(target).filePath(newID());
    QDir().mkpath(directory);
    QImage image(64, 256, QImage::Format_RGB32);
    image.fill(QColor("#286a80"));
    QVERIFY(image.save(directory + "/preview.png"));
    QJsonObject product{{"version", 1},
                        {"state", "completed"},
                        {"crs", "EPSG:4326"},
                        {"mode", "rgb"},
                        {"width", 64},
                        {"height", 256},
                        {"preview", "preview.png"},
                        {"bounds", Bounds{9, 45, 9.1, 45.1}.json()},
                        {"scene", QJsonObject{{"id", "GENERATED-PREVIEW"},
                                              {"platform", "Generated preview fixture"},
                                              {"acquiredAt", "2026-10-19T12:00:00Z"}}}};
    QFile file(directory + "/result.json");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(product).toJson());
    file.close();
    auto library = readProducts(file.fileName(), false);
    Missions m;
    const auto mid = m.create("Generated preview and map capture test"),
               sid = m.adoptAsset(mid, asset());
    const auto e = library[0].missionEvidence();
    m.addEvidence(e);
    const auto cid = m.retainCitation(mid, citationForEvidence(e));
    const auto fid = m.saveFinding(mid, finding(sid, cid), 0);
    QImage map(1200, 700, QImage::Format_RGB32);
    map.fill(QColor("#142e40"));
    const auto frozen = prepareBriefing(
        m.selected(), {fid}, library, map,
        {{"capturedAt", foundationNow()},
         {"attribution", "Generated evaluation bitmap; no real map or acquisition"}});
    QCOMPARE(frozen.images.size(), 2);
    QVERIFY(frozen.manifest["evidenceAvailability"]
                .toArray()[0]
                .toObject()["availability"]
                .toString()
                .contains("Preview available"));
    product["evaluationMutation"] = true;
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(product).toJson());
    file.close();
    const auto changed = prepareBriefing(m.selected(), {fid}, library);
    QVERIFY(changed.manifest["evidenceAvailability"]
                .toArray()[0]
                .toObject()["availability"]
                .toString()
                .contains("differs"));
    QVERIFY(QFile::remove(directory + "/preview.png"));
    const auto output = exportBriefing(frozen, target);
    QVERIFY(QFile::exists(output + "/briefing.pdf"));
    QCOMPARE(frozen.images.size(), 2);
    if (!configured.isEmpty()) {
      QFile path(QDir(target).filePath("preview-export-path.txt"));
      QVERIFY(path.open(QIODevice::WriteOnly));
      path.write(output.toUtf8());
    }
  }
  void nativeBriefingMissingEvidenceLongTextAndConflicts() {
    QTemporaryDir temp;
    const auto configured = qEnvironmentVariable("GODEYE_INVESTIGATION_QA");
    const auto target = configured.isEmpty() ? temp.path() : configured;
    QDir().mkpath(target);
    Missions m;
    const auto mid = m.create("GENERATED EVALUATION - civilian airport investigation");
    m.setField("objective", "Evaluate the connected investigation workflow using synthetic "
                            "civilian terminal evidence. This is not an operational assessment.");
    const auto a = asset();
    const auto sid = m.adoptAsset(mid, a);
    auto e = evidence();
    m.addEvidence(e);
    m.approveAssociation(mid, suggestAssociations(m.selected())[0]);
    const auto cid =
        investigation(m.selected())["citations"].toArray()[0].toObject()["id"].toString();
    const auto opposing = citation("Conflicting evaluation source");
    const auto other = m.retainCitation(mid, opposing);
    auto f = finding(sid, cid);
    f.citations.append(QJsonObject{{"citationID", other}, {"role", "Contradicting"}});
    f.alternatives = QString("An alternate interpretation remains possible. The fixture does not "
                             "establish real construction or activity. ")
                         .repeated(90);
    f.claim = "The generated civilian terminal boundary has conflicting source descriptions.";
    const auto fid = m.saveFinding(mid, f, 0);
    const auto prerequisite = m.addFindingTask(mid, fid, "Verify the conflicting source"),
               dependent = m.addFindingTask(mid, fid, "Prepare updated interpretation");
    m.editEntry("tasks", dependent,
                {{"dependencyIDs", QJsonArray{prerequisite}}, {"owner", "Evaluation analyst"}});
    auto reviewed = Finding::read(investigation(m.selected())["findings"].toArray()[0].toObject());
    reviewed.rationale += " Review reaffirmed with the conflicting evidence retained.";
    m.saveFinding(mid, reviewed, 1);
    m.save(QDir(target).filePath("civilian-case.json"));
    const auto before = m.plans();
    const auto frozen = prepareBriefing(m.selected(), {fid});
    validateBriefingManifest(frozen.manifest);
    QVERIFY(frozen.html.contains("Contradicting"));
    QVERIFY(frozen.html.contains("UNAVAILABLE"));
    const auto exported = exportBriefing(frozen, target);
    QVERIFY(QFile::exists(exported + "/briefing.pdf"));
    const auto manifest = QJsonDocument::fromJson(
                              JsonFile::read(exported + "/evidence-manifest.json", 8 * 1024 * 1024))
                              .object();
    validateBriefingManifest(manifest);
    QVERIFY(manifest.contains("pdfSHA256"));
    QCOMPARE(m.plans(), before);
    auto bad = manifest;
    bad["selectedFindingIDs"] = QJsonArray{};
    QVERIFY_EXCEPTION_THROWN(validateBriefingManifest(bad), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(prepareBriefing(m.selected(), {}), std::runtime_error);
    if (!configured.isEmpty()) {
      QFile path(QDir(target).filePath("export-path.txt"));
      QVERIFY(path.open(QIODevice::WriteOnly));
      path.write(exported.toUtf8());
    }
  }
};
QTEST_MAIN(InvestigationTests)
#include "InvestigationTests.moc"
