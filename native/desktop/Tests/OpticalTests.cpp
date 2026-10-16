#include "Core/OpticalCatalog.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <QUrlQuery>

class OpticalTests : public QObject {
  Q_OBJECT
  QByteArray fixture(const QString &name) {
    QFile file(QString::fromUtf8(GODEYE_FIXTURE_DIR) + "/" + name + ".json");
    if (!file.open(QIODevice::ReadOnly))
      qFatal("Optical fixture unavailable");
    return file.readAll();
  }
private slots:
  void queryKeepsUtcDatesAndRealCloudConstraint() {
    godeye::OpticalQuery query{
        {9.16, 45.43, 9.22, 45.49}, QDate(2026, 9, 1), QDate(2026, 9, 30), 20, "sentinel2"};
    QVERIFY(query.valid());
    const auto url = query.url();
    QCOMPARE(url.host(), QString("earth-search.aws.element84.com"));
    const QUrlQuery parameters(url);
    QCOMPARE(parameters.queryItemValue("datetime"),
             QString("2026-09-01T00:00:00.000Z/2026-09-30T23:59:59.999Z"));
    QCOMPARE(parameters.queryItemValue("collections"), QString("sentinel-2-c1-l2a"));
    query.end = query.start.addDays(367);
    QVERIFY(!query.valid());
    query.end = query.start;
    query.source = "cbers4a";
    QVERIFY(!QUrlQuery(query.url()).hasQueryItem("query"));
    query.region.east = query.region.west;
    QVERIFY(!query.valid());
  }
  void existingSentinelCatalogueAndTrustedAssets() {
    const auto bytes = fixture("sentinel2-catalog");
    const auto scenes = godeye::OpticalCatalog::parse(bytes, "sentinel2");
    QCOMPARE(scenes.size(), 1);
    const auto scene = scenes.first();
    QCOMPARE(scene["id"], QJsonValue("S2C_T32TNR_20260927T102053_L2A"));
    QVERIFY(scene["cloudCover"].toDouble() < 1);
    const auto asset = scene["visualURL"].toString(), id = scene["id"].toString();
    QVERIFY(godeye::OpticalCatalog::validSentinelAsset(asset, id));
    for (const auto &bad : {QString("file:///etc/passwd"), asset + "?key=x",
                            QString(asset).replace("https://", "https://user@"),
                            QString(asset).replace("TCI.tif", "../TCI.tif")})
      QVERIFY(!godeye::OpticalCatalog::validSentinelAsset(bad, id));
    auto altered = bytes;
    altered.replace("e84-earth-search-sentinel-data.s3.us-west-2.amazonaws.com", "localhost");
    QVERIFY(godeye::OpticalCatalog::parse(altered, "sentinel2").isEmpty());
    QVERIFY_EXCEPTION_THROWN(godeye::OpticalCatalog::parse("{}", "sentinel2"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(
        godeye::OpticalCatalog::parse(QByteArray(8 * 1024 * 1024 + 1, ' '), "sentinel2"),
        std::runtime_error);
  }
  void cbersUnknownCloudAndDeterministicArchiveAdmission() {
    const QString id = "CBERS_4A_WPM_20240915_123_456_L4";
    QJsonObject assets;
    for (int i = 0; i < 4; ++i)
      assets["BAND" + QString::number(i)] =
          QJsonObject{{"href", "https://untrusted.example/ignored"}};
    QJsonObject feature{{"id", id},
                        {"collection", "CB4A-WPM-L4-DN-1"},
                        {"bbox", QJsonArray{-47, -24, -46, -23}},
                        {"properties", QJsonObject{{"datetime", "2024-09-15T12:00:00Z"},
                                                   {"eo:cloud_cover", QJsonValue::Null}}},
                        {"assets", assets}};
    const auto scenes = godeye::OpticalCatalog::parse(
        QJsonDocument(QJsonObject{{"features", QJsonArray{feature}}}).toJson(), "cbers4a");
    QCOMPARE(scenes.size(), 1);
    QVERIFY(scenes.first()["cloudCover"].isNull());
    QCOMPARE(scenes.first()["bands"].toObject()["BAND0"].toString(),
             QString("https://brazil-eosats.s3.us-west-2.amazonaws.com/CBERS4A/WPM/123/456/") + id +
                 "/" + id + "_BAND0.tif");
    QVERIFY(godeye::OpticalCatalog::cbersAsset("../escape", "BAND0").isEmpty());
  }
};
QTEST_GUILESS_MAIN(OpticalTests)
#include "OpticalTests.moc"
