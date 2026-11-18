#include "MapToolsWindow.h"
#include "Core/Timeline.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
namespace helios {
namespace {
QString coordinateText(Coordinate p) {
  return QString::number(p.latitude, 'f', 6) + ", " + QString::number(p.longitude, 'f', 6);
}
QVector<Coordinate> thin(const QVector<Coordinate> &points, int maximum) {
  if (maximum < 2)
    return {};
  if (points.size() <= maximum)
    return points;
  QVector<Coordinate> result;
  for (int i = 0; i < maximum; ++i)
    result.append(points[qsizetype(i) * (points.size() - 1) / (maximum - 1)]);
  return result;
}
QVector<Coordinate> annotationPoints(const QJsonObject &value) {
  QVector<Coordinate> result;
  for (const auto point : value["points"].toArray())
    if (auto c = Coordinate::read(point.toObject()))
      result.append(*c);
  return result;
}
} // namespace
MapToolsWindow::MapToolsWindow(const QString &profile, MapWidget &map, DomainWindow &domain,
                               Missions &missions, bool networking, QWidget *parent)
    : QDialog(parent), map_(map), domain_(domain), missions_(missions),
      annotations_(profile + "/annotations.json"), places_(this), directions_(this),
      profile_(profile), networking_(networking) {
  setWindowFlag(Qt::Window, true);
  setAttribute(Qt::WA_QuitOnClose, false);
  setObjectName("mapToolsWindow");
  setWindowTitle("Map tools — Helios");
  resize(690, 610);
  auto *layout = new QVBoxLayout(this);
  auto *tabs = new QTabWidget;
  tabs->setObjectName("mapToolsTabs");
  tabs->setUsesScrollButtons(true);
  tabs->tabBar()->setExpanding(false);
  layout->addWidget(tabs);
  auto page = [tabs](const QString &name) {
    auto *p = new QWidget;
    auto *l = new QVBoxLayout(p);
    tabs->addTab(p, name);
    return l;
  };
  auto button = [](QBoxLayout *l, const QString &name) {
    auto *b = new QPushButton(name);
    l->addWidget(b);
    return b;
  };
  auto *searchLayout = page("Places");
  auto *searchBar = new QHBoxLayout;
  searchLayout->addLayout(searchBar);
  placeQuery_ = new QLineEdit;
  placeQuery_->setObjectName("placeQuery");
  placeQuery_->setPlaceholderText("Place or latitude, longitude");
  searchBar->addWidget(placeQuery_);
  auto *find = button(searchBar, "Search");
  auto *cancelSearch = button(searchBar, "Cancel");
  placeStatus_ = new QLabel("Photon / OpenStreetMap · search is sent only on request.");
  placeStatus_->setWordWrap(true);
  searchLayout->addWidget(placeStatus_);
  placeResults_ = new QListWidget;
  placeResults_->setObjectName("placeResults");
  searchLayout->addWidget(placeResults_, 1);
  connect(find, &QPushButton::clicked, this, [this] { search(placeQuery_->text()); });
  connect(placeQuery_, &QLineEdit::returnPressed, find, &QPushButton::click);
  connect(cancelSearch, &QPushButton::clicked, &places_, &MapLookup::cancel);
  connect(&places_, &MapLookup::changed, this, [this, find] {
    find->setEnabled(!places_.busy());
    placeStatus_->setText(places_.busy() ? "Searching Photon…" : places_.error());
  });
  connect(&places_, &MapLookup::completed, this, [this](bool ok) {
    placeResults_->clear();
    if (!ok)
      return;
    for (const auto &place : places_.places()) {
      auto *i =
          new QListWidgetItem(place.label + "\n" + coordinateText(place.coordinate), placeResults_);
      i->setData(Qt::UserRole, place.coordinate.json());
    }
    placeStatus_->setText(
        QString("%1 matches · Photon / © OpenStreetMap contributors").arg(placeResults_->count()));
  });
  connect(placeResults_, &QListWidget::itemActivated, this, [this](QListWidgetItem *i) {
    if (auto p = Coordinate::read(i->data(Qt::UserRole).toJsonObject())) {
      auto c = map_.camera();
      c.center = *p;
      c.range = 12000;
      map_.setCamera(c);
    }
  });
  auto *routeLayout = page("Directions");
  auto *routeForm = new QFormLayout;
  routeLayout->addLayout(routeForm);
  start_ = new QLineEdit;
  start_->setObjectName("routeStart");
  end_ = new QLineEdit;
  end_->setObjectName("routeEnd");
  routeForm->addRow("Start (latitude, longitude)", start_);
  routeForm->addRow("End (latitude, longitude)", end_);
  mode_ = new QComboBox;
  mode_->addItem("Driving", "car");
  mode_->addItem("Walking", "foot");
  mode_->addItem("Cycling", "bike");
  routeForm->addRow("Mode", mode_);
  auto *routeButtons = new QHBoxLayout;
  routeLayout->addLayout(routeButtons);
  connect(button(routeButtons, "Pick start"), &QPushButton::clicked, this, [this] {
    map_.beginPointCapture(this);
    pickEndpoint_ = 1;
    routeStatus_->setText("Click the start point on the map.");
  });
  connect(button(routeButtons, "Pick end"), &QPushButton::clicked, this, [this] {
    map_.beginPointCapture(this);
    pickEndpoint_ = 2;
    routeStatus_->setText("Click the end point on the map.");
  });
  auto *route = button(routeButtons, "Find route");
  auto *cancelRoute = button(routeButtons, "Cancel");
  auto *clearRoute = button(routeButtons, "Clear");
  routeStatus_ =
      new QLabel("OSRM / © OpenStreetMap contributors · endpoints at most 1,000 km apart.");
  routeStatus_->setWordWrap(true);
  routeLayout->addWidget(routeStatus_);
  routeSteps_ = new QListWidget;
  routeSteps_->setObjectName("routeSteps");
  routeLayout->addWidget(routeSteps_, 1);
  connect(route, &QPushButton::clicked, this, [this] {
    auto a = coordinate(start_->text()), b = coordinate(end_->text());
    if (!a || !b) {
      routeStatus_->setText("Enter valid latitude, longitude for both endpoints.");
      return;
    }
    if (!networking_) {
      routeStatus_->setText("Offline mode: directions are unavailable.");
      return;
    }
    if (!directions_.route(*a, *b, mode_->currentData().toString()))
      routeStatus_->setText("Choose endpoints within 1,000 km.");
  });
  connect(cancelRoute, &QPushButton::clicked, &directions_, &MapLookup::cancel);
  connect(clearRoute, &QPushButton::clicked, this, [this] {
    directions_.cancel();
    map_.setPaths("route", {});
    map_.setMarkers("route", {});
    routeSteps_->clear();
  });
  connect(&directions_, &MapLookup::changed, this, [this, route] {
    route->setEnabled(!directions_.busy());
    routeStatus_->setText(directions_.busy() ? "Finding route…" : directions_.error());
  });
  connect(&directions_, &MapLookup::completed, this, [this](bool ok) {
    if (!ok)
      return;
    const auto r = directions_.result();
    routeSteps_->clear();
    for (const auto &step : r.steps) {
      auto *i = new QListWidgetItem(
          step.instruction + QString(" · %1 m").arg(step.distance, 0, 'f', 0), routeSteps_);
      i->setData(Qt::UserRole, step.coordinate.json());
    }
    map_.setPaths("route", {thin(r.points, 1500)}, {}, QColor(60, 150, 255));
    if (!r.points.isEmpty()) {
      map_.setMarkers("route", {{{}, r.points.first(), {}}, {{}, r.points.last(), {}}},
                      QColor(70, 255, 140));
      auto c = map_.camera();
      c.center = r.points.first();
      c.range = std::clamp(r.distance * 2, 3000.0, 3000000.0);
      map_.setCamera(c);
    }
    routeStatus_->setText(QString("%1 km · %2 min · OSRM estimates · %3 retained route points; map "
                                  "display capped at 1,500 points.")
                              .arg(r.distance / 1000, 0, 'f', 1)
                              .arg(r.duration / 60, 0, 'f', 0)
                              .arg(r.points.size()));
  });
  connect(routeSteps_, &QListWidget::itemActivated, this, [this](QListWidgetItem *i) {
    if (auto p = Coordinate::read(i->data(Qt::UserRole).toJsonObject())) {
      auto c = map_.camera();
      c.center = *p;
      c.range = 3000;
      map_.setCamera(c);
    }
  });
  auto *drawLayout = page("Annotations");
  auto *drawForm = new QFormLayout;
  drawLayout->addLayout(drawForm);
  title_ = new QLineEdit;
  title_->setObjectName("annotationTitle");
  shape_ = new QComboBox;
  shape_->setObjectName("annotationShape");
  shape_->addItems({"Point", "Polyline", "Polygon"});
  drawForm->addRow("Title", title_);
  drawForm->addRow("Geometry", shape_);
  auto *drawButtons = new QHBoxLayout;
  drawLayout->addLayout(drawButtons);
  auto *begin = button(drawButtons, "Start drawing");
  begin->setObjectName("annotationBegin");
  auto *finish = button(drawButtons, "Save drawing");
  finish->setObjectName("annotationSave");
  auto *undo = button(drawButtons, "Undo point");
  auto *discard = button(drawButtons, "Cancel drawing");
  drawingStatus_ =
      new QLabel("User-authored annotations are stored locally. Click the map to add points.");
  drawingStatus_->setWordWrap(true);
  drawLayout->addWidget(drawingStatus_);
  annotationList_ = new QListWidget;
  annotationList_->setObjectName("annotationList");
  drawLayout->addWidget(annotationList_, 1);
  auto *annotationButtons = new QHBoxLayout;
  drawLayout->addLayout(annotationButtons);
  auto *show = button(annotationButtons, "Show");
  auto *rename = button(annotationButtons, "Rename…");
  auto *edit = button(annotationButtons, "Redraw");
  auto *remove = button(annotationButtons, "Remove");
  auto *evidence = button(annotationButtons, "Link to mission");
  connect(begin, &QPushButton::clicked, this, [this] {
    if (!ready_ || !resolveDrawing())
      return;
    drawing_ = true;
    editingAnnotation_.clear();
    drawingPoints_.clear();
    map_.beginPointCapture(this);
    drawPreview();
  });
  connect(discard, &QPushButton::clicked, this, [this] {
    drawing_ = false;
    map_.endPointCapture(this);
    editingAnnotation_.clear();
    drawingPoints_.clear();
    drawPreview();
    persistDraft();
  });
  connect(undo, &QPushButton::clicked, this, [this] {
    if (!drawingPoints_.isEmpty())
      drawingPoints_.removeLast();
    drawPreview();
  });
  connect(finish, &QPushButton::clicked, this, [this] {
    if (!ready_ || !drawing_)
      return;
    try {
      const bool closed = shape_->currentIndex() == 2;
      if (editingAnnotation_.isEmpty())
        annotations_.add(newAnnotation(title_->text(), drawingPoints_, closed));
      else
        annotations_.update(editingAnnotation_, title_->text(), drawingPoints_, closed);
      drawing_ = false;
      map_.endPointCapture(this);
      editingAnnotation_.clear();
      drawingPoints_.clear();
      drawPreview();
      domain_.network().setEnabled("annotations", true);
      updateAnnotations();
      persistDraft();
    } catch (const std::exception &e) {
      drawingStatus_->setText(e.what());
    }
  });
  auto selected = [this]() -> QJsonObject {
    auto *i = annotationList_->currentItem();
    return i ? i->data(Qt::UserRole).toJsonObject() : QJsonObject{};
  };
  connect(show, &QPushButton::clicked, this, [this, selected] {
    auto points = annotationPoints(selected());
    if (!points.isEmpty()) {
      auto c = map_.camera();
      c.center = points.first();
      c.range = 8000;
      map_.setCamera(c);
    }
  });
  connect(rename, &QPushButton::clicked, this, [this, selected] {
    const auto a = selected();
    if (a.isEmpty())
      return;
    bool ok = false;
    const auto title = QInputDialog::getText(this, "Rename annotation", "Title", QLineEdit::Normal,
                                             a["title"].toString(), &ok);
    if (ok)
      try {
        annotations_.update(a["id"].toString(), title, annotationPoints(a), a["closed"].toBool());
        updateAnnotations();
      } catch (const std::exception &e) {
        showError(e.what());
      }
  });
  connect(edit, &QPushButton::clicked, this, [this, selected] {
    const auto a = selected();
    if (a.isEmpty())
      return;
    if (!resolveDrawing())
      return;
    map_.beginPointCapture(this);
    editingAnnotation_ = a["id"].toString();
    title_->setText(a["title"].toString());
    shape_->setCurrentIndex(a["closed"].toBool() ? 2 : (a["points"].toArray().size() == 1 ? 0 : 1));
    drawingPoints_.clear();
    drawing_ = true;
    drawPreview();
  });
  connect(remove, &QPushButton::clicked, this, [this, selected] {
    const auto a = selected();
    if (a.isEmpty())
      return;
    try {
      annotations_.remove(a["id"].toString());
      updateAnnotations();
    } catch (const std::exception &e) {
      showError(e.what());
    }
  });
  connect(evidence, &QPushButton::clicked, this, [this, selected] {
    const auto a = selected();
    if (a.isEmpty())
      return;
    try {
      missions_.addEvidence({{"id", newID()},
                             {"kind", "annotation"},
                             {"referenceID", a["id"]},
                             {"title", a["title"]},
                             {"savedAt", foundationNow()},
                             {"annotation", a}});
      drawingStatus_->setText("Annotation snapshot linked to the selected mission.");
    } catch (const std::exception &e) {
      showError(e.what());
    }
  });
  connect(tabs, &QTabWidget::currentChanged, this, [this](int page) {
    pickEndpoint_ = 0;
    map_.endPointCapture(this);
    if (drawing_ && page == 2) {
      map_.beginPointCapture(this);
      drawPreview();
    } else if (drawing_)
      drawingStatus_->setText("Drawing paused. Return to Annotations to continue the saved draft.");
  });
  connect(&map_, &MapWidget::pointSelected, this, [this](Coordinate p, double, bool, bool) {
    if (!isVisible())
      return;
    if (pickEndpoint_) {
      (pickEndpoint_ == 1 ? start_ : end_)->setText(coordinateText(p));
      pickEndpoint_ = 0;
      if (!drawing_ || !title_->isVisible())
        map_.endPointCapture(this);
      return;
    }
    if (!drawing_ || !title_->isVisible())
      return;
    if (drawingPoints_.size() >= 2000) {
      drawingStatus_->setText("The drawing has reached its 2,000-point limit.");
      return;
    }
    if (shape_->currentIndex() == 0)
      drawingPoints_.clear();
    drawingPoints_.append(p);
    drawPreview();
  });
  auto *display = page("Display");
  auto *displayForm = new QFormLayout;
  display->addLayout(displayForm);
  basemap_ = new QComboBox;
  basemap_->addItem("Esri satellite", "satellite");
  basemap_->addItem("OpenStreetMap", "streets");
  displayForm->addRow("Basemap", basemap_);
  style_ = new QComboBox;
  style_->setObjectName("displayStyle");
  for (const auto &pair : QVector<QPair<QString, QString>>{{"Standard", "standard"},
                                                           {"Monochrome", "monochrome"},
                                                           {"Night vision display", "nightVision"},
                                                           {"Thermal-style display", "thermal"}})
    style_->addItem(pair.first, pair.second);
  displayForm->addRow("Display style", style_);
  auto *styleHelp = new QLabel("Display styles recolour basemap/daily imagery only. Night vision "
                               "and thermal-style display do not represent sensor measurements. "
                               "Scientific product previews retain their original colours.");
  styleHelp->setWordWrap(true);
  display->addWidget(styleHelp);
  imagery_ = new QComboBox;
  imagery_->setObjectName("dailySource");
  for (const auto s : {DailySource::Off, DailySource::TrueColor, DailySource::Terra,
                       DailySource::NightLights, DailySource::Thermal})
    imagery_->addItem(DailyImageryCache::sourceName(s), DailyImageryCache::sourceID(s));
  displayForm->addRow("NASA daily visualization", imagery_);
  date_ = new QDateEdit(QDateTime::currentDateTimeUtc().date());
  date_->setDisplayFormat("yyyy-MM-dd");
  date_->setCalendarPopup(true);
  date_->setMinimumDate(QDate(2000, 1, 1));
  date_->setMaximumDate(QDateTime::currentDateTimeUtc().date());
  displayForm->addRow("Requested UTC date", date_);
  opacity_ = new QSlider(Qt::Horizontal);
  opacity_->setRange(0, 100);
  opacity_->setValue(85);
  displayForm->addRow("Daily opacity", opacity_);
  dailyStatus_ = new QLabel;
  dailyStatus_->setWordWrap(true);
  display->addWidget(dailyStatus_);
  terrain_ = new QCheckBox("Terrain");
  terrain_->setChecked(map_.terrainEnabled());
  display->addWidget(terrain_);
  solar_ = new QCheckBox("Approximate solar terminator (current UTC)");
  display->addWidget(solar_);
  solarStatus_ = new QLabel;
  solarStatus_->setWordWrap(true);
  display->addWidget(solarStatus_);
  display->addStretch();
  connect(basemap_, &QComboBox::currentIndexChanged, this,
          [this] { map_.setStreets(basemap_->currentData() == "streets"); });
  connect(style_, &QComboBox::currentIndexChanged, this,
          [this] { map_.setDisplayStyle(style_->currentData().toString()); });
  connect(terrain_, &QCheckBox::toggled, &map_, &MapWidget::setTerrain);
  connect(imagery_, &QComboBox::currentIndexChanged, this, &MapToolsWindow::updateDaily);
  connect(date_, &QDateEdit::dateChanged, this, &MapToolsWindow::updateDaily);
  connect(opacity_, &QSlider::valueChanged, this, &MapToolsWindow::updateDaily);
  connect(&map_.dailyImagery(), &DailyImageryCache::changed, this, [this] {
    const auto &cache = map_.dailyImagery();
    dailyStatus_->setText(DailyImageryCache::sourceName(cache.source()) + " · requested UTC " +
                          cache.date().toString(Qt::ISODate) + "\n" +
                          DailyImageryCache::resolution(cache.source()) +
                          QString(" · %1 cached / %2 pending / %3 failed tiles\nNASA GIBS · dates "
                                  "label the requested visualization; pixels can have different "
                                  "observation times. Unavailable tiles leave the basemap visible.")
                              .arg(cache.cached())
                              .arg(cache.pending())
                              .arg(cache.failures()));
  });
  solarTimer_.setInterval(60000);
  connect(&solarTimer_, &QTimer::timeout, this, &MapToolsWindow::updateSolar);
  connect(solar_, &QCheckBox::toggled, this, [this](bool on) {
    if (on)
      solarTimer_.start();
    else
      solarTimer_.stop();
    updateSolar();
  });
  connect(&map_, &MapWidget::cameraChanged, this, [this] {
    if (solar_->isChecked())
      updateSolarCaption();
  });
  connect(&map_, &MapWidget::statusChanged, this, [this] {
    if (isVisible())
      synchronizeDisplayControls();
  });
  auto *workspaceLayout = page("Workspace");
  auto *workspaceHelp = new QLabel(
      "Workspace files retain display choices, camera, source configuration and annotations. Share "
      "links omit annotations. Provider credentials and mission documents remain separate.");
  workspaceHelp->setWordWrap(true);
  workspaceLayout->addWidget(workspaceHelp);
  auto *workspaceButtons = new QHBoxLayout;
  workspaceLayout->addLayout(workspaceButtons);
  auto *open = button(workspaceButtons, "Import JSON…");
  auto *save = button(workspaceButtons, "Export JSON…");
  auto *paste = button(workspaceButtons, "Paste link…");
  auto *viewLink = button(workspaceButtons, "Copy view link");
  auto *stateLink = button(workspaceButtons, "Copy workspace link");
  workspaceStatus_ = new QLabel;
  workspaceStatus_->setWordWrap(true);
  workspaceLayout->addWidget(workspaceStatus_);
  workspaceLayout->addStretch();
  connect(open, &QPushButton::clicked, this, [this] {
    const auto file =
        QFileDialog::getOpenFileName(this, "Import workspace", {}, "Workspace JSON (*.json)");
    if (file.isEmpty())
      return;
    try {
      applyWorkspace(loadWorkspace(file));
      workspaceStatus_->setText("Workspace imported from " + file);
    } catch (const std::exception &e) {
      showError(e.what());
    }
  });
  connect(save, &QPushButton::clicked, this, [this] {
    const auto file =
        QFileDialog::getSaveFileName(this, "Export workspace", {}, "Workspace JSON (*.json)");
    if (file.isEmpty())
      return;
    try {
      saveWorkspace(file, workspace());
      workspaceStatus_->setText("Workspace saved to " + file);
    } catch (const std::exception &e) {
      showError(e.what());
    }
  });
  connect(viewLink, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(cameraShareURL(map_.camera()).toString());
    workspaceStatus_->setText("View link copied.");
  });
  connect(stateLink, &QPushButton::clicked, this, [this] {
    try {
      QApplication::clipboard()->setText(workspaceShareURL(workspace()).toString());
      workspaceStatus_->setText("Workspace link copied; annotations omitted.");
    } catch (const std::exception &e) {
      showError(e.what());
    }
  });
  connect(paste, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const auto value = QInputDialog::getMultiLineText(
        this, "Open Helios link", "helios://view or helios://workspace",
        QApplication::clipboard()->text().left(16001), &ok);
    if (!ok)
      return;
    const QUrl url(value.trimmed());
    if (auto c = cameraFromShareURL(url)) {
      map_.setCamera(*c);
      workspaceStatus_->setText("View opened.");
      return;
    }
    if (auto state = workspaceFromShareURL(url))
      try {
        applyWorkspace(*state, true);
        workspaceStatus_->setText("Shared workspace opened.");
        return;
      } catch (const std::exception &e) {
        showError(e.what());
        return;
      }
    showError("This link is invalid or exceeds its limits.");
  });
  connect(&domain_.network(), &DomainNetwork::changed, this, [this](const QString &feed) {
    if (feed == "annotations")
      annotationVisibility();
  });
  try {
    annotations_.load();
    ready_ = true;
    updateAnnotations();
  } catch (const std::exception &e) {
    drawingStatus_->setText(QString("The annotation file was preserved and editing is disabled: ") +
                            e.what());
  }
  updateDaily();
  QSettings settings(profile + "/desktop.ini", QSettings::IniFormat);
  const auto saved = QJsonDocument::fromJson(settings.value("workspace").toByteArray());
  const auto draftBytes = settings.value("annotationDraft").toByteArray();
  if (draftBytes.size() <= 256 * 1024) {
    const auto draft = QJsonDocument::fromJson(draftBytes).object();
    const auto points = annotationPoints(draft);
    if (points.size() <= 2000 && draft["title"].toString().size() <= 256 && !draft.isEmpty()) {
      drawingPoints_ = points;
      drawing_ = true;
      editingAnnotation_ = draft["editingID"].toString();
      title_->setText(draft["title"].toString());
      shape_->setCurrentIndex(std::clamp(draft["shape"].toInt(), 0, 2));
      drawingStatus_->setText("An unsaved drawing was restored. Open Map tools to continue.");
    }
  }
  if (saved.isObject())
    try {
      applyWorkspace(saved.object(), true);
    } catch (const std::exception &e) {
      workspaceStatus_->setText(QString("Saved workspace was not restored: ") + e.what());
    }
}
std::optional<Coordinate> MapToolsWindow::coordinate(const QString &text) const {
  const auto parts = text.trimmed().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
  bool a = false, b = false;
  Coordinate p{parts.value(0).toDouble(&a), parts.value(1).toDouble(&b)};
  return parts.size() == 2 && a && b && p.valid() ? std::optional<Coordinate>(p) : std::nullopt;
}
void MapToolsWindow::search(const QString &query) {
  show();
  raise();
  placeQuery_->setText(query);
  if (auto p = coordinate(query)) {
    auto c = map_.camera();
    c.center = *p;
    c.range = 12000;
    map_.setCamera(c);
    placeStatus_->setText("Coordinate opened.");
    return;
  }
  if (!networking_) {
    placeStatus_->setText("Offline mode: place search is unavailable.");
    return;
  }
  if (!places_.search(query))
    placeStatus_->setText("Enter a place name of 1–200 characters.");
}
void MapToolsWindow::drawPreview() {
  QVector<QVector<Coordinate>> lines;
  if (drawingPoints_.size() >= 2) {
    auto points = drawingPoints_;
    if (shape_->currentIndex() == 2 && points.size() >= 3)
      points.append(points.first());
    lines.append(points);
  }
  map_.setPaths("drawing", lines, {}, QColor(255, 200, 50));
  QVector<MapMarker> markers;
  for (auto p : drawingPoints_)
    markers.append({{}, p, {}});
  map_.setMarkers("drawing", markers, QColor(255, 200, 50));
  drawingStatus_->setText(drawing_
                              ? QString("Drawing: %1 points. Click the map; save when complete.")
                                    .arg(drawingPoints_.size())
                              : "Drawing stopped. Saved annotations remain available below.");
}
void MapToolsWindow::updateAnnotations() {
  annotationList_->clear();
  QVector<MapMarker> markers;
  QVector<QVector<Coordinate>> paths;
  DomainSnapshot snapshot;
  snapshot.source = "User-authored local annotations";
  int count = 0;
  for (const auto value : annotations_.values()) {
    const auto a = value.toObject();
    auto points = annotationPoints(a);
    auto *i = new QListWidgetItem(
        a["title"].toString() + QString(" · %1 points").arg(points.size()), annotationList_);
    i->setData(Qt::UserRole, a);
    if (points.isEmpty())
      continue;
    markers.append({"annotation:" + a["id"].toString(), points.first(), {}});
    DomainRecord r;
    r.id = "annotation:" + a["id"].toString();
    r.feed = "annotations";
    r.label = a["title"].toString();
    r.source = "User-authored local annotation";
    r.coordinate = points.first();
    r.observedAt = missionTime(a["createdAt"]);
    if (a["closed"].toBool())
      r.footprint = points;
    r.details = {{"Geometry",
                  a["closed"].toBool() ? "Polygon" : (points.size() == 1 ? "Point" : "Polyline")}};
    auto line = points;
    if (a["closed"].toBool())
      line.append(points.first());
    r.paths = {line};
    snapshot.records.append(r);
    if (count < 1000 && points.size() >= 2) {
      auto reduced = thin(points, std::min(100, 1000 - count));
      if (reduced.size() >= 2) {
        if (a["closed"].toBool() && count + reduced.size() < 1000)
          reduced.append(reduced.first());
        count += reduced.size();
        paths.append(reduced);
      }
    }
  }
  annotationMarkers_ = markers;
  annotationPaths_ = paths;
  annotationVisibility();
  domain_.setAnnotations(snapshot);
}
void MapToolsWindow::annotationVisibility() {
  const bool visible = domain_.network().enabled("annotations");
  map_.setMarkers("annotations", visible ? annotationMarkers_ : QVector<MapMarker>{},
                  QColor(255, 160, 70));
  map_.setPaths("annotations", visible ? annotationPaths_ : QVector<QVector<Coordinate>>{}, {},
                QColor(255, 160, 70));
}
void MapToolsWindow::updateSolar() {
  if (!solar_->isChecked()) {
    solarState_.reset();
    map_.setPaths("solar", {});
    solarStatus_->clear();
    return;
  }
  solarState_.emplace();
  const auto points = solarState_->terminator();
  map_.setPaths("solar", {points}, {QVector<double>(points.size(), 50)}, QColor(255, 225, 100));
  updateSolarCaption();
}
void MapToolsWindow::updateSolarCaption() {
  if (!solarState_ || !solar_->isChecked())
    return;
  // The boundary depends on UTC time, not the camera. Panning only changes
  // the center's elevation caption; keep its time consistent with the drawn line.
  solarStatus_->setText(solarState_->date.toString("yyyy-MM-dd HH:mm 'UTC'") +
                        QString(" · approximate solar elevation at map center: %1°\nApproximate "
                                "day/night boundary; no illumination or measurement claim.")
                            .arg(solarState_->elevation(map_.camera().center), 0, 'f', 1));
}
void MapToolsWindow::updateDaily() {
  if (applying_)
    return;
  if (auto s = DailyImageryCache::readSource(imagery_->currentData().toString()))
    if (!map_.setDailyImagery(*s, date_->date(), opacity_->value() / 100.0)) {
      dailyStatus_->setText(
          "The selected source/date could not be applied. Choose a supported UTC date.");
      const QSignalBlocker a(imagery_), b(date_);
      imagery_->setCurrentIndex(
          imagery_->findData(DailyImageryCache::sourceID(map_.dailyImagery().source())));
      date_->setDate(map_.dailyImagery().date());
    }
}
QJsonObject MapToolsWindow::workspace() const {
  auto state = extensions_.isEmpty() ? defaultWorkspace() : extensions_;
  const auto c = domain_.network().configuration();
  state["camera"] = map_.camera().json();
  state["satelliteGroup"] = c.satelliteGroup;
  state["transitOperator"] = c.transitID;
  state["cameraProvider"] = c.cameraProvider;
  state["sentinelCollection"] = c.sentinelKind;
  QJsonArray layers;
  for (const auto &feed : domainFeeds())
    if (domain_.network().enabled(feed.id))
      layers.append(feed.id);
  state["layers"] = layers;
  state["basemap"] = map_.streets() ? "streets" : "satellite";
  state["style"] = map_.displayStyle();
  state["imagery"] = imagery_->currentData().toString();
  state["opacity"] = opacity_->value() / 100.0;
  state["solar"] = solar_->isChecked();
  state["terrain"] = map_.terrainEnabled();
  state["trackedID"] = domain_.followedID().isEmpty() ? QJsonValue(QJsonValue::Null)
                                                      : QJsonValue(domain_.followedID());
  state["annotations"] = annotations_.values();
  state["imageryDate"] = date_->date().toString(Qt::ISODate);
  return validatedWorkspace(state);
}
void MapToolsWindow::applyWorkspace(const QJsonObject &input, bool preserveAnnotations) {
  const auto state = validatedWorkspace(input);
  if (!ready_)
    throw std::runtime_error("Resolve the saved annotation file before importing a workspace.");
  // A shared link omits annotation content; it must not erase the local library.
  if (!preserveAnnotations)
    annotations_.replace(state["annotations"].toArray());
  extensions_ = state;
  applying_ = true;
  domain_.setConfiguration({state["satelliteGroup"].toString(), state["transitOperator"].toString(),
                            state["cameraProvider"].toString(),
                            state["sentinelCollection"].toString()});
  const auto layers = state["layers"].toArray();
  for (const auto &feed : domainFeeds())
    domain_.network().setEnabled(feed.id, layers.contains(feed.id));
  if (auto c = Camera::read(state["camera"].toObject()))
    map_.setCamera(*c);
  basemap_->setCurrentIndex(basemap_->findData(state["basemap"].toString()));
  style_->setCurrentIndex(style_->findData(state["style"].toString()));
  imagery_->setCurrentIndex(imagery_->findData(state["imagery"].toString()));
  opacity_->setValue(qRound(state["opacity"].toDouble() * 100));
  terrain_->setChecked(state["terrain"].toBool(true));
  solar_->setChecked(state["solar"].toBool());
  const auto date = QDate::fromString(state["imageryDate"].toString(), Qt::ISODate);
  if (date.isValid())
    date_->setDate(date);
  map_.setStreets(state["basemap"] == "streets");
  map_.setDisplayStyle(state["style"].toString());
  map_.setTerrain(state["terrain"].toBool(true));
  applying_ = false;
  updateDaily();
  updateSolar();
  updateAnnotations();
  domain_.restoreFollowing(state["trackedID"].toString());
}
bool MapToolsWindow::saveSession() {
  try {
    QSettings settings(profile_ + "/desktop.ini", QSettings::IniFormat);
    settings.setValue("workspace", QJsonDocument(workspace()).toJson(QJsonDocument::Compact));
    persistDraft();
    settings.sync();
    if (settings.status() != QSettings::NoError)
      throw std::runtime_error("Display choices could not be saved.");
    return true;
  } catch (const std::exception &e) {
    showError(e.what());
    return false;
  }
}
bool MapToolsWindow::openLink(const QUrl &url) {
  if (auto camera = cameraFromShareURL(url)) {
    map_.setCamera(*camera);
    return true;
  }
  if (auto state = workspaceFromShareURL(url))
    try {
      applyWorkspace(*state, true);
      return true;
    } catch (const std::exception &e) {
      workspaceStatus_->setText(e.what());
      return false;
    }
  return false;
}
bool MapToolsWindow::resolveDrawing() {
  if (drawingPoints_.isEmpty())
    return true;
  const auto choice = QMessageBox::question(
      this, "Unsaved drawing", "Save the current drawing before replacing it?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (choice == QMessageBox::Cancel)
    return false;
  if (choice == QMessageBox::Save)
    try {
      if (editingAnnotation_.isEmpty())
        annotations_.add(
            newAnnotation(title_->text(), drawingPoints_, shape_->currentIndex() == 2));
      else
        annotations_.update(editingAnnotation_, title_->text(), drawingPoints_,
                            shape_->currentIndex() == 2);
      updateAnnotations();
    } catch (const std::exception &e) {
      showError(e.what());
      return false;
    }
  drawingPoints_.clear();
  drawing_ = false;
  editingAnnotation_.clear();
  persistDraft();
  return true;
}
void MapToolsWindow::persistDraft() {
  QSettings settings(profile_ + "/desktop.ini", QSettings::IniFormat);
  if (drawing_) {
    QJsonArray points;
    for (auto point : drawingPoints_)
      points.append(point.json());
    settings.setValue("annotationDraft",
                      QJsonDocument(QJsonObject{{"title", title_->text()},
                                                {"points", points},
                                                {"shape", shape_->currentIndex()},
                                                {"editingID", editingAnnotation_}})
                          .toJson(QJsonDocument::Compact));
  } else
    settings.remove("annotationDraft");
  settings.sync();
}
void MapToolsWindow::hideEvent(QHideEvent *event) {
  map_.endPointCapture(this);
  pickEndpoint_ = 0;
  persistDraft();
  QDialog::hideEvent(event);
}
void MapToolsWindow::synchronizeDisplayControls() {
  const QSignalBlocker a(basemap_), b(style_), c(terrain_);
  basemap_->setCurrentIndex(basemap_->findData(map_.streets() ? "streets" : "satellite"));
  style_->setCurrentIndex(style_->findData(map_.displayStyle()));
  terrain_->setChecked(map_.terrainEnabled());
}
void MapToolsWindow::showEvent(QShowEvent *event) {
  synchronizeDisplayControls();
  QDialog::showEvent(event);
  if (drawing_ && title_->isVisible()) {
    map_.beginPointCapture(this);
    drawPreview();
  }
}
void MapToolsWindow::showError(const QString &message) {
  QMessageBox::warning(this, "Map tools", message);
}
} // namespace helios
