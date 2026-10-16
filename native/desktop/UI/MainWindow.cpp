#include "MainWindow.h"
#include "Core/JsonFile.h"
#include "DomainWindow.h"
#include "InferenceWindow.h"
#include "MapToolsWindow.h"
#include "MonitorPanel.h"
#include "TimelinePanel.h"
#include <QActionGroup>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

namespace godeye {
MainWindow::MainWindow(const QString &profile, bool networking, QWidget *parent)
    : QMainWindow(parent), settings_(profile + "/desktop.ini", QSettings::IniFormat),
      missions_(this) {
  QDir().mkpath(profile);
  setWindowTitle("GodEye — Desktop preview");
  resize(1280, 850);
  auto *root = new QWidget;
  auto *layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  selection_ = new QLabel;
  selection_->setMargin(8);
  selection_->hide();
  layout->addWidget(selection_);
  map_ = new MapWidget(profile + "/map-cache", networking);
  map_->setObjectName("geographicMap");
  layout->addWidget(map_, 1);
  overlay_ = new QLabel;
  overlay_->setTextFormat(Qt::PlainText);
  overlay_->setWordWrap(true);
  overlay_->setMargin(6);
  overlay_->hide();
  layout->addWidget(overlay_);
  domainSummary_ = new QLabel;
  domainSummary_->setTextFormat(Qt::PlainText);
  domainSummary_->setWordWrap(true);
  domainSummary_->setMargin(4);
  domainSummary_->hide();
  layout->addWidget(domainSummary_);
  setCentralWidget(root);
  satellite_ = new SatelliteWindow(profile, this);
  mission_ = new MissionWindow(missions_, *map_, this);
  processing_ = new ProcessingWindow(profile, *map_, this, networking);
  review_ = new ReviewWindow(profile, *map_, missions_, *satellite_, *processing_, this);
  domain_ = new DomainWindow(profile, this, networking);
  inference_ = new InferenceWindow(profile, this);
  domain_->setAttribute(Qt::WA_QuitOnClose, false);
  inference_->setAttribute(Qt::WA_QuitOnClose, false);
  tools_ = new MapToolsWindow(profile, *map_, *domain_, missions_, networking, this);
  connect(domain_, &DomainWindow::feedsChanged, this, [this](const QVector<DomainRecord> &records) {
    QVector<MapMarker> markers;
    QMap<QString, int> counts;
    QMap<QString, QVector<QVector<Coordinate>>> paths;
    QMap<QString, int> feedPoints;
    qsizetype pathPoints = 0;
    int omitted = 0;
    for (const auto &record : records) {
      if (record.feed == "annotations")
        continue;
      if (counts.value(record.feed) >= 300 || record.id.size() > 512 ||
          !record.coordinate.valid() ||
          (record.altitude && (!std::isfinite(*record.altitude) || *record.altitude < -2000 ||
                               *record.altitude > 1e8))) {
        ++omitted;
        continue;
      }
      ++counts[record.feed];
      markers.append({record.id, record.coordinate, record.altitude});
      for (const auto &line : record.paths) {
        if (pathPoints >= 2500 || feedPoints.value(record.feed) >= 700 || line.size() < 2)
          continue;
        const int take =
            std::min({100, int(2500 - pathPoints), 700 - feedPoints.value(record.feed)});
        if (take < 2)
          continue;
        QVector<Coordinate> reduced;
        const int amount = std::min(take, int(line.size()));
        for (int i = 0; i < amount; ++i)
          reduced.append(line[qsizetype(i) * (line.size() - 1) / (amount - 1)]);
        pathPoints += reduced.size();
        feedPoints[record.feed] += reduced.size();
        QString group = "sources";
        if (record.feed == "traffic") {
          const auto flow = record.details["Relative flow"].toDouble(-1);
          group = (record.details["Road closed"] == "Yes" || flow < .55) ? "traffic-slow"
                  : flow < .85                                           ? "traffic-moderate"
                                                                         : "traffic-free";
        }
        paths[group].append(reduced);
      }
    }
    if (!map_->setMarkers("sources", markers)) {
      map_->setMarkers("sources", {});
      omitted += markers.size();
      markers.clear();
    }
    map_->setPaths("sources", paths.value("sources"));
    map_->setPaths("traffic-slow", paths.value("traffic-slow"), {}, QColor(245, 65, 65));
    map_->setPaths("traffic-moderate", paths.value("traffic-moderate"), {}, QColor(255, 185, 55));
    map_->setPaths("traffic-free", paths.value("traffic-free"), {}, QColor(65, 220, 125));
    domainSummary_->setVisible(!records.isEmpty());
    domainSummary_->setText(
        QString("%1 source records on map%2 · Source times and attribution are in Sources. Lines "
                "are simplified for display; saved evidence retains admitted source geometry.")
            .arg(markers.size())
            .arg(omitted ? QString(" · %1 more in Sources (300 markers per feed / renderer limits)")
                               .arg(omitted)
                         : QString{}));
  });
  connect(map_, &MapWidget::markerSelected, this, [this](const QString &id) {
    if (id.startsWith("annotation:"))
      domain_->network().setEnabled("annotations", true);
    domain_->selectRecord(id);
    domain_->show();
    domain_->raise();
  });
  connect(domain_, &DomainWindow::focusRequested, this, [this](Coordinate point, double range) {
    frozenEvidence_ = false;
    map_->setMarkers("saved-evidence", {});
    map_->setPaths("saved-evidence", {});
    domain_->stopFollowing();
    auto camera = map_->camera();
    camera.center = point;
    camera.range = range;
    map_->setCamera(camera);
    show();
    raise();
  });
  connect(domain_, &DomainWindow::followPositionChanged, this, [this](Coordinate point) {
    followingCamera_ = true;
    auto camera = map_->camera();
    camera.center = point;
    map_->setCamera(camera);
    followingCamera_ = false;
  });
  auto *sourceViewTimer = new QTimer(this);
  sourceViewTimer->setSingleShot(true);
  sourceViewTimer->setInterval(150);
  connect(sourceViewTimer, &QTimer::timeout, this, [this] {
    const auto center = map_->camera().center;
    Bounds bounds{180, 90, -180, -90};
    bool valid = true;
    for (const QPointF p : {QPointF(0, 0), QPointF(map_->width(), 0), QPointF(0, map_->height()),
                            QPointF(map_->width(), map_->height())}) {
      const auto hit = map_->pick(p);
      if (!hit) {
        valid = false;
        break;
      }
      bounds.west = std::min(bounds.west, hit->coordinate.longitude);
      bounds.east = std::max(bounds.east, hit->coordinate.longitude);
      bounds.south = std::min(bounds.south, hit->coordinate.latitude);
      bounds.north = std::max(bounds.north, hit->coordinate.latitude);
    }
    domain_->setView(valid && bounds.validArea() ? bounds
                                                 : Bounds{center.longitude, center.latitude,
                                                          center.longitude, center.latitude});
  });
  connect(map_, &MapWidget::cameraChanged, this, [this, sourceViewTimer] {
    if (!followingCamera_)
      domain_->stopFollowing();
    sourceViewTimer->start();
  });
  connect(map_, &MapWidget::viewportChanged, sourceViewTimer, qOverload<>(&QTimer::start));
  sourceViewTimer->start();
  connect(domain_, &DomainWindow::liveSelectionRequested, this, [this] {
    frozenEvidence_ = false;
    map_->setMarkers("saved-evidence", {});
    map_->setPaths("saved-evidence", {});
  });
  connect(domain_, &DomainWindow::geometrySelected, this,
          [this](const QVector<QVector<Coordinate>> &paths, const QVector<double> &altitudes) {
            if (frozenEvidence_)
              return;
            map_->setMarkers("saved-evidence", {});
            map_->setPaths("saved-evidence", {});
            QVector<QVector<Coordinate>> reduced;
            QVector<QVector<double>> heights;
            qsizetype offset = 0, total = 0;
            for (const auto &path : paths) {
              const int take = std::min({int(path.size()), 1000, int(1500 - total)});
              QVector<Coordinate> line;
              QVector<double> h;
              if (take >= 2)
                for (int i = 0; i < take; ++i) {
                  const auto index = qsizetype(i) * (path.size() - 1) / (take - 1);
                  line.append(path[index]);
                  if (!altitudes.isEmpty() && offset + index < altitudes.size())
                    h.append(altitudes[offset + index]);
                }
              offset += path.size();
              total += line.size();
              if (line.size() >= 2) {
                reduced.append(line);
                heights.append(h);
              }
            }
            map_->setPaths("selected-source", reduced,
                           altitudes.isEmpty() ? QVector<QVector<double>>{} : heights,
                           QColor(255, 210, 80));
          });
  connect(domain_, &DomainWindow::evidenceRequested, this, [this](QJsonObject record) {
    try {
      missions_.addEvidence({{"id", newID()},
                             {"kind", "record"},
                             {"referenceID", record["id"]},
                             {"title", record["label"]},
                             {"savedAt", foundationNow()},
                             {"record", record}});
      showMission();
    } catch (const std::exception &e) {
      QMessageBox::warning(this, "Save source evidence", e.what());
    }
  });
  connect(
      inference_, &InferenceWindow::proposalAccepted, this,
      [this](const QString &missionID, const QJsonObject &proposal) {
        const auto id = proposal["id"].toString();
        if (!inferenceSource_ || missionID != inferenceMission_ ||
            proposal["evidenceID"] != inferenceEvidence_) {
          inference_->resolveProposal(id, false, "The analyzed evidence is no longer selected.");
          return;
        }
        const auto source = *inferenceSource_;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this,
                [this, watcher, missionID, proposal, id, source] {
                  auto error = watcher->result();
                  watcher->deleteLater();
                  bool linked = false;
                  if (missions_.selectedID() == missionID)
                    for (const auto value : missions_.selected()["items"].toArray()) {
                      const auto e = value.toObject();
                      if (e["id"] == proposal["evidenceID"] && e["kind"] == source.kind &&
                          e["referenceID"].toString().compare(source.jobID, Qt::CaseInsensitive) ==
                              0)
                        linked = true;
                    }
                  if (!linked)
                    error = "The source evidence link changed while validation was running.";
                  if (error.isEmpty())
                    try {
                      missions_.addObservationProposal(missionID, proposal);
                    } catch (const std::exception &e) {
                      error = QString::fromUtf8(e.what());
                    }
                  inference_->resolveProposal(id, error.isEmpty(), error);
                });
        watcher->setFuture(QtConcurrent::run([source, proposal] {
          try {
            const auto products = readProducts(source.manifest, false);
            bool found = false;
            for (const auto &current : products)
              if (current.id == source.id && current.manifestHash == source.manifestHash &&
                  current.preview == source.preview)
                found = true;
            if (!found)
              return QString("The source product changed. Reopen the evidence and analyze again.");
            const auto bytes = JsonFile::read(source.preview, 32 * 1024 * 1024);
            if (QString::fromLatin1(
                    QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) !=
                proposal["inputSHA256"].toString())
              return QString("The source preview changed after analysis. No proposal was added.");
            return QString{};
          } catch (const std::exception &e) {
            return QString::fromUtf8(e.what());
          }
        }));
      });
  monitors_ = new MonitorRunner(missions_, [this] { return satellite_->library(); }, this);
  monitors_->setNetworking(networking);
  auto *timeline = new TimelinePanel(missions_, *satellite_, *map_);
  auto *monitorPanel = new MonitorPanel(missions_, *monitors_);
  mission_->addPage(timeline, "Timeline");
  mission_->addPage(monitorPanel, "Monitors");
  connect(timeline, &TimelinePanel::evidenceRequested, this, &MainWindow::showEvidence);
  connect(timeline, &TimelinePanel::comparisonRequested, this,
          [this](const QVector<ProductLayer> &layers, double blend) {
            showLayers(layers, true);
            map_->setBlend(blend);
            show();
            raise();
          });
  connect(monitorPanel, &MonitorPanel::catalogueRequested, this,
          [this](const QJsonObject &monitor, Bounds bounds) {
            const auto end = QDateTime::currentDateTimeUtc().date(),
                       start = end.addDays(-monitor["lookbackDays"].toInt());
            auto *tabs = processing_->findChild<QTabWidget *>("processingTabs");
            if (monitor["kind"] == "New Sentinel-2 imagery") {
              processing_->setQuery(
                  {bounds, start, end, monitor["maximumCloud"].toDouble(), "sentinel2"});
              tabs->setCurrentIndex(0);
            } else {
              processing_->setRadarQuery({bounds, start, end});
              tabs->setCurrentIndex(1);
            }
            processing_->show();
            processing_->raise();
            if (auto *button = processing_->findChild<QPushButton *>(
                    monitor["kind"] == "New Sentinel-2 imagery" ? "opticalSearch" : "radarSearch"))
              button->click();
          });
  connect(satellite_, &SatelliteWindow::processingRequested, processing_, [this] {
    processing_->show();
    processing_->raise();
    processing_->activateWindow();
  });
  connect(processing_, &ProcessingWindow::productReady, this,
          [this](const QString &manifest) { satellite_->importManifest(manifest, true); });
  mission_->setAttribute(Qt::WA_QuitOnClose, false);
  satellite_->setAttribute(Qt::WA_QuitOnClose, false);
  auto *toolbar = addToolBar("Workspace");
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  auto *mapAction = toolbar->addAction("Map");
  connect(mapAction, &QAction::triggered, this, [this] {
    satellite_->hide();
    raise();
    map_->setFocus();
  });
  auto *satelliteAction = toolbar->addAction("Satellite");
  connect(satelliteAction, &QAction::triggered, this, &MainWindow::showSatellite);
  satelliteAction->setShortcut(QKeySequence("Ctrl+2"));
  for (const auto &section : QStringList{"Marine", "Sky", "Ground"}) {
    auto *action = toolbar->addAction(section);
    connect(action, &QAction::triggered, this, [this, section] {
      domain_->stopFollowing();
      domain_->setSection(section);
      domain_->show();
      domain_->raise();
    });
  }
  auto *reviewAction = new QAction("Infrastructure review", this);
  connect(reviewAction, &QAction::triggered, review_, [this] {
    review_->show();
    review_->raise();
    review_->activateWindow();
  });
  auto *missionAction = toolbar->addAction("Mission Control");
  connect(missionAction, &QAction::triggered, this, &MainWindow::showMission);
  missionAction->setShortcut(QKeySequence("Ctrl+6"));
  auto *surfaceAction = new QAction("3D Surface", this);
  auto *surface = new QDialog(this);
  surface->setWindowTitle("3D Surface — GodEye");
  surface->setAttribute(Qt::WA_QuitOnClose, false);
  surface->resize(620, 340);
  auto *surfaceLayout = new QVBoxLayout(surface);
  auto *surfaceHelp =
      new QLabel("Load triangle photogrammetry from an HTTPS tileset.json or a local directory. "
                 "Remote references stay on the selected origin; local references stay in the "
                 "selected directory. "
                 "Unsupported content is reported as a format failure.");
  surfaceHelp->setWordWrap(true);
  surfaceLayout->addWidget(surfaceHelp);
  auto *surfaceURL = new QLineEdit;
  surfaceURL->setAccessibleName("3D Tiles URL or local tileset.json");
  surfaceURL->setPlaceholderText("https://…/tileset.json");
  surfaceLayout->addWidget(surfaceURL);
  auto loadSurface = [this, surfaceURL] {
    if (!map_->loadTileset(QUrl::fromUserInput(surfaceURL->text())))
      QMessageBox::warning(
          this, "3D Tiles",
          "Choose a local tileset.json or an HTTPS URL without embedded credentials.");
  };
  auto *surfaceButtons = new QHBoxLayout;
  auto *loadURL = new QPushButton("Load URL");
  connect(loadURL, &QPushButton::clicked, this, loadSurface);
  connect(surfaceURL, &QLineEdit::returnPressed, this, loadSurface);
  surfaceButtons->addWidget(loadURL);
  auto *openLocal = new QPushButton("Open local tileset…");
  connect(openLocal, &QPushButton::clicked, this, [this, surfaceURL, loadSurface] {
    const auto path =
        QFileDialog::getOpenFileName(this, "Open local 3D Tiles", {}, "Tileset JSON (*.json)");
    if (path.isEmpty())
      return;
    surfaceURL->setText(QUrl::fromLocalFile(path).toString());
    loadSurface();
  });
  surfaceButtons->addWidget(openLocal);
  auto *focusSurface = new QPushButton("Focus coverage");
  connect(focusSurface, &QPushButton::clicked, this, [this] {
    if (!map_->focusTileset())
      status_->setText("The 3D Tiles root has not loaded yet.");
  });
  surfaceButtons->addWidget(focusSurface);
  auto *removeSurface = new QPushButton("Remove");
  connect(removeSurface, &QPushButton::clicked, map_, &MapWidget::removeTileset);
  surfaceButtons->addWidget(removeSurface);
  surfaceLayout->addLayout(surfaceButtons);
  auto *publicSurface = new QPushButton("Load public AGI headquarters photogrammetry");
  connect(publicSurface, &QPushButton::clicked, this, [surfaceURL, loadSurface] {
    surfaceURL->setText("https://pelican-public.s3.amazonaws.com/3dtiles/agi-hq/tileset.json");
    loadSurface();
  });
  surfaceLayout->addWidget(publicSurface);
  auto *surfaceStatus = new QLabel("No 3D Tiles loaded.");
  surfaceStatus->setTextFormat(Qt::PlainText);
  surfaceStatus->setWordWrap(true);
  surfaceLayout->addWidget(surfaceStatus);
  auto *surfaceCredits = new QLabel;
  surfaceCredits->setTextFormat(Qt::PlainText);
  surfaceCredits->setWordWrap(true);
  surfaceLayout->addWidget(surfaceCredits);
  surfaceLayout->addStretch();
  connect(map_, &MapWidget::statusChanged, surface, [this, surfaceStatus, surfaceCredits] {
    surfaceStatus->setText(map_->sceneStatus().isEmpty() ? "No 3D Tiles loaded."
                                                         : map_->sceneStatus());
    surfaceCredits->setText(map_->sceneCredits());
  });
  connect(surfaceAction, &QAction::triggered, surface, [surface] {
    surface->show();
    surface->raise();
    surface->activateWindow();
  });
  auto *sourcesAction = toolbar->addAction("Sources");
  connect(sourcesAction, &QAction::triggered, this, [this] {
    domain_->setSection("All");
    domain_->show();
    domain_->raise();
  });
  auto *toolsAction = toolbar->addAction("Map tools");
  connect(toolsAction, &QAction::triggered, tools_, [this] {
    tools_->show();
    tools_->raise();
  });
  auto *inferenceAction = new QAction("Local image assistance…", this);
  connect(inferenceAction, &QAction::triggered, this, &MainWindow::showInference);
  addToolBarBreak();
  toolbar = addToolBar("Navigation");
  toolbar->setMovable(false);
  search_ = new QLineEdit;
  search_->setPlaceholderText("Place or latitude, longitude");
  search_->setMaximumWidth(250);
  search_->setAccessibleName("Go to geographic coordinate");
  toolbar->addWidget(search_);
  connect(search_, &QLineEdit::returnPressed, this, [this] {
    const auto parts =
        search_->text().trimmed().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
    bool a = false, b = false;
    const double lat = parts.value(0).toDouble(&a), lon = parts.value(1).toDouble(&b);
    const Coordinate p{lat, lon};
    if (parts.size() != 2 || !a || !b || !p.valid()) {
      tools_->search(search_->text());
      return;
    }
    auto c = map_->camera();
    c.center = p;
    c.range = std::min(c.range, 20000.0);
    map_->setCamera(c);
  });
  auto *in = toolbar->addAction("Zoom in");
  connect(in, &QAction::triggered, this, [this] { map_->zoom(2); });
  auto *out = toolbar->addAction("Zoom out");
  connect(out, &QAction::triggered, this, [this] { map_->zoom(-2); });
  auto *north = toolbar->addAction("North-up");
  connect(north, &QAction::triggered, this, [this] { map_->orient(0, 0); });
  auto *world = toolbar->addAction("World");
  connect(world, &QAction::triggered, this,
          [this] { map_->setCamera(Camera{{0, 0}, 30000000, 0, 0}); });
  auto *cancel = toolbar->addAction("Cancel selection");
  cancel->setVisible(false);
  cancel->setShortcut(Qt::Key_Escape);
  connect(cancel, &QAction::triggered, map_, &MapWidget::cancelRegion);
  auto *view = menuBar()->addMenu("View");
  view->addAction(satelliteAction);
  view->addAction(missionAction);
  view->addAction(surfaceAction);
  view->addAction(reviewAction);
  view->addAction(sourcesAction);
  view->addAction(toolsAction);
  view->addAction(inferenceAction);
  auto *basemap = view->addMenu("Basemap");
  auto *group = new QActionGroup(this);
  for (const auto &label : QStringList{"Esri satellite", "OpenStreetMap"}) {
    auto *a = basemap->addAction(label);
    a->setCheckable(true);
    a->setChecked(label == "Esri satellite");
    group->addAction(a);
    connect(a, &QAction::triggered, this,
            [this, label] { map_->setStreets(label == "OpenStreetMap"); });
  }
  auto *terrain = view->addAction("Terrain");
  terrain->setCheckable(true);
  terrain->setChecked(true);
  connect(terrain, &QAction::toggled, map_, &MapWidget::setTerrain);
  auto *tiltToolbar = addToolBar("3D camera");
  tiltToolbar->setMovable(false);
  tiltToolbar->addWidget(new QLabel("Tilt "));
  auto *tilt = new QSlider(Qt::Horizontal);
  tilt->setRange(0, 80);
  tilt->setMaximumWidth(140);
  tilt->setAccessibleName("Camera tilt in degrees");
  tiltToolbar->addWidget(tilt);
  tiltToolbar->addWidget(new QLabel(" Heading "));
  auto *heading = new QSlider(Qt::Horizontal);
  heading->setRange(0, 359);
  heading->setMaximumWidth(160);
  heading->setAccessibleName("Camera heading in degrees");
  tiltToolbar->addWidget(heading);
  connect(tilt, &QSlider::valueChanged, this,
          [this](int value) { map_->orient(map_->camera().heading, value); });
  connect(heading, &QSlider::valueChanged, this,
          [this](int value) { map_->orient(value, map_->camera().tilt); });
  auto *file = menuBar()->addMenu("File");
  auto *open = file->addAction("Open mission library…");
  connect(open, &QAction::triggered, mission_, &MissionWindow::openLibrary);
  auto *save = file->addAction("Save mission library");
  save->setShortcut(QKeySequence::Save);
  connect(save, &QAction::triggered, this, [this] { mission_->save(); });
  auto *quit = file->addAction("Quit");
  quit->setShortcut(QKeySequence::Quit);
  connect(quit, &QAction::triggered, this, &QWidget::close);
  auto *help = menuBar()->addMenu("Help");
  auto *about = help->addAction("Desktop migration status");
  connect(about, &QAction::triggered, this, [this] {
    QMessageBox::information(
        this, "GodEye desktop preview",
        "C++20 / Qt desktop client with GPU map, terrain, saved satellite previews and mission "
        "documents, satellite acquisition/processing and infrastructure review.\n\n"
        "Native source feeds, map tools and CPU image assistance are available. "
        "Fresh Mac checks and real SNAP processing remain unverified.\n\n" +
            map_->backend() + " · " + map_->gpu());
  });
  auto *credits = new QLabel(
      "<a href=\"https://www.esri.com/en-us/legal/terms/data-attributions\">Esri imagery "
      "providers</a> · <a href=\"https://www.openstreetmap.org/copyright\">OSM contributors</a> · "
      "<a href=\"https://github.com/tilezen/joerd/blob/master/docs/attribution.md\">Mapzen terrain "
      "credits</a> · Basemap mosaic, not live");
  credits->setOpenExternalLinks(true);
  statusBar()->addWidget(credits, 1);
  coordinates_ = new QLabel;
  statusBar()->addPermanentWidget(coordinates_);
  status_ = new QLabel;
  auto *sceneInfo = new QLabel;
  sceneInfo->setTextFormat(Qt::PlainText);
  sceneInfo->setWordWrap(true);
  sceneInfo->setMargin(6);
  sceneInfo->hide();
  layout->addWidget(sceneInfo);
  auto *displayInfo = new QLabel;
  displayInfo->setObjectName("mapDisplayCaption");
  displayInfo->setTextFormat(Qt::PlainText);
  displayInfo->setWordWrap(true);
  displayInfo->setMargin(6);
  displayInfo->hide();
  layout->addWidget(displayInfo);
  layout->addWidget(status_);
  status_->setMargin(6);
  status_->setTextFormat(Qt::PlainText);
  auto update = [this, tilt, heading] {
    const auto c = map_->camera();
    coordinates_->setText(QString("%1°, %2° · %3 km")
                              .arg(c.center.latitude, 0, 'f', 4)
                              .arg(c.center.longitude, 0, 'f', 4)
                              .arg(c.range / 1000, 0, 'f', 1));
    QSignalBlocker a(tilt), b(heading);
    tilt->setValue(int(c.tilt));
    heading->setValue(int(c.heading));
  };
  connect(map_, &MapWidget::cameraChanged, this, update);
  connect(map_, &MapWidget::statusChanged, this, [this, sceneInfo, displayInfo] {
    QStringList display;
    const auto &daily = map_->dailyImagery();
    if (daily.source() != DailySource::Off)
      display.append(QString("NASA GIBS · %1 · requested UTC date %2. Visualization date; pixel "
                             "observation times may differ.")
                         .arg(DailyImageryCache::sourceName(daily.source()),
                              daily.date().toString(Qt::ISODate)));
    const auto style = map_->displayStyle();
    if (style == "monochrome")
      display.append("Monochrome basemap/daily display; scientific product colours are retained.");
    else if (style != "standard")
      display.append(
          style == "thermal"
              ? "Thermal-style recolouring for display; no thermal sensor measurement."
              : "Night vision recolouring for display; no night vision sensor measurement.");
    const auto displayText = display.join('\n');
    if (displayInfo->text() != displayText)
      displayInfo->setText(displayText);
    displayInfo->setVisible(!display.isEmpty());
    sceneInfo->setVisible(!map_->sceneStatus().isEmpty());
    sceneInfo->setText(map_->sceneStatus() + "\n" + map_->sceneCredits());
    status_->setText(
        !map_->error().isEmpty()
            ? map_->error()
            : QString("%1 · %2 · %3 loaded map tiles · %4 pending · %5 failed requests")
                  .arg(map_->backend().isEmpty() ? "Initializing GPU" : map_->backend(),
                       map_->gpu())
                  .arg(map_->renderedTiles())
                  .arg(map_->tiles().pending())
                  .arg(map_->tiles().failures()));
  });
  connect(map_, &MapWidget::selectionChanged, this, [this, cancel](bool active) {
    selection_->setVisible(active);
    selection_->setText(
        "Select two corners on the map. Regions are bounded to 10° per axis; Escape cancels.");
    cancel->setVisible(active);
  });
  connect(map_, &MapWidget::pointSelected, this,
          [this](Coordinate p, double height, bool terrain, bool scene) {
            status_->setText(QString("%1, %2 · rendered height %3 m · %4")
                                 .arg(p.latitude, 0, 'f', 6)
                                 .arg(p.longitude, 0, 'f', 6)
                                 .arg(height, 0, 'f', 1)
                                 .arg(scene     ? "loaded 3D Tiles geometry"
                                      : terrain ? "loaded terrain mesh"
                                                : "ellipsoid mesh/fallback"));
          });
  connect(satellite_, &SatelliteWindow::layersChanged, this, &MainWindow::showLayers);
  connect(review_, &ReviewWindow::layersChanged, this, &MainWindow::showLayers);
  connect(satellite_, &SatelliteWindow::blendChanged, map_, &MapWidget::setBlend);
  connect(satellite_, &SatelliteWindow::pixelGridChanged, map_, &MapWidget::setPixelGrid);
  connect(satellite_, &SatelliteWindow::linkRequested, this, &MainWindow::attachProduct);
  connect(mission_, &MissionWindow::evidenceRequested, this, &MainWindow::showEvidence);
  connect(mission_, &MissionWindow::attachDisplayedImagery, this, [this] {
    QVector<ProductLayer> acquisitions;
    for (const auto &p : activeLayers_)
      if (p.kind == "optical" || p.kind == "radar")
        acquisitions.append(p);
    if (acquisitions.isEmpty()) {
      QMessageBox::information(this, "Attach imagery",
                               "Show an imported satellite product on the map first.");
      return;
    }
    for (const auto &p : acquisitions)
      attachProduct(p);
  });
  connect(&missions_, &Missions::changed, this, &MainWindow::synchronizeMission);
  const auto camera = QJsonDocument::fromJson(settings_.value("camera").toByteArray()).object();
  if (auto c = Camera::read(camera)) {
    followingCamera_ = true;
    map_->setCamera(*c);
    followingCamera_ = false;
  }
  update();
  const auto library = settings_.value("missionLibrary").toString();
  if (!library.isEmpty() && QFileInfo::exists(library)) {
    try {
      missions_.load(library);
    } catch (const std::exception &e) {
      status_->setText("Saved mission library could not be opened; its files were preserved. " +
                       QString::fromUtf8(e.what()));
    }
  }
  monitors_->start();
}
void MainWindow::showInference() {
  QVector<QPair<ProductLayer, QString>> choices;
  QStringList labels;
  if (!missions_.filename().isEmpty())
    for (const auto value : missions_.selected()["items"].toArray()) {
      const auto evidence = value.toObject();
      if (evidence["kind"] != "optical")
        continue;
      for (const auto &product : satellite_->library())
        if (product.kind == "optical" &&
            product.jobID.compare(evidence["referenceID"].toString(), Qt::CaseInsensitive) == 0) {
          choices.append({product, evidence["id"].toString()});
          labels.append(product.label + " · " + evidence["id"].toString().left(8));
        }
    }
  if (choices.isEmpty()) {
    QMessageBox::information(this, "Local image assistance",
                             "Save a mission and link an imported optical product to it first.");
    return;
  }
  bool ok = true;
  const auto label = choices.size() == 1
                         ? labels.first()
                         : QInputDialog::getItem(this, "Local image assistance",
                                                 "Linked optical evidence", labels, 0, false, &ok);
  if (!ok)
    return;
  const auto index = labels.indexOf(label);
  if (index < 0)
    return;
  inferenceSource_ = choices[index].first;
  inferenceEvidence_ = choices[index].second;
  inferenceMission_ = missions_.selectedID();
  inference_->setEvidence(*inferenceSource_, inferenceEvidence_, inferenceMission_);
  inference_->show();
  inference_->raise();
}
void MainWindow::showMission() {
  mission_->show();
  mission_->raise();
  mission_->activateWindow();
}
void MainWindow::showSatellite() {
  satellite_->show();
  satellite_->raise();
  satellite_->activateWindow();
}
void MainWindow::showLayers(const QVector<ProductLayer> &layers, bool focus) {
  activeLayers_ = layers;
  map_->setOverlays(layers);
  overlay_->setVisible(!layers.isEmpty());
  QStringList labels;
  for (const auto &p : layers)
    labels.append(p.label);
  overlay_->setText(labels.join('\n') +
                    (layers.size() == 2
                         ? "\nVisual overlay comparison; registration is not implied."
                         : QString{}));
  if (focus && !layers.isEmpty())
    map_->focus(layers.last().bounds);
}
void MainWindow::synchronizeMission() {
  QVector<Bounds> areas;
  QVector<Coordinate> points;
  const auto p = missions_.selected(), b = p["brief"].toObject();
  bool linked = false;
  if (p["id"] == inferenceMission_)
    for (const auto item : p["items"].toArray())
      if (item.toObject()["id"] == inferenceEvidence_)
        linked = true;
  if (!linked)
    inference_->invalidateEvidence();
  for (const auto v : b["areas"].toArray())
    if (auto region = Bounds::read(v.toObject()["region"]))
      areas.append(*region);
  for (const auto v : b["observations"].toArray())
    if (auto c = Coordinate::read(v.toObject()["coordinate"].toObject()))
      points.append(*c);
  map_->setAreas(areas);
  map_->setMarkers(points);
}
void MainWindow::attachProduct(const ProductLayer &product) {
  try {
    for (const auto v : missions_.selected()["items"].toArray()) {
      const auto e = v.toObject();
      if (e["kind"].toString() == product.kind &&
          e["referenceID"].toString().compare(product.jobID, Qt::CaseInsensitive) == 0 &&
          e["acquisition"].toObject()["displayBand"].toString() == product.band) {
        status_->setText("This output is already linked to the selected mission.");
        return;
      }
    }
    missions_.addEvidence(product.missionEvidence());
    showMission();
  } catch (const std::exception &e) {
    QMessageBox::warning(this, "Link imagery", e.what());
  }
}
void MainWindow::showEvidence(const QJsonObject &e) {
  domain_->stopFollowing();
  frozenEvidence_ = false;
  map_->setPaths("selected-source", {});
  map_->setPaths("saved-evidence", {});
  map_->setMarkers("saved-evidence", {});
  QJsonObject record = e["record"].toObject();
  if (e["kind"] == "annotation") {
    const auto a = e["annotation"].toObject();
    auto points = a["points"].toArray();
    auto line = points;
    if (a["closed"].toBool() && !line.isEmpty())
      line.append(line.first());
    record = {{"id", "annotation:" + a["id"].toString()},
              {"label", a["title"]},
              {"source", "User-authored annotation"},
              {"coordinate", points.isEmpty() ? QJsonValue() : points.first()},
              {"observedAt", a["createdAt"]},
              {"details",
               QJsonObject{{"Geometry", a["closed"].toBool() ? "Polygon" : "Point / polyline"}}},
              {"footprint", a["closed"].toBool() ? points : QJsonArray{}},
              {"paths", QJsonArray{line}}};
  }
  if (!record.isEmpty()) {
    frozenEvidence_ = true;
    QVector<QVector<Coordinate>> paths;
    qsizetype count = 0;
    auto add = [&](const QJsonArray &values) {
      const int take = std::min({int(values.size()), 500, int(1500 - count)});
      if (take < 2)
        return;
      QVector<Coordinate> line;
      for (int i = 0; i < take; ++i)
        if (auto p = Coordinate::read(
                values[qsizetype(i) * (values.size() - 1) / (take - 1)].toObject()))
          line.append(*p);
      count += line.size();
      if (line.size() >= 2)
        paths.append(line);
    };
    for (const auto path : record["paths"].toArray())
      add(path.toArray());
    if (paths.isEmpty())
      add(record["footprint"].toArray());
    if (auto point = Coordinate::read(record["coordinate"].toObject())) {
      std::optional<double> height;
      if (record["altitude"].isDouble())
        height = record["altitude"].toDouble();
      map_->setMarkers("saved-evidence", {{{}, *point, height}}, QColor(255, 210, 80));
    }
    map_->setPaths("saved-evidence", paths, {}, QColor(255, 210, 80));
    domain_->showSavedRecord(record);
  }
  if (auto camera = Camera::read(e["camera"].toObject()))
    map_->setCamera(*camera);
  else if (auto region = Bounds::read(e["site"].toObject()["region"]))
    map_->focus(*region);
  else if (auto point = Coordinate::read(e["record"].toObject()["coordinate"].toObject())) {
    auto c = map_->camera();
    c.center = *point;
    c.range = std::min(c.range, 8000.0);
    map_->setCamera(c);
  } else if (e["kind"] == "optical" || e["kind"] == "radar") {
    const auto ref = e["referenceID"].toString(),
               band = e["acquisition"].toObject()["displayBand"].toString();
    for (const auto &p : satellite_->library())
      if (p.jobID.compare(ref, Qt::CaseInsensitive) == 0 && (band.isEmpty() || p.band == band)) {
        satellite_->showProduct(p.id);
        show();
        raise();
        return;
      }
    if (auto b = Bounds::read(e["acquisition"].toObject()["bounds"]))
      map_->focus(*b);
    QMessageBox::information(
        this, "Image reference",
        "The mission retains this acquisition's metadata, but its raster has not been imported "
        "into the desktop library. Import its result.json to view pixels.");
  } else if (e["annotation"].isObject()) {
    const auto points = e["annotation"].toObject()["points"].toArray();
    if (!points.isEmpty())
      if (auto p = Coordinate::read(points.first().toObject())) {
        auto c = map_->camera();
        c.center = *p;
        c.range = std::min(c.range, 8000.0);
        map_->setCamera(c);
      }
  }
  show();
  raise();
}
void MainWindow::closeEvent(QCloseEvent *event) {
  const auto *timeline = mission_->findChild<TimelinePanel *>();
  if (satellite_->busy() || processing_->busy() || review_->busy() || inference_->busy() ||
      (timeline && timeline->busy())) {
    QMessageBox::information(this, "Background work",
                             "Wait for acquisition, processing or export to finish, or cancel the "
                             "active work before quitting.");
    event->ignore();
    return;
  }
  if (!review_->close()) {
    event->ignore();
    return;
  }
  if (!mission_->resolveDrafts()) {
    event->ignore();
    return;
  }
  if (missions_.dirty()) {
    const auto choice =
        QMessageBox::question(this, "Unsaved missions", "Save the mission library before quitting?",
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !mission_->save())) {
      event->ignore();
      return;
    }
  }
  if (!tools_->saveSession()) {
    event->ignore();
    return;
  }
  tools_->hide();
  settings_.setValue("camera", QJsonDocument(map_->camera().json()).toJson(QJsonDocument::Compact));
  settings_.setValue("missionLibrary", missions_.filename());
  domain_->network().cancel();
  domain_->hide();
  inference_->hide();
  satellite_->hide();
  mission_->hide();
  processing_->hide();
  monitors_->stop();
  event->accept();
}
} // namespace godeye
