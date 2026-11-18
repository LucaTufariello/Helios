#include "ProcessingWindow.h"
#include "Core/Infrastructure.h"
#include "Core/Missions.h"
#include "WorkspaceStyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace helios {
ProcessingWindow::ProcessingWindow(const QString &profile, MapWidget &map, QWidget *parent,
                                   bool networking)
    : QDialog(parent, Qt::Tool), profile_(profile), networking_(networking), map_(map),
      settings_(profile + "/desktop.ini", QSettings::IniFormat), catalog_(this) {
  queue_ = new ScientificQueue(profile + "/jobs", this);
  connect(queue_, &ScientificQueue::started, this,
          [this](const QString &, WorkerJob *job, bool automatic) {
            job_ = job;
            if (automatic)
              status_->setText("Background site analysis running. Manual work takes priority.");
            connect(job, &WorkerJob::changed, this, [this, job] {
              if (job_ == job) {
                status_->setText(job->record()["stage"].toString(job->error()));
                refresh();
              }
            });
            refresh();
          });
  connect(queue_, &ScientificQueue::finished, this,
          [this](const QString &, const QString &directory, bool success, bool automatic,
                 const QString &error) {
            const auto record = WorkerJob::readRecord(directory + "/run.json");
            if (!automatic && success) {
              if (record["operation"] == "compare")
                emit reviewReady(directory + "/product/result.json");
              else if (record["operation"] != "inspect")
                emit productReady(directory + "/product/result.json");
              else
                details_->setPlainText(QString::fromUtf8(
                    QJsonDocument(WorkerJob::readRecord(directory + "/inspection.json")).toJson()));
            }
            if (!success)
              status_->setText(error);
            reloadJobs();
            refresh();
          });
  connect(queue_, &ScientificQueue::changed, this, [this] { refresh(); });
  setWindowTitle("Helios — Satellite acquisition and processing");
  setAttribute(Qt::WA_QuitOnClose, false);
  resize(900, 940);
  setMinimumSize(640, 660);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(12);
  design::heading(layout, "SATELLITE OPERATIONS", "Processing Center",
                  "Find acquisitions, define coverage, and manage local processing jobs.");
  auto *regionBox = new QGroupBox("Query bounds · decimal degrees");
  auto *boundsLayout = new QFormLayout(regionBox);
  auto spin = [](double low, double high, int decimals, double value) {
    auto *field = new QDoubleSpinBox;
    field->setRange(low, high);
    field->setDecimals(decimals);
    field->setValue(value);
    return field;
  };
  west_ = spin(-180, 180, 6, map.camera().center.longitude - 0.03);
  east_ = spin(-180, 180, 6, map.camera().center.longitude + 0.03);
  south_ = spin(-85, 85, 6, map.camera().center.latitude - 0.03);
  north_ = spin(-85, 85, 6, map.camera().center.latitude + 0.03);
  west_->setAccessibleName("Query west longitude");
  east_->setAccessibleName("Query east longitude");
  south_->setAccessibleName("Query south latitude");
  north_->setAccessibleName("Query north latitude");
  auto *we = new QHBoxLayout;
  we->addWidget(west_);
  we->addWidget(new QLabel("East"));
  we->addWidget(east_);
  auto *sn = new QHBoxLayout;
  sn->addWidget(south_);
  sn->addWidget(new QLabel("North"));
  sn->addWidget(north_);
  boundsLayout->addRow("West", we);
  boundsLayout->addRow("South", sn);
  auto *regionActions = new QHBoxLayout;
  auto *draw = new QPushButton("Draw region on map");
  connect(draw, &QPushButton::clicked, this, [this] {
    map_.beginRegion(this);
    map_.window()->raise();
    map_.setFocus();
  });
  regionActions->addWidget(draw);
  auto *view = new QPushButton("Use current map view");
  connect(view, &QPushButton::clicked, this, [this] {
    Bounds bounds{180, 85, -180, -85};
    for (const auto point :
         {QPointF(1, 1), QPointF(map_.width() - 2, 1), QPointF(1, map_.height() - 2),
          QPointF(map_.width() - 2, map_.height() - 2)}) {
      const auto hit = map_.pick(point);
      if (!hit) {
        status_->setText("Zoom to a local map view or draw a region.");
        return;
      }
      bounds.west = std::min(bounds.west, hit->coordinate.longitude);
      bounds.east = std::max(bounds.east, hit->coordinate.longitude);
      bounds.south = std::min(bounds.south, hit->coordinate.latitude);
      bounds.north = std::max(bounds.north, hit->coordinate.latitude);
    }
    if (!bounds.validArea()) {
      status_->setText(
          "Choose a nonempty region spanning at most 10° per axis; zoom in or draw it.");
      return;
    }
    setRegion(bounds);
  });
  regionActions->addWidget(view);
  boundsLayout->addRow(regionActions);
  connect(&map_, &MapWidget::regionSelected, this, [this](Bounds bounds, QObject *owner) {
    if (owner != this)
      return;
    setRegion(bounds);
    show();
    raise();
  });
  layout->addWidget(regionBox);
  auto *tabs = new QTabWidget;
  tabs->setObjectName("processingTabs");
  layout->addWidget(tabs, 1);
  auto *optical = new QWidget;
  auto *opticalLayout = new QVBoxLayout(optical);
  auto *form = new QFormLayout;
  opticalLayout->addLayout(form);
  source_ = new QComboBox;
  source_->setObjectName("opticalSource");
  source_->addItem("Sentinel-2 L2A · 10 m RGB", "sentinel2");
  source_->addItem("CBERS-4A WPM · 2 m PAN / 8 m RGB", "cbers4a");
  source_->addItem("Local PRISMA L2D HE5 · 5 m PAN / 30 m spectral", "prisma");
  form->addRow("Source", source_);
  sourceInfo_ = new QLabel;
  sourceInfo_->setWordWrap(true);
  opticalLayout->addWidget(sourceInfo_);
  start_ = new QDateEdit(QDateTime::currentDateTimeUtc().date().addDays(-30));
  end_ = new QDateEdit(QDateTime::currentDateTimeUtc().date());
  start_->setCalendarPopup(true);
  end_->setCalendarPopup(true);
  start_->setDisplayFormat("yyyy-MM-dd");
  end_->setDisplayFormat("yyyy-MM-dd");
  auto *dates = new QHBoxLayout;
  dates->addWidget(start_);
  dates->addWidget(new QLabel("Through"));
  dates->addWidget(end_);
  form->addRow("Dates (UTC)", dates);
  cloud_ = spin(0, 100, 1, 20);
  form->addRow("Maximum cloud (%)", cloud_);
  mode_ = new QComboBox;
  mode_->setObjectName("opticalMode");
  mode_->addItem("PAN", "pan");
  mode_->addItem("Native RGB", "rgb");
  mode_->addItem("Spectral band", "spectral");
  form->addRow("Display mode", mode_);
  wavelength_ = spin(400, 2500, 1, 660);
  form->addRow("Wavelength (nm)", wavelength_);
  prisma_ = new QLineEdit;
  prisma_->setMaxLength(4096);
  auto *prismaPath = new QHBoxLayout;
  prismaPath->addWidget(prisma_);
  auto *choosePrisma = new QPushButton("Choose HE5…");
  connect(choosePrisma, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(this, "Import local PRISMA L2D", {},
                                                   "PRISMA HE5 (*.he5 *.HE5 *.h5)");
    if (!path.isEmpty())
      prisma_->setText(path);
  });
  prismaPath->addWidget(choosePrisma);
  form->addRow("PRISMA product", prismaPath);
  clip_ = new QCheckBox("Clip PRISMA to query bounds");
  clip_->setChecked(true);
  form->addRow(clip_);
  auto *searchActions = new QHBoxLayout;
  auto *search = new QPushButton("Search dated acquisitions");
  search->setObjectName("opticalSearch");
  search->setEnabled(networking_);
  searchActions->addWidget(search);
  connect(search, &QPushButton::clicked, this, [this] {
    if (!networking_) {
      status_->setText("Offline mode: optical catalogue searches are unavailable.");
      return;
    }
    scenes_.clear();
    acquisitions_->clear();
    catalog_.search({region(), start_->date(), end_->date(), cloud_->value(),
                     source_->currentData().toString()});
  });
  auto *focus = new QPushButton("Focus acquisition bounds");
  searchActions->addWidget(focus);
  connect(focus, &QPushButton::clicked, this, [this] {
    const int row = acquisitions_->currentRow();
    if (row >= 0 && row < scenes_.size())
      if (auto bounds = Bounds::read(scenes_[row]["bounds"]))
        map_.focus(*bounds);
  });
  opticalLayout->addLayout(searchActions);
  acquisitions_ = new QListWidget;
  acquisitions_->setObjectName("opticalAcquisitions");
  opticalLayout->addWidget(acquisitions_, 1);
  auto *load = new QPushButton("Load selected optical image");
  load->setObjectName("opticalLoad");
  startButtons_.append(load);
  opticalLayout->addWidget(load);
  connect(load, &QPushButton::clicked, this, [this] {
    const auto source = source_->currentData().toString();
    if (source == "prisma")
      startPrisma(prisma_->text(), mode_->currentData().toString(), wavelength_->value(),
                  clip_->isChecked() ? std::optional(region()) : std::nullopt);
    else {
      const int row = acquisitions_->currentRow();
      if (row < 0 || row >= scenes_.size()) {
        status_->setText("Select a dated acquisition first.");
        return;
      }
      startOptical(scenes_[row], region(),
                   source == "sentinel2" ? "rgb" : mode_->currentData().toString());
    }
  });
  tabs->addTab(optical, "Optical acquisition");
  auto sourceChanged = [this, search, choosePrisma, focus, form, dates, prismaPath, load] {
    catalog_.cancel();
    scenes_.clear();
    acquisitions_->clear();
    const auto source = source_->currentData().toString();
    const bool local = source == "prisma";
    load->setProperty("requiresNetwork", !local);
    load->setEnabled(!(job_ && job_->running()) && (networking_ || local));
    search->setVisible(!local);
    focus->setVisible(!local);
    acquisitions_->setVisible(!local);
    prisma_->setEnabled(local);
    choosePrisma->setEnabled(local);
    clip_->setEnabled(local);
    mode_->setEnabled(source != "sentinel2");
    qobject_cast<QStandardItemModel *>(mode_->model())->item(2)->setEnabled(local);
    if (!local && mode_->currentData() == "spectral")
      mode_->setCurrentIndex(0);
    if (source == "sentinel2")
      mode_->setCurrentIndex(1);
    form->setRowVisible(dates, !local);
    form->setRowVisible(cloud_, source == "sentinel2");
    form->setRowVisible(mode_, source != "sentinel2");
    form->setRowVisible(wavelength_, local && mode_->currentData() == "spectral");
    form->setRowVisible(prismaPath, local);
    form->setRowVisible(clip_, local);
    wavelength_->setEnabled(local);
    start_->setEnabled(!local);
    end_->setEnabled(!local);
    cloud_->setEnabled(source == "sentinel2");
    sourceInfo_->setText(
        source == "sentinel2" ? "Copernicus Sentinel-2 · Element84 Earth Search / AWS public COG "
                                "archive. Ten-metre pixels cannot resolve small buildings or "
                                "vehicles; this display is not a scientific reflectance product."
        : source == "cbers4a"
            ? "INPE CBERS-4A / Brazil Data Cube and CBERS on AWS. Regional WPM L4 archive, "
              "predominantly South America. Cloud cover may be unknown. "
              "PAN and native RGB have different resolutions; sharpening is not applied."
            : "Import an ASI-ordered geocoded L2D product. PAN is 5 m; RGB/spectral bands are 30 "
              "m. Account ordering and automatic material identification are not provided.");
  };
  connect(source_, &QComboBox::currentIndexChanged, this, sourceChanged);
  connect(mode_, &QComboBox::currentIndexChanged, this, [this, form] {
    form->setRowVisible(wavelength_,
                        source_->currentData() == "prisma" && mode_->currentData() == "spectral");
  });
  sourceChanged();
  connect(&catalog_, &OpticalCatalog::changed, this, [this] {
    if (catalog_.busy())
      status_->setText("Searching the selected public optical catalogue…");
    refresh();
  });
  connect(&catalog_, &OpticalCatalog::failed, this, [this](const QString &error) {
    status_->setText(error);
    refresh();
  });
  connect(&catalog_, &OpticalCatalog::results, this, [this](QVector<QJsonObject> scenes) {
    scenes_ = std::move(scenes);
    for (const auto &scene : scenes_) {
      const auto cloud = scene["cloudCover"];
      auto *item = new QListWidgetItem(
          scene["acquiredAt"].toString() + " · " + scene["id"].toString() + " · cloud " +
              (cloud.isNull() ? "unknown" : QString::number(cloud.toDouble()) + "%"),
          acquisitions_);
      item->setToolTip(scene["id"].toString());
    }
    if (!scenes_.isEmpty())
      acquisitions_->setCurrentRow(0);
    status_->setText(
        scenes_.isEmpty()
            ? "No supported acquisitions matched these dates, region and cloud constraints."
            : QString("%1 dated acquisitions found (up to 40 returned matches). Select one to load "
                      "pixels.")
                  .arg(scenes_.size()));
    refresh();
  });
  radarPanel_ = new RadarPanel(profile, map, [this] { return region(); }, nullptr, networking_);
  auto *radarScroll = new QScrollArea;
  radarScroll->setWidgetResizable(true);
  radarScroll->setWidget(radarPanel_);
  tabs->addTab(radarScroll, "Radar acquisition");
  connect(radarPanel_, &RadarPanel::status, this,
          [this](const QString &message) { status_->setText(message); });
  connect(radarPanel_, &RadarPanel::changed, this, &ProcessingWindow::refresh);
  connect(radarPanel_, &RadarPanel::inspect, this, [this, tabs](const QString &path) {
    radar_->setText(path);
    tabs->setCurrentIndex(2);
    startRadar(path, {}, true);
  });
  auto *sar = new QWidget;
  auto *sarLayout = new QVBoxLayout(sar);
  auto *sarInfo = new QLabel("Sentinel-1 GRD backscatter uses external ESA SNAP 14+ with the "
                             "Microwave Toolbox. Sigma0/dB outputs remain separate from bounded "
                             "display previews; real scientific validation remains required.");
  sarInfo->setWordWrap(true);
  sarLayout->addWidget(sarInfo);
  auto *sarForm = new QFormLayout;
  sarLayout->addLayout(sarForm);
  radar_ = new QLineEdit;
  radar_->setMaxLength(4096);
  sarForm->addRow("GRD SAFE / ZIP", radar_);
  auto *inputActions = new QHBoxLayout;
  auto *safe = new QPushButton("Open SAFE folder…");
  inputActions->addWidget(safe);
  connect(safe, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getExistingDirectory(this, "Open Sentinel-1 GRD SAFE");
    if (!path.isEmpty())
      radar_->setText(path);
  });
  auto *zip = new QPushButton("Open ZIP…");
  inputActions->addWidget(zip);
  connect(zip, &QPushButton::clicked, this, [this] {
    const auto path =
        QFileDialog::getOpenFileName(this, "Open GRD archive", {}, "SAFE archive (*.zip)");
    if (!path.isEmpty())
      radar_->setText(path);
  });
  sarForm->addRow(inputActions);
  vv_ = new QCheckBox("VV");
  vh_ = new QCheckBox("VH");
  vv_->setChecked(true);
  vh_->setChecked(true);
  auto *polarizations = new QHBoxLayout;
  polarizations->addWidget(vv_);
  polarizations->addWidget(vh_);
  sarForm->addRow("Polarizations", polarizations);
  orbit_ = new QCheckBox("Apply orbit file");
  orbit_->setChecked(true);
  sarForm->addRow(orbit_);
  speckle_ = new QCheckBox("Refined Lee speckle filter");
  sarForm->addRow(speckle_);
  spacing_ = spin(5, 100, 1, 20);
  sarForm->addRow("Output spacing (m)", spacing_);
  auto *inspect = new QPushButton("Inspect GRD metadata");
  startButtons_.append(inspect);
  sarLayout->addWidget(inspect);
  connect(inspect, &QPushButton::clicked, this, [this] { startRadar(radar_->text(), {}, true); });
  auto *process = new QPushButton("Process calibrated Sigma0");
  process->setProperty("requiresNetwork", true);
  startButtons_.append(process);
  sarLayout->addWidget(process);
  connect(process, &QPushButton::clicked, this, [this] {
    QJsonArray polarizations;
    if (vv_->isChecked())
      polarizations.append("VV");
    if (vh_->isChecked())
      polarizations.append("VH");
    if (polarizations.isEmpty()) {
      status_->setText("Select at least one polarization.");
      return;
    }
    startRadar(radar_->text(), {{"polarizations", polarizations},
                                {"applyOrbit", orbit_->isChecked()},
                                {"speckle", speckle_->isChecked()},
                                {"pixelSpacing", spacing_->value()},
                                {"lowPercentile", 2},
                                {"highPercentile", 98}});
  });
  sarLayout->addStretch();
  tabs->addTab(sar, "Raw GRD processing");
  auto *engine = new QWidget;
  auto *engineLayout = new QVBoxLayout(engine);
  auto *engineForm = new QFormLayout;
  engineLayout->addLayout(engineForm);
  auto pathField = [&](const QString &label, const QString &value) {
    auto *field = new QLineEdit(value);
    field->setMaxLength(4096);
    auto *row = new QHBoxLayout;
    row->addWidget(field);
    auto *choose = new QPushButton("Choose…");
    row->addWidget(choose);
    connect(choose, &QPushButton::clicked, this, [this, field, label] {
      const auto path = QFileDialog::getOpenFileName(this, label);
      if (!path.isEmpty())
        field->setText(path);
    });
    engineForm->addRow(label, row);
    return field;
  };
  python_ = pathField(
      "Raster Python",
      settings_.value("rasterPython", QStandardPaths::findExecutable("python3")).toString());
  gpt_ = pathField("SNAP GPT",
                   settings_.value("snapGPT", QStandardPaths::findExecutable("gpt")).toString());
  connect(python_, &QLineEdit::textChanged, this,
          [this](const QString &text) { settings_.setValue("rasterPython", text); });
  connect(gpt_, &QLineEdit::textChanged, this,
          [this](const QString &text) { settings_.setValue("snapGPT", text); });
  auto *check = new QPushButton("Check Python raster imports");
  check->setObjectName("checkRasterPython");
  engineLayout->addWidget(check);
  connect(check, &QPushButton::clicked, this, &ProcessingWindow::checkPython);
  engineStatus_ =
      new QLabel("Select an environment with NumPy, Rasterio/GDAL, Pillow and h5py. Monitoring "
                 "additionally needs requirements-monitoring.txt. SNAP is a separate "
                 "dependency for raw radar processing; optical viewing does not require SNAP.");
  engineStatus_->setWordWrap(true);
  engineLayout->addWidget(engineStatus_);
  auto *reload = new QPushButton("Refresh saved jobs");
  engineLayout->addWidget(reload);
  connect(reload, &QPushButton::clicked, this, &ProcessingWindow::reloadJobs);
  history_ = new QListWidget;
  engineLayout->addWidget(history_, 1);
  connect(history_, &QListWidget::currentItemChanged, this, [this] {
    if (!history_->currentItem())
      return;
    const auto directory = history_->currentItem()->data(Qt::UserRole).toString();
    details_->setPlainText(
        QString::fromUtf8(QJsonDocument(WorkerJob::readRecord(directory + "/run.json"))
                              .toJson(QJsonDocument::Indented)));
  });
  connect(history_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
    const auto file = item->data(Qt::UserRole).toString() + "/product/result.json";
    if (WorkerJob::readRecord(file)["state"] == "completed") {
      const auto run = WorkerJob::readRecord(item->data(Qt::UserRole).toString() + "/run.json");
      if (run["operation"] == "compare")
        emit reviewReady(file);
      else
        emit productReady(file);
    }
  });
  auto *files = new QPushButton("Open selected job files");
  engineLayout->addWidget(files);
  connect(files, &QPushButton::clicked, this, [this] {
    if (history_->currentItem())
      QDesktopServices::openUrl(
          QUrl::fromLocalFile(history_->currentItem()->data(Qt::UserRole).toString()));
  });
  tabs->addTab(engine, "Engine and jobs");
  details_ = new QPlainTextEdit;
  details_->setMaximumBlockCount(2000);
  details_->setReadOnly(true);
  details_->setMaximumHeight(140);
  layout->addWidget(details_);
  progress_ = new QProgressBar;
  layout->addWidget(progress_);
  status_ = new QLabel(
      networking_
          ? "Choose bounds and search for dated imagery, or import a local product."
          : "Offline mode: local PRISMA, GRD inspection and saved-product comparison remain "
            "available. Raw GRD processing can require orbit/DEM downloads and is disabled.");
  status_->setObjectName("processingStatus");
  status_->setTextFormat(Qt::PlainText);
  status_->setWordWrap(true);
  layout->addWidget(status_);
  cancel_ = new QPushButton("Cancel current work");
  layout->addWidget(cancel_);
  connect(cancel_, &QPushButton::clicked, this, &ProcessingWindow::cancel);
  connect(&preflight_, &QProcess::readyReadStandardOutput, this, [this] {
    preflightOutput_ = (preflightOutput_ + preflight_.readAllStandardOutput()).right(16384);
  });
  connect(&preflight_, &QProcess::readyReadStandardError, this, [this] {
    preflightErrors_ = (preflightErrors_ + preflight_.readAllStandardError()).right(16384);
  });
  connect(&preflight_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      engineStatus_->setText(
          "Selected Python could not start. Choose an executable raster environment.");
      refresh();
    }
  });
  connect(&preflight_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
    const auto versions = QJsonDocument::fromJson(preflightOutput_).object();
    if (code == 0 && exit == QProcess::NormalExit && !versions.isEmpty())
      engineStatus_->setText(
          QString("Python %1 · NumPy %2 · Rasterio %3 / GDAL %4 · Pillow %5 · h5py %6. Imports "
                  "verified; real processing is a separate check.")
              .arg(versions["python"].toString(), versions["numpy"].toString(),
                   versions["rasterio"].toString(), versions["gdal"].toString(),
                   versions["pillow"].toString(), versions["h5py"].toString()));
    else
      engineStatus_->setText("Raster imports failed. Select the prepared Python environment.\n" +
                             QString::fromUtf8(preflightErrors_).right(2000));
    refresh();
  });
  reloadJobs();
  refresh();
}
ProcessingWindow::~ProcessingWindow() {
  disconnect(queue_, nullptr, this, nullptr);
  if (queue_->activeJob())
    disconnect(queue_->activeJob(), nullptr, this, nullptr);
  delete queue_;
  queue_ = nullptr;
}
bool ProcessingWindow::busy() const {
  return queue_->busy() || catalog_.busy() || (radarPanel_ && radarPanel_->busy()) ||
         preflight_.state() != QProcess::NotRunning;
}
void ProcessingWindow::setEngines(const QString &python, const QString &gpt) {
  python_->setText(python);
  if (!gpt.isEmpty())
    gpt_->setText(gpt);
}
bool ProcessingWindow::setQuery(const OpticalQuery &query) {
  if (!query.valid())
    return false;
  source_->setCurrentIndex(source_->findData(query.source));
  setRegion(query.region);
  start_->setDate(query.start);
  end_->setDate(query.end);
  cloud_->setValue(query.maximumCloud);
  return true;
}
bool ProcessingWindow::setRadarQuery(const RadarQuery &query) {
  if (!query.valid())
    return false;
  setRegion(query.region);
  return radarPanel_->setQuery(query);
}
Bounds ProcessingWindow::region() const {
  return {west_->value(), south_->value(), east_->value(), north_->value()};
}
void ProcessingWindow::setRegion(Bounds bounds) {
  west_->setValue(bounds.west);
  south_->setValue(bounds.south);
  east_->setValue(bounds.east);
  north_->setValue(bounds.north);
}
bool ProcessingWindow::submitMonitoring(WorkerRequest request, const QString &token) {
  request.python = python_->text();
  request.worker = WorkerJob::workerPath(false);
  request.gpt = gpt_->text();
  return queue_->submit(std::move(request), token, true);
}
bool ProcessingWindow::launch(WorkerRequest request) {
  request.python = python_->text();
  request.worker = WorkerJob::workerPath(request.optical);
  request.gpt = gpt_->text();
  const auto accepted = queue_->submit(std::move(request), newID(), false);
  if (!accepted)
    status_->setText(
        "The scientific queue is full. Cancel pending work before submitting another job.");
  refresh();
  return accepted;
}
bool ProcessingWindow::startOptical(const QJsonObject &scene, Bounds bounds, const QString &mode) {
  if (!networking_) {
    status_->setText("Offline mode: remote optical pixel retrieval is unavailable.");
    return false;
  }
  if (!bounds.validArea()) {
    status_->setText("Choose valid query bounds spanning at most 10° per axis.");
    return false;
  }
  WorkerRequest request;
  request.optical = true;
  request.operation = "render";
  request.document = {{"scene", scene}, {"region", bounds.json()}};
  if (scene["source"] == "cbers4a")
    request.document["mode"] = mode;
  return launch(request);
}
bool ProcessingWindow::startPrisma(const QString &path, const QString &mode, double wavelength,
                                   std::optional<Bounds> bounds) {
  if (!QFileInfo(path).isFile() || (bounds && !bounds->validArea())) {
    status_->setText("Choose an existing PRISMA L2D HE5 and valid optional clipping bounds.");
    return false;
  }
  source_->setCurrentIndex(source_->findData("prisma"));
  prisma_->setText(path);
  mode_->setCurrentIndex(mode_->findData(mode));
  wavelength_->setValue(wavelength);
  clip_->setChecked(bounds.has_value());
  if (bounds)
    setRegion(*bounds);
  WorkerRequest request;
  request.optical = true;
  request.operation = "render";
  request.document = {{"source", "prisma"},
                      {"originalInput", QFileInfo(path).absoluteFilePath()},
                      {"mode", mode},
                      {"wavelengthNanometres", wavelength}};
  if (bounds)
    request.document["region"] = bounds->json();
  return launch(request);
}
bool ProcessingWindow::startRadar(const QString &path, const QJsonObject &options, bool inspect) {
  if (!networking_ && !inspect) {
    status_->setText("Offline mode: raw GRD processing is disabled because SNAP may retrieve orbit "
                     "files or DEM tiles. Local metadata inspection remains available.");
    return false;
  }
  WorkerRequest request;
  request.operation = inspect ? "inspect" : "process";
  request.input = path;
  request.options = options;
  return launch(request);
}
bool ProcessingWindow::startReview(const QJsonObject &site, const QString &before,
                                   const QString &after, const QJsonObject &options) {
  const auto issue =
      changeCompatibility(WorkerJob::readRecord(before), WorkerJob::readRecord(after),
                          options["polarization"].toString());
  if (!issue.isEmpty()) {
    status_->setText(issue);
    return false;
  }
  WorkerRequest request;
  request.operation = "compare";
  request.document = {{"site", site},
                      {"options", options},
                      {"beforeDirectory", QFileInfo(before).absolutePath()},
                      {"afterDirectory", QFileInfo(after).absolutePath()}};
  return launch(request);
}
void ProcessingWindow::cancel() {
  catalog_.cancel();
  radarPanel_->cancel();
  queue_->cancelAll();
  if (preflight_.state() != QProcess::NotRunning)
    preflight_.kill();
  refresh();
}
void ProcessingWindow::refresh() {
  const bool processing = queue_->busy();
  for (auto *button : startButtons_)
    button->setEnabled((networking_ || !button->property("requiresNetwork").toBool()));
  cancel_->setEnabled(busy());
  if (processing && job_) {
    const auto fraction = job_->record()["progress"];
    if (fraction.isDouble()) {
      progress_->setRange(0, 1000);
      progress_->setValue(int(fraction.toDouble() * 1000));
    } else
      progress_->setRange(0, 0);
  } else if (catalog_.busy() || radarPanel_->busy() || preflight_.state() != QProcess::NotRunning)
    progress_->setRange(0, 0);
  else {
    progress_->setRange(0, 1000);
    progress_->setValue(job_ && job_->record()["state"] == "completed" ? 1000 : 0);
  }
}
void ProcessingWindow::reloadJobs() {
  history_->clear();
  QDirIterator directories(profile_ + "/jobs",
                           QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  int count = 0;
  while (directories.hasNext() && count++ < 500) {
    const auto directory = directories.next();
    WorkerJob::recover(directory);
    const auto record = WorkerJob::readRecord(directory + "/run.json");
    if (record.isEmpty())
      continue;
    auto *item = new QListWidgetItem(record["createdAt"].toString().left(64) + " · " +
                                         record["operation"].toString().left(64) + " · " +
                                         record["state"].toString().left(16) + " · " +
                                         record["inputName"].toString().left(200),
                                     history_);
    item->setData(Qt::UserRole, directory);
  }
}
void ProcessingWindow::checkPython() {
  if (preflight_.state() != QProcess::NotRunning)
    return;
  preflightOutput_.clear();
  preflightErrors_.clear();
  const auto generation = ++preflightGeneration_;
  preflight_.start(python_->text(),
                   {"-I", "-c",
                    "import json,sys,numpy,rasterio,PIL,h5py; "
                    "print(json.dumps(dict(python=sys.version.split()[0],numpy=numpy.__version__,"
                    "rasterio=rasterio.__version__,gdal=rasterio.__gdal_version__,pillow=PIL.__"
                    "version__,h5py=h5py.__version__)))"});
  QTimer::singleShot(15000, &preflight_, [this, generation] {
    if (generation == preflightGeneration_ && preflight_.state() != QProcess::NotRunning)
      preflight_.kill();
  });
  refresh();
}
} // namespace helios
