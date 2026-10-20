#include "Core/WorkerJob.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

class WorkerTests : public QObject {
  Q_OBJECT
  godeye::WorkerRequest request(const QString &root, const QByteArray &behavior) {
    const auto input = root + "/input 'quotes' $() & unicode —.json";
    QFile file(input);
    if (!file.open(QIODevice::WriteOnly) || file.write(behavior) != behavior.size())
      qFatal("Fixture write failed");
    return {QString::fromUtf8(GODEYE_TEST_PYTHON),
            QString::fromUtf8(GODEYE_TEST_WORKER),
            "render",
            input,
            {},
            {},
            true,
            {},
            QString::fromUtf8(GODEYE_TEST_WORKER_HOST)};
  }
  QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  }
private slots:
  void realEventsArgumentsAndAtomicRecord() {
    QTemporaryDir root;
    const auto config = request(root.path(), "complete");
    godeye::WorkerJob job(root.path() + "/jobs");
    QSignalSpy done(&job, &godeye::WorkerJob::finished);
    QVERIFY(job.start(config));
    QVERIFY(!job.start(config));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
    QVERIFY(done.first().first().toBool());
    QCOMPARE(job.record()["state"], QJsonValue("completed"));
    QCOMPARE(job.record()["pythonExecutable"],
             QJsonValue(QFileInfo(config.python).absoluteFilePath()));
    QCOMPARE(godeye::WorkerJob::readRecord(job.resultFile())["originalInput"],
             QJsonValue(config.input));
    QVERIFY(godeye::WorkerJob::readRecord(job.resultFile())["inheritedJobLock"].toBool());
    QCOMPARE(godeye::WorkerJob::readRecord(job.directory() + "/run.json")["state"],
             QJsonValue("completed"));
    QVERIFY(QFileInfo(job.directory() + "/worker.log").size() <= 1024 * 1024);
  }
  void failures_data() {
    QTest::addColumn<QByteArray>("behavior");
    QTest::newRow("exit-failure") << QByteArray("failed");
    QTest::newRow("completed-without-result") << QByteArray("missing-result");
    QTest::newRow("oversized-status") << QByteArray("oversized");
  }
  void failures() {
    QFETCH(QByteArray, behavior);
    QTemporaryDir root;
    godeye::WorkerJob job(root.path() + "/jobs");
    QSignalSpy done(&job, &godeye::WorkerJob::finished);
    QVERIFY(job.start(request(root.path(), behavior)));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 7000);
    QVERIFY(!done.first().first().toBool());
    QCOMPARE(job.record()["state"], QJsonValue("failed"));
    QVERIFY(!job.error().isEmpty());
  }
  void cancellationStopsOwnedChildrenAndPreservesUnrelatedProcess() {
    QFETCH(QByteArray, behavior);
    QTemporaryDir root;
    godeye::WorkerJob job(root.path() + "/jobs");
    QSignalSpy done(&job, &godeye::WorkerJob::finished);
    QProcess unrelated;
    unrelated.start(QString::fromUtf8(GODEYE_TEST_PYTHON),
                    {"-I", "-c", "import time; time.sleep(30)"});
    QVERIFY(unrelated.waitForStarted());
    QVERIFY(job.start(request(root.path(), behavior)));
    const auto heartbeat = job.directory() + "/product/heartbeat";
    QTRY_VERIFY_WITH_TIMEOUT(!read(heartbeat).isEmpty(), 5000);
    QString error;
    QVERIFY(!godeye::WorkerJob::recover(job.directory(), &error));
    QVERIFY(error.contains("still owns"));
    job.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 7000);
    QVERIFY(!done.first().first().toBool());
    const auto last = read(heartbeat);
    QTest::qWait(150);
    QCOMPARE(read(heartbeat), last);
    QVERIFY(!QFileInfo::exists(job.directory() + "/product/preview.png"));
    QCOMPARE(godeye::WorkerJob::readRecord(job.resultFile())["state"], QJsonValue("failed"));
    QVERIFY(QFileInfo::exists(job.directory() + "/product/diagnostics.log"));
    QCOMPARE(unrelated.state(), QProcess::Running);
    unrelated.terminate();
    QVERIFY(unrelated.waitForFinished());
  }
  void cancellationStopsOwnedChildrenAndPreservesUnrelatedProcess_data() {
    QTest::addColumn<QByteArray>("behavior");
    QTest::newRow("stubborn-worker") << QByteArray("child");
    QTest::newRow("worker-exits-before-stubborn-child") << QByteArray("child-cooperative");
  }
  void cancelDuringStartup() {
    QTemporaryDir root;
    godeye::WorkerJob job(root.path() + "/jobs");
    QSignalSpy done(&job, &godeye::WorkerJob::finished);
    QVERIFY(job.start(request(root.path(), "sleep")));
    job.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
    QVERIFY(!done.first().first().toBool());
  }
  void recoveryIgnoresSavedPidAndRetainsFiles() {
    QTemporaryDir root;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto path = root.path() + "/" + id;
    QDir().mkpath(path);
    QFile record(path + "/run.json");
    QVERIFY(record.open(QIODevice::WriteOnly));
    record.write(
        QJsonDocument(QJsonObject{{"version", 1},
                                  {"id", id},
                                  {"state", "processing"},
                                  {"workerPID", double(QCoreApplication::applicationPid())}})
            .toJson());
    record.close();
    QFile original(path + "/original.tif");
    QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("ORIGINAL SCIENTIFIC BYTES");
    original.close();
    QVERIFY(godeye::WorkerJob::recover(path));
    QCOMPARE(godeye::WorkerJob::readRecord(path + "/run.json")["state"], QJsonValue("failed"));
    QCOMPARE(read(original.fileName()), QByteArray("ORIGINAL SCIENTIFIC BYTES"));
  }
  void invalidInputAndCorruptRecordsPreserved() {
    QTemporaryDir root;
    auto config = request(root.path(), "complete");
    config.python = root.path() + "/missing-python";
    godeye::WorkerJob job(root.path() + "/jobs");
    QVERIFY(!job.start(config));
    QVERIFY(job.directory().isEmpty());
    const auto path = root.path() + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(path);
    QFile record(path + "/run.json");
    QVERIFY(record.open(QIODevice::WriteOnly));
    record.write("broken JSON");
    record.close();
    QVERIFY(!godeye::WorkerJob::recover(path));
    QCOMPARE(read(record.fileName()), QByteArray("broken JSON"));
  }
};
QTEST_GUILESS_MAIN(WorkerTests)
#include "WorkerTests.moc"
