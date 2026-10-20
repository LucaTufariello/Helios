#include "UI/DossierPanel.h"
#include "UI/WorkspaceStyle.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
using namespace godeye;
class DossierUITests : public QObject {
  Q_OBJECT
  static QJsonObject raster() {
    return {{"id", newID()},
            {"kind", "optical"},
            {"referenceID", newID()},
            {"title", "Generated source"},
            {"savedAt", foundationNow()},
            {"acquisition", QJsonObject{{"bounds", Bounds{9, 45, 9.1, 45.1}.json()},
                                        {"sourceID", "GENERATED"},
                                        {"sensor", "Generated"},
                                        {"displayBand", "RGB"},
                                        {"acquiredAt", 0.0}}}};
  }
  static void click(DossierPanel &p, const QString &name) {
    auto *button = p.findChild<QPushButton *>(name);
    QVERIFY(button);
    QVERIFY(button->isEnabled());
    button->click();
  }
private slots:
  void readyMeansLatestProjectionIsPublished() {
    Missions m;
    m.create("One");
    const auto area = m.addArea("Area", "", {9, 45, 9.1, 45.1});
    DossierPanel p(m);
    p.setArea(area);
    for (int i = 0; i < 20; ++i) {
      const auto purpose = QString("Revision %1").arg(i);
      m.editEntry("areas", area, {{"purpose", purpose}});
      QTRY_VERIFY(!p.busy());
      QCOMPARE(p.currentView().area["purpose"].toString(), purpose);
    }
  }
  void explicitLinksPinsAndContext() {
    Missions m;
    const auto mission = m.create("One"), area = m.addArea("First", "Question", {9, 45, 9.1, 45.1});
    m.addEvidence(raster());
    const auto evidence = m.selected()["items"].toArray()[0].toObject()["id"].toString();
    DossierPanel p(m);
    p.resize(700, 700);
    p.setArea(area);
    p.show();
    QTRY_VERIFY(!p.busy());
    QVERIFY(p.currentView().linked.isEmpty());
    QCOMPARE(p.currentView().suggestions.size(), 1);
    p.findChild<QTabWidget *>("dossierTabs")->setCurrentIndex(1);
    p.findChild<QTabWidget *>("dossierEvidenceTabs")->setCurrentIndex(1);
    p.findChild<QListWidget *>("dossierSuggestedEvidence")->setCurrentRow(0);
    click(p, "dossierLink");
    QTRY_VERIFY(!p.busy());
    QCOMPARE(p.currentView().linked.size(), 1);
    p.findChild<QTabWidget *>("dossierEvidenceTabs")->setCurrentIndex(0);
    p.findChild<QListWidget *>("dossierLinkedEvidence")->setCurrentRow(0);
    click(p, "dossierPinBaseline");
    QTRY_VERIFY(!p.busy());
    QVERIFY(p.currentView().linked[0].baseline);
    QSignalSpy show(&p, &DossierPanel::evidenceRequested);
    click(p, "dossierShowEvidence");
    QCOMPARE(show.count(), 1);
    QCOMPARE(show.first()[0].toJsonObject()["id"].toString(), evidence);
    QVERIFY(p.findChild<QLabel *>("dossierSourceDetails")->text().contains("unavailable"));
    click(p, "dossierUnlink");
    QTRY_VERIFY(!p.busy());
    QVERIFY(p.currentView().linked.isEmpty());
    QCOMPARE(m.selected()["items"].toArray().size(), 1);
    const auto other = m.create("Other");
    const auto otherArea = m.addArea("Second", "", {10, 46, 10.1, 46.1});
    p.setArea(otherArea);
    m.select(mission);
    p.setArea(area);
    m.select(other);
    p.setArea(otherArea);
    QTRY_VERIFY(!p.busy());
    QCOMPARE(p.currentView().missionID, other);
    QCOMPARE(p.currentView().areaID, otherArea);
    QVERIFY(p.currentView().suggestions.isEmpty());
  }
  void cancelledAndStaleDialogsDoNotWriteAndKeepDraft() {
    Missions m;
    const auto mission = m.create("One"), area = m.addArea("First", "", {9, 45, 9.1, 45.1});
    const auto second = m.addArea("Second", "", {10, 46, 10.1, 46.1});
    DossierPanel p(m);
    p.setArea(area);
    p.show();
    QTRY_VERIFY(!p.busy());
    const auto before = m.plans();
    QTimer::singleShot(0, &p, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->reject();
    });
    click(p, "dossierAdd-tasks");
    QCOMPARE(m.plans(), before);
    QTimer::singleShot(0, &p, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      auto *title = dialog->findChild<QLineEdit *>("dossierEntryTitle");
      title->setText("Retain this draft");
      p.setArea(second);
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
      QVERIFY(dialog->isVisible());
      QCOMPARE(title->text(), QString("Retain this draft"));
      QCOMPARE(m.plans(), before);
      dialog->reject();
    });
    click(p, "dossierAdd-tasks");
    QTRY_VERIFY(!p.busy());
    QCOMPARE(m.plans(), before);
    QTimer::singleShot(0, &p, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->findChild<QLineEdit *>("dossierEntryTitle")->setText("Wrong mission");
      m.create("Other");
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
      QVERIFY(dialog->isVisible());
      QVERIFY(m.selected()["brief"].toObject()["tasks"].toArray().isEmpty());
      dialog->reject();
    });
    click(p, "dossierAdd-tasks");
    m.select(mission);
    QVERIFY(m.selected()["brief"].toObject()["tasks"].toArray().isEmpty());
  }
  void observationReviewAndFollowupAreScoped() {
    Missions m;
    m.create("One");
    const auto area = m.addArea("First", "", {9, 45, 9.1, 45.1});
    const auto observation =
        m.addObservation("Generated object", "Infrastructure", {45.05, 9.05}, {}, area);
    DossierPanel p(m);
    p.setArea(area);
    p.show();
    QTRY_VERIFY(!p.busy());
    p.findChild<QListWidget *>("dossier-observations")->setCurrentRow(0);
    QTimer::singleShot(0, &p, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->findChild<QComboBox *>("dossierReviewState")->setCurrentText("Uncertain");
      dialog->findChild<QPlainTextEdit *>("dossierReviewRationale")
          ->setPlainText("Needs another dated source");
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    click(p, "dossierReviewObservation");
    QTRY_VERIFY(!p.busy());
    QCOMPARE(p.currentView().observations[0].toObject()["state"].toString(), QString("Uncertain"));
    QTimer::singleShot(0, &p, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->findChild<QLineEdit *>("dossierEntryTitle")->setText("Obtain corroborating source");
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    click(p, "dossierFollowUp");
    QTRY_VERIFY(!p.busy());
    QCOMPARE(p.currentView().tasks.size(), 1);
    QCOMPARE(p.currentView().tasks[0].toObject()["observationID"].toString(), observation);
    QCOMPARE(p.currentView().tasks[0].toObject()["areaID"].toString(), area);
    m.removeEntry("observations", observation);
    QTRY_VERIFY(!p.busy());
    QVERIFY(!p.currentView().tasks[0].toObject().contains("observationID"));
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  design::apply(app);
  DossierUITests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "DossierUITests.moc"
