#include "Graphics/TileCache.h"
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace helios;
namespace {
std::vector<float> flat(float height) {
  return std::vector<float>(256 * 256, height);
}
std::vector<float> gradient() {
  std::vector<float> samples(256 * 256);
  for (int y = 0; y < 256; ++y)
    for (int x = 0; x < 256; ++x)
      samples[size_t(y * 256 + x)] = float(x + y * 1000 - 100000);
  return samples;
}
double interpolatedGradient(double u, double v) {
  return std::clamp(u * 256 - .5, 0., 255.) +
         1000 * std::clamp(v * 256 - .5, 0., 255.) - 100000;
}
} // namespace
class TileCacheTests : public QObject {
  Q_OBJECT
private slots:
  void missingTerrainHasNoElevation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TileCache cache(directory.path(), false);
    QVERIFY(!cache.height({45, 9}));
    QImage image(256, 256, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    cache.injectImage(TileID::containing({45, 9}, 14), image);
    QVERIFY(!cache.height({45, 9}));
  }
  void bilinearSamplesKeepPixelCentersAndEdges() {
    QTemporaryDir directory;
    TileCache cache(directory.path(), false);
    for (const TileID tile : {TileID{0, 0, 0}, TileID{4, 2, 3}, TileID{8600, 5900, 14}}) {
      cache.injectTerrain(tile, gradient());
      for (const double u : {0., .001, .25, .5, .999})
        for (const double v : {.001, .25, .5, .75, .999}) {
          const auto elevation = cache.height(tile.coordinate(u, v));
          QVERIFY(elevation.has_value());
          QVERIFY2(std::abs(*elevation - interpolatedGradient(u, v)) < .0001,
                   qPrintable(QString("zoom %1, UV %2/%3: %4")
                                  .arg(tile.zoom)
                                  .arg(u)
                                  .arg(v)
                                  .arg(*elevation, 0, 'f', 8)));
        }
    }
  }
  void highestPresentCoveringLevelWins() {
    QTemporaryDir directory;
    TileCache cache(directory.path(), false);
    const TileID detail{8600, 5900, 14};
    const auto point = detail.coordinate(.35, .65);
    const auto parent = TileID::containing(point, 8);
    cache.injectTerrain({0, 0, 0}, flat(-200));
    cache.injectTerrain(parent, flat(600));
    cache.injectTerrain(detail, flat(1300));
    QCOMPARE(cache.height(point).value(), 1300.);
    QCOMPARE(cache.height(TileID{detail.x + 1, detail.y, detail.zoom}.coordinate(.5, .5)).value(),
             600.);
    QCOMPARE(cache.height({-30, -140}).value(), -200.);
    cache.injectTerrain(detail, flat(-30));
    QCOMPARE(cache.height(point).value(), -30.);
    QCOMPARE(cache.height({-30, -140}).value(), -200.);
  }
  void antimeridianAndPolesKeepExistingSampling() {
    QTemporaryDir directory;
    TileCache cache(directory.path(), false);
    cache.injectTerrain({0, 0, 0}, gradient());
    QCOMPARE(cache.height({0, -180}).value(), interpolatedGradient(0, .5));
    QCOMPARE(cache.height({0, 180}).value(), interpolatedGradient(1, .5));
    QCOMPARE(cache.height({90, 0}).value(), interpolatedGradient(.5, 0));
    QCOMPARE(cache.height({-90, 0}).value(), interpolatedGradient(.5, 1));
    cache.injectTerrain({0, 1, 1}, gradient());
    // At finer levels both sides of the antimeridian map to the wrapped tile's
    // west edge. Zoom zero retains its existing east-edge convention above.
    QCOMPARE(cache.height({0, -180}).value(), interpolatedGradient(0, 0));
    QCOMPARE(cache.height({0, 180}).value(), interpolatedGradient(0, 0));
  }
  void sameTileImageryDoesNotReplaceTerrain() {
    QTemporaryDir directory;
    TileCache cache(directory.path(), false);
    const TileID tile{8600, 5900, 14};
    const auto point = tile.coordinate(.5, .5);
    cache.injectTerrain(tile, flat(123));
    QImage image(256, 256, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    cache.injectImage(tile, image);
    cache.injectImage(tile, image, true);
    QCOMPARE(cache.cached(), 3);
    QCOMPARE(cache.height(point).value(), 123.);
    cache.injectTerrain(tile, flat(456));
    QCOMPARE(cache.cached(), 3);
    QCOMPARE(cache.height(point).value(), 456.);
  }
  void evictionRemovesIndexAndFallsBackToRetainedParent() {
    QTemporaryDir directory;
    TileCache cache(directory.path(), false);
    const TileID world{0, 0, 0}, detail{8600, 5900, 14};
    const auto point = detail.coordinate(.5, .5);
    cache.request({world}, false, true);
    cache.injectTerrain(world, flat(15));
    cache.injectTerrain(detail, flat(800));
    QCOMPARE(cache.height(point).value(), 800.);
    QImage image(256, 256, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    for (int x = 0; x < 224; ++x)
      cache.injectImage({x, 0, 8}, image);
    QCOMPARE(cache.cached(), 224);
    QCOMPARE(cache.height(point).value(), 15.);
    cache.request({}, false, false);
    for (int x = 0; x < 224; ++x)
      cache.injectImage({x, 1, 8}, image);
    QCOMPARE(cache.cached(), 224);
    QVERIFY(!cache.height(point));
    cache.injectTerrain(detail, flat(900));
    QCOMPARE(cache.cached(), 224);
    QCOMPARE(cache.height(point).value(), 900.);
  }
  void terrainInjectionAlsoHonoursCacheBound() {
    QTemporaryDir directory;
    TileCache cache(directory.path(), false);
    const auto samples = flat(100);
    for (int x = 0; x < 225; ++x)
      cache.injectTerrain({x, 0, 8}, samples);
    QCOMPARE(cache.cached(), 224);
    QVERIFY(!cache.height(TileID{0, 0, 8}.coordinate(.5, .5)));
    QCOMPARE(cache.height(TileID{224, 0, 8}.coordinate(.5, .5)).value(), 100.);
  }
  void invalidSamplesAndCoordinatesAreRejected() {
    QTemporaryDir directory;
    TileCache cache(directory.path(), false);
    const TileID world{0, 0, 0};
    cache.injectTerrain(world, flat(25));
    QVERIFY_EXCEPTION_THROWN(cache.injectTerrain(world, {1}), std::runtime_error);
    auto samples = flat(25);
    samples[123] = std::numeric_limits<float>::quiet_NaN();
    QVERIFY_EXCEPTION_THROWN(cache.injectTerrain(world, samples), std::runtime_error);
    QCOMPARE(cache.height({0, 0}).value(), 25.);
    QVERIFY(!cache.height({91, 0}));
    QVERIFY(!cache.height({0, 181}));
    QVERIFY(!cache.height({std::numeric_limits<double>::quiet_NaN(), 0}));
  }
};
QTEST_GUILESS_MAIN(TileCacheTests)
#include "TileCacheTests.moc"
