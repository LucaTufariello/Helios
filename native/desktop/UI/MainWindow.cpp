#include "MainWindow.h"
#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
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
#include <QToolBar>
#include <QVBoxLayout>

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
  setCentralWidget(root);
  satellite_ = new SatelliteWindow(profile, this);
  mission_ = new MissionWindow(missions_, *map_, this);
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
  auto *missionAction = toolbar->addAction("Mission Control");
  connect(missionAction, &QAction::triggered, this, &MainWindow::showMission);
  missionAction->setShortcut(QKeySequence("Ctrl+6"));
  toolbar->addSeparator();
  search_ = new QLineEdit;
  search_->setPlaceholderText("Latitude, longitude");
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
      status_->setText("Enter latitude, longitude in decimal degrees.");
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
        "documents.\n\nLive domain feeds, satellite acquisition/processing controls, "
        "photorealistic 3D Tiles and AI inference are still in the existing macOS "
        "client.\n\nLinux/Wayland/Vulkan and GPU inference require hardware validation.\n\n" +
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
  connect(map_, &MapWidget::statusChanged, this, [this] {
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
  connect(map_, &MapWidget::pointSelected, this, [this](Coordinate p, double height, bool terrain) {
    status_->setText(QString("%1, %2 · rendered height %3 m · %4")
                         .arg(p.latitude, 0, 'f', 6)
                         .arg(p.longitude, 0, 'f', 6)
                         .arg(height, 0, 'f', 1)
                         .arg(terrain ? "loaded terrain mesh" : "ellipsoid mesh/fallback"));
  });
  connect(satellite_, &SatelliteWindow::layersChanged, this,
          [this](const QVector<ProductLayer> &layers, bool focus) {
            map_->setOverlays(layers);
            overlay_->setVisible(!layers.isEmpty());
            QStringList labels;
            for (const auto &p : layers)
              labels.append(p.label);
            overlay_->setText(labels.join("\n") +
                              (layers.size() == 2
                                   ? "\nVisual overlay comparison; registration is not implied."
                                   : QString()));
            if (focus && !layers.isEmpty())
              map_->focus(layers.last().bounds);
          });
  connect(satellite_, &SatelliteWindow::blendChanged, map_, &MapWidget::setBlend);
  connect(satellite_, &SatelliteWindow::pixelGridChanged, map_, &MapWidget::setPixelGrid);
  connect(satellite_, &SatelliteWindow::linkRequested, this, &MainWindow::attachProduct);
  connect(mission_, &MissionWindow::evidenceRequested, this, &MainWindow::showEvidence);
  connect(mission_, &MissionWindow::attachDisplayedImagery, this, [this] {
    if (satellite_->displayed().isEmpty()) {
      QMessageBox::information(this, "Attach imagery",
                               "Show an imported satellite product on the map first.");
      return;
    }
    for (const auto &p : satellite_->displayed())
      attachProduct(p);
  });
  connect(&missions_, &Missions::changed, this, &MainWindow::synchronizeMission);
  const auto camera = QJsonDocument::fromJson(settings_.value("camera").toByteArray()).object();
  if (auto c = Camera::read(camera))
    map_->setCamera(*c);
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
void MainWindow::synchronizeMission() {
  QVector<Bounds> areas;
  QVector<Coordinate> points;
  const auto p = missions_.selected(), b = p["brief"].toObject();
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
  if (satellite_->busy()) {
    QMessageBox::information(this, "Background work",
                             "Wait for the current product import/export to finish, or cancel its "
                             "export before quitting.");
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
  settings_.setValue("camera", QJsonDocument(map_->camera().json()).toJson(QJsonDocument::Compact));
  settings_.setValue("missionLibrary", missions_.filename());
  satellite_->hide();
  mission_->hide();
  event->accept();
}
} // namespace godeye
