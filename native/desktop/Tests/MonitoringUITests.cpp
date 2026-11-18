#include "UI/MonitoringInboxPanel.h"
#include "UI/SiteMonitoringPanel.h"
#include "UI/WorkspaceStyle.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
using namespace helios;
class MonitoringUITests : public QObject {
  Q_OBJECT
private slots:
  void setupStartsPausedKeepsAreaDraftsAndPersistsExplicitly() {
    QTemporaryDir root;
    Missions m;
    m.create("One");
    const auto a = m.addArea("First", "", {9, 45, 9.1, 45.1}),
               b = m.addArea("Second", "", {10, 46, 10.1, 46.1});
    m.save(root.filePath("plans.json"));
    ScientificQueue q(root.filePath("jobs"));
    MonitoringRunner runner(
        root.filePath("jobs"), m, q, [] { return QVector<ProductLayer>{}; },
        [](WorkerRequest, const QString &) { return false; });
    SiteMonitoringPanel p(m, runner);
    p.resize(600, 650);
    p.setArea(a);
    p.show();
    QVERIFY(!p.findChild<QCheckBox *>("monitoringEnabled")->isChecked());
    p.findChild<QSpinBox *>("monitoringInterval")->setValue(48);
    p.setArea(b);
    QCOMPARE(p.findChild<QSpinBox *>("monitoringInterval")->value(), 24);
    p.setArea(a);
    QCOMPARE(p.findChild<QSpinBox *>("monitoringInterval")->value(), 48);
    p.findChild<QPushButton *>("saveMonitoring")->click();
    QVERIFY(!m.dirty());
    const auto profile =
        monitoringArea(m.selected(), a)["dossier"].toObject()["monitoring"].toObject();
    QCOMPARE(profile["intervalHours"].toInt(), 48);
    QVERIFY(!profile["enabled"].toBool());
    p.findChild<QCheckBox *>("monitoringEnabled")->setChecked(true);
    p.findChild<QPushButton *>("saveMonitoring")->click();
    QVERIFY(!monitoringArea(m.selected(), a)["dossier"]
                 .toObject()["monitoring"]
                 .toObject()["enabled"]
                 .toBool());
    // Clear explicit unsaved attempt before normal object teardown.
    p.findChild<QPushButton *>("reloadMonitoring")->click();
    runner.stop();
  }
  void staleReviewDialogDoesNotWriteToAnotherMissionAndKeepsRationale() {
    QTemporaryDir root;
    Missions m;
    const auto mission = m.create("One"), area = m.addArea("First", "", {9, 45, 9.1, 45.1});
    m.save(root.filePath("plans.json"));
    QJsonObject e{{"version", 1},
                  {"id", newID()},
                  {"key", QString(64, 'a')},
                  {"areaID", area},
                  {"areaName", "First"},
                  {"title", "Generated discovery"},
                  {"sourceID", "GENERATED"},
                  {"kind", "Acquisition"},
                  {"status", "Discovered"},
                  {"reviewState", "New"},
                  {"rationale", ""},
                  {"reason", "Generated catalogue record"},
                  {"createdAt", foundationNow()},
                  {"updatedAt", foundationNow()}};
    m.upsertMonitoringEvent(mission, e);
    ScientificQueue q(root.filePath("jobs"));
    MonitoringRunner runner(
        root.filePath("jobs"), m, q, [] { return QVector<ProductLayer>{}; },
        [](WorkerRequest, const QString &) { return false; });
    MonitoringInboxPanel p(m, runner);
    p.resize(600, 650);
    p.show();
    QCOMPARE(p.findChild<QListWidget *>("monitoringInboxList")->count(), 1);
    QTimer::singleShot(0, &p, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      auto *note = dialog->findChild<QPlainTextEdit *>("inboxReviewRationale");
      QVERIFY(note);
      note->setPlainText("Keep this draft");
      m.create("Other");
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
      QVERIFY(dialog->isVisible());
      QCOMPARE(note->toPlainText(), QString("Keep this draft"));
      dialog->reject();
    });
    p.findChild<QPushButton *>("inboxReview")->click();
    m.select(mission);
    QCOMPARE(m.selected()["brief"]
                 .toObject()["monitoringInbox"]
                 .toArray()[0]
                 .toObject()["reviewState"]
                 .toString(),
             QString("New"));
    QTimer::singleShot(0, &p, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->findChild<QComboBox *>("inboxReviewState")->setCurrentText("Investigating");
      dialog->findChild<QPlainTextEdit *>("inboxReviewRationale")
          ->setPlainText("Need dated optical corroboration");
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    p.findChild<QPushButton *>("inboxReview")->click();
    QCOMPARE(m.selected()["brief"]
                 .toObject()["monitoringInbox"]
                 .toArray()[0]
                 .toObject()["reviewState"]
                 .toString(),
             QString("Investigating"));
    runner.stop();
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  design::apply(app);
  MonitoringUITests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "MonitoringUITests.moc"
