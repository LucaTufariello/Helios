#include "Core/Inference.h"
#include "Core/WorkerJob.h"
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace helios;
class InferenceTests : public QObject {
  Q_OBJECT
  QJsonObject generatedResult() {
    const auto evidence = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject proposal{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                         {"title", "Generated proposal"},
                         {"category", "Building"},
                         {"coordinate", Coordinate{22, 11}.json()},
                         {"bounds", Bounds{10.5, 21, 11.5, 23}.json()},
                         {"evidenceID", evidence},
                         {"state", "Unreviewed"},
                         {"method", "ONNX CPU proposal · GENERATED-QA-detector.onnx"},
                         {"modelScore", .81},
                         {"modelName", "GENERATED-QA-detector.onnx"},
                         {"inputSHA256", QString(64, 'a')},
                         {"inputTransform", "RGB; normalized upper-left XYXY"},
                         {"createdAt", 0.0},
                         {"note", "Generated fixture only"},
                         {"modelSHA256", QString(64, 'b')},
                         {"modelContractSHA256", QString(64, 'c')},
                         {"modelBackend", "onnx-cpu"},
                         {"imageBox", QJsonArray{.25, .25, .75, .75}},
                         {"imagePixelBox", QJsonArray{50, 50, 150, 150}},
                         {"futureModelExtension", QJsonObject{{"preserve", true}}}};
    return {{"version", 1},
            {"state", "completed"},
            {"backend", "onnx-cpu"},
            {"missionID", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"evidenceID", evidence},
            {"sourceSHA256", QString(64, 'a')},
            {"modelSHA256", QString(64, 'b')},
            {"contractSHA256", QString(64, 'c')},
            {"sourceBounds", Bounds{10, 20, 12, 24}.json()},
            {"accuracyValidated", false},
            {"providers", QJsonArray{"CPUExecutionProvider"}},
            {"proposalCount", 1},
            {"proposals", QJsonArray{proposal}}};
  }
private slots:
  void resultProvenanceAndImagePixelBoxesAreStrict() {
    const auto result = generatedResult();
    const auto mission = result["missionID"].toString();
    const auto evidence = result["evidenceID"].toString();
    const auto proposal = result["proposals"].toArray().first().toObject();
    QString error;
    QCOMPARE(inferenceProposals(result, mission, evidence, &error), QJsonArray{proposal});
    QVERIFY(error.isEmpty());
    for (const auto key : {"inputSHA256", "modelSHA256", "modelContractSHA256", "modelBackend",
                           "imageBox", "imagePixelBox"}) {
      auto invalid = proposal;
      invalid.remove(key);
      auto changed = result;
      changed["proposals"] = QJsonArray{invalid};
      QVERIFY(inferenceProposals(changed, mission, evidence, &error).isEmpty());
      QVERIFY(!error.isEmpty());
    }
    const QList<QPair<QString, QJsonValue>> invalidFields{
        {"imageBox", QJsonArray{0, 0, 1.01, 1}},
        {"imageBox", QJsonArray{0, 0, 1}},
        {"imagePixelBox", QJsonArray{0, 0, 2049, 200}},
        {"imagePixelBox", QJsonArray{-1, 0, 200, 200}},
        {"imagePixelBox", QJsonArray{0, 0, 0, 200}},
        {"imagePixelBox", QJsonArray{0, 200, 200, 0}},
        {"imagePixelBox", QJsonArray{0, 0, "200", 200}},
        {"imagePixelBox", QJsonValue::Null},
        {"modelSHA256", QString(64, 'B')},
        {"modelContractSHA256", QString(64, 'c') + "\n"},
        {"modelBackend", "onnx-gpu"}};
    for (const auto &field : invalidFields) {
      auto invalid = proposal;
      invalid[field.first] = field.second;
      auto changed = result;
      changed["proposals"] = QJsonArray{invalid};
      QVERIFY(inferenceProposals(changed, mission, evidence, &error).isEmpty());
      QVERIFY(!error.isEmpty());
    }
    auto changed = result;
    changed["sourceSHA256"] = QString(64, 'a') + "\n";
    auto invalid = proposal;
    invalid["inputSHA256"] = changed["sourceSHA256"];
    changed["proposals"] = QJsonArray{invalid};
    QVERIFY(inferenceProposals(changed, mission, evidence, &error).isEmpty());
    QVERIFY(!error.isEmpty());
  }
  void requestRequiresLinkedOpticalEvidence() {
    ProductLayer layer;
    layer.kind = "radar";
    layer.preview = "/example/preview.png";
    layer.bounds = {10, 20, 12, 24};
    const auto mission = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto evidence = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(inferenceRequest(layer, "contract.json", evidence, mission, .5).isEmpty());
    layer.kind = "optical";
    layer.acquisition = "2026-10-12T10:28:13.131Z";
    auto request = inferenceRequest(layer, "contract.json", evidence, mission, .5);
    QCOMPARE(request["bounds"].toArray(), QJsonArray({10, 20, 12, 24}));
    QCOMPARE(request["missionID"].toString(), mission);
    QCOMPARE(request["evidenceID"].toString(), evidence);
    QVERIFY(request["observedAt"].isDouble());
    QVERIFY(inferenceRequest(layer, "contract.json", evidence, mission, 2).isEmpty());
    QVERIFY(inferenceRequest(layer, "contract.json", "missing", mission, .5).isEmpty());
    layer.bounds.east = 9;
    QVERIFY(inferenceRequest(layer, "contract.json", evidence, mission, .5).isEmpty());
  }
  void realGeneratedCpuWorkerBoundary() {
    const auto python = qEnvironmentVariable("HELIOS_INFERENCE_PYTHON");
    if (python.isEmpty())
      QSKIP("Set HELIOS_INFERENCE_PYTHON to the optional inference environment for CPU worker "
            "integration.");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QProcess fixture;
    fixture.start(python, {"-I", HELIOS_INFERENCE_FIXTURE, root.path() + "/fixture"});
    QVERIFY(fixture.waitForFinished(30000));
    QCOMPARE(fixture.exitCode(), 0);
    const auto request = WorkerJob::readRecord(root.path() + "/fixture/request.json");
    QVERIFY(!request.isEmpty());
    WorkerRequest args;
    args.python = python;
    args.worker = HELIOS_INFERENCE_WORKER;
    args.host = HELIOS_TEST_WORKER_HOST;
    args.operation = "render";
    args.optical = true;
    args.document = request;
    WorkerJob job(root.path() + "/jobs");
    QSignalSpy finished(&job, &WorkerJob::finished);
    QVERIFY(job.start(args));
    QVERIFY(finished.wait(30000));
    QVERIFY2(finished.first().first().toBool(), qPrintable(job.error()));
    auto result = WorkerJob::readRecord(job.resultFile());
    QString error;
    auto values = inferenceProposals(result, request["missionID"].toString(),
                                     request["evidenceID"].toString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(values.size(), 1);
    auto proposal = values.first().toObject();
    QCOMPARE(proposal["state"].toString(), "Unreviewed");
    const auto bounds = proposal["bounds"].toArray();
    QVERIFY(std::abs(bounds[1].toDouble() - 21.2) < 1e-6);
    QVERIFY(inferenceProposals(result, QUuid::createUuid().toString(),
                               request["evidenceID"].toString(), &error)
                .isEmpty());
    QVERIFY(!error.isEmpty());
    auto invalid = proposal;
    invalid["state"] = "Corroborated";
    result["proposals"] = QJsonArray{invalid};
    QVERIFY(inferenceProposals(result, request["missionID"].toString(),
                               request["evidenceID"].toString(), &error)
                .isEmpty());
    WorkerJob cancelled(root.path() + "/jobs");
    QSignalSpy stopped(&cancelled, &WorkerJob::finished);
    QVERIFY(cancelled.start(args));
    cancelled.cancel();
    QVERIFY(stopped.wait(10000));
    QVERIFY(!stopped.first().first().toBool());
    QVERIFY(!cancelled.running());
    QVERIFY(!QFile::exists(cancelled.resultFile()));
  }
};
QTEST_GUILESS_MAIN(InferenceTests)
#include "InferenceTests.moc"
