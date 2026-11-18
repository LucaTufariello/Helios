#include "Core/RadarDownload.h"
#include "FakeNetwork.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace helios;
class DownloadTests : public QObject {
  Q_OBJECT
  RadarScene scene() {
    RadarScene value;
    value.id = "11111111-2222-3333-4444-555555555555";
    value.name = "S1A_IW_GRDH_1SDV_20260820T170616_20260820T170641_004358_0080C1_5973.SAFE";
    value.acquisition = "2026-08-20T17:06:16Z";
    value.online = true;
    value.contentLength = 64;
    return value;
  }
  RadarQuery query() {
    return {{9, 45, 9.1, 45.1}, QDate(2026, 8, 1), QDate(2026, 8, 31)};
  }
  QByteArray envelope() {
    return QByteArray::fromHex("504b0304") + QByteArray(40, 'x') + QByteArray::fromHex("504b0506") +
           QByteArray(18, 0);
  }
  RadarDownload::NetworkFactory factory(std::initializer_list<qa::ReplySpec> responses) {
    QQueue<qa::ReplySpec> queue;
    for (const auto &response : responses)
      queue.enqueue(response);
    return [queue] {
      auto network = std::make_unique<qa::FakeNetwork>();
      network->responses = queue;
      return network;
    };
  }
  void cleanPartial(const QString &root) {
    const QDir dir(root + "/" + scene().id);
    QVERIFY(dir.entryList({".partial-*"}, QDir::Files | QDir::Hidden).isEmpty());
  }
private slots:
  void writesSourceArchiveAndAtomicProvenanceWithoutReplacement() {
    QTemporaryDir root;
    RadarDownload download(nullptr, factory({{envelope()}}));
    QSignalSpy done(&download, &RadarDownload::finished);
    QVERIFY(download.start(scene(), query(), "GENERATED_TOKEN", root.path()));
    QTRY_COMPARE(done.size(), 1);
    QVERIFY2(done[0][1].toString().isEmpty(), qPrintable(done[0][1].toString()));
    const auto archive = done[0][0].toString();
    QCOMPARE(QFileInfo(archive).fileName(), scene().archiveFilename());
    QCOMPARE(RadarDownload::savedArchive(QFileInfo(archive).absolutePath(), scene()), archive);
    QFile record(QFileInfo(archive).absolutePath() + "/download.json");
    QVERIFY(record.open(QIODevice::ReadOnly));
    const auto bytes = record.readAll();
    QVERIFY(!bytes.contains("GENERATED_TOKEN"));
    const auto json = QJsonDocument::fromJson(bytes).object();
    QCOMPARE(json["scene"].toObject()["name"].toString(), scene().name);
    QVERIFY(json["query"].isObject());
    done.clear();
    QVERIFY(download.start(scene(), query(), "GENERATED_TOKEN", root.path()));
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(done[0][1].toString().contains("already exists"));
    QFile saved(archive);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), envelope());
    cleanPartial(root.path());
  }
  void rejectsRedirectBeforeSendingAuthorization() {
    struct Capture : qa::FakeNetwork {
      std::shared_ptr<QVector<QNetworkRequest>> capture;
      explicit Capture(std::shared_ptr<QVector<QNetworkRequest>> value) : capture(value) {}
      QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                   QIODevice *outgoing) override {
        capture->append(request);
        return FakeNetwork::createRequest(operation, request, outgoing);
      }
    };
    QTemporaryDir root;
    auto requests = std::make_shared<QVector<QNetworkRequest>>();
    RadarDownload download(nullptr, [requests] {
      auto network = std::make_unique<Capture>(requests);
      network->responses.enqueue({{}, 302, 0, QUrl("https://untrusted.example/file")});
      return network;
    });
    QSignalSpy done(&download, &RadarDownload::finished);
    QVERIFY(download.start(scene(), query(), "GENERATED_TOKEN", root.path()));
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(done[0][1].toString().contains("not forwarded"));
    QCOMPARE(requests->size(), 1);
    QCOMPARE(requests->first().rawHeader("Authorization"), QByteArray("Bearer GENERATED_TOKEN"));
    cleanPartial(root.path());
    QVERIFY(
        !RadarDownload::trusted(QUrl("https://download.dataspace.copernicus.eu.evil.test/file")));
  }
  void byteLimitsLengthAndZipAdmission_data() {
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<qint64>("length");
    QTest::addColumn<qint64>("limit");
    QTest::newRow("announced size") << envelope() << qint64(1000) << qint64(100);
    QTest::newRow("received size") << QByteArray(1000, 'x') << qint64(1) << qint64(100);
    QTest::newRow("truncated") << envelope() << qint64(100) << qint64(1000);
    QTest::newRow("nonzip") << QByteArray(50, 'x') << qint64(50) << qint64(1000);
  }
  void byteLimitsLengthAndZipAdmission() {
    QFETCH(QByteArray, body);
    QFETCH(qint64, length);
    QFETCH(qint64, limit);
    QTemporaryDir root;
    RadarDownload download(nullptr, factory({{body, 200, length}}));
    QSignalSpy done(&download, &RadarDownload::finished);
    QVERIFY(download.start(scene(), query(), "GENERATED_TOKEN", root.path(), limit));
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(done[0][0].toString().isEmpty());
    QVERIFY(!done[0][1].toString().isEmpty());
    cleanPartial(root.path());
  }
  void cancellationRemovesOnlyOwnedPartialBytes() {
    QTemporaryDir root;
    QFile unrelated(root.path() + "/keep.bin");
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    unrelated.write("KEEP");
    unrelated.close();
    RadarDownload download(nullptr, factory({{envelope(), 200, -1, {}, true}}));
    QSignalSpy done(&download, &RadarDownload::finished);
    QVERIFY(download.start(scene(), query(), "GENERATED_TOKEN", root.path()));
    QTest::qWait(30);
    download.cancel();
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(done[0][1].toString().contains("cancelled"));
    cleanPartial(root.path());
    QVERIFY(unrelated.open(QIODevice::ReadOnly));
    QCOMPARE(unrelated.readAll(), QByteArray("KEEP"));
  }
};
QTEST_GUILESS_MAIN(DownloadTests)
#include "DownloadTests.moc"
