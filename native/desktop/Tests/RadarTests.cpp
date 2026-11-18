#include "Core/RadarCatalog.h"
#include "FakeNetwork.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

class RadarTests : public QObject {
  Q_OBJECT
  QJsonObject row() {
    return {
        {"Id", "11111111-2222-3333-4444-555555555555"},
        {"Name", "S1A_IW_GRDH_1SDV_20260820T170616_20260820T170641_004358_0080C1_5973.SAFE"},
        {"Online", true},
        {"ContentLength", 12345},
        {"ContentDate", QJsonObject{{"Start", "2026-08-20T17:06:16Z"}}},
        {"Attributes", QJsonArray{QJsonObject{{"Name", "orbitDirection"}, {"Value", "ASCENDING"}},
                                  QJsonObject{{"Name", "relativeOrbitNumber"}, {"Value", 117}}}},
        {"GeoFootprint",
         QJsonObject{{"type", "Polygon"},
                     {"coordinates", QJsonArray{QJsonArray{QJsonArray{9, 45}, QJsonArray{10, 45},
                                                           QJsonArray{10, 46}, QJsonArray{9, 46},
                                                           QJsonArray{9, 45}}}}}}};
  }
  QByteArray page(QJsonArray rows) {
    return QJsonDocument(QJsonObject{{"value", rows}}).toJson();
  }
  QByteArray token(const QString &access = "GENERATED_ACCESS", int seconds = 600) {
    return QJsonDocument(QJsonObject{{"access_token", access},
                                     {"refresh_token", "GENERATED_REFRESH"},
                                     {"expires_in", seconds}})
        .toJson();
  }
private slots:
  void queryModesPolarizationUtcAndPagination() {
    helios::RadarQuery query{{9, 45, 9.1, 45.1}, QDate(2026, 8, 1), QDate(2026, 8, 31), "VV", "SM"};
    const QUrlQuery url(query.url(20));
    const auto filter = url.queryItemValue("$filter");
    QVERIFY(filter.contains("_S6_GRD"));
    QVERIFY(filter.contains("_1SSV_"));
    QVERIFY(filter.contains("Start lt 2026-09-01T00:00:00Z"));
    QCOMPARE(url.queryItemValue("$skip"), QString("20"));
    QVERIFY_EXCEPTION_THROWN(query.url(21), std::runtime_error);
    query.region.east = query.region.west;
    QVERIFY(!query.valid());
  }
  void sceneAdmissionAndSourceBasedFilename() {
    const auto parsed = helios::RadarCatalog::parse(page({row(), row()}));
    QCOMPARE(parsed.scenes.size(), 1);
    QCOMPARE(parsed.rejected, 1);
    const auto scene = parsed.scenes.first();
    QCOMPARE(scene.orbit, QString("ASCENDING"));
    QCOMPARE(*scene.relativeOrbit, 117);
    QCOMPARE(scene.footprint.first().size(), 5);
    QVERIFY(scene.archiveFilename().startsWith("S1A_IW_GRDH_1SDV_20260820T"));
    QVERIFY(scene.archiveFilename().endsWith(".zip"));
    QCOMPARE(scene.downloadURL().host(), QString("download.dataspace.copernicus.eu"));
    auto bad = row();
    bad["Name"] = bad["Name"].toString().replace("GRDH", "SLC_");
    QVERIFY(helios::RadarCatalog::parse(page({bad})).scenes.isEmpty());
    bad = row();
    bad["ContentLength"] = true;
    QVERIFY(helios::RadarCatalog::parse(page({bad})).scenes.isEmpty());
    bad = row();
    bad["ContentLength"] = double(40ll * 1024 * 1024 * 1024 + 1);
    QVERIFY(helios::RadarCatalog::parse(page({bad})).scenes.isEmpty());
    QVERIFY_EXCEPTION_THROWN(helios::RadarCatalog::parse("{}"), std::runtime_error);
    QJsonArray tooMany;
    for (int i = 0; i < 21; ++i)
      tooMany.append(row());
    QVERIFY_EXCEPTION_THROWN(helios::RadarCatalog::parse(page(tooMany)), std::runtime_error);
  }
  void boundedHttpAndNoRedirectForwarding() {
    helios::qa::FakeNetwork network;
    network.responses.enqueue({QByteArray(1000, 'x'), 200, 1000});
    helios::HttpRequest request(nullptr, &network);
    QSignalSpy done(&request, &helios::HttpRequest::completed);
    request.start(QUrl("https://catalogue.dataspace.copernicus.eu/test"), 100);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(!done.first()[2].toString().isEmpty());
    done.clear();
    network.responses.enqueue({{}, 302, 0, QUrl("https://untrusted.example/redirect")});
    request.start(QUrl("https://identity.dataspace.copernicus.eu/test"), 100,
                  "password=GENERATED_SECRET");
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(!done.first()[2].toString().contains("GENERATED_SECRET"));
    QCOMPARE(network.requests.size(), 2);
  }
  void accountFormEncodingRefreshAndSignOut() {
    helios::qa::FakeNetwork network;
    network.responses.enqueue({token("FIRST", 1)});
    network.responses.enqueue({token("SECOND", 600)});
    helios::CopernicusAccount account(nullptr, &network);
    account.signIn("qa@example.test", "a+b&c=d e", "123456");
    QTRY_VERIFY(account.signedIn());
    QVERIFY(!account.busy());
    QVERIFY(network.bodies.first().contains("password=a%2Bb%26c%3Dd%20e"));
    bool received = false;
    account.access([&](const QString &value, const QString &error) {
      QCOMPARE(value, QString("SECOND"));
      QVERIFY(error.isEmpty());
      received = true;
    });
    QTRY_VERIFY(received);
    QCOMPARE(network.requests.size(), 2);
    QVERIFY(network.bodies.last().contains("grant_type=refresh_token"));
    account.signOut();
    QVERIFY(!account.signedIn());
  }
  void signOutAndMalformedTokensNeverReviveAnOldSession() {
    helios::qa::FakeNetwork network;
    network.responses.enqueue({token(), 200, -1, {}, true});
    helios::CopernicusAccount account(nullptr, &network);
    account.signIn("qa@example.test", "GENERATED_SECRET");
    QVERIFY(account.busy());
    account.signOut();
    QTest::qWait(20);
    QVERIFY(!account.signedIn());
    QVERIFY(!account.busy());
    network.responses.enqueue(
        {QJsonDocument(QJsonObject{{"error_description", "GENERATED_SECRET"}}).toJson(), 401});
    QSignalSpy failed(&account, &helios::CopernicusAccount::failed);
    account.signIn("qa@example.test", "GENERATED_SECRET");
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(!account.signedIn());
    QVERIFY(!failed.first().first().toString().contains("GENERATED_SECRET"));
  }
};
QTEST_GUILESS_MAIN(RadarTests)
#include "RadarTests.moc"
