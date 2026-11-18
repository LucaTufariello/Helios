#include "MainWindow.h"
#include "Core/JsonFile.h"
#include "Core/MonitoringRunner.h"
#include "DomainWindow.h"
#include "DossierPanel.h"
#include "InferenceWindow.h"
#include "InvestigationWorkbench.h"
#include "MapToolsWindow.h"
#include "MonitorPanel.h"
#include "MonitoringInboxPanel.h"
#include "SiteMonitoringPanel.h"
#include "TimelinePanel.h"
#include "WorkspacePanel.h"
#include "WorkspaceStyle.h"
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
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
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

namespace helios {
MainWindow::MainWindow(const QString &profile, bool networking, QWidget *parent)
    : QMainWindow(parent), settings_(profile + "/desktop.ini", QSettings::IniFormat),
      missions_(this) {
  QDir().mkpath(profile);
  setWindowTitle("Helios — Geospatial intelligence");
  resize(1440, 960);
  setMinimumSize(1080, 700);
  auto *header = new QWidget;
  header->setObjectName("workspaceHeader");
  header->setProperty("role", "header");
  header->setFixedHeight(66);
  auto *root = new QWidget;
  auto *layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(header);
  selection_ = new QLabel;
  selection_->setProperty("role", "notice");
  selection_->setMargin(8);
  selection_->hide();
  layout->addWidget(selection_);
  map_ = new MapWidget(profile + "/map-cache", networking);
  map_->setObjectName("geographicMap");
  workspace_ = new WorkspaceSurface(map_);
  layout->addWidget(workspace_, 1);
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
  domain_ = new DomainWindow(profile, this, networking, true);
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
    showSources("All", 2);
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
  auto *dossier = mission_->dossierPanel();
  monitoring_ = new MonitoringRunner(
      profile + "/jobs", missions_, processing_->scientificQueue(),
      [this] { return satellite_->library(); },
      [this](WorkerRequest request, const QString &token) {
        return processing_->submitMonitoring(std::move(request), token);
      },
      this);
  connect(monitors_, &MonitorRunner::changed, monitoring_,
          [this] { monitoring_->setAutomatic(monitors_->automatic()); });
  auto *siteMonitoring = new SiteMonitoringPanel(missions_, *monitoring_);
  connect(siteMonitoring, &SiteMonitoringPanel::resumeAutomaticRequested, monitors_,
          &MonitorRunner::start);
  dossier->addPage(siteMonitoring, "Monitoring");
  connect(dossier, &DossierPanel::areaChanged, siteMonitoring, &SiteMonitoringPanel::setArea);
  siteMonitoring->setArea(dossier->areaID());
  auto *inbox = new MonitoringInboxPanel(missions_, *monitoring_);
  inbox->setObjectName("missionReviewInbox");
  mission_->addPage(inbox, "Review inbox");
  for (auto *panel : {siteMonitoring->findChild<MonitoringInboxPanel *>(), inbox}) {
    connect(panel, &MonitoringInboxPanel::reviewRequested, review_, &ReviewWindow::openReview);
    connect(panel, &MonitoringInboxPanel::dossierRequested, this, [this](const QString &area) {
      mission_->openDossier(area);
      showMission();
    });
  }
  connect(siteMonitoring, &SiteMonitoringPanel::drawControlRequested, this, [this, siteMonitoring] {
    map_->beginRegion(siteMonitoring);
    show();
    raise();
  });
  connect(map_, &MapWidget::regionSelected, siteMonitoring,
          [this, siteMonitoring](Bounds bounds, QObject *owner) {
            if (owner == siteMonitoring) {
              siteMonitoring->acceptControl(bounds);
              showMission();
            }
          });
  connect(siteMonitoring, &SiteMonitoringPanel::focusControlRequested, this, [this](Bounds bounds) {
    map_->focus(bounds);
    show();
    raise();
  });
  connect(satellite_, &SatelliteWindow::libraryChanged, monitoring_, &MonitoringRunner::refresh);
  connect(monitors_, &MonitorRunner::discovered, monitoring_, &MonitoringRunner::discovered);
  monitoring_->refresh();
  auto *investigations = new InvestigationWorkbench(profile, missions_, mission_);
  mission_->addPage(investigations->overview(), "Investigation");
  mission_->addPage(investigations->assetsPage(), "Assets");
  mission_->addPage(investigations->connectionsPage(), "Connections");
  mission_->addPage(investigations->findingsPage(), "Findings");
  investigations->setLibrary(satellite_->library());
  connect(satellite_, &SatelliteWindow::libraryChanged, investigations,
          [this, investigations] { investigations->setLibrary(satellite_->library()); });
  investigations->setCaptureProvider([this] {
    return qMakePair(
        map_->grabFramebuffer(),
        QJsonObject{{"capturedAt", foundationNow()},
                    {"camera", map_->camera().json()},
                    {"attribution",
                     "Map context: Helios display; basemap tiles © OpenStreetMap contributors and "
                     "Esri World Imagery as configured in the map. Satellite overlays retain "
                     "provider attribution per evidence source."}});
  });
  connect(investigations, &InvestigationWorkbench::focusRequested, this, [this](Bounds bounds) {
    map_->focus(bounds);
    show();
    raise();
  });
  connect(investigations, &InvestigationWorkbench::evidenceRequested, this,
          &MainWindow::showEvidence);
  connect(investigations, &InvestigationWorkbench::dossierRequested, mission_,
          &MissionWindow::openDossier);
  connect(investigations, &InvestigationWorkbench::findingsRequested, this, [this, investigations] {
    mission_->showPage(investigations->findingsPage());
    mission_->show();
    mission_->raise();
  });
  connect(review_, &ReviewWindow::findingRequested, investigations,
          &InvestigationWorkbench::promote);
  connect(dossier, &DossierPanel::findingRequested, investigations,
          &InvestigationWorkbench::promote);
  connect(dossier, &DossierPanel::assetRequested, this, [this, investigations](const QString &) {
    mission_->showPage(investigations->assetsPage());
  });
  for (auto *panel : {siteMonitoring->findChild<MonitoringInboxPanel *>(), inbox})
    connect(panel, &MonitoringInboxPanel::findingRequested, investigations,
            &InvestigationWorkbench::promote);
  dossier->setLibrary(satellite_->library());
  connect(satellite_, &SatelliteWindow::libraryChanged, this,
          [this, dossier] { dossier->setLibrary(satellite_->library()); });
  connect(dossier, &DossierPanel::evidenceRequested, this, &MainWindow::showEvidence);
  connect(dossier, &DossierPanel::focusRequested, this, [this](Bounds bounds) {
    map_->focus(bounds);
    show();
    raise();
    activateWindow();
  });
  connect(dossier, &DossierPanel::acquisitionRequested, this, [this](Bounds bounds, bool radar) {
    const auto end = QDateTime::currentDateTimeUtc().date(), start = end.addDays(-14);
    auto *tabs = processing_->findChild<QTabWidget *>("processingTabs");
    const bool applied = radar ? processing_->setRadarQuery({bounds, start, end})
                               : processing_->setQuery({bounds, start, end, 30, "sentinel2"});
    if (applied)
      tabs->setCurrentIndex(radar ? 1 : 0);
    processing_->show();
    processing_->raise();
    processing_->activateWindow();
  });
  connect(dossier, &DossierPanel::comparisonRequested, this,
          [this, timeline](const QString &area, const QString &base, const QString &later) {
            timeline->prepareComparison(area, base, later);
            mission_->showPage(timeline);
          });
  connect(dossier, &DossierPanel::reviewRequested, review_, &ReviewWindow::prepareArea);
  connect(review_, &ReviewWindow::dossierRequested, this, [this](const QString &area) {
    mission_->openDossier(area);
    showMission();
  });
  connect(map_, &MapWidget::pointSelected, dossier,
          [dossier](Coordinate point, double, bool, bool) { dossier->setMapSelection(point); });
  connect(mission_, &MissionWindow::importSiteRequested, this, [this] {
    const auto mission = missions_.selectedID();
    QStringList labels;
    QVector<QJsonObject> sites;
    if (review_->sites())
      for (const auto value : review_->sites()->sites()) {
        const auto site = value.toObject();
        sites.append(site);
        labels.append(site["name"].toString() + " · " + site["id"].toString().left(8));
      }
    if (sites.isEmpty()) {
      QMessageBox::information(
          mission_, "Import saved site",
          "Save a site in Infrastructure review first, or draw a new area here.");
      return;
    }
    bool ok = false;
    const auto picked = QInputDialog::getItem(mission_, "Import saved site",
                                              "Copy a frozen boundary into the selected mission",
                                              labels, 0, false, &ok);
    if (!ok)
      return;
    try {
      mission_->openDossier(missions_.importDossierSite(mission, sites[labels.indexOf(picked)]));
    } catch (const std::exception &e) {
      QMessageBox::warning(mission_, "Import saved site", QString::fromUtf8(e.what()));
    }
  });
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
  using design::Symbol;
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(20, 0, 16, 0);
  headerLayout->setSpacing(6);
  auto *logo = new QLabel;
  logo->setPixmap(design::icon(Symbol::Globe).pixmap(30, 30));
  headerLayout->addWidget(logo);
  auto *brand = new QVBoxLayout;
  brand->setSpacing(0);
  brand->addWidget(design::label("HELIOS", "brand"));
  auto *tagline = design::label("GEOSPATIAL INTELLIGENCE", "kicker");
  tagline->setObjectName("workspaceTagline");
  brand->addWidget(tagline);
  headerLayout->addLayout(brand);
  headerLayout->addSpacing(24);
  auto *sections = new QButtonGroup(this);
  sections->setExclusive(true);
  auto sectionButton = [&](const QString &name, Symbol symbol) {
    auto *b = new QToolButton;
    b->setText(name);
    b->setIcon(design::icon(symbol));
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setProperty("role", "nav");
    b->setCheckable(true);
    b->setObjectName("section-" + name);
    b->setAccessibleName(name + " workspace");
    sections->addButton(b);
    headerLayout->addWidget(b);
    return b;
  };
  auto *mapButton = sectionButton("Map", Symbol::Globe);
  mapButton->setChecked(true);
  connect(mapButton, &QToolButton::clicked, this, &MainWindow::hideWorkspacePanels);
  auto *mapAction = new QAction("Map", this);
  mapAction->setShortcut(QKeySequence("Ctrl+1"));
  connect(mapAction, &QAction::triggered, mapButton, &QToolButton::click);
  addAction(mapAction);
  auto *satelliteButton = sectionButton("Satellite", Symbol::Satellite);
  connect(satelliteButton, &QToolButton::clicked, this, &MainWindow::showSatellite);
  auto *satelliteAction = new QAction("Satellite", this);
  connect(satelliteAction, &QAction::triggered, satelliteButton, &QToolButton::click);
  satelliteAction->setShortcut(QKeySequence("Ctrl+2"));
  int shortcut = 3;
  for (auto entry :
       {qMakePair(QString("Marine"), Symbol::Marine), qMakePair(QString("Sky"), Symbol::Aircraft),
        qMakePair(QString("Ground"), Symbol::Ground)}) {
    auto *b = sectionButton(entry.first, entry.second);
    connect(b, &QToolButton::clicked, this, [this, name = entry.first] {
      domain_->stopFollowing();
      showSources(name);
    });
    auto *key = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(shortcut++)), this);
    connect(key, &QShortcut::activated, b, &QToolButton::click);
  }
  headerLayout->addStretch();
  auto *sourceCount = design::label("0 sources on", "secondary");
  sourceCount->setObjectName("enabledSourceCount");
  sourceCount->setMinimumWidth(85);
  auto updateSourceCount = [this, sourceCount] {
    int count = 0;
    for (const auto &feed : domainFeeds())
      if (domain_->network().enabled(feed.id))
        ++count;
    sourceCount->setText(QString("%1 source%2 on").arg(count).arg(count == 1 ? "" : "s"));
  };
  connect(&domain_->network(), &DomainNetwork::changed, sourceCount, updateSourceCount);
  updateSourceCount();
  headerLayout->addWidget(sourceCount);
  headerLayout->addSpacing(10);
  auto *missionButton = new QToolButton;
  missionButton->setText("Mission Control");
  missionButton->setIcon(design::icon(Symbol::Mission));
  missionButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  missionButton->setProperty("role", "nav");
  missionButton->setObjectName("openMissionControl");
  connect(missionButton, &QToolButton::clicked, this, &MainWindow::showMission);
  headerLayout->addWidget(missionButton);
  auto *overflow = new QToolButton;
  overflow->setIcon(design::icon(Symbol::More));
  overflow->setProperty("role", "quiet");
  overflow->setAccessibleName("Application menu");
  overflow->setToolTip("Application menu");
  overflow->setObjectName("applicationMenu");
  overflow->setPopupMode(QToolButton::InstantPopup);
  auto *applicationMenu = new QMenu(overflow);
  overflow->setMenu(applicationMenu);
  headerLayout->addWidget(overflow);
  auto *reviewAction = new QAction("Infrastructure review", this);
  connect(reviewAction, &QAction::triggered, review_, [this] {
    review_->show();
    review_->raise();
    review_->activateWindow();
  });
  auto *missionAction = new QAction("Mission Control", this);
  connect(missionAction, &QAction::triggered, this, &MainWindow::showMission);
  missionAction->setShortcut(QKeySequence("Ctrl+6"));
  auto *surfaceAction = new QAction("3D Surface", this);
  auto *surface = new QDialog(this);
  surface->setWindowTitle("3D Surface — Helios");
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
  auto *sourcesAction = new QAction("Data sources", this);
  connect(sourcesAction, &QAction::triggered, this, [this] { showSources(); });
  auto *toolsAction = new QAction("Workspace tools", this);
  connect(toolsAction, &QAction::triggered, this, &MainWindow::showMapTools);
  auto *inferenceAction = new QAction("Local image assistance…", this);
  connect(inferenceAction, &QAction::triggered, this, &MainWindow::showInference);
  auto addPanel = [this](const QString &id, const QString &title, Symbol icon, QWidget *content,
                         QSize size, bool right) {
    auto *panel = new WorkspacePanel(id, title, icon, content, workspace_, size, right);
    panels_.insert(id, panel);
    workspace_->addPanel(panel);
    panel->restoreLayout(settings_.value("panels/" + id).toMap());
    return panel;
  };
  addPanel("satellite", "Satellite analysis", Symbol::Satellite, satellite_, {680, 660}, true);
  addPanel("sources", "Sources & observations", Symbol::Layers, domain_, {470, 660}, false);
  addPanel("tools", "Workspace tools", Symbol::Tools, tools_, {680, 660}, true);
  auto *layerContent = new QWidget;
  auto *layerLayout = new QVBoxLayout(layerContent);
  layerLayout->setContentsMargins(16, 16, 16, 16);
  layerLayout->setSpacing(14);
  layerLayout->addWidget(design::label("SURFACE", "kicker"));
  auto *layerForm = new QFormLayout;
  auto *mapChoice = new QComboBox;
  mapChoice->addItems({"Satellite imagery", "Street mapping"});
  layerForm->addRow("Basemap", mapChoice);
  auto *terrainChoice = new QCheckBox("Terrain relief");
  terrainChoice->setChecked(true);
  layerForm->addRow("Surface", terrainChoice);
  auto *styleChoice = new QComboBox;
  for (const auto &name : QStringList{"Standard", "Monochrome", "Thermal", "Night vision"})
    styleChoice->addItem(name, name == "Night vision" ? "nightVision" : name.toLower());
  layerForm->addRow("Display", styleChoice);
  layerLayout->addLayout(layerForm);
  auto *displayNote = design::label("Display styles change visualization only. Source measurements "
                                    "and scientific exports retain their original values.",
                                    "secondary");
  displayNote->setWordWrap(true);
  layerLayout->addWidget(displayNote);
  connect(mapChoice, &QComboBox::currentIndexChanged, this,
          [this](int i) { map_->setStreets(i == 1); });
  connect(terrainChoice, &QCheckBox::toggled, map_, &MapWidget::setTerrain);
  connect(styleChoice, &QComboBox::currentIndexChanged, this,
          [this, styleChoice] { map_->setDisplayStyle(styleChoice->currentData().toString()); });
  connect(map_, &MapWidget::statusChanged, layerContent,
          [this, mapChoice, terrainChoice, styleChoice] {
            QSignalBlocker a(mapChoice), b(terrainChoice), c(styleChoice);
            mapChoice->setCurrentIndex(map_->streets() ? 1 : 0);
            terrainChoice->setChecked(map_->terrainEnabled());
            styleChoice->setCurrentIndex(styleChoice->findData(map_->displayStyle()));
          });
  auto *cameraTitle = design::label("CAMERA", "kicker");
  layerLayout->addWidget(cameraTitle);
  auto *tilt = new QSlider(Qt::Horizontal);
  tilt->setRange(0, 80);
  tilt->setAccessibleName("Camera tilt in degrees");
  auto *heading = new QSlider(Qt::Horizontal);
  heading->setRange(0, 359);
  heading->setAccessibleName("Camera heading in degrees");
  auto *cameraForm = new QFormLayout;
  cameraForm->addRow("Tilt", tilt);
  cameraForm->addRow("Heading", heading);
  layerLayout->addLayout(cameraForm);
  connect(tilt, &QSlider::valueChanged, this,
          [this](int value) { map_->orient(map_->camera().heading, value); });
  connect(heading, &QSlider::valueChanged, this,
          [this](int value) { map_->orient(value, map_->camera().tilt); });
  auto *advancedMap = new QPushButton("Daily imagery & workspace tools");
  connect(advancedMap, &QPushButton::clicked, this, &MainWindow::showMapTools);
  layerLayout->addWidget(advancedMap);
  layerLayout->addStretch();
  addPanel("layers", "Map & layers", Symbol::Layers, layerContent, {380, 560}, true);
  auto *searchCard = new QFrame;
  searchCard->setProperty("role", "panel");
  auto *searchLayout = new QHBoxLayout(searchCard);
  searchLayout->setContentsMargins(10, 3, 5, 3);
  searchLayout->setSpacing(6);
  auto *searchIcon = new QLabel;
  searchIcon->setPixmap(design::icon(Symbol::Search).pixmap(18, 18));
  searchLayout->addWidget(searchIcon);
  search_ = new QLineEdit;
  search_->setObjectName("workspaceSearch");
  search_->setClearButtonEnabled(true);
  search_->setPlaceholderText("Search places or coordinates…");
  search_->setFrame(false);
  search_->setAccessibleName("Search places or latitude, longitude");
  searchLayout->addWidget(search_, 1);
  auto *find = new QToolButton;
  find->setIcon(design::icon(Symbol::Search));
  find->setProperty("role", "quiet");
  find->setToolTip("Search location");
  find->setAccessibleName("Search location");
  searchLayout->addWidget(find);
  auto search = [this] {
    const auto parts =
        search_->text().trimmed().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
    bool a = false, b = false;
    const Coordinate point{parts.value(0).toDouble(&a), parts.value(1).toDouble(&b)};
    if (parts.size() == 2 && a && b && point.valid()) {
      auto c = map_->camera();
      c.center = point;
      c.range = std::min(c.range, 20000.0);
      map_->setCamera(c);
    } else if (!search_->text().trimmed().isEmpty()) {
      showMapTools();
      tools_->search(search_->text());
    }
  };
  connect(search_, &QLineEdit::returnPressed, this, search);
  connect(find, &QToolButton::clicked, this, search);
  auto *searchShortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
  connect(searchShortcut, &QShortcut::activated, this, [this] {
    search_->setFocus();
    search_->selectAll();
  });
  auto *mapActions = new QFrame;
  mapActions->setProperty("role", "panel");
  auto *mapActionsLayout = new QHBoxLayout(mapActions);
  mapActionsLayout->setContentsMargins(5, 3, 5, 3);
  mapActionsLayout->setSpacing(2);
  auto tool = [&](const QString &title, Symbol symbol, const std::function<void()> &fn,
                  bool text = true) {
    auto *b = new QToolButton;
    b->setText(title);
    b->setIcon(design::icon(symbol));
    b->setProperty("role", "quiet");
    b->setToolTip(title);
    b->setAccessibleName(title);
    b->setToolButtonStyle(text ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    connect(b, &QToolButton::clicked, this, fn);
    mapActionsLayout->addWidget(b);
    return b;
  };
  auto panelTool = [&](const QString &title, Symbol symbol, const QString &id,
                       const std::function<void()> &fn) {
    auto *b = tool(title, symbol, [this, id, fn] {
      if (panels_.value(id)->isVisible())
        panels_.value(id)->hide();
      else
        fn();
    });
    b->setCheckable(true);
    connect(panels_.value(id), &WorkspacePanel::visibilityChanged, b, &QToolButton::setChecked);
    return b;
  };
  panelTool("Layers", Symbol::Layers, "layers", [this] { panels_["layers"]->reveal(); });
  tool("Inspect", Symbol::Inspect, [this] { showSources("All", 2); });
  tool("Data", Symbol::Data, [this] { showSources("All", 2, true); });
  panelTool("Tools", Symbol::Tools, "tools", [this] { showMapTools(); });
  tool("Zoom in", Symbol::Plus, [this] { map_->zoom(2); }, false);
  tool("Zoom out", Symbol::Minus, [this] { map_->zoom(-2); }, false);
  tool("North-up", Symbol::North, [this] { map_->orient(0, 0); }, false);
  tool(
      "Full globe", Symbol::Globe, [this] { map_->setCamera(Camera{{0, 0}, 30000000, 0, 0}); },
      false);
  workspace_->setCommands(searchCard, mapActions);
  auto *cancel = new QAction("Cancel selection", this);
  cancel->setShortcut(Qt::Key_Escape);
  cancel->setEnabled(false);
  addAction(cancel);
  connect(cancel, &QAction::triggered, map_, &MapWidget::cancelRegion);
  auto *hidePanels = new QShortcut(QKeySequence("Ctrl+Shift+H"), this);
  connect(hidePanels, &QShortcut::activated, this, &MainWindow::hideWorkspacePanels);
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
        this, "Helios desktop preview",
        "C++20 / Qt desktop client with GPU map, terrain, saved satellite previews and mission "
        "documents, satellite acquisition/processing and infrastructure review.\n\n"
        "Native source feeds, map tools and CPU image assistance are available. "
        "Fresh Mac checks and real SNAP processing remain unverified.\n\n" +
            map_->backend() + " · " + map_->gpu());
  });
  applicationMenu->addMenu(file);
  applicationMenu->addMenu(view);
  applicationMenu->addMenu(help);
  applicationMenu->addSeparator();
  connect(applicationMenu->addAction("Hide all panels  ·  Ctrl+Shift+H"), &QAction::triggered, this,
          &MainWindow::hideWorkspacePanels);
  connect(applicationMenu->addAction("Reset panel layout"), &QAction::triggered, workspace_,
          &WorkspaceSurface::resetPanels);
#ifndef Q_OS_MACOS
  menuBar()->hide();
#endif
  auto *credits = new QLabel(
      "<a href=\"https://www.esri.com/en-us/legal/terms/data-attributions\">Esri imagery "
      "providers</a> · <a href=\"https://www.openstreetmap.org/copyright\">OSM contributors</a> · "
      "<a href=\"https://github.com/tilezen/joerd/blob/master/docs/attribution.md\">Mapzen terrain "
      "credits</a> · Basemap mosaic, not live");
  credits->setOpenExternalLinks(true);
  credits->setProperty("role", "secondary");
  credits->setContentsMargins(10, 3, 10, 3);
  statusBar()->setSizeGripEnabled(false);
  statusBar()->addWidget(credits, 1);
  coordinates_ = design::label({}, "mono");
  coordinates_->setContentsMargins(8, 3, 14, 3);
  statusBar()->addPermanentWidget(coordinates_);
  status_ = design::label({}, "mono");
  auto *sceneInfo = design::label({}, "secondary");
  sceneInfo->setTextFormat(Qt::PlainText);
  sceneInfo->setWordWrap(true);
  sceneInfo->setMargin(6);
  sceneInfo->hide();
  layout->addWidget(sceneInfo);
  auto *displayInfo = design::label({}, "secondary");
  displayInfo->setObjectName("mapDisplayCaption");
  displayInfo->setTextFormat(Qt::PlainText);
  displayInfo->setWordWrap(true);
  displayInfo->setMargin(6);
  displayInfo->hide();
  layout->addWidget(displayInfo);
  layout->addWidget(status_);
  overlay_->setProperty("role", "secondary");
  domainSummary_->setProperty("role", "secondary");
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
    cancel->setEnabled(active);
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
  if (auto *button = findChild<QToolButton *>("section-Satellite"))
    button->setChecked(true);
  panels_["satellite"]->reveal();
}
void MainWindow::showSources(const QString &section, int page, bool raw) {
  domain_->setSection(section);
  domain_->showWorkspacePage(page, raw);
  panels_["sources"]->reveal();
}
void MainWindow::showMapTools() {
  panels_["tools"]->reveal();
}
void MainWindow::hideWorkspacePanels() {
  workspace_->hidePanels();
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
void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  const bool compact = width() < 1180;
  if (auto *tagline = findChild<QLabel *>("workspaceTagline"))
    tagline->setVisible(!compact);
  if (auto *count = findChild<QLabel *>("enabledSourceCount"))
    count->setVisible(!compact);
  if (auto *mission = findChild<QToolButton *>("openMissionControl")) {
    mission->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    mission->setToolTip("Mission Control · Ctrl+6");
    mission->setAccessibleName("Mission Control");
  }
}
MainWindow::~MainWindow() {
  if (monitoring_) {
    monitoring_->stop();
    delete monitoring_;
    monitoring_ = nullptr;
  }
  monitors_->stop();
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
  for (auto it = panels_.cbegin(); it != panels_.cend(); ++it)
    settings_.setValue("panels/" + it.key(), it.value()->layoutState());
  settings_.setValue("camera", QJsonDocument(map_->camera().json()).toJson(QJsonDocument::Compact));
  settings_.setValue("missionLibrary", missions_.filename());
  domain_->network().cancel();
  domain_->hide();
  inference_->hide();
  satellite_->hide();
  mission_->hide();
  processing_->hide();
  monitoring_->stop();
  monitors_->stop();
  event->accept();
}
} // namespace helios
