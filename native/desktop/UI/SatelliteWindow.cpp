#include "SatelliteWindow.h"
#include "Core/LibraryStorage.h"
#include "WorkspaceStyle.h"
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace godeye {
namespace {
using ReadResult = std::pair<QVector<ProductLayer>, QString>;
ReadResult load(const QString &path, bool images) {
  try {
    return {readProducts(path, images), {}};
  } catch (const std::exception &e) {
    return {{}, QString::fromUtf8(e.what())};
  }
}
} // namespace
SatelliteWindow::SatelliteWindow(const QString &profile, QWidget *parent)
    : QDialog(parent, Qt::Tool), profile_(profile),
      settings_(profile + "/desktop.ini", QSettings::IniFormat) {
  setWindowTitle("GodEye — Satellite library");
  resize(820, 720);
  setMinimumSize(640, 540);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 12, 16, 12);
  layout->setSpacing(12);
  design::heading(layout, "IMAGERY WORKSPACE", "Satellite analysis");
  auto *intro = new QLabel(
      "Build a library of dated optical and radar acquisitions for your areas of interest.");
  intro->setWordWrap(true);
  intro->setProperty("role", "secondary");
  layout->addWidget(intro);
  auto *actions = new QHBoxLayout;
  layout->addLayout(actions);
  auto *import = new QPushButton("Import product…");
  auto *folder = new QPushButton("Add folder…");
  actions->addWidget(import);
  actions->addWidget(folder);
  auto *processing = new QPushButton("Find imagery…");
  processing->setProperty("role", "primary");
  actions->addWidget(processing);
  connect(processing, &QPushButton::clicked, this, &SatelliteWindow::processingRequested);
  auto *split = new QSplitter;
  split->setChildrenCollapsible(false);
  auto *libraryStack = new QStackedWidget;
  layout->addWidget(libraryStack, 1);
  auto *empty = new QWidget;
  auto *emptyLayout = new QVBoxLayout(empty);
  emptyLayout->setContentsMargins(44, 30, 44, 30);
  emptyLayout->setSpacing(14);
  emptyLayout->addStretch();
  auto *emptyIcon = new QLabel;
  emptyIcon->setPixmap(design::icon(design::Symbol::Satellite).pixmap(48, 48));
  emptyIcon->setAlignment(Qt::AlignCenter);
  emptyLayout->addWidget(emptyIcon);
  auto *emptyTitle = design::label("Your imagery workspace", "title");
  emptyTitle->setAlignment(Qt::AlignCenter);
  emptyLayout->addWidget(emptyTitle);
  auto *emptyHelp =
      design::label("Find an acquisition for an area of interest, or import a completed product to "
                    "inspect its imagery, provenance and scientific outputs.",
                    "secondary");
  emptyHelp->setAlignment(Qt::AlignCenter);
  emptyHelp->setWordWrap(true);
  emptyLayout->addWidget(emptyHelp);
  emptyLayout->addStretch();
  libraryStack->addWidget(empty);
  libraryStack->addWidget(split);

  list_ = new QListWidget;
  list_->setObjectName("satelliteProducts");
  auto updateLibraryState = [libraryStack, this] {
    libraryStack->setCurrentIndex(list_->count() ? 1 : 0);
  };
  connect(list_->model(), &QAbstractItemModel::rowsInserted, this, updateLibraryState);
  connect(list_->model(), &QAbstractItemModel::rowsRemoved, this, updateLibraryState);

  list_->setMinimumWidth(200);
  list_->setMaximumWidth(360);
  list_->setTextElideMode(Qt::ElideRight);
  split->addWidget(list_);
  auto *details = new QWidget;
  auto *detail = new QVBoxLayout(details);
  auto *scroll = new QScrollArea;
  scroll->setObjectName("satelliteDetailsScroll");
  scroll->setMinimumWidth(360);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidgetResizable(true);
  scroll->setWidget(details);
  split->addWidget(scroll);
  summary_ = new QLabel("No satellite product selected.");
  summary_->setTextFormat(Qt::PlainText);
  summary_->setWordWrap(true);
  summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  detail->addWidget(summary_);
  thumbnail_ = new QLabel;
  thumbnail_->setMinimumSize(200, 160);
  thumbnail_->setMaximumHeight(220);
  thumbnail_->setAlignment(Qt::AlignCenter);
  detail->addWidget(thumbnail_, 1);
  metadata_ = new QPlainTextEdit;
  metadata_->setReadOnly(true);
  metadata_->setMaximumHeight(150);
  detail->addWidget(metadata_);
  auto add = [&](const QString &title, const std::function<void()> &fn) {
    auto *b = new QPushButton(title);
    detail->addWidget(b);
    connect(b, &QPushButton::clicked, this, fn);
    return b;
  };
  add("Show selected image on map", [this] {
    if (list_->currentItem())
      showProduct(list_->currentItem()->data(Qt::UserRole).toString());
  });
  add("Use as second comparison layer", [this] {
    if (list_->currentItem())
      showProduct(list_->currentItem()->data(Qt::UserRole).toString(), true);
  });
  add("Link selected image to mission", [this] {
    if (!list_->currentItem())
      return;
    const auto id = list_->currentItem()->data(Qt::UserRole).toString();
    for (const auto &p : displayed_)
      if (p.id == id) {
        emit linkRequested(p);
        return;
      }
    QMessageBox::information(
        this, "Link imagery",
        "Show the selected image on the map first, then link it to the mission.");
  });
  auto *blend = new QSlider(Qt::Horizontal);
  blend->setRange(0, 100);
  blend->setValue(90);
  auto *opacityLabel = new QLabel("Top layer opacity · visual comparison, without registration");
  opacityLabel->setWordWrap(true);
  detail->addWidget(opacityLabel);
  detail->addWidget(blend);
  connect(blend, &QSlider::valueChanged, this,
          [this](int value) { emit blendChanged(value / 100.0); });
  auto *grid = new QCheckBox("Pixel-grid display (no smoothing)");
  grid->setChecked(true);
  detail->addWidget(grid);
  connect(grid, &QCheckBox::toggled, this, &SatelliteWindow::pixelGridChanged);
  add("Remove map overlays", [this] {
    displayed_.clear();
    emit layersChanged({}, false);
  });
  png_ = add("Export original preview PNG…", [this] { exportSelected("png"); });
  visualization_ = add("Export display GeoTIFF…", [this] { exportSelected("display"); });
  scientific_ = add("Export scientific GeoTIFF…", [this] { exportSelected("scientific"); });
  add("Remove from desktop library…", [this] {
    if (busy_ || !list_->currentItem())
      return;
    if (QMessageBox::question(
            this, "Remove library reference",
            "Remove this imported library reference? The original satellite files will be kept.",
            QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
      return;
    auto path = list_->currentItem()->data(Qt::UserRole + 1).toString();
    const auto id = list_->currentItem()->data(Qt::UserRole).toString();
    for (const auto &p : library_)
      if (p.id == id)
        path = p.manifest;
    forgetManifest(path);
  });
  add("Retry saved product path", [this] {
    if (list_->currentItem()) {
      auto path = list_->currentItem()->data(Qt::UserRole + 1).toString();
      if (!path.isEmpty())
        importManifest(path);
    }
  });
  add("Move owned job to Trash…", [this] {
    if (busy_ || !list_->currentItem())
      return;
    ProductLayer selectedProduct;
    for (const auto &p : library_)
      if (p.id == list_->currentItem()->data(Qt::UserRole).toString())
        selectedProduct = p;
    if (selectedProduct.manifest.isEmpty())
      return;
    if (QMessageBox::question(this, "Move owned job to Trash",
                              "Move this profile's processing job, outputs and diagnostics to the "
                              "operating system Trash? Mission links retain their acquisition "
                              "snapshots. Imported source folders are managed separately.",
                              QMessageBox::Cancel | QMessageBox::Yes) != QMessageBox::Yes)
      return;
    busy_ = true;
    status_->setText("Checking job ownership and moving the saved job to Trash…");
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, selectedProduct] {
      const auto error = watcher->result();
      watcher->deleteLater();
      busy_ = false;
      if (error.isEmpty()) {
        forgetManifest(selectedProduct.manifest);
        status_->setText(
            "Owned job moved to Trash. Mission acquisition snapshots remain available.");
      } else
        status_->setText(error);
      pumpImports();
    });
    const auto root = profile_ + "/jobs";
    watcher->setFuture(QtConcurrent::run([root, selectedProduct] {
      try {
        trashOwnedProduct(root, selectedProduct);
        return QString{};
      } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
      }
    }));
  });
  detail->addStretch();
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({260, 520});
  status_ = new QLabel;
  status_->setWordWrap(true);
  status_->setTextFormat(Qt::PlainText);
  layout->addWidget(status_);
  cancel_ = new QPushButton("Cancel export");
  cancel_->hide();
  layout->addWidget(cancel_);
  connect(cancel_, &QPushButton::clicked, this, [this] {
    if (cancelled_)
      cancelled_->store(true);
  });
  connect(list_, &QListWidget::currentRowChanged, this, [this] { selected(); });
  connect(import, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, "Import completed satellite product",
        settings_.value("lastImport", QDir::homePath() + "/Library/Application Support/GodEye")
            .toString(),
        "Product manifest (*.json)");
    if (!path.isEmpty()) {
      settings_.setValue("lastImport", QFileInfo(path).absolutePath());
      importManifest(path);
    }
  });
  connect(folder, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getExistingDirectory(
        this, "Add job library folder", settings_.value("lastImport", QDir::homePath()).toString());
    if (path.isEmpty())
      return;
    QDir dir(path);
    if (QFileInfo::exists(dir.filePath("result.json")))
      importManifest(dir.filePath("result.json"));
    else {
      const auto names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
      if (names.size() > 500) {
        QMessageBox::warning(this, "Library size",
                             "Choose a folder with at most 500 job subfolders.");
        return;
      }
      for (const auto &name : names) {
        QFileInfo child(dir.filePath(name));
        if (!child.isSymLink() && QFileInfo::exists(child.filePath() + "/result.json"))
          importManifest(child.filePath() + "/result.json");
      }
    }
  });
  selected();
  const auto manifests = settings_.value("manifests").toStringList();
  for (const auto &path : manifests.mid(0, 500))
    if (path.size() <= 4096)
      pendingImports_.append(path);
  pumpImports();
}
void SatelliteWindow::importManifest(const QString &filename, bool activate) {
  const auto path = QFileInfo(filename).absoluteFilePath();
  QSet<QString> known = unresolved_;
  for (const auto &p : library_)
    known.insert(p.manifest);
  for (const auto &p : pendingImports_)
    known.insert(p);
  if (path.size() > 4096 || (!known.contains(path) && known.size() >= 500)) {
    status_->setText("The library is limited to 500 product references. Remove a reference before "
                     "importing another.");
    return;
  }
  if (activate)
    activateImports_.insert(path);
  if (!pendingImports_.contains(path))
    pendingImports_.append(path);
  pumpImports();
}
void SatelliteWindow::pumpImports() {
  if (busy_ || pendingImports_.isEmpty())
    return;
  busy_ = true;
  const auto path = pendingImports_.takeFirst();
  status_->setText("Reading product metadata…");
  auto *watcher = new QFutureWatcher<ReadResult>(this);
  connect(watcher, &QFutureWatcher<ReadResult>::finished, this, [this, watcher, path] {
    const auto result = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    if (!result.second.isEmpty()) {
      status_->setText("Import failed: " + result.second);
      if (!unresolved_.contains(path)) {
        unresolved_.insert(path);
        auto *item = new QListWidgetItem(
            "Unavailable saved product · " + QFileInfo(path).absolutePath().left(300), list_);
        item->setData(Qt::UserRole + 1, path);
        item->setToolTip(path);
      }
      remember();
    } else {
      unresolved_.remove(path);
      for (int i = list_->count(); i-- > 0;)
        if (list_->item(i)->data(Qt::UserRole).toString().isEmpty() &&
            list_->item(i)->data(Qt::UserRole + 1) == path)
          delete list_->takeItem(i);
      int added = 0;
      for (auto p : result.first) {
        bool exists = false, conflict = false;
        for (auto &old : library_)
          if (old.id == p.id) {
            exists = true;
            conflict = old.manifestHash != p.manifestHash;
            if (!conflict && old.manifest != p.manifest) {
              auto relocated = p;
              relocated.image = {};
              relocated.metadata = {};
              old = std::move(relocated);
            }
            break;
          }
        if (conflict) {
          status_->setText(
              "This product identity already exists with different processing metadata. Remove its "
              "old library reference before importing the replacement.");
          continue;
        }
        if (exists) {
          for (int row = 0; row < list_->count(); ++row)
            if (list_->item(row)->data(Qt::UserRole) == p.id)
              list_->item(row)->setData(Qt::UserRole + 1, p.manifest);
          continue;
        }
        if (library_.size() >= 500) {
          status_->setText("The desktop library is limited to 500 output entries.");
          break;
        }
        p.image = {};
        p.metadata = {};
        library_.append(p);
        auto *item = new QListWidgetItem(p.label, list_);
        item->setData(Qt::UserRole, p.id);
        item->setData(Qt::UserRole + 1, p.manifest);
        item->setToolTip(p.sourceID);
        ++added;
      }
      if (added)
        status_->setText(
            QString("Imported %1 output(s). Choose one and show it on the map.").arg(added));
      remember();
    }
    selected();
    if (activateImports_.remove(path) && result.second.isEmpty() && !result.first.isEmpty())
      for (const auto &product : library_)
        if (product.manifest == result.first.first().manifest &&
            product.manifestHash == result.first.first().manifestHash) {
          showProduct(product.id);
          break;
        }
    pumpImports();
  });
  watcher->setFuture(QtConcurrent::run([path] { return load(path, false); }));
}
void SatelliteWindow::remember() {
  QStringList paths;
  for (const auto &p : library_)
    if (!paths.contains(p.manifest))
      paths.append(p.manifest);
  for (const auto &path : pendingImports_)
    if (!paths.contains(path))
      paths.append(path);
  for (const auto &path : unresolved_)
    if (!paths.contains(path))
      paths.append(path);
  settings_.setValue("manifests", paths);
  emit libraryChanged();
}
void SatelliteWindow::forgetManifest(const QString &path) {
  QSet<QString> removed;
  for (const auto &p : library_)
    if (p.manifest == path)
      removed.insert(p.id);
  for (qsizetype i = library_.size(); i-- > 0;)
    if (library_[i].manifest == path)
      library_.removeAt(i);
  for (qsizetype i = displayed_.size(); i-- > 0;)
    if (displayed_[i].manifest == path || removed.contains(displayed_[i].id))
      displayed_.removeAt(i);
  for (int i = list_->count(); i-- > 0;)
    if (list_->item(i)->data(Qt::UserRole + 1) == path)
      delete list_->takeItem(i);
  unresolved_.remove(path);
  pendingImports_.removeAll(path);
  emit layersChanged(displayed_, false);
  remember();
  selected();
}
void SatelliteWindow::selected() {
  png_->setEnabled(false);
  visualization_->setEnabled(false);
  scientific_->setEnabled(false);
  if (!list_->currentItem()) {
    summary_->setText("No satellite product selected.");
    thumbnail_->clear();
    metadata_->clear();
    return;
  }
  const auto id = list_->currentItem()->data(Qt::UserRole).toString();
  if (id.isEmpty()) {
    summary_->setText(
        "Unavailable saved product\n" + list_->currentItem()->data(Qt::UserRole + 1).toString() +
        "\nRetry its saved path or import the complete product from its new location.");
    thumbnail_->clear();
    metadata_->clear();
    return;
  }
  for (const auto &p : library_)
    if (p.id == id) {
      summary_->setText(
          p.label + "\n" + p.sourceID +
          QString("\nPreview: %1 × %2 · declared EPSG:4326\nRaster: %3 × %4\n%5%6; enlarging a "
                  "preview adds no detail.")
              .arg(p.pixelSize.width())
              .arg(p.pixelSize.height())
              .arg(p.rasterSize.width())
              .arg(p.rasterSize.height())
              .arg(p.kind == "radar" ? "Requested output spacing: " : "Source spacing: ")
              .arg(p.resolution > 0 ? QString::number(p.resolution) + " m" : "unavailable"));
      png_->setEnabled(true);
      visualization_->setEnabled(!p.geotiff.isEmpty());
      scientific_->setEnabled(!p.scientific.isEmpty());
      thumbnail_->clear();
      metadata_->setPlainText(
          "Show this product to read its bounded preview and processing metadata.");
      for (const auto &active : displayed_)
        if (active.id == id) {
          thumbnail_->setPixmap(QPixmap::fromImage(active.image)
                                    .scaled(280, 220, Qt::KeepAspectRatio, Qt::FastTransformation));
          metadata_->setPlainText(
              QString::fromUtf8(QJsonDocument(active.metadata).toJson(QJsonDocument::Indented)));
        }
      return;
    }
}
void SatelliteWindow::showProduct(const QString &id, bool compare) {
  if (busy_) {
    status_->setText("Wait for the current import or export to finish.");
    return;
  }
  ProductLayer descriptor;
  bool found = false;
  for (const auto &p : library_)
    if (p.id == id) {
      descriptor = p;
      found = true;
      break;
    }
  if (!found)
    return;
  busy_ = true;
  status_->setText("Loading bounded display preview…");
  auto *watcher = new QFutureWatcher<ReadResult>(this);
  connect(watcher, &QFutureWatcher<ReadResult>::finished, this,
          [this, watcher, id, compare, descriptor] {
            const auto result = watcher->result();
            watcher->deleteLater();
            busy_ = false;
            if (!result.second.isEmpty())
              status_->setText("Preview loading failed: " + result.second);
            else
              for (const auto &p : result.first)
                if (p.id == id) {
                  if (p.manifestHash != descriptor.manifestHash) {
                    status_->setText("The product changed on disk. Reimport it before displaying.");
                    break;
                  }
                  if (!compare)
                    displayed_ = {p};
                  else {
                    for (qsizetype i = displayed_.size(); i-- > 0;)
                      if (displayed_[i].id == id)
                        displayed_.removeAt(i);
                    if (displayed_.size() == 2)
                      displayed_.removeLast();
                    displayed_.append(p);
                  }
                  emit layersChanged(displayed_, !compare);
                  status_->setText("Dated preview loaded. Comparisons are visual; independent "
                                   "geographic/radiometric validation remains separate.");
                  for (int i = 0; i < list_->count(); ++i)
                    if (list_->item(i)->data(Qt::UserRole).toString() == id)
                      list_->setCurrentRow(i);
                  selected();
                  break;
                }
            pumpImports();
          });
  watcher->setFuture(QtConcurrent::run([path = descriptor.manifest] { return load(path, true); }));
}
void SatelliteWindow::exportSelected(const QString &kind) {
  if (busy_ || !list_->currentItem())
    return;
  const auto id = list_->currentItem()->data(Qt::UserRole).toString();
  for (const auto &p : library_)
    if (p.id == id) {
      const auto source = kind == "png"          ? p.preview
                          : kind == "scientific" ? p.scientific
                                                 : p.geotiff;
      if (source.isEmpty())
        return;
      QString safeID = p.sourceID.left(160);
      safeID.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
      const auto name = safeID + "-" + p.band +
                        (kind == "png"          ? "-preview.png"
                         : kind == "scientific" ? "-scientific.tif"
                                                : "-display.tif");
      const auto path = QFileDialog::getSaveFileName(
          this, "Export satellite product",
          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + name,
          kind == "png" ? "PNG image (*.png)" : "GeoTIFF (*.tif)");
      if (path.isEmpty())
        return;
      busy_ = true;
      cancelled_ = std::make_shared<std::atomic_bool>(false);
      cancel_->show();
      status_->setText("Exporting the original file in bounded blocks…");
      auto *watcher = new QFutureWatcher<QString>(this);
      connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const auto error = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        cancel_->hide();
        cancelled_.reset();
        status_->setText(error.isEmpty() ? "Export completed." : error);
        pumpImports();
      });
      watcher->setFuture(QtConcurrent::run([source, path, cancelled = cancelled_] {
        try {
          exportProductFile(source, path, cancelled);
          return QString();
        } catch (const std::exception &e) {
          return QString::fromUtf8(e.what());
        }
      }));
      return;
    }
}
} // namespace godeye
