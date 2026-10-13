#include "Validation.h"
#include "UI/MainWindow.h"
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <numbers>

namespace godeye {
int runValidation(QApplication &app, const QString &output, bool live,
                  const QString &actualProduct) {
  QTemporaryDir profile(QDir::tempPath() + "/godeye-desktop-validation-XXXXXX");
  if (!profile.isValid() || output.isEmpty()) {
    qWarning("Validation requires --qa-output DIRECTORY");
    return 2;
  }
  QDir().mkpath(output);
  MainWindow window(profile.path(), live);
  window.show();
  QStringList checks;
  QTimer::singleShot(500, &window, [&] {
    auto require = [&](bool ok, const QString &label) {
      if (!ok)
        throw std::runtime_error(label.toStdString());
      checks.append(label);
    };
    try {
      require(window.map().error().isEmpty(), "GPU renderer initialized");
      require(window.map().backend() == "Metal" || window.map().backend() == "Vulkan" ||
                  window.map().backend() == "D3D11",
              "Actual native GPU backend selected");
      require(!window.map().gpu().isEmpty(), "Actual GPU name available");
      window.missions().create("GENERATED QA — desktop infrastructure review");
      window.missions().setBrief(
          "GENERATED QA — desktop infrastructure review",
          "Verify shared map, mission documents and bounded satellite previews.",
          "Generated test data; no real operational finding.");
      window.missions().addArea("GENERATED QA area", "Desktop integration test",
                                {9.16, 45.43, 9.22, 45.49});
      window.missions().addNote("GENERATED QA note", "Review fixture only.",
                                "Generated local fixture");
      window.missions().addTask("Review generated fixture");
      window.missions().addObservation("GENERATED QA observation", "Test fixture", {45.46, 9.19});
      window.missions().addView("GENERATED QA view", window.map().camera());
      const auto file = profile.path() + "/missions.json";
      window.missions().save(file);
      window.missions().load(file);
      require(window.missions().selected()["items"].toArray().size() == 1,
              "Mission view survives save and reload");
      require(window.missions().selected()["brief"].toObject()["notes"].toArray().size() == 1,
              "Mission notes survive save and reload");
      window.showMission();
      QTest::qWait(100);
      require(window.missionWindow().isVisible(), "Separate Mission Control window opened");
      window.show();
      window.raise();
      window.map().focus({9.16, 45.43, 9.22, 45.49});
      QTest::qWait(150);
      require(window.map().camera().range < 20000, "Mission area focuses the shared map");
      window.map().orient(123, 55);
      QTest::qWait(100);
      const auto hit = window.map().pick(window.map().rect().center());
      require(hit.has_value(), "Tilted geographic picking returns a surface");
      require(std::abs(hit->coordinate.latitude - window.map().camera().center.latitude) < 0.02,
              "Picking retains geographic latitude");
      window.map().orient(0, 0);
      window.map().setCamera(Camera{{45.46, 9.19}, 5000, 0, 0});
      QTest::qWait(100);
      require(window.map().frames() > 2, "GPU frames rendered after navigation");
      const double beforeKeyboard = window.map().camera().center.longitude;
      QTest::keyClick(&window.map(), Qt::Key_Right);
      QTest::qWait(30);
      require(window.map().camera().center.longitude > beforeKeyboard,
              "Keyboard map navigation changes the shared camera");
      if (live) {
        window.map().setCamera(Camera{{45.8326, 6.8652}, 50000, 0, 35});
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < 20000 && (window.map().renderedTiles() < 8 ||
                                          !window.map().tiles().height({45.8326, 6.8652})))
          QTest::qWait(100);
        require(window.map().renderedTiles() >= 8, "Real public basemap tiles rendered");
        const auto elevation = window.map().tiles().height({45.8326, 6.8652});
        require(elevation && *elevation > 1000, "Real Terrarium terrain decoded near Mont Blanc");
        window.map().grabFramebuffer().save(output + "/live-terrain.png");
      } else {
        QImage elevationImage(256, 256, QImage::Format_RGBA8888);
        elevationImage.fill(QColor(128, 0, 0));
        elevationImage.setPixelColor(0, 0, QColor(128, 1, 128));
        elevationImage.setPixelColor(1, 0, QColor(127, 255, 192));
        QByteArray encoded;
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::WriteOnly);
        require(elevationImage.save(&buffer, "PNG"), "Terrain fixture PNG encoded");
        const auto decoded = TileCache::decode({0, 0, 1}, encoded, true);
        require(decoded->elevations[0] == 1.5f && decoded->elevations[1] == -0.25f,
                "Terrarium numeric channels retain fractional and negative heights");
        window.map().setCamera(Camera{{45.46, 9.19}, 3000, 0, 0});
        const auto terrainID = TileID::containing({45.46, 9.19}, 14);
        for (int y = -1; y <= 1; ++y)
          for (int x = -1; x <= 1; ++x)
            if (auto id = TileID::make(terrainID.x + x, terrainID.y + y, 14))
              window.map().tiles().injectTerrain(*id, std::vector<float>(256 * 256, 100));
        const auto ids = visibleTiles(window.map().camera(),
                                      double(window.map().width()) / window.map().height());
        QImage tile(256, 256, QImage::Format_RGBA8888);
        tile.fill(QColor(70, 70, 70));
        window.map().tiles().injectImage(TileID::containing({45.46, 9.19}, ids.first().zoom), tile);
        QTest::qWait(150);
        const auto terrainHit = window.map().pick(window.map().rect().center());
        require(terrainHit && terrainHit->terrain && std::abs(terrainHit->height - 100) < 1,
                "Picking uses actual loaded terrain triangles");
        QImage preview(64, 64, QImage::Format_RGBA8888);
        for (int y = 0; y < 64; ++y)
          for (int x = 0; x < 64; ++x)
            preview.setPixelColor(
                x, y, y < 32 ? (x < 32 ? Qt::red : Qt::green) : (x < 32 ? Qt::blue : Qt::yellow));
        const auto productDirectory = profile.path() + "/" + newID();
        QDir().mkpath(productDirectory);
        require(preview.save(productDirectory + "/preview.png"), "Generated optical preview saved");
        QJsonObject product{
            {"version", 1},
            {"state", "completed"},
            {"crs", "EPSG:4326"},
            {"width", 64},
            {"height", 64},
            {"bounds", QJsonArray{9.18, 45.45, 9.20, 45.47}},
            {"preview", "preview.png"},
            {"sourceResolutionMetres", QJsonArray{10, 10}},
            {"scene", QJsonObject{{"id", "GENERATED_QA_NOT_A_REAL_SATELLITE_ACQUISITION"},
                                  {"platform", "GENERATED QA"},
                                  {"acquiredAt", "2026-10-13T12:00:00Z"}}}};
        QFile manifest(productDirectory + "/result.json");
        require(manifest.open(QIODevice::WriteOnly), "Product fixture manifest opened");
        manifest.write(QJsonDocument(product).toJson());
        manifest.close();
        const auto layers = readProducts(manifest.fileName());
        window.map().setOverlays(layers);
        window.map().setBlend(1);
        QTest::qWait(150);
        auto pixels = window.map().grabFramebuffer();
        const auto sample = [&](Coordinate p) {
          const auto basis = window.map().camera().basis(100);
          const auto diff = subtract(p.ecef(102), basis.eye);
          const double depth = dot(diff, basis.forward), tangent = std::tan(std::numbers::pi / 8);
          const double aspect = double(pixels.width()) / pixels.height();
          const double x = dot(diff, basis.right) / (depth * tangent * aspect),
                       y = dot(diff, basis.up) / (depth * tangent);
          return pixels.pixelColor(int((x + 1) * 0.5 * pixels.width()),
                                   int((1 - y) * 0.5 * pixels.height()));
        };
        auto nw = sample({45.465, 9.185}), ne = sample({45.465, 9.195}),
             sw = sample({45.455, 9.185});
        require(nw.red() > 220 && nw.green() < 30 && ne.green() > 220 && ne.red() < 30 &&
                    sw.blue() > 220,
                "Georeferenced texture north/south and east/west orientation");
        pixels.save(output + "/generated-raster-overlay.png");
        auto second = layers.first();
        second.id += "-SECOND-GENERATED-QA";
        second.image.fill(Qt::magenta);
        window.map().setOverlays({layers.first(), second});
        window.map().setBlend(0.5);
        QTest::qWait(100);
        pixels = window.map().grabFramebuffer();
        const auto blended = sample({45.465, 9.185});
        require(blended.red() > 220 && blended.blue() > 90 && blended.blue() < 170,
                "Two-layer visual blending uses the selected opacity");
        window.map().setOverlays(layers);
        window.satelliteWindow().importManifest(manifest.fileName());
        QElapsedTimer importing;
        importing.start();
        while (window.satelliteWindow().busy() && importing.elapsed() < 5000)
          QTest::qWait(50);
        require(window.satelliteWindow().library().size() == 1,
                "Satellite library imports bounded product metadata");
        window.satelliteWindow().showProduct(window.satelliteWindow().library().first().id);
        importing.restart();
        while (window.satelliteWindow().busy() && importing.elapsed() < 5000)
          QTest::qWait(50);
        require(window.satelliteWindow().displayed().size() == 1,
                "Satellite library activates a real decoded preview");
        window.attachProduct(window.satelliteWindow().displayed().first());
        require(window.missions().selected()["items"].toArray().size() == 2,
                "Satellite product linked to mission with acquisition metadata");
        window.showSatellite();
        QTest::qWait(50);
        auto *details = window.satelliteWindow().findChild<QScrollArea *>("satelliteDetailsScroll");
        require(details && details->viewport()->width() >= 340 &&
                    details->horizontalScrollBar()->maximum() == 0,
                "Satellite inspector retains readable width without horizontal clipping");
        window.satelliteWindow().resize(640, 540);
        QTest::qWait(50);
        require(details->viewport()->width() >= 340 &&
                    details->horizontalScrollBar()->maximum() == 0,
                "Satellite inspector remains usable at its minimum window size");
        window.satelliteWindow().resize(820, 720);
        QTest::qWait(50);
        window.satelliteWindow().grab().save(output + "/satellite-library.png");
        window.map().orient(80, 55);
        QTest::qWait(100);
        require(!window.map().grabFramebuffer().isNull(),
                "Draped overlay renders with camera tilt");
        window.map().setOverlays({});
        window.map().setTerrain(false);
        window.map().orient(0, 0);
        QTest::qWait(100);
        require(window.map().error().isEmpty(),
                "Terrain and overlay resource lifecycle survives removal");
        window.resize(1000, 700);
        QTest::qWait(100);
        require(!window.map().grabFramebuffer().isNull(), "GPU target survives window resize");
        window.missions().save(file);
        exportProductFile(file, output + "/qt-mission-roundtrip.json");
        require(QFileInfo::exists(output + "/qt-mission-roundtrip.json"),
                "Qt-authored mission fixture retained for Swift compatibility checks");
      }
      if (!actualProduct.isEmpty()) {
        const auto real = readProducts(actualProduct);
        require(!real.isEmpty(),
                "Existing processed product imported without changing source files");
        for (const auto &layer : real) {
          window.map().setOverlays({layer});
          window.map().setBlend(1);
          window.map().focus(layer.bounds);
          QTest::qWait(150);
          require(window.map().error().isEmpty(),
                  "Existing " + layer.band + " output displayed on GPU");
          const auto frame = window.map().grabFramebuffer();
          require(!frame.isNull(), "Existing " + layer.band + " output framebuffer readback");
          frame.save(output + "/existing-product-" + layer.band + ".png");
        }
      }
      auto image = window.map().grabFramebuffer();
      require(!image.isNull(), "Actual GPU framebuffer readback");
      image.save(output + "/map.png");
      window.grab().save(output + "/workspace.png");
      window.missionWindow().grab().save(output + "/mission-control.png");
      QFile result(output + "/validation.json");
      require(result.open(QIODevice::WriteOnly), "Validation report created");
      QJsonArray labels;
      for (const auto &s : checks)
        labels.append(s);
      result.write(
          QJsonDocument(
              QJsonObject{{"passed", true},
                          {"checks", labels},
                          {"backend", window.map().backend()},
                          {"gpu", window.map().gpu()},
                          {"fixtures", "Generated local QA; not real satellite acquisitions"}})
              .toJson(QJsonDocument::Indented));
      qInfo("DESKTOP_VALIDATION passed %lld checks", static_cast<long long>(checks.size()));
      app.exit(0);
    } catch (const std::exception &e) {
      QFile result(output + "/validation.json");
      if (result.open(QIODevice::WriteOnly))
        result.write(QJsonDocument(QJsonObject{{"passed", false},
                                               {"error", QString::fromUtf8(e.what())},
                                               {"completedChecks", checks.size()}})
                         .toJson());
      qWarning("DESKTOP_VALIDATION failed: %s", e.what());
      app.exit(1);
    }
  });
  return app.exec();
}
} // namespace godeye
