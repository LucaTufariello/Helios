#include "Core/DailyImagery.h"
#include "FakeNetwork.h"
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkDiskCache>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace helios;
using namespace helios::qa;
namespace {
QByteArray encoded(QSize size = {256, 256}, const char *format = "PNG", QColor color = Qt::red) {
  QImage image(size, QImage::Format_RGBA8888);
  image.fill(color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, format))
    return {};
  return bytes;
}
} // namespace
class DailyImageryTests : public QObject {
  Q_OBJECT
private slots:
  void urlSourcesDatesAndZoomAdmission() {
    const QDate date(2026, 10, 12);
    const auto trueColor = DailyImageryCache::url(DailySource::TrueColor, date, {134, 91, 8});
    QCOMPARE(trueColor.scheme(), "https");
    QCOMPARE(trueColor.host(), "gibs.earthdata.nasa.gov");
    QCOMPARE(trueColor.path(), "/wmts/epsg3857/best/VIIRS_SNPP_CorrectedReflectance_TrueColor/"
                               "default/2026-10-12/GoogleMapsCompatible_Level9/8/91/134.jpg");
    QVERIFY(DailyImageryCache::url(DailySource::Terra, date, {1, 1, 1})
                .path()
                .contains("MODIS_Terra_CorrectedReflectance_TrueColor"));
    QVERIFY(
        DailyImageryCache::url(DailySource::NightLights, date, {1, 1, 1}).path().endsWith(".png"));
    QVERIFY(DailyImageryCache::url(DailySource::Thermal, date, {1, 1, 1})
                .query()
                .contains("Thermal_Anomalies_All"));
    QCOMPARE(DailyImageryCache::maximumZoom(DailySource::NightLights), 8);
    QCOMPARE(DailyImageryCache::maximumZoom(DailySource::Thermal), 7);
    for (auto source : {DailySource::Off, static_cast<DailySource>(99)})
      QVERIFY(DailyImageryCache::url(source, date, {0, 0, 0}).isEmpty());
    for (auto invalid : {QDate{}, QDate(2023, 2, 29), QDate(1999, 12, 31), QDate(2099, 1, 1)})
      QVERIFY(DailyImageryCache::url(DailySource::Terra, invalid, {0, 0, 0}).isEmpty());
    for (auto tile : {TileID{-1, 0, 0}, TileID{512, 0, 9}, TileID{0, 0, 10}, TileID{0, 0, -1}})
      QVERIFY(DailyImageryCache::url(DailySource::Terra, date, tile).isEmpty());
    QCOMPARE(DailyImageryCache::readSource("nightLights"), std::optional(DailySource::NightLights));
    QVERIFY(!DailyImageryCache::readSource("../../private"));
  }
  void boundedImageDecoder() {
    const auto png = encoded();
    QVERIFY(!png.isEmpty());
    QCOMPARE(DailyImageryCache::decode(png).size(), QSize(256, 256));
    QCOMPARE(DailyImageryCache::decode(encoded({256, 256}, "JPEG")).format(),
             QImage::Format_RGBA8888);
    QVERIFY(DailyImageryCache::decode(encoded({255, 256})).isNull());
    QVERIFY(DailyImageryCache::decode(encoded({256, 256}, "BMP")).isNull());
    QVERIFY(DailyImageryCache::decode("<ServiceException>Unavailable date</ServiceException>")
                .isNull());
    QVERIFY(DailyImageryCache::decode(QByteArray(2 * 1024 * 1024 + 1, '\0')).isNull());
    auto transparent = DailyImageryCache::decode(encoded({256, 256}, "PNG", Qt::transparent));
    QVERIFY(!transparent.isNull());
    QCOMPARE(transparent.pixelColor(0, 0).alpha(), 0);
  }
  void defaultOffOfflineAndDiskBudget() {
    FakeNetwork network;
    QTemporaryDir directory;
    DailyImageryCache cache(directory.path(), true, nullptr, &network);
    cache.request({{0, 0, 0}});
    QVERIFY(!cache.busy());
    QCOMPARE(network.requests.size(), 0);
    DailyImageryCache offline(directory.path(), false, nullptr, &network);
    QVERIFY(offline.configure(DailySource::Terra, QDate(2026, 10, 12)));
    offline.request({{0, 0, 0}});
    QVERIFY(!offline.busy());
    QVERIFY(!offline.image({0, 0, 0}));
    QCOMPARE(network.requests.size(), 0);
    DailyImageryCache disk(directory.path(), true);
    auto *storage = disk.findChild<QNetworkDiskCache *>();
    QVERIFY(storage);
    QCOMPARE(storage->maximumCacheSize(), 128ll * 1024 * 1024);
  }
  void parentReuseAndStaleGenerationCancellation() {
    FakeNetwork network;
    QTemporaryDir directory;
    DailyImageryCache cache(directory.path(), true, nullptr, &network);
    network.responses.enqueue({encoded()});
    network.responses.enqueue({{}, 200, -1, {}, true});
    network.responses.enqueue({{}, 200, -1, {}, true});
    QVERIFY(cache.configure(DailySource::TrueColor, QDate(2026, 10, 12)));
    cache.request({{2, 2, 2}});
    QTRY_COMPARE(cache.cached(), 1);
    auto parent = cache.image({2, 2, 2});
    QVERIFY(parent);
    QCOMPARE(parent->id.zoom, 0);
    QCOMPARE(parent->image.pixelColor(0, 0), QColor(Qt::red));
    const auto generation = cache.generation();
    QVERIFY(cache.configure(DailySource::Terra, QDate(2026, 10, 11)));
    QVERIFY(cache.generation() > generation);
    QVERIFY(!cache.image({2, 2, 2}));
    QCOMPARE(cache.failures(), 0);
    network.responses.enqueue({encoded({256, 256}, "PNG", Qt::blue)});
    cache.request({{0, 0, 0}});
    QTRY_VERIFY(!cache.busy());
    QCOMPARE(cache.cached(), 1);
    QCOMPARE(cache.image({2, 2, 2})->image.pixelColor(0, 0), QColor(Qt::blue));
    QVERIFY(!cache.configure(DailySource::Terra, QDate{}));
    QCOMPARE(cache.date(), QDate(2026, 10, 11));
    QVERIFY(cache.configure(DailySource::Off, {}));
    QVERIFY(!cache.image({0, 0, 0}));
    QVERIFY(!cache.busy());
  }
  void fetchConcurrencyAndViewportCancellation() {
    FakeNetwork network;
    QTemporaryDir directory;
    DailyImageryCache cache(directory.path(), true, nullptr, &network);
    for (int i = 0; i < 20; ++i)
      network.responses.enqueue({{}, 200, -1, {}, true});
    QVERIFY(cache.configure(DailySource::Thermal, QDate(2026, 10, 12)));
    cache.request({{0, 0, 4}, {15, 0, 4}, {0, 15, 4}, {15, 15, 4}});
    QCOMPARE(network.requests.size(), 4);
    QVERIFY(cache.pending() > 4);
    cache.request({});
    QTRY_VERIFY(!cache.busy());
    QCOMPARE(cache.failures(), 0);
    QCOMPARE(network.requests.size(), 4);
  }
  void generationChangeWhileDecodeCompletes() {
    FakeNetwork network;
    QTemporaryDir directory;
    DailyImageryCache cache(directory.path(), true, nullptr, &network);
    network.responses.enqueue({encoded()});
    QVERIFY(cache.configure(DailySource::TrueColor, QDate(2026, 10, 12)));
    cache.request({{0, 0, 0}});
    auto *first = network.findChild<QNetworkReply *>();
    QVERIFY(first);
    connect(first, &QNetworkReply::finished, &cache, [&] {
      QVERIFY(cache.configure(DailySource::Terra, QDate(2026, 10, 11)));
      network.responses.enqueue({encoded({256, 256}, "PNG", Qt::blue)});
      cache.request({{0, 0, 0}});
    });
    QTRY_VERIFY(!cache.busy());
    QCOMPARE(cache.cached(), 1);
    QCOMPARE(cache.image({0, 0, 0})->image.pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(cache.failures(), 0);
  }
  void failuresRedirectCapsAndBackoff() {
    for (auto spec : {ReplySpec{encoded(), 302, -1, QUrl("https://other.invalid/private")},
                      ReplySpec{encoded(), 200, 3 * 1024 * 1024}, ReplySpec{"not an image", 200},
                      ReplySpec{encoded(), 404}}) {
      FakeNetwork network;
      QTemporaryDir directory;
      DailyImageryCache cache(directory.path(), true, nullptr, &network);
      network.responses.enqueue(spec);
      QVERIFY(cache.configure(DailySource::Terra, QDate(2026, 10, 12)));
      cache.request({{0, 0, 0}});
      QTRY_VERIFY(!cache.busy());
      QCOMPARE(cache.failures(), 1);
      QVERIFY(!cache.image({0, 0, 0}));
      cache.request({{0, 0, 0}});
      QCOMPARE(network.requests.size(), 1);
    }
  }
  void cacheBudgetAndHighZoomParentCap() {
    FakeNetwork network;
    QTemporaryDir directory;
    DailyImageryCache cache(directory.path(), true, nullptr, &network);
    for (int i = 0; i < 300; ++i)
      network.responses.enqueue({encoded()});
    QVERIFY(cache.configure(DailySource::NightLights, QDate(2026, 10, 12)));
    QVector<TileID> wanted;
    for (int i = 0; i < 64; ++i)
      wanted.append({i * 8, (i * 7) % 512, 9});
    cache.request(wanted);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.busy(), 10000);
    QVERIFY(cache.cached() <= 128);
    QCOMPARE(cache.failures(), 0);
    for (const auto &request : network.requests)
      QVERIFY(request.url().path().contains("GoogleMapsCompatible_Level8/"));
    const auto calls = network.requests.size();
    cache.request({{0, 0, 19}});
    QTRY_VERIFY_WITH_TIMEOUT(!cache.busy(), 10000);
    for (const auto &request : network.requests.sliced(calls))
      QVERIFY(!request.url().path().contains("/19/"));
  }
  void publicTileOptIn() {
    if (!qEnvironmentVariableIsSet("HELIOS_DAILY_LIVE"))
      QSKIP("Set HELIOS_DAILY_LIVE for an actual NASA GIBS visualization tile check.");
    QTemporaryDir directory;
    DailyImageryCache cache(directory.path(), true);
    QVERIFY(cache.configure(DailySource::Terra, QDate(2026, 10, 12)));
    const auto tile = TileID::containing({45.46, 9.18}, 6);
    cache.request({tile});
    QTRY_VERIFY_WITH_TIMEOUT(!cache.busy(), 45000);
    auto value = cache.image(tile);
    QVERIFY(value);
    QCOMPARE(cache.failures(), 0);
    QCOMPARE(value->id.zoom, 6);
    const auto qa = qEnvironmentVariable("HELIOS_DAILY_QA");
    if (!qa.isEmpty()) {
      QVERIFY(QDir().mkpath(qa));
      QVERIFY(value->image.save(qa + "/nasa-terra-20261012-z6.png"));
      QFile file(qa + "/daily-imagery.json");
      QVERIFY(file.open(QIODevice::WriteOnly));
      QJsonObject report{
          {"provider", "NASA GIBS"},
          {"layer", "MODIS_Terra_CorrectedReflectance_TrueColor"},
          {"requestedUTCDate", "2026-10-12"},
          {"url", DailyImageryCache::url(DailySource::Terra, cache.date(), tile).toString()},
          {"tile", tile.key()},
          {"decodedWidth", value->image.width()},
          {"decodedHeight", value->image.height()},
          {"cachedTiles", cache.cached()},
          {"failedTiles", cache.failures()},
          {"disclosure",
           "Daily visualization date; not per-pixel acquisition time or a scientific product"}};
      const auto bytes = QJsonDocument(report).toJson();
      QCOMPARE(file.write(bytes), bytes.size());
    }
  }
  void otherPublicSourcesOptIn() {
    if (!qEnvironmentVariableIsSet("HELIOS_DAILY_LIVE"))
      QSKIP("Set HELIOS_DAILY_LIVE for all other public NASA visualization sources.");
    QTemporaryDir directory;
    DailyImageryCache cache(directory.path(), true);
    const auto tile = TileID::containing({45.46, 9.18}, 6);
    QJsonArray reports;
    for (auto source : {DailySource::TrueColor, DailySource::NightLights, DailySource::Thermal}) {
      const auto selectedDate =
          source == DailySource::NightLights ? QDate(2023, 7, 7) : QDate(2026, 10, 12);
      QVERIFY(cache.configure(source, selectedDate));
      cache.request({tile});
      QTRY_VERIFY_WITH_TIMEOUT(!cache.busy(), 45000);
      auto value = cache.image(tile);
      QVERIFY2(value, qPrintable(DailyImageryCache::sourceID(source)));
      QCOMPARE(cache.failures(), 0);
      QCOMPARE(value->id.zoom, 6);
      int transparent = 0;
      for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
          if (value->image.pixelColor(x, y).alpha() == 0)
            ++transparent;
      reports.append(
          QJsonObject{{"source", DailyImageryCache::sourceID(source)},
                      {"url", DailyImageryCache::url(source, cache.date(), tile).toString()},
                      {"tile", tile.key()},
                      {"requestedUTCDate", selectedDate.toString(Qt::ISODate)},
                      {"cachedTiles", cache.cached()},
                      {"failedTiles", cache.failures()},
                      {"transparentPixels", transparent},
                      {"totalPixels", 256 * 256}});
      const auto qa = qEnvironmentVariable("HELIOS_DAILY_QA");
      if (!qa.isEmpty()) {
        QVERIFY(QDir().mkpath(qa));
        QVERIFY(value->image.save(qa + "/nasa-" + DailyImageryCache::sourceID(source) + "-" +
                                  selectedDate.toString("yyyyMMdd") + "-z6.png"));
      }
    }
    const auto qa = qEnvironmentVariable("HELIOS_DAILY_QA");
    if (!qa.isEmpty()) {
      QFile file(qa + "/daily-other-sources.json");
      QVERIFY(file.open(QIODevice::WriteOnly));
      const auto bytes = QJsonDocument(reports).toJson();
      QCOMPARE(file.write(bytes), bytes.size());
    }
  }
};
QTEST_GUILESS_MAIN(DailyImageryTests)
#include "DailyImageryTests.moc"
