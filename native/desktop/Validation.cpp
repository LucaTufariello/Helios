#include "Validation.h"
#include "Tests/SceneFixture.h"
#include "UI/InferenceWindow.h"
#include "UI/MainWindow.h"
#include "UI/MapToolsWindow.h"
#include "UI/MissionEditor.h"
#include "UI/MonitorPanel.h"
#include "UI/TimelinePanel.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <ctime>
#include <numbers>
#include <sys/resource.h>

namespace godeye {
int runValidation(QApplication &app, const QString &output, bool live, const QString &actualProduct,
                  const QString &actualTileset, const QString &python, bool retrieveOptical,
                  bool searchRadar) {
  QElapsedTimer elapsed;
  elapsed.start();
  const auto cpuStart = std::clock();
  QTemporaryDir profile(QDir::tempPath() + "/godeye-desktop-validation-XXXXXX");
  if (!profile.isValid() || output.isEmpty()) {
    qWarning("Validation requires --qa-output DIRECTORY");
    return 2;
  }
  QDir().mkpath(output);
  // Synthetic tile/scene assertions require an offline map. Provider checks
  // use actual requests and retain their own acquisition/catalogue assertions.
  const bool generated = !live && !retrieveOptical && !searchRadar;
  MainWindow window(profile.path(), !generated);
  window.show();
  QStringList checks;
  QJsonArray actualAcquisitions;
  QJsonObject actualDaily;
  QTimer modalGuard;
  modalGuard.setInterval(2000);
  QObject::connect(&modalGuard, &QTimer::timeout, &window, [&] {
    if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
      qWarning("NATIVE_QA_MODAL: %s", qPrintable(message->text()));
      message->grab().save(output + "/unexpected-dialog.png");
      message->reject();
    }
  });
  modalGuard.start();
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
      require(window.map().hardwareGpu(), "Hardware GPU selected without software Vulkan fallback");
      if (generated) {
        auto &processing = window.processingWindow();
        require(!processing.startOptical({}, Bounds{9, 45, 9.1, 45.1}) && !processing.job(),
                "Offline native acquisition refuses remote optical work before creating a job");
        require(!processing.startRadar("GENERATED-MISSING-INPUT", {}, false) && !processing.job(),
                "Offline native acquisition refuses raw GRD work that can fetch orbits/DEM");
        const auto *opticalSearch = processing.findChild<QPushButton *>("opticalSearch"),
                   *radarSearch = processing.findChild<QPushButton *>("radarSearch");
        require(opticalSearch && radarSearch && !opticalSearch->isEnabled() &&
                    !radarSearch->isEnabled(),
                "Offline catalogue controls are visibly disabled");
      }
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
        const auto requestedDate = QDate(2026, 10, 12);
        require(window.map().setDailyImagery(DailySource::Terra, requestedDate, 1),
                "Dated NASA Terra visualization starts on the native terrain map");
        wait.restart();
        while (window.map().dailyImagery().busy() && wait.elapsed() < 45000)
          QTest::qWait(100);
        require(window.map().dailyImagery().cached() > 0,
                "Actual NASA visualization PNG tiles reach the native cache");
        actualDaily = {{"provider", "NASA GIBS"},
                       {"source", DailyImageryCache::sourceID(DailySource::Terra)},
                       {"requestedUTCDate", requestedDate.toString(Qt::ISODate)},
                       {"cachedTiles", window.map().dailyImagery().cached()},
                       {"failedTiles", window.map().dailyImagery().failures()},
                       {"limitation", "Dated visualization; pixel observation times can differ. "
                                      "Not a calibrated product validation."}};
        QTest::qWait(250);
        const auto colour = window.map().grabFramebuffer();
        colour.save(output + "/daily-terra-terrain.png");
        window.map().setDisplayStyle("monochrome");
        QTest::qWait(150);
        const auto monochrome = window.map().grabFramebuffer();
        int coloured = 0, gray = 0, samples = 0;
        for (int y = monochrome.height() * 2 / 5; y < monochrome.height() * 3 / 5; y += 2)
          for (int x = monochrome.width() * 2 / 5; x < monochrome.width() * 3 / 5; x += 2) {
            const auto c = colour.pixelColor(x, y), m = monochrome.pixelColor(x, y);
            ++samples;
            if (std::abs(c.red() - c.green()) + std::abs(c.green() - c.blue()) > 8)
              ++coloured;
            if (std::abs(m.red() - m.green()) + std::abs(m.green() - m.blue()) <= 3)
              ++gray;
          }
        require(coloured > samples / 20 && gray > samples * 9 / 10,
                "Native display shader converts actual dated imagery to monochrome");
        monochrome.save(output + "/daily-terra-monochrome.png");
        window.map().setDisplayStyle("standard");
        window.map().setDailyImagery(DailySource::Off, requestedDate, 0);

      } else if (generated) {
        window.map().setTerrain(false);
        window.map().setCamera(Camera{{45.3, 9}, 1000000, 0, 0});
        QImage coarse(256, 256, QImage::Format_RGBA8888);
        coarse.fill(QColor(40, 90, 130));
        for (int y = 0; y < 4; ++y)
          for (int x = 0; x < 4; ++x)
            window.map().tiles().injectImage({x, y, 2}, coarse);
        QTest::qWait(150);
        const auto regional = window.map().grabFramebuffer();
        int gaps = 0;
        for (int y = regional.height() / 4; y < regional.height() * 3 / 4; ++y)
          for (int x = regional.width() / 4; x < regional.width() * 3 / 4; ++x) {
            const auto c = regional.pixelColor(x, y);
            if (c.red() < 30 && c.green() < 50 && c.blue() < 60)
              ++gaps;
          }
        regional.save(output + "/regional-coarse-imagery.png");
        require(gaps == 0, "Coarse regional imagery remains above the globe without dark seams");
        window.map().setTerrain(true);
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
        auto terrainHit = window.map().pick(window.map().rect().center());
        QElapsedTimer terrainWait;
        terrainWait.start();
        while ((!terrainHit || !terrainHit->terrain || std::abs(terrainHit->height - 100) >= 1) &&
               terrainWait.elapsed() < 5000) {
          QTest::qWait(100);
          terrainHit = window.map().pick(window.map().rect().center());
        }
        require(terrainHit && terrainHit->terrain && std::abs(terrainHit->height - 100) < 1,
                QString("Picking uses actual loaded terrain triangles (height %1, terrain %2)")
                    .arg(terrainHit ? terrainHit->height : -9999)
                    .arg(terrainHit ? terrainHit->terrain : false));
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
        window.map().setDisplayStyle("monochrome");
        QTest::qWait(100);
        auto *displayCaption = window.findChild<QLabel *>("mapDisplayCaption");
        require(displayCaption && displayCaption->isVisible() &&
                    displayCaption->text().contains("Monochrome"),
                "Style disclosure appears after a GPU frame without reentrant rendering");
        pixels = window.map().grabFramebuffer();
        const auto unchanged = sample({45.465, 9.185});
        require(unchanged.red() > 220 && unchanged.green() < 30 && unchanged.blue() < 30,
                "Product preview retains original colours when the basemap display style changes");
        window.map().setDisplayStyle("standard");
        QTest::qWait(100);
        require(!displayCaption->isVisible() && window.map().hardwareGpu(),
                "Returning to standard display safely removes the map caption");

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
        const auto tileset =
            qa::sceneFixture(profile.path() + "/scene-fixture", "roof.glb", 4, "OPAQUE", 1, true);
        require(window.map().loadTileset(QUrl::fromLocalFile(tileset)),
                "Generated 3D Tiles admitted through the portable engine");
        QElapsedTimer sceneWait;
        sceneWait.start();
        while (window.map().sceneMeshes() == 0 && sceneWait.elapsed() < 5000)
          QTest::qWait(30);
        require(window.map().sceneMeshes() == 1,
                "Generated GLB geometry uploaded to the actual GPU");
        window.map().setCamera(Camera{{0, 0}, 500, 0, 0});
        QTest::qWait(250);
        const auto roof = window.map().pick(window.map().rect().center());
        require(roof && roof->scene && std::abs(roof->height - 100) < 0.1,
                "Picking returns the rendered 3D roof above the ellipsoid");
        auto sceneFrame = window.map().grabFramebuffer();
        const auto roofPixel = sceneFrame.pixelColor(sceneFrame.rect().center());
        require(roofPixel.red() > 100 && roofPixel.red() < 150 && roofPixel.green() > 180 &&
                    roofPixel.blue() > 220,
                "glTF sRGB texture, linear material factors and display encoding match");
        require(sceneFrame.save(output + "/generated-scene.png"),
                "Generated GPU scene screenshot retained");
        QTest::qWait(250);
        const auto idleFrames = window.map().frames();
        QTest::qWait(350);
        require(window.map().frames() == idleFrames,
                "Loaded scene stops rendering while the map is idle");
        window.map().removeTileset();
        QTest::qWait(80);
        const auto cleared = window.map().pick(window.map().rect().center());
        require(cleared && !cleared->scene,
                "Removing 3D Tiles removes its GPU resources and picking mask");
        for (int i = 0; i < 6; ++i) {
          require(window.map().loadTileset(QUrl::fromLocalFile(tileset)),
                  "Repeated scene open admitted");
          QTest::qWait(25);
          window.map().removeTileset();
        }
        require(window.map().error().isEmpty(),
                "Repeated scene open/close preserves the Vulkan target");
      }
      auto retainDiagnostics = [&] {
        auto *job = window.processingWindow().job();
        if (!job)
          return;
        QDir().mkpath(output + "/worker-diagnostics");
        for (const auto &name : {"run.json", "request.json", "worker.log", "product/result.json",
                                 "product/diagnostics.log"}) {
          QFile input(job->directory() + "/" + name);
          if (!input.open(QIODevice::ReadOnly))
            continue;
          input.seek(std::max<qint64>(0, input.size() - 65536));
          QFile saved(output + "/worker-diagnostics/" + QString(name).replace('/', '-'));
          if (saved.open(QIODevice::WriteOnly))
            saved.write(input.read(65536));
        }
      };
      auto waitForProduct = [&](const QString &label) {
        QElapsedTimer wait;
        wait.start();
        while (window.processingWindow().busy() && wait.elapsed() < 180000)
          QTest::qWait(100);
        auto *job = window.processingWindow().job();
        retainDiagnostics();
        require(job && !job->running() && job->record()["state"] == "completed",
                label +
                    " worker completed: " + (job ? job->error() : QString("no worker started")));
        while (window.satelliteWindow().busy() && wait.elapsed() < 185000)
          QTest::qWait(50);
        const auto products = readProducts(job->resultFile());
        require(!products.isEmpty(), label + " result admitted with original georeferencing");
        require(window.satelliteWindow().displayed().size() == 1 &&
                    window.satelliteWindow().displayed().first().id == products.first().id,
                label + " automatically entered the saved library and shared map");
        require(products.first().jobID.compare(QFileInfo(job->directory()).fileName(),
                                               Qt::CaseInsensitive) == 0,
                label + " retains the owned job UUID across product storage");
        window.map().setBlend(1);
        QTest::qWait(150);
        require(!window.map().grabFramebuffer().isNull(), label + " actual GPU preview readback");
        return products.first();
      };
      if (!python.isEmpty() && generated) {
        window.processingWindow().setEngines(python);
        const auto source = profile.path() + "/PRS_L2D_GENERATED_QA_20260922102800_0001.he5";
        QProcess fixture;
        fixture.start(python,
                      {"-I", "-c",
                       "import sys,h5py,numpy as np\n"
                       "with h5py.File(sys.argv[1],'w') as h:\n"
                       " h.attrs.update(Epsg_Code=32632,Product_ULcorner_easting=500000.,Product_"
                       "ULcorner_northing=5050000.,Product_LRcorner_easting=500355.,Product_"
                       "LRcorner_northing=5049645.,L2ScalePanMin=.1,L2ScalePanMax=.9)\n"
                       " g=h.create_group('HDFEOS/SWATHS/PRS_L2D_PCO/Data Fields')\n"
                       " g['Cube']=np.full((72,72),32768,dtype='uint16')\n"
                       " flags=np.zeros((72,72),dtype='uint8'); flags[:5,:5]=1; "
                       "g['PIXEL_L2_ERR_MATRIX']=flags\n",
                       source});
        QElapsedTimer wait;
        wait.start();
        while (fixture.state() != QProcess::NotRunning && wait.elapsed() < 15000)
          QTest::qWait(50);
        if (fixture.state() != QProcess::NotRunning) {
          fixture.kill();
          fixture.waitForFinished(1000);
        }
        require(fixture.exitStatus() == QProcess::NormalExit && fixture.exitCode() == 0 &&
                    QFileInfo::exists(source),
                "Generated PRISMA HDF fixture created with the selected raster environment");
        require(window.processingWindow().startPrisma(source, "pan", 660),
                "PRISMA import starts through the portable UI job boundary");
        const auto layer = waitForProduct("Generated PRISMA");
        require(layer.resolution == 5 && !layer.scientific.isEmpty(),
                "PRISMA retains source spacing and separate scientific values");
        QDir().mkpath(output + "/generated-prisma");
        exportProductFile(layer.manifest, output + "/generated-prisma/result.json");
        exportProductFile(layer.preview, output + "/generated-prisma/preview.png");
        exportProductFile(layer.scientific, output + "/generated-prisma/source-values.tif");
        window.map().grabFramebuffer().save(output + "/generated-prisma-map.png");
        window.processingWindow().show();
        QTest::qWait(50);
        window.processingWindow().grab().save(output + "/processing-window.png");
        window.processingWindow().hide();
        QFile reviewFixture(":/godeye/Tests/review_fixture.py");
        require(reviewFixture.open(QIODevice::ReadOnly),
                "Generated review fixture resource available outside the source tree");
        const auto sources = profile.path() + "/generated-review-sources";
        fixture.start(python, {"-I", "-c", QString::fromUtf8(reviewFixture.readAll()), sources});
        wait.restart();
        while (fixture.state() != QProcess::NotRunning && wait.elapsed() < 15000)
          QTest::qWait(50);
        if (fixture.state() != QProcess::NotRunning) {
          fixture.kill();
          fixture.waitForFinished(1000);
        }
        const auto request = WorkerJob::readRecord(sources + "/request.json");
        require(fixture.exitCode() == 0 && !request.isEmpty(),
                "Generated calibrated-power review inputs created");
        require(window.processingWindow().startReview(
                    request["site"].toObject(),
                    request["beforeDirectory"].toString() + "/result.json",
                    request["afterDirectory"].toString() + "/result.json",
                    request["options"].toObject()),
                "Site comparison starts through the portable processing boundary");
        wait.restart();
        while (window.processingWindow().busy() && wait.elapsed() < 30000)
          QTest::qWait(50);
        auto *reviewJob = window.processingWindow().job();
        retainDiagnostics();
        require(reviewJob && !reviewJob->running() && reviewJob->record()["state"] == "completed",
                "Actual change-review worker completed on generated power grids");
        const auto review = ChangeReview::read(reviewJob->resultFile());
        require(review.result["candidates"].toArray().size() == 1,
                "Generated known power ratio yields one connected candidate");
        const auto evidence = review.result["evidence"].toArray();
        require(!QUuid(evidence[0].toObject()["jobID"].toString()).isNull() &&
                    evidence[0].toObject()["jobID"] != "product",
                "Portable review preserves source job UUIDs");
        auto &reviewWindow = window.reviewWindow();
        reviewWindow.openReview(reviewJob->resultFile());
        auto *candidateList = reviewWindow.findChild<QListWidget *>("reviewCandidates");
        auto *decision = reviewWindow.findChild<QComboBox *>("reviewDecision");
        auto *rationale = reviewWindow.findChild<QPlainTextEdit *>("reviewRationale");
        auto *display = reviewWindow.findChild<QPushButton *>("reviewDisplay");
        require(candidateList && candidateList->count() == 1 && decision && rationale && display,
                "Native review candidates and decision controls loaded");
        decision->setCurrentText("Follow up");
        rationale->setPlainText("Generated QA decision; not an operational finding.");
        require(reviewWindow.close(), "Review window saves analyst draft on close");
        ChangeNotes savedNotes(review);
        savedNotes.load();
        require(savedNotes.document()["candidates"].toObject()["1"].toObject()["decision"] ==
                    "Follow up",
                "Analyst decision and rationale survive reopen");
        reviewWindow.show();
        QTest::mouseClick(display, Qt::LeftButton);
        wait.restart();
        while (reviewWindow.busy() && wait.elapsed() < 5000)
          QTest::qWait(50);
        window.show();
        window.raise();
        QTest::qWait(150);
        require(!reviewWindow.busy() &&
                    window.map().overlayIDs() == QStringList{"review:" + review.id + ":Change"} &&
                    !window.map().grabFramebuffer().isNull(),
                "Measured change preview displayed on the shared GPU map");
        const auto exports = output + "/generated-review";
        QDir().mkpath(exports);
        for (const auto &name : {"result.json", "notes.json", "delta-db.tif", "candidate-mask.tif",
                                 "change-display.tif", "Before.png", "After.png", "Change.png"})
          exportProductFile(safeProductFile(review.directory, name), exports + "/" + name);
        reviewWindow.grab().save(output + "/infrastructure-review.png");
        window.map().grabFramebuffer().save(output + "/infrastructure-change-map.png");
        reviewWindow.hide();
        const auto beforeManifest = request["beforeDirectory"].toString() + "/result.json",
                   afterManifest = request["afterDirectory"].toString() + "/result.json";
        const auto first = readProducts(beforeManifest).first(),
                   second = readProducts(afterManifest).first();
        window.satelliteWindow().importManifest(beforeManifest);
        window.satelliteWindow().importManifest(afterManifest);
        wait.restart();
        while (window.satelliteWindow().busy() && wait.elapsed() < 10000)
          QTest::qWait(50);
        require(!window.satelliteWindow().busy(),
                "Generated review acquisitions enter the saved satellite library");
        window.attachProduct(first);
        window.attachProduct(second);
        auto legacy = first.missionEvidence();
        legacy["id"] = newID();
        legacy.remove("acquisition");
        legacy["title"] = "GENERATED QA legacy acquisition link";
        window.missions().addEvidence(legacy);
        auto *timeline = window.missionWindow().findChild<TimelinePanel *>();
        auto *missionTabs = window.missionWindow().findChild<QTabWidget *>("missionTabs");
        require(timeline && missionTabs, "Native Mission Control timeline available");
        missionTabs->setCurrentWidget(timeline);
        timeline->refresh();
        auto *retain = timeline->findChild<QPushButton *>("timelineRetain");
        auto *baseline = timeline->findChild<QComboBox *>("timelineBaseline");
        auto *later = timeline->findChild<QComboBox *>("timelineLater");
        auto *compare = timeline->findChild<QPushButton *>("timelineCompare");
        require(retain && baseline && later && compare,
                "Native timeline acquisition and comparison controls available");
        QTest::mouseClick(retain, Qt::LeftButton);
        bool retained = false;
        QString firstEntry, secondEntry;
        for (const auto value : window.missions().selected()["items"].toArray()) {
          const auto item = value.toObject();
          if (item["id"] == legacy["id"])
            retained = item["acquisition"].toObject()["sensor"] == "Sentinel-1A";
          if (item["referenceID"].toString().compare(first.jobID, Qt::CaseInsensitive) == 0 &&
              item["id"] != legacy["id"])
            firstEntry = item["id"].toString();
          if (item["referenceID"].toString().compare(second.jobID, Qt::CaseInsensitive) == 0)
            secondEntry = item["id"].toString();
        }
        require(retained, "Legacy link retains sensor, source and UTC acquisition snapshot from "
                          "metadata-only library");
        baseline->setCurrentIndex(baseline->findData(firstEntry));
        later->setCurrentIndex(later->findData(secondEntry));
        QTest::mouseClick(compare, Qt::LeftButton);
        wait.restart();
        while (timeline->busy() && wait.elapsed() < 10000)
          QTest::qWait(50);
        QTest::qWait(150);
        require(window.map().overlayIDs() == QStringList{first.id, second.id},
                "Timeline comparison loads both bounded previews onto the shared map");
        window.missions().save(file);
        window.missions().load(file);
        require(window.missions().selected()["comparison"].toObject()["baseID"] ==
                    rasterReference(first),
                "Visual comparison survives mission save/reopen");
        missionTabs->setCurrentWidget(timeline);
        window.missionWindow().grab().save(output + "/mission-timeline.png");
        const auto task = window.missions()
                              .selected()["brief"]
                              .toObject()["tasks"]
                              .toArray()
                              .first()
                              .toObject()["id"]
                              .toString();
        bool editorReady = false;
        QTimer::singleShot(100, &window, [&] {
          auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
          if (!dialog)
            return;
          auto *known = dialog->findChild<QCheckBox *>("entry_dueEnabled");
          auto *due = dialog->findChild<QDateTimeEdit *>("entry_due");
          auto *buttons = dialog->findChild<QDialogButtonBox *>();
          if (!known || !due || !buttons) {
            dialog->reject();
            return;
          }
          known->setChecked(true);
          due->setDateTime(missionTime("2026-11-01T12:34:56Z"));
          editorReady = true;
          QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
        });
        QTimer::singleShot(5000, &window, [] {
          auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
          if (dialog && dialog->objectName() == "missionEntryEditor")
            dialog->reject();
        });
        require(editMissionEntry(&window.missionWindow(), window.missions(), "tasks", task) &&
                    editorReady,
                "Native task due-date editor applies a UTC deadline");
        const auto due = window.missions()
                             .selected()["brief"]
                             .toObject()["tasks"]
                             .toArray()
                             .first()
                             .toObject()["due"];
        require(missionTime(due) == missionTime("2026-11-01T12:34:56Z"),
                "Task deadline keeps its exact UTC meaning");
        auto &tools = window.mapToolsWindow();
        tools.show();
        auto *toolTabs = tools.findChild<QTabWidget *>("mapToolsTabs");
        auto *annotationTitle = tools.findChild<QLineEdit *>("annotationTitle");
        auto *annotationShape = tools.findChild<QComboBox *>("annotationShape");
        auto *annotationBegin = tools.findChild<QPushButton *>("annotationBegin");
        auto *annotationSave = tools.findChild<QPushButton *>("annotationSave");
        auto *annotations = tools.findChild<QListWidget *>("annotationList");
        require(toolTabs && annotationTitle && annotationShape && annotationBegin &&
                    annotationSave && annotations,
                "Native drawing controls available");
        toolTabs->setCurrentIndex(2);
        annotationTitle->setText("GENERATED QA polygon");
        annotationShape->setCurrentIndex(2);
        window.map().setCamera(Camera{{45.46, 9.19}, 20000, 0, 0});
        QTest::qWait(100);
        QTest::mouseClick(annotationBegin, Qt::LeftButton);
        for (const auto fraction : {QPointF(.45, .45), QPointF(.55, .45), QPointF(.5, .55)})
          QTest::mouseClick(&window.map(), Qt::LeftButton, Qt::NoModifier,
                            QPoint(int(window.map().width() * fraction.x()),
                                   int(window.map().height() * fraction.y())));
        QTest::mouseClick(annotationSave, Qt::LeftButton);
        require(annotations->count() == 1,
                "Three actual map picks save a local polygon annotation");
        auto state = tools.workspace();
        const auto shared = workspaceFromShareURL(workspaceShareURL(state));
        require(shared && (*shared)["annotations"].toArray().isEmpty(),
                "Workspace link strips annotation content");
        tools.applyWorkspace(*shared, true);
        require(annotations->count() == 1,
                "Opening a shared workspace retains the local annotation library");
        const auto annotationFile = profile.path() + "/annotations.json";
        AnnotationLibrary localAnnotations(annotationFile);
        localAnnotations.load();
        require(localAnnotations.values().size() == 1 &&
                    localAnnotations.values()[0].toObject()["points"].toArray().size() == 3,
                "Annotation geometry survives local store reload");
        const auto savedAnnotation = localAnnotations.values().first().toObject();
        window.showEvidence({{"kind", "annotation"}, {"annotation", savedAnnotation}});
        require(window.showingSavedEvidence() && window.map().hasPathLayer("saved-evidence"),
                "Saved annotation opens with frozen geometry on the shared map");
        const auto snapshot = window.domainWindow().network().snapshot("annotations");
        window.domainWindow().setAnnotations(snapshot);
        QTest::qWait(50);
        require(window.showingSavedEvidence() && window.map().hasPathLayer("saved-evidence"),
                "Automatic source refresh preserves frozen evidence geometry");
        window.domainWindow().selectRecord("annotation:" + savedAnnotation["id"].toString());
        require(!window.showingSavedEvidence() && !window.map().hasPathLayer("saved-evidence"),
                "Intentional live record selection replaces frozen evidence geometry");
        if (auto *inspector = window.domainWindow().findChild<QDialog *>("savedRecordInspector"))
          inspector->close();
        toolTabs->setCurrentIndex(3);
        auto *displayStyle = tools.findChild<QComboBox *>("displayStyle");
        require(displayStyle, "Native display shader control available");
        window.map().setOverlays({});
        displayStyle->setCurrentIndex(1);
        QTest::qWait(100);
        require(window.map().displayStyle() == "monochrome" &&
                    !window.map().grabFramebuffer().isNull(),
                "Native basemap shader changes without a web runtime");
        displayStyle->setCurrentIndex(0);
        window.map().setOverlays({first, second});
        tools.grab().save(output + "/map-tools-display.png");
        toolTabs->setCurrentIndex(2);
        tools.grab().save(output + "/map-tools-annotations.png");
        require(tools.saveSession(), "Workspace display settings save for restart");
        tools.hide();
        window.domainWindow().restoreFollowing("GENERATED-QA-PENDING-TARGET");
        require(tools.saveSession(), "Following target is retained in workspace state");
        {
          QSettings legacy(profile.path() + "/desktop.ini", QSettings::IniFormat);
          legacy.setValue(
              "camera", QJsonDocument(window.map().camera().json()).toJson(QJsonDocument::Compact));
          legacy.sync();
          MainWindow restarted(profile.path(), false);
          require(restarted.domainWindow().followedID() == "GENERATED-QA-PENDING-TARGET",
                  "Startup camera restore preserves pending source following");
        }
        window.domainWindow().stopFollowing();
        const auto inferencePython = qEnvironmentVariable("GODEYE_INFERENCE_PYTHON");
        if (!inferencePython.isEmpty()) {
          QFile script(":/godeye/Tests/inference_fixture.py");
          require(script.open(QIODevice::ReadOnly),
                  "Generated inference fixture is bundled for source-independent QA");
          const auto modelDirectory = profile.path() + "/generated-inference";
          QProcess modelFixture;
          modelFixture.start(inferencePython,
                             {"-I", "-c", QString::fromUtf8(script.readAll()), modelDirectory});
          wait.restart();
          while (modelFixture.state() != QProcess::NotRunning && wait.elapsed() < 15000)
            QTest::qWait(50);
          if (modelFixture.state() != QProcess::NotRunning) {
            modelFixture.kill();
            modelFixture.waitForFinished(1000);
          }
          require(modelFixture.exitCode() == 0,
                  "Input-dependent QA model created locally; no trained detector claim");
          window.showInference();
          auto &assistance = window.inferenceWindow();
          assistance.findChild<QLineEdit *>("inferencePython")->setText(inferencePython);
          assistance.findChild<QLineEdit *>("inferenceContract")
              ->setText(modelDirectory + "/GENERATED_QA.contract.json");
          assistance.findChild<QDoubleSpinBox *>("inferenceThreshold")->setValue(.1);
          auto *analyze = assistance.findChild<QPushButton *>("inferenceAnalyze");
          auto *accept = assistance.findChild<QPushButton *>("inferenceAccept");
          auto *proposals = assistance.findChild<QListWidget *>("inferenceProposals");
          auto analyzeFixture = [&] {
            QTest::mouseClick(analyze, Qt::LeftButton);
            wait.restart();
            while (assistance.busy() && wait.elapsed() < 30000)
              QTest::qWait(50);
            require(
                !assistance.busy() && proposals->count() == 1,
                "Generated CPU model returns one input-dependent proposal through the native host");
            proposals->item(0)->setSelected(true);
            wait.restart();
            while (!accept->isEnabled() && wait.elapsed() < 5000)
              QTest::qWait(10);
            require(accept->isEnabled(), "Native proposal review enables explicit acceptance");
          };
          analyzeFixture();
          const auto proposal = proposals->item(0)->data(Qt::UserRole).toJsonObject();
          assistance.grab().save(output + "/local-assistance-review.png");
          QTest::mouseClick(accept, Qt::LeftButton);
          wait.restart();
          while (assistance.busy() && wait.elapsed() < 10000)
            QTest::qWait(50);
          bool added = false;
          for (const auto value :
               window.missions().selected()["brief"].toObject()["observations"].toArray())
            if (value.toObject()["id"] == proposal["id"])
              added = value.toObject()["state"] == "Unreviewed";
          require(
              added && !window.missions().dirty(),
              "Reviewed model proposal checks source hashes and saves atomically as Unreviewed");
          window.missions().setObservationReview(proposal["id"].toString(), "Uncertain",
                                                 "Generated QA analyst review; no accuracy claim.");
          window.missions().save(file);
          window.missions().load(file);
          bool reviewed = false;
          for (const auto value :
               window.missions().selected()["brief"].toObject()["observations"].toArray())
            if (value.toObject()["id"] == proposal["id"])
              reviewed = value.toObject()["state"] == "Uncertain" &&
                         value.toObject()["inputSHA256"] == proposal["inputSHA256"];
          require(reviewed,
                  "Analyst review survives mission reopen with model/source provenance intact");
          analyzeFixture();
          const auto before =
              window.missions().selected()["brief"].toObject()["observations"].toArray().size();
          const auto preview =
              WorkerJob::readRecord(assistance.job()->directory() + "/request.json")["source"]
                  .toString();
          require(preview.startsWith(profile.path() + "/") && !QFileInfo(preview).isSymLink(),
                  "Stale-source check is confined to owned generated QA pixels");
          QFile changed(preview);
          require(changed.open(QIODevice::Append),
                  "Owned QA optical preview opened for stale-source test");
          changed.write("GENERATED QA SOURCE MUTATION");
          changed.close();
          QTest::mouseClick(accept, Qt::LeftButton);
          wait.restart();
          while (assistance.busy() && wait.elapsed() < 10000)
            QTest::qWait(50);
          require(
              window.missions().selected()["brief"].toObject()["observations"].toArray().size() ==
                      before &&
                  assistance.findChild<QLabel *>("inferenceStatus")->text().contains("changed"),
              "Changed source bytes prevent proposal acceptance into the mission");
          assistance.hide();
        }
        const auto monitor = window.missions().addMonitor({{"kind", "Evidence availability"}});
        window.missions().save(file);
        auto *monitorPanel = window.missionWindow().findChild<MonitorPanel *>();
        auto *monitors = window.missionWindow().findChild<QListWidget *>("missionMonitors");
        auto *runMonitor = window.missionWindow().findChild<QPushButton *>("monitorRun");
        require(monitorPanel && monitors && runMonitor,
                "Native mission monitor controls available");
        missionTabs->setCurrentWidget(monitorPanel);
        for (int row = 0; row < monitors->count(); ++row)
          if (monitors->item(row)->data(Qt::UserRole) == monitor)
            monitors->setCurrentRow(row);
        QTest::qWait(80);
        QTest::mouseClick(runMonitor, Qt::LeftButton);
        wait.restart();
        while (window.monitorRunner().running(window.missions().selectedID(), monitor) &&
               wait.elapsed() < 10000)
          QTest::qWait(50);
        QJsonObject checkpoint;
        for (const auto value :
             window.missions().selected()["brief"].toObject()["monitors"].toArray())
          if (value.toObject()["id"] == monitor)
            checkpoint = value.toObject();
        require(checkpoint.contains("lastSuccess") &&
                    checkpoint["summary"].toString().contains("linked items available locally"),
                "In-app availability check saves a real local checkpoint: " +
                    QString::fromUtf8(QJsonDocument(checkpoint).toJson(QJsonDocument::Compact)));
        window.missions().load(file);
        bool reopened = false;
        for (const auto value :
             window.missions().selected()["brief"].toObject()["monitors"].toArray())
          if (value.toObject()["id"] == monitor)
            reopened = value.toObject().contains("lastSuccess");
        require(reopened, "Monitor checkpoint survives mission relaunch simulation");
        missionTabs->setCurrentWidget(monitorPanel);
        window.missionWindow().grab().save(output + "/mission-monitors.png");
      }
      if (retrieveOptical) {
        require(!python.isEmpty(), "Public optical validation requires --raster-python FILE");
        auto &processing = window.processingWindow();
        processing.setEngines(python);
        require(processing.setQuery({{9.18, 45.455, 9.185, 45.46},
                                     QDateTime::currentDateTimeUtc().date().addDays(-30),
                                     QDateTime::currentDateTimeUtc().date(),
                                     100,
                                     "sentinel2"}),
                "Public optical query uses bounded region and UTC dates");
        processing.show();
        processing.raise();
        QTest::qWait(50);
        auto *search = processing.findChild<QPushButton *>("opticalSearch");
        auto *load = processing.findChild<QPushButton *>("opticalLoad");
        auto *scenes = processing.findChild<QListWidget *>("opticalAcquisitions");
        require(search && load && scenes, "Native optical search/load controls available");
        QTest::mouseClick(search, Qt::LeftButton);
        QElapsedTimer wait;
        wait.start();
        while (processing.busy() && wait.elapsed() < 50000)
          QTest::qWait(100);
        const auto *status = processing.findChild<QLabel *>("processingStatus");
        require(!processing.busy() && scenes->count() > 0,
                "Actual dated Sentinel-2 acquisitions found: " +
                    (status ? status->text() : QString{}));
        scenes->setCurrentRow(0);
        QTest::mouseClick(load, Qt::LeftButton);
        const auto layer = waitForProduct("Public Sentinel-2");
        require(layer.sourceID.startsWith("S2") && !layer.acquisition.isEmpty(),
                "Actual optical source ID and acquisition time retained");
        QDir().mkpath(output + "/public-optical");
        exportProductFile(layer.manifest, output + "/public-optical/result.json");
        exportProductFile(layer.preview, output + "/public-optical/preview.png");
        if (!layer.geotiff.isEmpty())
          exportProductFile(layer.geotiff, output + "/public-optical/true-color.tif");
        actualAcquisitions.append(QJsonObject{{"sourceID", layer.sourceID},
                                              {"acquiredAt", layer.acquisition},
                                              {"bounds", layer.bounds.json()},
                                              {"previewWidth", layer.pixelSize.width()},
                                              {"previewHeight", layer.pixelSize.height()}});
        window.attachProduct(layer);
        window.missions().save(file);
        window.missions().load(file);
        require(window.missions().selected()["items"].toArray().size() >= 2,
                "Real acquisition link survives mission save/reopen");
        window.map().grabFramebuffer().save(output + "/public-optical-map.png");
        processing.grab().save(output + "/public-optical-workflow.png");
        processing.hide();
      }
      if (searchRadar) {
        auto &processing = window.processingWindow();
        require(processing.setRadarQuery({{9.18, 45.455, 9.185, 45.46},
                                          QDateTime::currentDateTimeUtc().date().addDays(-30),
                                          QDateTime::currentDateTimeUtc().date()}),
                "Public radar query uses bounded region and UTC dates");
        processing.show();
        processing.raise();
        auto *tabs = processing.findChild<QTabWidget *>("processingTabs");
        require(tabs, "Native acquisition tabs available");
        tabs->setCurrentIndex(1);
        auto *search = processing.findChild<QPushButton *>("radarSearch");
        auto *focus = processing.findChild<QPushButton *>("radarFootprint");
        require(search && focus, "Native Sentinel-1 search and footprint controls available");
        QTest::mouseClick(search, Qt::LeftButton);
        QElapsedTimer wait;
        wait.start();
        while (processing.busy() && wait.elapsed() < 50000)
          QTest::qWait(100);
        const auto scenes = processing.radarPanel()->scenes();
        const auto *status = processing.findChild<QLabel *>("processingStatus");
        require(!processing.busy() && !scenes.isEmpty(),
                "Actual Sentinel-1 acquisitions found: " + (status ? status->text() : QString{}));
        require(!scenes.first().footprint.isEmpty(),
                "Actual acquisition footprint supplied by Copernicus");
        QTest::mouseClick(focus, Qt::LeftButton);
        QTest::qWait(3000);
        for (const auto &scene : scenes)
          actualAcquisitions.append(scene.json());
        processing.grab().save(output + "/public-radar-workflow.png");
        window.map().grabFramebuffer().save(output + "/public-radar-footprint.png");
        processing.hide();
      }
      if (!actualTileset.isEmpty()) {
        require(window.map().loadTileset(QUrl::fromUserInput(actualTileset)),
                "Existing 3D Tiles source admitted");
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < 30000 && window.map().sceneMeshes() == 0)
          QTest::qWait(50);
        require(window.map().sceneMeshes() > 0, "Actual 3D Tiles content rendered on the GPU");
        require(!window.map().grabFramebuffer().isNull(), "Actual 3D Tiles framebuffer readback");
        window.map().grabFramebuffer().save(output + "/existing-scene.png");
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
              QJsonObject{
                  {"passed", true},
                  {"checks", labels},
                  {"backend", window.map().backend()},
                  {"gpu", window.map().gpu()},
                  {"sceneSource", actualTileset},
                  {"sceneStatus", window.map().sceneStatus()},
                  {"actualAcquisitions", actualAcquisitions},
                  {"dailyVisualization", actualDaily},
                  {"elapsedSeconds", elapsed.elapsed() / 1000.0},
                  {"processCpuSeconds", double(std::clock() - cpuStart) / CLOCKS_PER_SEC},
                  {"peakResidentBytes",
                   [] {
                     struct rusage usage{};
                     getrusage(RUSAGE_SELF, &usage);
#if defined(Q_OS_MACOS)
                     return double(usage.ru_maxrss);
#else
                              return double(usage.ru_maxrss) * 1024;
#endif
                   }()},
                  {"fixtures",
                   live ? "Live public basemap, terrain and NASA visualization; not calibrated "
                          "optical/SAR product validation."
                   : retrieveOptical
                       ? "Generated fixtures plus the identified real public optical acquisition."
                   : searchRadar ? "Generated fixtures plus public radar catalogue metadata; no "
                                   "archive download."
                                 : "Generated local QA; not real satellite acquisitions"}})
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
