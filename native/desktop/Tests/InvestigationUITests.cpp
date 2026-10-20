#include "UI/InvestigationWorkbench.h"
#include "UI/MissionEditor.h"
#include "UI/WorkspaceStyle.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
using namespace godeye;
class InvestigationUITests : public QObject {
  Q_OBJECT
  static Asset sample() {
    Asset a;
    a.id = newID();
    a.name = "GENERATED EVALUATION - civilian airport";
    a.category = "Civilian infrastructure";
    a.boundary = Bounds{9, 45, 9.1, 45.1};
    return a;
  }
private slots:
  void legacyTaskEditorDoesNotTreatMissingOptionalFieldsAsAnExternalEdit() {
    Missions m;
    m.create("Case");
    const auto id = m.addTask("Legacy task");
    QWidget host;
    QTimer::singleShot(0, [&] {
      auto *dialog = host.findChild<QDialog *>("missionEntryEditor");
      QVERIFY(dialog);
      dialog->findChild<QLineEdit *>("entry_owner")->setText("Local analyst");
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    QVERIFY(editMissionEntry(&host, m, "tasks", id));
    QCOMPARE(m.selected()["brief"].toObject()["tasks"].toArray()[0].toObject()["owner"].toString(),
             QString("Local analyst"));
  }
  void boundedNeighborhoodRemainsResponsiveWithALargeRegistry() {
    QTemporaryDir profile;
    QWidget host;
    host.resize(1250, 900);
    Missions m;
    m.create("Graph evaluation");
    InvestigationWorkbench work(profile.path(), m, &host);
    auto center = sample();
    QJsonArray assets{center.json()}, edges;
    for (int n = 0; n < 300; ++n) {
      auto a = sample();
      a.name = QString("Generated civilian node %1").arg(n);
      assets.append(a.json());
      AssetRelationship r;
      r.id = newID();
      r.fromAssetID = center.id;
      r.toAssetID = a.id;
      r.kind = "Connected to";
      edges.append(r.json());
    }
    QFile file(profile.filePath("Registry/assets.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        QJsonDocument(QJsonObject{{"version", 1}, {"assets", assets}, {"relationships", edges}})
            .toJson());
    file.close();
    work.registry().load();
    auto *layout = new QVBoxLayout(&host);
    layout->addWidget(work.connectionsPage());
    host.show();
    QElapsedTimer elapsed;
    elapsed.start();
    work.connectionsPage()->findChild<QComboBox *>("connectionsSource")->setCurrentIndex(1);
    QApplication::processEvents();
    int count = 0;
    for (auto *item :
         work.connectionsPage()->findChild<QGraphicsView *>("connectionsGraph")->scene()->items())
      if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
        ++count;
    QCOMPARE(count, 100);
    QVERIFY(work.connectionsPage()
                ->findChild<QLabel *>("connectionsStatus")
                ->text()
                .contains("TRUNCATED"));
    QVERIFY(elapsed.elapsed() < 1500);
    const auto root = qEnvironmentVariable("GODEYE_INVESTIGATION_QA");
    if (!root.isEmpty()) {
      QDir().mkpath(root);
      QVERIFY(host.grab().save(QDir(root).filePath("investigation-bounded-graph.png")));
      QFile timing(QDir(root).filePath("graph-timing.json"));
      QVERIFY(timing.open(QIODevice::WriteOnly));
      timing.write(QJsonDocument(QJsonObject{{"registryNodes", 301},
                                             {"registryEdges", 300},
                                             {"displayedNodes", count},
                                             {"layoutAndFirstPaintMs", double(elapsed.elapsed())}})
                       .toJson());
    }
  }
  void briefingPreviewKeepsItsFrozenCaseAfterLiveEdits() {
    QTemporaryDir profile;
    QWidget host;
    Missions m;
    const auto mid = m.create("Frozen preview evaluation");
    Finding f;
    f.claim = "Original claim";
    const auto id = m.saveFinding(mid, f, 0);
    InvestigationWorkbench work(profile.path(), m, &host);
    QTimer::singleShot(0, [&] {
      auto *d = host.findChild<QDialog *>("briefingPreview");
      QVERIFY(d);
      auto *list = d->findChild<QListWidget *>("briefingFindingSelection");
      QVERIFY(list);
      list->item(0)->setCheckState(Qt::Checked);
      auto *prepare = d->findChild<QPushButton *>("briefingPrepare"),
           *exportButton = d->findChild<QPushButton *>("briefingExport");
      QVERIFY(prepare && exportButton);
      QVERIFY(!exportButton->isEnabled());
      prepare->click();
      QTRY_VERIFY_WITH_TIMEOUT(exportButton->isEnabled(), 5000);
      auto changed = Finding::read(investigation(m.selected())["findings"].toArray()[0].toObject());
      changed.claim = "Live case was edited after preparation";
      m.saveFinding(mid, changed, 1);
      const auto text = d->findChild<QTextBrowser *>("briefingDocumentPreview")->toPlainText();
      QVERIFY(text.contains("Original claim"));
      QVERIFY(!text.contains(changed.claim));
      QCOMPARE(investigation(m.selected())["findings"].toArray()[0].toObject()["state"].toString(),
               QString("Unreviewed"));
      Q_UNUSED(id);
      d->reject();
    });
    work.overview()->findChild<QPushButton *>("exportInvestigationBriefing")->click();
  }
  void caseRegistryGraphAndNativeScreens() {
    QTemporaryDir profile;
    QWidget host;
    host.resize(1250, 900);
    Missions m;
    const auto mid = m.create("Generated civilian infrastructure case"),
               area = m.addArea("Generated terminal", "Evaluation only", {9, 45, 9.1, 45.1});
    InvestigationWorkbench work(profile.path(), m, &host);
    auto a = sample();
    work.registry().saveAsset(a, 0);
    const auto sid = m.adoptAsset(mid, a, area);
    auto b = sample();
    b.name = "Generated terminal road access";
    work.registry().saveAsset(b, 0);
    m.adoptAsset(mid, b);
    AssetRelationship r;
    r.id = newID();
    r.kind = "Connected to";
    r.fromAssetID = a.id;
    r.toAssetID = b.id;
    work.registry().saveRelationship(r, 0);
    m.adoptRelationship(mid, r);
    Finding f;
    f.claim = "Generated evaluation: the terminal and access road share a documented connection.";
    f.assetSnapshotIDs = {sid};
    m.saveFinding(mid, f, 0);
    const auto root = qEnvironmentVariable("GODEYE_INVESTIGATION_QA");
    if (!root.isEmpty())
      QDir().mkpath(root);
    auto *layout = new QVBoxLayout(&host);
    const QList<QPair<QWidget *, QString>> pages{
        {work.overview(), "investigation-overview"},
        {work.assetsPage(), "investigation-registry"},
        {work.connectionsPage(), "investigation-connections"},
        {work.findingsPage(), "investigation-findings"}};
    for (const auto &entry : pages) {
      layout->addWidget(entry.first);
      entry.first->show();
      host.show();
      QTest::qWait(30);
      if (entry.first == work.connectionsPage()) {
        auto *graph = entry.first->findChild<QGraphicsView *>("connectionsGraph");
        // The graph was populated while its page was hidden. Nodes must fit
        // the real visible viewport instead of retaining a thumbnail scale.
        QTRY_VERIFY(graph->transform().m11() * 170 >= 80);
        const auto fitted = graph->mapFromScene(graph->scene()->itemsBoundingRect()).boundingRect();
        QVERIFY(fitted.width() <= graph->viewport()->width());
        QVERIFY(fitted.height() <= graph->viewport()->height());
      }
      if (!root.isEmpty())
        QVERIFY(host.grab().save(QDir(root).filePath(entry.second + ".png")));
      entry.first->hide();
      layout->removeWidget(entry.first);
    }
    QCOMPARE(work.connectionsPage()
                 ->findChild<QGraphicsView *>("connectionsGraph")
                 ->scene()
                 ->selectedItems()
                 .size(),
             1);
    QVERIFY(work.assetsPage()->findChild<QListWidget *>("registryAssets")->count() == 2);
    a.name = "Registry revision after case adoption";
    work.registry().saveAsset(a, 1);
    QVERIFY(work.overview()
                ->findChild<QListWidget *>("caseAssetSnapshots")
                ->item(0)
                ->text()
                .contains("update available"));
    QCOMPARE(investigation(m.selected())["snapshots"]
                 .toArray()[0]
                 .toObject()["asset"]
                 .toObject()["name"]
                 .toString(),
             QString("GENERATED EVALUATION - civilian airport"));
  }
  void reviewedFindingEditorAndStaleDraft() {
    QTemporaryDir profile;
    QWidget host;
    Missions m;
    const auto first = m.create("First");
    InvestigationWorkbench work(profile.path(), m, &host);
    auto *open = work.findingsPage()->findChild<QPushButton *>("newFinding");
    QVERIFY(open);
    QTimer::singleShot(0, [&] {
      auto *dialog = host.findChild<QDialog *>("findingEditor");
      QVERIFY(dialog);
      auto *claim = dialog->findChild<QPlainTextEdit *>("findingClaim");
      claim->setPlainText("Draft that must survive a context change");
      m.create("Second");
      QTimer::singleShot(0, [&] {
        auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(warning);
        warning->accept();
      });
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
      QVERIFY(dialog->isVisible());
      QCOMPARE(claim->toPlainText(), QString("Draft that must survive a context change"));
      dialog->reject();
    });
    open->click();
    QVERIFY(investigation(m.selected())["findings"].toArray().isEmpty());
    m.select(first);
    QVERIFY(investigation(m.selected())["findings"].toArray().isEmpty());
  }
  void findingSaveRejectsUnsupportedJudgment() {
    QTemporaryDir profile;
    QWidget host;
    Missions m;
    m.create("Case");
    InvestigationWorkbench work(profile.path(), m, &host);
    QTimer::singleShot(0, [&] {
      auto *dialog = host.findChild<QDialog *>("findingEditor");
      QVERIFY(dialog);
      dialog->findChild<QPlainTextEdit *>("findingClaim")->setPlainText("A claim without evidence");
      dialog->findChild<QPlainTextEdit *>("findingRationale")->setPlainText("Analyst rationale");
      dialog->findChild<QComboBox *>("findingState")->setCurrentText("Supported");
      QTimer::singleShot(0, [] {
        auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(warning);
        warning->accept();
      });
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
      QVERIFY(dialog->isVisible());
      QVERIFY(investigation(m.selected())["findings"].toArray().isEmpty());
      dialog->findChild<QComboBox *>("findingState")->setCurrentText("Uncertain");
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    work.findingsPage()->findChild<QPushButton *>("newFinding")->click();
    QCOMPARE(investigation(m.selected())["findings"].toArray().size(), 1);
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  design::apply(app);
  InvestigationUITests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "InvestigationUITests.moc"
