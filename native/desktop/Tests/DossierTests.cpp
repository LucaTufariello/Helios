#include "Core/Dossiers.h"
#include "Core/Infrastructure.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace helios;
class DossierTests : public QObject {
  Q_OBJECT
  static QJsonObject area(const Missions &m, const QString &id) {
    for (const auto v : m.selected()["brief"].toObject()["areas"].toArray())
      if (v.toObject()["id"] == id)
        return v.toObject();
    return {};
  }
  static QJsonObject raster() {
    return {{"id", newID()},
            {"kind", "optical"},
            {"referenceID", newID()},
            {"title", "Generated optical"},
            {"savedAt", foundationNow()},
            {"acquisition", QJsonObject{{"bounds", QJsonArray{9, 45, 9.1, 45.1}},
                                        {"sensor", "Generated sensor"},
                                        {"sourceID", "GENERATED"},
                                        {"displayBand", "RGB"},
                                        {"acquiredAt", 0.0}}}};
  }
private slots:
  void legacyAreasStayUncuratedAndSuggestionsDoNotMutate() {
    Missions m;
    m.create("Generated");
    auto id = m.addArea("Area", "Question", {9, 45, 9.1, 45.1});
    m.addEvidence(raster());
    const auto before = m.plans();
    const auto d = dossierView(m.selected(), id, {});
    QCOMPARE(d.suggestions.size(), 1);
    QVERIFY(d.linked.isEmpty());
    QVERIFY(!area(m, id).contains("dossier"));
    QCOMPARE(m.plans(), before);
    const auto other = m.create("Other");
    Q_UNUSED(other);
    QVERIFY(dossierView(m.selected(), id, {}).area.isEmpty());
  }
  void linksPinsRoundTripAndUnlinkRetainsEvidence() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("Generated");
    const auto id = m.addArea("Area", "Question", {9, 45, 9.1, 45.1});
    auto item = raster();
    const auto evidence = m.linkDossierEvidence(mission, id, item);
    QCOMPARE(m.linkDossierEvidence(mission, id, item), evidence);
    QCOMPARE(m.selected()["items"].toArray().size(), 1);
    m.setDossierBaseline(mission, id, evidence, true);
    m.setDossierBaseline(mission, id, evidence, true);
    QCOMPARE(area(m, id)["dossier"].toObject()["baselineEvidenceIDs"].toArray().size(), 1);
    auto d = dossierView(m.selected(), id, {});
    QCOMPARE(d.linked.size(), 1);
    QVERIFY(d.linked[0].baseline);
    QVERIFY(!d.linked[0].available);
    QCOMPARE(d.linked[0].time.toString(Qt::ISODate), QString("2001-01-01T00:00:00Z"));
    m.save(root.filePath("plans.json"));
    Missions copy;
    copy.load(root.filePath("plans.json"));
    QCOMPARE(copy.plans(), m.plans());
    m.setDossierEvidence(mission, id, evidence, false);
    QCOMPARE(m.selected()["items"].toArray().size(), 1);
    QVERIFY(area(m, id)["dossier"].toObject()["baselineEvidenceIDs"].toArray().isEmpty());
    QVERIFY(area(m, id)["dossier"].toObject()["linkedEvidenceIDs"].toArray().isEmpty());
  }
  void invalidAndStaleWritesAreAtomic() {
    Missions m;
    const auto mission = m.create("Generated");
    const auto id = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    m.addView("View", Camera{});
    const auto evidence = m.selected()["items"].toArray()[0].toObject()["id"].toString();
    m.setDossierEvidence(mission, id, evidence, true);
    const auto before = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.setDossierBaseline(mission, id, evidence, true), std::runtime_error);
    QCOMPARE(m.plans(), before);
    QVERIFY_EXCEPTION_THROWN(m.linkDossierEvidence(mission, newID(), raster()), std::runtime_error);
    QCOMPARE(m.plans(), before);
    auto invalid = m.selected();
    auto b = invalid["brief"].toObject();
    auto areas = b["areas"].toArray();
    auto a = areas[0].toObject();
    auto d = a["dossier"].toObject();
    d["linkedEvidenceIDs"] = QJsonArray{evidence, evidence};
    a["dossier"] = d;
    areas[0] = a;
    b["areas"] = areas;
    invalid["brief"] = b;
    QVERIFY_EXCEPTION_THROWN(validateMissions({invalid}), std::runtime_error);
    m.create("Other");
    const auto other = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.setDossierEvidence(mission, id, evidence, true), std::runtime_error);
    QCOMPARE(m.plans(), other);
  }
  void sourceSnapshotsAndReviewTaskCleanup() {
    Missions m;
    const auto mission = m.create("Generated");
    QJsonObject site{{"id", newID()},
                     {"name", "Generated site"},
                     {"kind", "Port"},
                     {"createdAt", foundationNow()},
                     {"region", Bounds{9, 45, 9.1, 45.1}.json()}};
    const auto id = m.importDossierSite(mission, site);
    site["name"] = "Changed elsewhere";
    site["region"] = Bounds{10, 46, 10.1, 46.1}.json();
    QCOMPARE(m.importDossierSite(mission, site), id);
    QCOMPARE(area(m, id)["name"].toString(), QString("Generated site"));
    QCOMPARE(m.selected()["items"].toArray().size(), 1);
    const auto evidence = m.linkDossierEvidence(mission, id, raster());
    m.setDossierBaseline(mission, id, evidence, true);
    const auto second = m.addArea("Second", "", {9, 45, 9.1, 45.1});
    m.setDossierEvidence(mission, second, evidence, true);
    const auto observation =
        m.addObservation("Object", "Infrastructure", {45.05, 9.05}, evidence, id);
    m.setObservationReview(observation, "Uncertain", "Needs corroboration");
    const auto task = m.addTask("Follow up", id, observation);
    Q_UNUSED(task);
    m.addNote("Context", "Question remains open", "", evidence.toLower(), id.toLower());
    auto view = dossierView(m.selected(), id, {});
    QCOMPARE(view.tasks.size(), 1);
    QCOMPARE(view.notes.size(), 1);
    QCOMPARE(view.observations.size(), 1);
    m.removeEntry("items", evidence);
    QVERIFY(area(m, id)["dossier"].toObject()["baselineEvidenceIDs"].toArray().isEmpty());
    QVERIFY(area(m, second)["dossier"].toObject()["linkedEvidenceIDs"].toArray().isEmpty());
    QVERIFY(
        !m.selected()["brief"].toObject()["notes"].toArray()[0].toObject().contains("evidenceID"));
    m.removeEntry("observations", observation);
    QVERIFY(!m.selected()["brief"].toObject()["tasks"].toArray()[0].toObject().contains(
        "observationID"));
    m.removeEntry("areas", id);
    QVERIFY(!m.selected()["brief"].toObject()["tasks"].toArray()[0].toObject().contains("areaID"));
    QCOMPARE(m.selected()["brief"].toObject()["notes"].toArray().size(), 1);
    validateMissions(m.plans());
  }
  void corruptDocumentPreservesWorkAndLastGoodDossierBackup() {
    QTemporaryDir root;
    const auto path = root.filePath("plans.json");
    Missions m;
    const auto mission = m.create("Generated"), id = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto evidence = m.linkDossierEvidence(mission, id, raster());
    m.setDossierBaseline(mission, id, evidence, true);
    const auto baseline = m.plans();
    m.save(path);
    m.addTask("New follow-up", id);
    m.save(path);
    const auto current = m.plans();
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{corrupt");
    file.close();
    QVERIFY_EXCEPTION_THROWN(m.load(path), std::runtime_error);
    QCOMPARE(m.plans(), current);
    m.addTask("Unsaved recovery work", id);
    const auto unsaved = m.plans();
    QVERIFY_EXCEPTION_THROWN(m.save(path), std::runtime_error);
    QCOMPARE(m.plans(), unsaved);
    QVERIFY(m.dirty());
    Missions restored;
    restored.load(path + ".backup");
    QCOMPARE(restored.plans(), baseline);
    m.save(root.filePath("recovered.json"));
    Missions copy;
    copy.load(root.filePath("recovered.json"));
    QCOMPARE(copy.plans(), unsaved);
  }
  void boundaryEditsKeepPinsAndRecomputeSuggestions() {
    Missions m;
    const auto mission = m.create("Generated");
    const auto id = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    const auto evidence = m.linkDossierEvidence(mission, id, raster());
    m.setDossierBaseline(mission, id, evidence, true);
    m.editEntry("areas", id, {{"region", Bounds{10, 46, 10.1, 46.1}.json()}});
    auto d = dossierView(m.selected(), id, {});
    QVERIFY(d.linked[0].baseline);
    QVERIFY(d.linked[0].warnings.join(' ').contains("does not overlap"));
    QVERIFY(d.suggestions.isEmpty());
    QVERIFY(!evidenceOverlaps(raster(), {9.1, 45, 9.2, 45.1}));
  }
};
QTEST_GUILESS_MAIN(DossierTests)
#include "DossierTests.moc"
