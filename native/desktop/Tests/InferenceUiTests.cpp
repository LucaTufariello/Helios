#include "UI/InferenceWindow.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace godeye;
class InferenceUiTests : public QObject {
  Q_OBJECT
private slots:
  void generatedCpuReviewAndFrozenContext() {
    const auto python = qEnvironmentVariable("GODEYE_INFERENCE_PYTHON");
    if (python.isEmpty())
      QSKIP("Set GODEYE_INFERENCE_PYTHON for native local-assistance GUI integration.");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QCoreApplication::setOrganizationName("GodEyeGeneratedQA");
    QCoreApplication::setApplicationName("InferenceUI");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path() + "/settings");
    QProcess fixture;
    fixture.start(python, {"-I", GODEYE_INFERENCE_FIXTURE, root.path() + "/fixture"});
    QVERIFY(fixture.waitForFinished(30000));
    QCOMPARE(fixture.exitCode(), 0);
    auto request = WorkerJob::readRecord(root.path() + "/fixture/request.json");
    ProductLayer layer;
    layer.kind = "optical";
    layer.label = "GENERATED QA preview · not a trained detector";
    layer.preview = root.path() + "/fixture/GENERATED_QA.png";
    layer.bounds = {10, 20, 12, 24};
    layer.pixelSize = {40, 20};
    layer.manifestHash = "generated";
    InferenceWindow window(root.path() + "/profile");
    const auto mission = request["missionID"].toString(),
               evidence = request["evidenceID"].toString();
    window.setEvidence(layer, evidence, mission);
    window.findChild<QLineEdit *>("inferencePython")->setText(python);
    window.findChild<QLineEdit *>("inferenceContract")
        ->setText(root.path() + "/fixture/GENERATED_QA.contract.json");
    auto *analyze = window.findChild<QPushButton *>("inferenceAnalyze");
    auto *accept = window.findChild<QPushButton *>("inferenceAccept");
    auto *list = window.findChild<QListWidget *>("inferenceProposals");
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::mouseClick(analyze, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(window.job() && !window.busy() && list->count() == 1, 30000);
    list->item(0)->setSelected(true);
    // The preview future can finish before its GUI completion callback runs.
    QTRY_VERIFY_WITH_TIMEOUT(accept->isEnabled(), 5000);
    window.invalidateEvidence();
    QVERIFY(!accept->isEnabled());
    window.setEvidence(layer, evidence, QUuid::createUuid().toString(QUuid::WithoutBraces));
    QVERIFY(!accept->isEnabled());
    window.setEvidence(layer, evidence, mission);
    QVERIFY(accept->isEnabled());
    const auto qa = qEnvironmentVariable("GODEYE_INFERENCE_QA");
    if (!qa.isEmpty()) {
      QVERIFY(QDir().mkpath(qa));
      QVERIFY(window.grab().save(qa + "/generated-assistance.png"));
      QFile saved(qa + "/generated-native-result.json");
      QVERIFY(saved.open(QIODevice::WriteOnly));
      const auto bytes = QJsonDocument(WorkerJob::readRecord(window.job()->resultFile())).toJson();
      QCOMPARE(saved.write(bytes), bytes.size());
    }
    QSignalSpy proposals(&window, &InferenceWindow::proposalAccepted);
    QTest::mouseClick(accept, Qt::LeftButton);
    QCOMPARE(proposals.count(), 1);
    QVERIFY(window.busy());
    QVERIFY(!analyze->isEnabled());
    QVERIFY(!accept->isEnabled());
    QCOMPARE(proposals.first().at(0).toString(), mission);
    const auto proposal = proposals.first().at(1).toJsonObject();
    QCOMPARE(proposal["evidenceID"].toString(), evidence);
    window.resolveProposal(proposal["id"].toString(), false, "Generated verification failure");
    QVERIFY(!window.busy());
    QVERIFY(accept->isEnabled());
    QTest::mouseClick(accept, Qt::LeftButton);
    QCOMPARE(proposals.count(), 2);
    window.resolveProposal(proposal["id"].toString(), true);
    QVERIFY(!window.busy());
    QVERIFY(list->item(0)->data(Qt::UserRole).toJsonObject().isEmpty());
    QVERIFY(!(list->item(0)->flags() & Qt::ItemIsSelectable));
  }
};
QTEST_MAIN(InferenceUiTests)
#include "InferenceUiTests.moc"
