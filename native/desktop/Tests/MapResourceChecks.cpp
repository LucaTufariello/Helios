// Opt-in native hardware/resource scenario. All scene/overlay pixels are
// generated QA fixtures; no provider acquisition or scientific finding exists.
#include "Graphics/MapWidget.h"
#include "SceneFixture.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sys/resource.h>
using namespace helios;
namespace {
class AuditedMapWidget : public MapWidget {
public:
  AuditedMapWidget(const QString &directory, bool networking) : MapWidget(directory, networking) {
    connect(
        this, &MapWidget::statusChanged, this,
        [this] {
          ++statusNotifications_;
          if (callbackDepth_ > 0)
            ++statusInsideGpuCallbacks_;
        },
        Qt::DirectConnection);
  }
  int statusNotifications() const {
    return statusNotifications_;
  }
  int statusInsideGpuCallbacks() const {
    return statusInsideGpuCallbacks_;
  }
  int maximumGpuCallbackDepth() const {
    return maximumCallbackDepth_;
  }

protected:
  void initialize(QRhiCommandBuffer *cb) override {
    QScopedValueRollback<int> active(callbackDepth_, callbackDepth_ + 1);
    maximumCallbackDepth_ = std::max(maximumCallbackDepth_, callbackDepth_);
    MapWidget::initialize(cb);
  }
  void render(QRhiCommandBuffer *cb) override {
    QScopedValueRollback<int> active(callbackDepth_, callbackDepth_ + 1);
    maximumCallbackDepth_ = std::max(maximumCallbackDepth_, callbackDepth_);
    MapWidget::render(cb);
  }

private:
  int callbackDepth_ = 0, maximumCallbackDepth_ = 0;
  int statusNotifications_ = 0, statusInsideGpuCallbacks_ = 0;
};
double cpuSeconds() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
         (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.;
}
qint64 rssBytes() {
#if defined(Q_OS_LINUX)
  QFile file("/proc/self/status");
  if (file.open(QIODevice::ReadOnly))
    for (const auto &line : file.readAll().split('\n'))
      if (line.startsWith("VmRSS:"))
        return line.simplified().split(' ').value(1).toLongLong() * 1024;
#endif
  return -1;
}
qint64 peakBytes() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#if defined(Q_OS_MACOS)
  return usage.ru_maxrss;
#else
  return usage.ru_maxrss * 1024;
#endif
}
QJsonObject nvidia() {
  QProcess process;
  process.start("nvidia-smi",
                {"--query-gpu=name,memory.used,utilization.gpu", "--format=csv,noheader,nounits"});
  if (!process.waitForFinished(3000) || process.exitCode() != 0)
    return {{"available", false}, {"scope", "Optional NVIDIA device-wide counters unavailable"}};
  return {{"available", true},
          {"scope",
           "Device-wide counters include desktop and other applications; not per-process GPU time"},
          {"columns", "name,usedVRAMMiB,gpuUtilizationPercent"},
          {"samples", QString::fromUtf8(process.readAllStandardOutput()).trimmed()}};
}
QByteArray digest(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  return hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
bool nonBlank(const QImage &image) {
  if (image.isNull())
    return false;
  const auto first = image.pixelColor(image.rect().center());
  int distinct = 0;
  for (int y = 0; y < image.height(); y += std::max(1, image.height() / 20))
    for (int x = 0; x < image.width(); x += std::max(1, image.width() / 20))
      if (image.pixelColor(x, y) != first)
        ++distinct;
  return distinct > 5;
}
ProductLayer overlay(const QString &id, QColor a, QColor b) {
  ProductLayer layer;
  layer.id = id;
  layer.jobID = id;
  layer.kind = "optical";
  layer.label = "GENERATED QA " + id;
  layer.bounds = {-.003, -.003, .003, .003};
  layer.pixelSize = {2048, 2048};
  layer.rasterSize = layer.pixelSize;
  layer.image = QImage(layer.pixelSize, QImage::Format_RGBA8888);
  for (int y = 0; y < 2048; ++y)
    for (int x = 0; x < 2048; ++x)
      layer.image.setPixelColor(x, y, ((x / 256 + y / 256) % 2) ? a : b);
  return layer;
}
} // namespace
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("Helios generated map resource QA");
  const auto output = qEnvironmentVariable("HELIOS_RESOURCE_QA");
  if (!qEnvironmentVariableIsSet("HELIOS_RESOURCE_CHECK") || output.isEmpty()) {
    qWarning(
        "Opt-in only: set HELIOS_RESOURCE_CHECK=1 and HELIOS_RESOURCE_QA to a new QA directory.");
    return 2;
  }
  if (!QDir().mkpath(output))
    return 2;
  QTemporaryDir profile;
  if (!profile.isValid())
    return 2;
  AuditedMapWidget map(profile.path() + "/tiles", false);
  map.resize(1000, 700);
  map.setWindowTitle("GENERATED QA — scene, two overlays and resource travel");
  map.show();
  QJsonObject report{
      {"generated", true},
      {"scenario", "Three repeated near/far/dateline/tilt travel cycles with generated GLB scene "
                   "and two generated optical overlays, followed by flat 1234 m terrain, "
                   "dateline/corner and transparent-product picking checks; no scientific data"},
      {"startedAtUTC", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {"qtVersion", qVersion()},
      {"platform", QGuiApplication::platformName()},
      {"networking", false},
      {"limitations", "Single actual screen/DPI; Qt minimize/restore only. NVIDIA metrics are "
                      "device-wide. Small generated scene/2048px overlays do not benchmark a large "
                      "photogrammetry site or scientific detector."}};
  QJsonArray checks, samples;
  QElapsedTimer elapsed;
  elapsed.start();
  const auto initialCPU = cpuSeconds();
  QTimer::singleShot(200, &map, [&] {
    const auto require = [&](bool condition, const QString &label) {
      checks.append(QJsonObject{{"check", label}, {"passed", condition}});
      if (!condition)
        throw std::runtime_error(label.toStdString());
    };
    const auto sample = [&](const QString &stage, bool device = false) {
      QJsonObject value{{"stage", stage},
                        {"elapsedSeconds", elapsed.elapsed() / 1000.},
                        {"cpuSeconds", cpuSeconds() - initialCPU},
                        {"rssBytes", rssBytes()},
                        {"peakRSSBytes", peakBytes()},
                        {"frames", double(map.frames())},
                        {"renderedBasemapTiles", map.renderedTiles()},
                        {"retainedTileCache", map.tiles().cached()},
                        {"pendingTiles", map.tiles().pending()},
                        {"sceneMeshes", map.sceneMeshes()},
                        {"overlayCount", map.overlayIDs().size()},
                        {"camera", map.camera().json()},
                        {"sceneStatus", map.sceneStatus()}};
      if (device)
        value["nvidiaDeviceWide"] = nvidia();
      samples.append(value);
    };
    try {
      require(QTest::qWaitForWindowExposed(&map, 5000), "Native window exposed");
      require(map.error().isEmpty() && map.hardwareGpu(), "Actual hardware GPU selected");
      require(map.backend() == "Vulkan" || map.backend() == "Metal" || map.backend() == "D3D11",
              "Native GPU backend selected");
      require(map.statusNotifications() > 0 && map.statusInsideGpuCallbacks() == 0,
              "Initial GPU status is delivered outside initialize/render callbacks");
      report["backend"] = map.backend();
      report["gpu"] = map.gpu();
      report["devicePixelRatio"] = map.devicePixelRatioF();
      sample("startup", true);
      map.setCamera(Camera{{0, 0}, 50000000, 0, 0});
      QTest::qWait(150);
      std::optional<MapMarker> offGlobeMarker;
      QPointF offGlobeScreen;
      for (double altitude : {1000000., 5000000., 10000000., 20000000.}) {
        for (int latitude = -75; latitude <= 75 && !offGlobeMarker; latitude += 15)
          for (int longitude = -180; longitude < 180 && !offGlobeMarker; longitude += 15) {
            const Coordinate coordinate{double(latitude), double(longitude)};
            const auto screen = map.screenPoint(coordinate, altitude);
            if (!screen || !map.rect().adjusted(20, 20, -20, -20).contains(screen->toPoint()))
              continue;
            const double x = screen->x() / map.width() * 2 - 1,
                         y = 1 - screen->y() / map.height() * 2;
            if (ellipsoidPick(map.camera(), x, y, double(map.width()) / map.height()))
              continue;
            offGlobeMarker = MapMarker{"GENERATED_QA_SPACE_MARKER", coordinate, altitude};
            offGlobeScreen = *screen;
          }
        if (offGlobeMarker)
          break;
      }
      require(offGlobeMarker.has_value(),
              "Visible altitude marker found on a ray outside the ellipsoid");
      require(map.setMarkers("resource-space", {*offGlobeMarker}),
              "Generated altitude marker admitted");
      QTest::qWait(100);
      QSignalSpy markerSelected(&map, &MapWidget::markerSelected);
      QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier, offGlobeScreen.toPoint());
      require(markerSelected.size() == 1 &&
                  markerSelected.first().first().toString() == offGlobeMarker->id,
              "Native click selects an off-globe projected altitude marker");
      report["offGlobeMarker"] = QJsonObject{{"coordinate", offGlobeMarker->coordinate.json()},
                                             {"altitudeMetres", *offGlobeMarker->altitude},
                                             {"screenX", offGlobeScreen.x()},
                                             {"screenY", offGlobeScreen.y()}};
      map.setMarkers("resource-space", {});
      const auto scene =
          qa::sceneFixture(profile.path() + "/generated-scene", "roof.glb", 4, "OPAQUE", 1, true);
      report["generatedSceneSHA256"] = QString::fromLatin1(digest(scene));
      report["generatedGlbSHA256"] =
          QString::fromLatin1(digest(QFileInfo(scene).absolutePath() + "/roof.glb"));
      auto first = overlay("GENERATED_QA_A", QColor(220, 40, 20), QColor(250, 190, 20));
      auto second = overlay("GENERATED_QA_B", QColor(20, 60, 220), QColor(20, 200, 220));
      first.image.save(output + "/GENERATED_QA_A.png");
      second.image.save(output + "/GENERATED_QA_B.png");
      report["overlayPixels"] = QJsonArray{2048, 2048};
      report["overlayASHA256"] = QString::fromLatin1(digest(output + "/GENERATED_QA_A.png"));
      report["overlayBSHA256"] = QString::fromLatin1(digest(output + "/GENERATED_QA_B.png"));
      map.setOverlays({first, second});
      first.image = {};
      second.image = {};
      map.setBlend(.5);
      map.setCamera(Camera{{0, 0}, 2000, 0, 0});
      require(!map.loadTileset(QUrl("https://example.invalid/tileset.json")) &&
                  map.sceneStatus().isEmpty() && map.error().contains("Offline mode"),
              "Offline mode rejects remote 3D Tiles before creating a scene");
      require(map.loadTileset(QUrl::fromLocalFile(scene)), "Generated scene admitted");
      require(map.error().isEmpty(), "Local tileset remains available after offline rejection");
      QElapsedTimer waiting;
      waiting.start();
      while (map.sceneMeshes() == 0 && waiting.elapsed() < 10000)
        QTest::qWait(30);
      require(map.sceneMeshes() == 1 && map.overlayIDs().size() == 2,
              "Generated scene and two overlays loaded simultaneously");
      map.setCamera(Camera{{0, 0}, 500, 0, 0});
      const Coordinate groundCoordinate{.001, .001};
      require(
          map.setMarkers("resource-ground", {{"GENERATED_QA_GROUND_MARKER", groundCoordinate, {}}}),
          "Ordinary generated marker admitted for point capture");
      QTest::qWait(150);
      const auto groundScreen = map.screenPoint(groundCoordinate);
      require(groundScreen.has_value() && map.pick(*groundScreen).has_value(),
              "Ordinary marker location has a rendered ground hit");
      QObject captureOwner;
      map.beginPointCapture(&captureOwner);
      QSignalSpy pointSelected(&map, &MapWidget::pointSelected);
      markerSelected.clear();
      QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier, groundScreen->toPoint());
      require(pointSelected.size() == 1 && markerSelected.isEmpty(),
              "Owned point capture takes priority over an ordinary map marker");
      map.endPointCapture(&captureOwner);
      map.setMarkers("resource-ground", {});
      sample("loaded", true);
      qint64 firstCycleRSS = -1;
      const QVector<Camera> travel{
          {{0, 0}, 2000, 0, 0},           {{0, 0}, 50000000, 0, 0},
          {{15, 179.98}, 500000, 70, 55}, {{-15, -179.98}, 500000, 250, 65},
          {{60, 80}, 40000, 120, 75},     {{0, 0}, 500, 0, 0}};
      for (int cycle = 0; cycle < 3; ++cycle) {
        const auto cycleCPU = cpuSeconds();
        const auto cycleStarted = elapsed.elapsed();
        const auto frames = map.frames();
        for (int i = 0; i < travel.size(); ++i) {
          map.setCamera(travel[i]);
          QTest::qWait(180);
          require(map.error().isEmpty(),
                  QString("Cycle %1 travel %2 GPU error-free").arg(cycle + 1).arg(i + 1));
          const auto image = map.grabFramebuffer();
          require(nonBlank(image),
                  QString("Cycle %1 travel %2 nonblank GPU framebuffer").arg(cycle + 1).arg(i + 1));
          sample(QString("cycle-%1-travel-%2").arg(cycle + 1).arg(i + 1));
          if (i == 1 || i == travel.size() - 1)
            require(
                image.save(output +
                           QString("/cycle-%1-%2.png").arg(cycle + 1).arg(i == 1 ? "far" : "near")),
                "GPU travel screenshot retained");
        }
        require(map.frames() > frames,
                QString("Cycle %1 renders navigation frames").arg(cycle + 1));
        report[QString("cycle%1ActiveCPUSeconds").arg(cycle + 1)] = cpuSeconds() - cycleCPU;
        report[QString("cycle%1ActiveWallSeconds").arg(cycle + 1)] =
            (elapsed.elapsed() - cycleStarted) / 1000.;
        require(map.overlayIDs().size() == 2 && map.sceneMeshes() == 1,
                QString("Cycle %1 returns to scene plus both overlays").arg(cycle + 1));
        const auto hit = map.pick(map.rect().center());
        require(hit && hit->scene && std::abs(hit->height - 100) < .1,
                "Generated roof picking remains correct after travel");
        QTest::qWait(800);
        const auto idleFrames = map.frames();
        const auto idleCPU = cpuSeconds();
        const auto idleStarted = elapsed.elapsed();
        QTest::qWait(1000);
        const auto idleDuration = (elapsed.elapsed() - idleStarted) / 1000.;
        report[QString("cycle%1IdleCPUSeconds").arg(cycle + 1)] = cpuSeconds() - idleCPU;
        report[QString("cycle%1IdleWallSeconds").arg(cycle + 1)] = idleDuration;
        report[QString("cycle%1IdleFrames").arg(cycle + 1)] = double(map.frames() - idleFrames);
        require(map.frames() == idleFrames,
                QString("Cycle %1 stops rendering while idle").arg(cycle + 1));
        map.showMinimized();
        QTest::qWait(180);
        const bool minimized = map.isMinimized();
        report[QString("cycle%1MinimizeAcknowledged").arg(cycle + 1)] = minimized;
        if (minimized) {
          require(true, "Compositor acknowledges Qt minimized state");
        } else {
          checks.append(
              QJsonObject{{"check", "Native minimized state"},
                          {"passed", QJsonValue::Null},
                          {"skipped", true},
                          {"reason", "This compositor did not acknowledge the Qt minimize request; "
                                     "native hide/show is tested instead."}});
          map.hide();
          QTest::qWait(100);
          report["minimizeLimitation"] =
              "Compositor did not acknowledge Qt minimization. Actual minimize/restore remains "
              "unverified; hide/show target restoration was exercised.";
        }
        map.showNormal();
        QTest::qWait(250);
        require(!map.isMinimized() && map.error().isEmpty() && nonBlank(map.grabFramebuffer()),
                minimized ? "Native GPU target survives minimize/restore"
                          : "Native GPU target survives hide/show restoration");
        sample(QString("cycle-%1-complete").arg(cycle + 1), true);
        require(
            map.statusInsideGpuCallbacks() == 0 && map.maximumGpuCallbackDepth() == 1,
            QString("Cycle %1 status notifications do not reenter GPU callbacks").arg(cycle + 1));
        if (cycle == 0)
          firstCycleRSS = rssBytes();
      }
      if (firstCycleRSS > 0 && rssBytes() > 0) {
        report["rssGrowthAfterFirstCycleBytes"] = rssBytes() - firstCycleRSS;
        require(rssBytes() <= firstCycleRSS + 96ll * 1024 * 1024,
                "Repeated identical cycles stay within a 96 MiB RSS growth allowance");
      }
      map.setOverlays({});
      map.removeTileset();
      QTest::qWait(180);
      require(map.overlayIDs().isEmpty() && map.sceneMeshes() == 0,
              "Scene and both overlays removed from rendered set");
      sample("removed", true);
      // Known flat elevations exercise the picking broad phase at tile edges,
      // wrapped longitude and high latitudes without a network/provider oracle.
      constexpr double flatElevation = 1234;
      map.setTerrain(true);
      map.tiles().injectTerrain({0, 0, 0}, std::vector<float>(256 * 256, flatElevation));
      QImage basemap(256, 256, QImage::Format_RGBA8888);
      basemap.fill(QColor(40, 100, 140));
      const auto loadPickView = [&](const Camera &camera) {
        map.setCamera(camera);
        auto tiles = visibleTiles(camera, double(map.width()) / std::max(1, map.height()));
        for (int y = 0; y < 4; ++y)
          for (int x = 0; x < 4; ++x)
            if (!tiles.contains(TileID{x, y, 2}))
              tiles.append({x, y, 2});
        for (const auto &id : tiles)
          map.tiles().injectImage(id, basemap);
        QTest::qWait(220);
        require(!map.grabFramebuffer().isNull() && map.error().isEmpty(),
                "Generated flat-terrain picking view renders without GPU errors");
      };
      QJsonArray boundaryPicks;
      const QVector<Camera> pickViews{{{0, 0}, 5000, 0, 0},
                                      {{15, 179.98}, 5000, 70, 35},
                                      {{-15, -179.98}, 5000, 250, 35},
                                      {{84.9, 80}, 5000, 120, 0}};
      for (int view = 0; view < pickViews.size(); ++view) {
        loadPickView(pickViews[view]);
        int point = 0;
        for (const QPointF pixel :
             {QPointF(map.width() * .5, map.height() * .5), QPointF(0, 0), QPointF(map.width(), 0),
              QPointF(0, map.height()), QPointF(map.width(), map.height())}) {
          const auto hit = map.pick(pixel);
          require(hit && hit->coordinate.valid() && std::isfinite(hit->height) && hit->terrain &&
                      !hit->scene && std::abs(hit->height - flatElevation) < .75,
                  QString("Flat 1234 m surface pick survives view %1 center/corner %2")
                      .arg(view + 1)
                      .arg(++point));
          const auto projected = map.screenPoint(hit->coordinate, hit->height);
          require(projected && QLineF(*projected, pixel).length() < .1,
                  "Picked boundary coordinate projects back to its native widget position");
          boundaryPicks.append(QJsonObject{{"view", view + 1},
                                           {"pixelX", pixel.x()},
                                           {"pixelY", pixel.y()},
                                           {"coordinate", hit->coordinate.json()},
                                           {"heightMetres", hit->height}});
        }
      }
      report["generatedFlatTerrainBoundaryPicks"] = boundaryPicks;
      loadPickView(pickViews.first());
      ProductLayer masked;
      masked.id = masked.jobID = "GENERATED_QA_PICK_MASK";
      masked.kind = "optical";
      masked.label = "GENERATED QA transparency and opacity picking";
      masked.bounds = {-.003, -.003, .003, .003};
      masked.pixelSize = masked.rasterSize = {32, 32};
      masked.image = QImage(masked.pixelSize, QImage::Format_RGBA8888);
      masked.image.fill(Qt::transparent);
      for (int y = 0; y < 32; ++y)
        for (int x = 16; x < 32; ++x)
          masked.image.setPixelColor(x, y, QColor(230, 30, 30));
      map.setOverlays({masked});
      map.setBlend(1);
      QTest::qWait(100);
      const auto pickCoordinate = [&](Coordinate coordinate) {
        const auto pixel = map.screenPoint(coordinate, flatElevation);
        require(pixel.has_value(), "Generated overlay picking coordinate projects onto map");
        return map.pick(*pixel);
      };
      const auto transparent = pickCoordinate({0, -.0015});
      const auto opaque = pickCoordinate({0, .0015});
      const auto outside = pickCoordinate({0, .004});
      require(transparent && transparent->terrain &&
                  std::abs(transparent->height - flatElevation) < .15,
              "Transparent product pixels retain the underlying 1234 m terrain pick");
      require(opaque && opaque->terrain && std::abs(opaque->height - (flatElevation + 2)) < .15,
              "Opaque product pixels retain the lifted 1236 m product-surface pick");
      require(outside && outside->terrain && std::abs(outside->height - flatElevation) < .15,
              "Outside product bounds the 1234 m terrain remains pickable");
      map.setBlend(0);
      QTest::qWait(80);
      const auto invisible = pickCoordinate({0, .0015});
      require(invisible && invisible->terrain && std::abs(invisible->height - flatElevation) < .15,
              "Zero-opacity product pixels retain the underlying 1234 m terrain pick");
      map.setOverlays({});
      QTest::qWait(80);
      sample("generated-picking-boundaries");
      report["passed"] = true;
    } catch (const std::exception &error) {
      report["passed"] = false;
      report["failure"] = error.what();
    }
    report["checks"] = checks;
    report["samples"] = samples;
    report["totalWallSeconds"] = elapsed.elapsed() / 1000.;
    report["totalCPUSeconds"] = cpuSeconds() - initialCPU;
    report["peakRSSBytes"] = peakBytes();
    report["statusNotifications"] = map.statusNotifications();
    report["statusInsideGpuCallbacks"] = map.statusInsideGpuCallbacks();
    report["maximumGpuCallbackDepth"] = map.maximumGpuCallbackDepth();
    report["executableSHA256"] =
        QString::fromLatin1(digest(QCoreApplication::applicationFilePath()));
#ifdef HELIOS_SOURCE_ROOT
    QJsonObject sources;
    for (const auto &name :
         {"native/desktop/Graphics/MapWidget.cpp", "native/desktop/Graphics/MapWidget.h",
          "native/desktop/Graphics/TileCache.cpp", "native/desktop/Core/DailyImagery.cpp",
          "native/desktop/shaders/globe.vert", "native/desktop/shaders/globe.frag",
          "native/desktop/Tests/MapResourceChecks.cpp"})
      sources[name] = QString::fromLatin1(digest(QString(HELIOS_SOURCE_ROOT) + "/" + name));
    report["sourceSHA256"] = sources;
#endif
    const auto bytes = QJsonDocument(report).toJson();
    QSaveFile file(output + "/map-resource-checks.json");
    file.setDirectWriteFallback(false);
    const bool saved =
        file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    app.exit(saved && report["passed"].toBool() ? 0 : 1);
  });
  return app.exec();
}
