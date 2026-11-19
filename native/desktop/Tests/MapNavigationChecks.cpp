// Opt-in, deterministic hardware benchmark. All imagery, terrain and markers
// are generated fixtures. Times are evidence, never machine-specific pass gates.
#include "Graphics/MapWidget.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif
using namespace helios;
namespace {
QJsonObject distribution(QVector<double> samples) {
  if (samples.isEmpty())
    return {{"count", 0}};
  std::sort(samples.begin(), samples.end());
  const auto percentile = [&](double fraction) {
    return samples[std::clamp(qsizetype(std::ceil(samples.size() * fraction)) - 1, qsizetype(0),
                              samples.size() - 1)];
  };
  double sum = 0;
  for (double value : samples)
    sum += value;
  return {{"count", samples.size()}, {"min", samples.front()}, {"mean", sum / samples.size()},
          {"p50", percentile(.5)},   {"p95", percentile(.95)}, {"p99", percentile(.99)},
          {"max", samples.back()}};
}
double cpuSeconds() {
#if defined(Q_OS_UNIX)
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
         (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
#else
  return -1;
#endif
}
QByteArray digest(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  return hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
class TimedMap : public MapWidget {
public:
  explicit TimedMap(const QString &directory) : MapWidget(directory, false) {
    clock.start();
    connect(this, &MapWidget::statusChanged, this, [this] { ++statusCount; });
  }
  void begin(const QString &name) {
    phase = name;
    renders.clear();
    framesBetween.clear();
    paints.clear();
    updates.clear();
    timerSpacing.clear();
    cameraCalls.clear();
    arrivals.clear();
    picks.clear();
    lastRender = -1;
    frameStart = frames();
    timeStart = clock.nsecsElapsed();
    cpuStart = cpuSeconds();
    firstRender = -1;
    minTiles = 1000000;
    maxTiles = 0;
    statusCount = 0;
  }
  QJsonObject finish() const {
    return {{"phase", phase},
            {"frames", double(frames() - frameStart)},
            {"wallMS", (clock.nsecsElapsed() - timeStart) / 1e6},
            {"cpuMS", (cpuSeconds() - cpuStart) * 1000},
            {"firstRenderMS", firstRender},
            {"renderMS", distribution(renders)},
            {"frameSpacingMS", distribution(framesBetween)},
            {"paintEventMS", distribution(paints)},
            {"updateEventMS", distribution(updates)},
            {"timerSpacingMS", distribution(timerSpacing)},
            {"cameraCallMS", distribution(cameraCalls)},
            {"terrainArrivalMS", distribution(arrivals)},
            {"pickMS", distribution(picks)},
            {"minimumRenderedTiles", minTiles == 1000000 ? 0 : minTiles},
            {"maximumRenderedTiles", maxTiles},
            {"statusNotifications", statusCount}};
  }
  QVector<double> timerSpacing, cameraCalls, arrivals, picks;

protected:
  void render(QRhiCommandBuffer *cb) override {
    const auto start = clock.nsecsElapsed();
    if (lastRender >= 0)
      framesBetween.append((start - lastRender) / 1e6);
    lastRender = start;
    MapWidget::render(cb);
    const auto duration = (clock.nsecsElapsed() - start) / 1e6;
    renders.append(duration);
    if (firstRender < 0)
      firstRender = duration;
    minTiles = std::min(minTiles, renderedTiles());
    maxTiles = std::max(maxTiles, renderedTiles());
  }
  bool event(QEvent *event) override {
    const auto start = clock.nsecsElapsed();
    const auto type = event->type();
    const bool handled = MapWidget::event(event);
    const auto duration = (clock.nsecsElapsed() - start) / 1e6;
    if (type == QEvent::Paint)
      paints.append(duration);
    if (type == QEvent::UpdateRequest)
      updates.append(duration);
    return handled;
  }

private:
  QElapsedTimer clock;
  QString phase;
  QVector<double> renders, framesBetween, paints, updates;
  qint64 lastRender = -1, timeStart = 0;
  quint64 frameStart = 0;
  double cpuStart = 0, firstRender = -1;
  int minTiles = 1000000, maxTiles = 0, statusCount = 0;
};
const Camera baseCamera{{45.4641, 9.1919}, 80000, 12, 25};
Camera pathCamera(int step, int steps, bool rotation) {
  const double angle = double(step) / (steps - 1) * 2 * std::numbers::pi;
  Camera camera = baseCamera;
  camera.center.latitude += .04 * std::sin(angle);
  camera.center.longitude += .05 * std::cos(angle);
  camera.range += 8000 * std::sin(angle);
  if (rotation) {
    camera.heading += 11 * std::sin(angle);
    camera.tilt += 5 * std::cos(angle);
  }
  return camera;
}
QImage imagery(TileID id) {
  QImage image(256, 256, QImage::Format_RGBA8888);
  for (int y = 0; y < 256; ++y) {
    auto *pixels = image.scanLine(y);
    for (int x = 0; x < 256; ++x) {
      const int index = x * 4;
      const bool check = ((x / 32 + y / 32 + id.x + id.y) & 1) != 0;
      pixels[index] = check ? 40 : 90;
      pixels[index + 1] = uchar(70 + (x + id.x) % 90);
      pixels[index + 2] = uchar(50 + (y + id.y) % 80);
      pixels[index + 3] = 255;
    }
  }
  return image;
}
QVector<MapMarker> markers(int count) {
  QVector<MapMarker> result;
  result.reserve(count);
  for (int i = 0; i < count; ++i) {
    const double angle = i * 2.399963229728653;
    const double radius = .22 * std::sqrt((i + .5) / count);
    result.append({QString("generated-%1").arg(i),
                   {baseCamera.center.latitude + radius * std::sin(angle),
                    baseCamera.center.longitude + radius * std::cos(angle)},
                   {}});
  }
  return result;
}
bool nonBlank(const QImage &image) {
  if (image.isNull())
    return false;
  const auto center = image.pixelColor(image.rect().center());
  int different = 0;
  for (int y = 0; y < image.height(); y += std::max(1, image.height() / 15))
    for (int x = 0; x < image.width(); x += std::max(1, image.width() / 15))
      different += image.pixelColor(x, y) != center;
  return different > 5;
}
} // namespace
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("Helios generated navigation benchmark");
  const auto output = qEnvironmentVariable("HELIOS_NAVIGATION_QA");
  if (!qEnvironmentVariableIsSet("HELIOS_NAVIGATION_CHECK") || output.isEmpty()) {
    qWarning("Opt-in: set HELIOS_NAVIGATION_CHECK=1 and HELIOS_NAVIGATION_QA to a QA directory.");
    return 2;
  }
  if (!QDir().mkpath(output))
    return 2;
  QTemporaryDir profile;
  if (!profile.isValid())
    return 2;
  TimedMap map(profile.path() + "/tiles");
  // A tiling compositor can ignore resize(), changing aspect and visible work
  // between runs. Equal min/max constraints keep comparison workload stable.
  map.setFixedSize(1100, 760);
  map.setWindowTitle("GENERATED QA — map navigation timing");
  map.setCamera(baseCamera);
  map.show();
  QElapsedTimer total;
  total.start();
  const double startCPU = cpuSeconds();
  QJsonObject report{{"generated", true},
                     {"networking", false},
                     {"qtVersion", qVersion()},
                     {"startedAtUTC", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                     {"platform", QGuiApplication::platformName()},
                     {"fixtureVersion", 1},
                     {"camera", baseCamera.json()},
                     {"nominalCameraIntervalMS", 16},
                     {"limitations",
                      "Generated 256px map tiles and flat 120m terrain, one native "
                      "window/screen/DPI; no provider requests, MainWindow feed refresh, large "
                      "3D scene, GPU timestamp query, or physical input latency. Render timings "
                      "measure CPU work/command recording; frame/paint timings also expose "
                      "event and compositor pacing. No timing thresholds determine pass/fail."}};
  QJsonArray checks, phases;
  QTimer::singleShot(0, &map, [&] {
    const auto require = [&](bool ok, const QString &label) {
      checks.append(QJsonObject{{"check", label}, {"passed", ok}});
      if (!ok)
        throw std::runtime_error(label.toStdString());
    };
    const auto sweep = [&](const QString &phase, bool rotation) {
      map.begin(phase);
      QEventLoop loop;
      QTimer tick;
      tick.setTimerType(Qt::PreciseTimer);
      tick.setInterval(16);
      QElapsedTimer clock;
      clock.start();
      qint64 lastTick = 0;
      constexpr int count = 90;
      int step = 0;
      QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        const auto now = clock.nsecsElapsed();
        map.timerSpacing.append((now - lastTick) / 1e6);
        lastTick = now;
        QElapsedTimer call;
        call.start();
        map.setCamera(pathCamera(step, count, rotation));
        map.cameraCalls.append(call.nsecsElapsed() / 1e6);
        if (++step == count) {
          tick.stop();
          QTimer::singleShot(16, &loop, &QEventLoop::quit);
        }
      });
      tick.start();
      loop.exec();
      phases.append(map.finish());
      require(step == count && map.error().isEmpty(), phase + " completed 90 camera updates");
    };
    try {
      require(QTest::qWaitForWindowExposed(&map, 5000), "Native window exposed");
      QTest::qWait(150);
      require(map.hardwareGpu() && map.error().isEmpty(), "Actual hardware GPU selected");
      report["backend"] = map.backend();
      report["gpu"] = map.gpu();
      report["devicePixelRatio"] = map.devicePixelRatioF();
      report["windowLogicalSize"] = QJsonObject{{"width", map.width()}, {"height", map.height()}};
      const auto initialFramebuffer = map.grabFramebuffer();
      report["framebufferPixelSize"] = QJsonObject{{"width", initialFramebuffer.width()},
                                                   {"height", initialFramebuffer.height()}};
      auto tiles = visibleTiles(baseCamera, double(map.width()) / map.height());
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
          const TileID tile{x, y, 2};
          if (!tiles.contains(tile))
            tiles.prepend(tile);
        }
      report["generatedImageryTiles"] = tiles.size();
      QJsonArray tileIDs;
      for (auto id : tiles)
        tileIDs.append(id.key());
      report["tileIDs"] = tileIDs;
      map.begin("first-imagery-upload");
      for (auto id : tiles)
        map.tiles().injectImage(id, imagery(id));
      QTest::qWait(180);
      phases.append(map.finish());
      require(map.renderedTiles() == tiles.size() && map.renderedTiles() >= 40,
              "Generated visible imagery and 16 context tiles rendered");
      map.begin("cold-terrain-arrivals");
      const std::vector<float> terrain(256 * 256, 120);
      QEventLoop arrivalLoop;
      QTimer arrivals;
      arrivals.setTimerType(Qt::PreciseTimer);
      arrivals.setInterval(30);
      int next = 0;
      QObject::connect(&arrivals, &QTimer::timeout, &arrivalLoop, [&] {
        for (int count = 0; count < 4 && next < tiles.size(); ++count) {
          QElapsedTimer call;
          call.start();
          map.tiles().injectTerrain(tiles[next++], terrain);
          map.arrivals.append(call.nsecsElapsed() / 1e6);
        }
        if (next == tiles.size()) {
          arrivals.stop();
          QTimer::singleShot(50, &arrivalLoop, &QEventLoop::quit);
        }
      });
      arrivals.start();
      arrivalLoop.exec();
      phases.append(map.finish());
      require(next == tiles.size(), "All deterministic terrain batches arrived");
      require(map.tiles().height(baseCamera.center).value_or(-1) == 120,
              "Generated terrain height admitted");
      sweep("warm-pan-no-markers", false);
      sweep("warm-pan-rotate-no-markers", true);
      for (int count : {300, 3000}) {
        require(map.setMarkers("generated", markers(count)), "Generated marker layer admitted");
        QTest::qWait(80);
        sweep(QString("warm-pan-rotate-%1-markers").arg(count), true);
      }
      map.setCamera(baseCamera);
      QTest::qWait(100);
      map.begin("repeated-five-point-picks");
      const QVector<QPointF> points{{map.width() * .5, map.height() * .5},
                                    {map.width() * .15, map.height() * .15},
                                    {map.width() * .85, map.height() * .15},
                                    {map.width() * .15, map.height() * .85},
                                    {map.width() * .85, map.height() * .85}};
      int validPicks = 0, terrainPicks = 0;
      for (int repeat = 0; repeat < 12; ++repeat)
        for (auto point : points) {
          QElapsedTimer call;
          call.start();
          const auto hit = map.pick(point);
          map.picks.append(call.nsecsElapsed() / 1e6);
          validPicks += hit && hit->coordinate.valid();
          terrainPicks += hit && hit->terrain;
        }
      phases.append(map.finish());
      report["validPicks"] = validPicks;
      report["terrainPicks"] = terrainPicks;
      require(validPicks == 60 && terrainPicks > 0,
              "Center and four inset corners return valid picks");
      const auto screenshot = map.grabFramebuffer();
      require(nonBlank(screenshot) && screenshot.save(output + "/generated-navigation.png"),
              "Native framebuffer remains populated after navigation");
      QTest::qWait(150);
      map.begin("settled-idle");
      QTest::qWait(400);
      phases.append(map.finish());
      require(map.error().isEmpty() && map.tiles().failures() == 0 && map.tiles().pending() == 0,
              "Offline generated run completed without renderer or transport failure");
      report["passed"] = true;
    } catch (const std::exception &error) {
      report["passed"] = false;
      report["failure"] = error.what();
    }
    report["checks"] = checks;
    report["phases"] = phases;
    report["wallSeconds"] = total.elapsed() / 1000.;
    report["cpuSeconds"] = cpuSeconds() - startCPU;
    report["executableSHA256"] =
        QString::fromLatin1(digest(QCoreApplication::applicationFilePath()));
#ifdef HELIOS_SOURCE_ROOT
    QJsonObject sources;
    for (const auto &name :
         {"native/desktop/Tests/MapNavigationChecks.cpp", "native/desktop/Core/Geography.cpp",
          "native/desktop/Core/DailyImagery.cpp", "native/desktop/shaders/globe.vert",
          "native/desktop/shaders/globe.frag"})
      sources[name] = QString::fromLatin1(digest(QString(HELIOS_SOURCE_ROOT) + "/" + name));
#ifdef HELIOS_NAVIGATION_GRAPHICS_ROOT
    const QString graphicsRoot = HELIOS_NAVIGATION_GRAPHICS_ROOT;
    report["frozenGraphicsBaseline"] = true;
#else
    const QString graphicsRoot = QString(HELIOS_SOURCE_ROOT) + "/native/desktop/Graphics";
    report["frozenGraphicsBaseline"] = false;
#endif
    for (const auto &name : {"MapWidget.cpp", "MapWidget.h", "TileCache.cpp", "TileCache.h"})
      sources[QString("native/desktop/Graphics/") + name] =
          QString::fromLatin1(digest(graphicsRoot + "/" + name));
    report["sourceSHA256"] = sources;
#endif
    const auto bytes = QJsonDocument(report).toJson();
    QSaveFile file(output + "/map-navigation-checks.json");
    file.setDirectWriteFallback(false);
    const bool saved =
        file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    app.exit(saved && report["passed"].toBool() ? 0 : 1);
  });
  return app.exec();
}
