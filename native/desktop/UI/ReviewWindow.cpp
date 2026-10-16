#include "ReviewWindow.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace godeye {
namespace {
QJsonObject findSite(SiteLibrary *library, const QString &id) {
  if (library)
    for (const auto value : library->sites())
      if (value.toObject()["id"] == id)
        return value.toObject();
  return {};
}
} // namespace
ReviewWindow::ReviewWindow(const QString &profile, MapWidget &map, Missions &missions,
                           SatelliteWindow &satellite, ProcessingWindow &processing,
                           QWidget *parent)
    : QDialog(parent, Qt::Tool), map_(map), missions_(missions), satellite_(satellite),
      processing_(processing), settings_(profile + "/desktop.ini", QSettings::IniFormat) {
  setWindowTitle("GodEye — Infrastructure review");
  setAttribute(Qt::WA_QuitOnClose, false);
  resize(720, 850);
  auto *outer = new QVBoxLayout(this);
  auto *intro = new QLabel(
      "Compare calibrated radar acquisitions over a saved site. Candidate patches measure "
      "backscatter changes; analyst review and corroboration are required to interpret them.");
  intro->setWordWrap(true);
  outer->addWidget(intro);
  tabs_ = new QTabWidget;
  outer->addWidget(tabs_, 1);
  auto section = [this](QWidget *widget, const QString &label) {
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(widget);
    tabs_->addTab(scroll, label);
  };
  auto button = [this](QVBoxLayout *layout, const QString &text, std::function<void()> fn) {
    auto *b = new QPushButton(text);
    layout->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, fn] { run(fn); });
    return b;
  };
  auto *siteBox = new QGroupBox("1 · Saved site boundary");
  auto *siteLayout = new QVBoxLayout(siteBox);
  section(siteBox, "Saved sites");
  site_ = new QComboBox;
  site_->setObjectName("reviewSite");
  siteLayout->addWidget(site_);
  auto *siteForm = new QFormLayout;
  siteLayout->addLayout(siteForm);
  name_ = new QLineEdit;
  name_->setMaxLength(120);
  kind_ = new QLineEdit("Infrastructure");
  kind_->setMaxLength(80);
  siteForm->addRow("Name", name_);
  siteForm->addRow("Kind", kind_);
  bounds_ = new QLabel("Draw a site boundary on the map.");
  bounds_->setWordWrap(true);
  siteLayout->addWidget(bounds_);
  button(siteLayout, "Draw new site boundary", [this] {
    map_.beginRegion(this);
    map_.window()->raise();
  });
  button(siteLayout, "Save new site", [this] {
    if (!sites_ || !boundary_)
      throw std::runtime_error("Draw a boundary and open a readable site library first.");
    const auto id = sites_->add(name_->text(), kind_->text(), *boundary_);
    refreshSites(id);
    status_->setText("Site saved locally.");
  });
  button(siteLayout, "Rename selected site", [this] {
    if (!sites_)
      return;
    const auto id = site_->currentData().toString();
    sites_->rename(id, name_->text(), kind_->text());
    refreshSites(id);
  });
  button(siteLayout, "Focus selected site", [this] {
    const auto site = findSite(sites_.get(), site_->currentData().toString());
    if (const auto bounds = Bounds::read(site["region"]))
      map_.focus(*bounds);
  });
  button(siteLayout, "Link site to selected mission", [this] {
    const auto site = findSite(sites_.get(), site_->currentData().toString());
    if (site.isEmpty())
      throw std::runtime_error("Choose a saved site first.");
    missions_.addEvidence({{"id", newID()},
                           {"kind", "site"},
                           {"referenceID", site["id"]},
                           {"title", site["name"]},
                           {"savedAt", foundationNow()},
                           {"site", site}});
    status_->setText("Site snapshot linked to the selected mission.");
  });
  button(siteLayout, "Remove selected saved boundary", [this] {
    if (!sites_)
      return;
    sites_->remove(site_->currentData().toString());
    refreshSites();
    status_->setText("Saved boundary removed. Reviews and source acquisitions remain available; "
                     "the previous site library is in its .backup file.");
  });
  button(siteLayout, "Open copied site library…", [this] {
    const auto path =
        QFileDialog::getOpenFileName(this, "Open site library", {}, "Site library (*.json)");
    if (path.isEmpty())
      return;
    auto library = std::make_unique<SiteLibrary>(path);
    library->load();
    sites_ = std::move(library);
    settings_.setValue("siteLibrary", path);
    refreshSites();
  });
  connect(site_, &QComboBox::currentIndexChanged, this, [this] {
    const auto site = findSite(sites_.get(), site_->currentData().toString());
    if (!site.isEmpty()) {
      name_->setText(site["name"].toString());
      kind_->setText(site["kind"].toString());
      const auto bounds = Bounds::read(site["region"]);
      if (bounds)
        bounds_->setText(QString("Saved bounds: %1, %2 → %3, %4")
                             .arg(bounds->south, 0, 'f', 6)
                             .arg(bounds->west, 0, 'f', 6)
                             .arg(bounds->north, 0, 'f', 6)
                             .arg(bounds->east, 0, 'f', 6));
    }
  });
  connect(&map_, &MapWidget::regionSelected, this, [this](Bounds bounds, QObject *owner) {
    if (owner != this)
      return;
    boundary_ = bounds;
    bounds_->setText(QString("New boundary: %1, %2 → %3, %4")
                         .arg(bounds.south, 0, 'f', 6)
                         .arg(bounds.west, 0, 'f', 6)
                         .arg(bounds.north, 0, 'f', 6)
                         .arg(bounds.east, 0, 'f', 6));
    show();
    raise();
  });
  auto *compareBox = new QGroupBox("2 · Compatible acquisitions and measured change");
  auto *compareLayout = new QVBoxLayout(compareBox);
  section(compareBox, "Compare acquisitions");
  auto *form = new QFormLayout;
  compareLayout->addLayout(form);
  before_ = new QComboBox;
  after_ = new QComboBox;
  form->addRow("Baseline", before_);
  form->addRow("Later", after_);
  button(compareLayout, "Refresh processed SAR acquisitions", [this] { refreshProducts(); });
  polarization_ = new QComboBox;
  polarization_->addItems({"VV", "VH"});
  form->addRow("Polarization", polarization_);
  threshold_ = new QDoubleSpinBox;
  threshold_->setRange(1, 20);
  threshold_->setValue(3);
  threshold_->setSuffix(" dB");
  form->addRow("Absolute change threshold", threshold_);
  window_ = new QComboBox;
  window_->addItems({"3", "5", "7"});
  window_->setCurrentText("5");
  form->addRow("Linear-power mean window (pixels)", window_);
  minimum_ = new QSpinBox;
  minimum_->setRange(9, 10000);
  minimum_->setValue(25);
  form->addRow("Minimum connected pixels", minimum_);
  auto *limit = new QLabel(
      "Up to one million source-grid pixels. The later linear power is bilinearly aligned to the "
      "baseline; source spacing, calibration and terrain correction are inherited and require "
      "independent validation. Thresholds are measurements, not probabilities.");
  limit->setWordWrap(true);
  compareLayout->addWidget(limit);
  button(compareLayout, "Compare selected site acquisitions", [this] {
    const auto selected = findSite(sites_.get(), site_->currentData().toString());
    if (selected.isEmpty())
      throw std::runtime_error("Choose a saved site.");
    if (!processing_.startReview(selected, before_->currentData().toString(),
                                 after_->currentData().toString(),
                                 {{"polarization", polarization_->currentText()},
                                  {"thresholdDb", threshold_->value()},
                                  {"averagingWindow", window_->currentText().toInt()},
                                  {"minimumPixels", minimum_->value()}}))
      throw std::runtime_error("Review could not start. Check acquisition compatibility and the "
                               "processing window status.");
    processing_.show();
    processing_.raise();
  });
  auto *results = new QGroupBox("3 · Saved review, candidates and analyst decisions");
  auto *resultLayout = new QVBoxLayout(results);
  section(results, "Review results");
  reviews_ = new QComboBox;
  reviews_->setObjectName("savedReviews");
  resultLayout->addWidget(reviews_);
  button(resultLayout, "Open existing review result…", [this] {
    const auto path = QFileDialog::getOpenFileName(this, "Open completed change review", {},
                                                   "Review result (result.json)");
    if (!path.isEmpty())
      openReview(path);
  });
  quality_ = new QLabel("No review selected.");
  quality_->setTextFormat(Qt::PlainText);
  quality_->setWordWrap(true);
  resultLayout->addWidget(quality_);
  view_ = new QComboBox;
  view_->addItems({"Change", "Before", "After"});
  resultLayout->addWidget(view_);
  button(resultLayout, "Show selected review view on shared map", [this] {
    showLayer();
  })->setObjectName("reviewDisplay");
  button(resultLayout, "Remove review overlay", [this] {
    emit layersChanged({}, false);
    map_.setPaths("change-candidate", {});
  });
  candidates_ = new QListWidget;
  candidates_->setMinimumHeight(130);
  candidates_->setObjectName("reviewCandidates");
  resultLayout->addWidget(candidates_);
  connect(candidates_, &QListWidget::currentRowChanged, this, [this] { chooseCandidate(); });
  decision_ = new QComboBox;
  decision_->setObjectName("reviewDecision");
  decision_->addItems({"Unreviewed", "Follow up", "Explained / dismiss", "Uncertain"});
  resultLayout->addWidget(decision_);
  rationale_ = new QPlainTextEdit;
  rationale_->setObjectName("reviewRationale");
  rationale_->setMaximumHeight(120);
  rationale_->setPlaceholderText("Analyst rationale · up to 4000 characters");
  resultLayout->addWidget(rationale_);
  reference_ = new QLineEdit;
  reference_->setMaxLength(1000);
  reference_->setPlaceholderText("Analyst citation / URL");
  resultLayout->addWidget(reference_);
  button(resultLayout, "Save analyst decision", [this] { saveDecision(); });
  exportKind_ = new QComboBox;
  exportKind_->addItems({"Evidence and decisions (JSON)", "delta-db.tif", "candidate-mask.tif",
                         "change-display.tif", "Change.png", "Before.png", "After.png"});
  resultLayout->addWidget(exportKind_);
  button(resultLayout, "Export selected review artifact…", [this] { exportFile(); });
  button(resultLayout, "Open review files", [this] {
    if (review_)
      QDesktopServices::openUrl(QUrl::fromLocalFile(review_->directory));
  });
  status_ = new QLabel;
  status_->setTextFormat(Qt::PlainText);
  status_->setWordWrap(true);
  outer->addWidget(status_);
  try {
    sites_ = std::make_unique<SiteLibrary>(
        settings_.value("siteLibrary", profile + "/Satellite/sites.json").toString());
    sites_->load();
    refreshSites();
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
    sites_.reset();
  }
  const auto paths = settings_.value("changeReviews").toStringList().mid(0, 500);
  for (const auto &path : paths)
    reviews_->addItem(QFileInfo(path).absolutePath(), path);
  connect(reviews_, &QComboBox::currentIndexChanged, this, [this] {
    if (!loading_)
      run([this] { loadReview(reviews_->currentData().toString()); });
  });
  connect(&processing_, &ProcessingWindow::reviewReady, this, &ReviewWindow::openReview);
  refreshProducts();
  if (!paths.isEmpty())
    run([this, &paths] { loadReview(paths.first()); });
}
ReviewWindow::~ReviewWindow() = default;
void ReviewWindow::run(const std::function<void()> &action) {
  try {
    action();
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
  }
}
void ReviewWindow::refreshSites(const QString &selected) {
  QSignalBlocker block(site_);
  site_->clear();
  site_->addItem("Choose saved site", QString{});
  if (sites_)
    for (const auto value : sites_->sites()) {
      const auto o = value.toObject();
      site_->addItem(o["name"].toString(), o["id"].toString());
    }
  site_->setCurrentIndex(std::max(0, site_->findData(selected)));
  block.unblock();
  if (site_->currentIndex() > 0)
    emit site_->currentIndexChanged(site_->currentIndex());
}
void ReviewWindow::refreshProducts() {
  for (auto *combo : {before_, after_}) {
    const auto selected = combo->currentData();
    combo->clear();
    combo->addItem("Select processed acquisition", QString{});
    QSet<QString> seen;
    for (const auto &p : satellite_.library())
      if (p.kind == "radar" && !seen.contains(p.manifest)) {
        combo->addItem(p.acquisition + " · " + p.sourceID, p.manifest);
        seen.insert(p.manifest);
      }
    combo->setCurrentIndex(std::max(0, combo->findData(selected)));
  }
}
void ReviewWindow::openReview(const QString &manifest) {
  run([&] {
    if (!saveDecision())
      return;
    loadReview(manifest);
    QSignalBlocker block(reviews_);
    auto index = reviews_->findData(manifest);
    if (index < 0) {
      reviews_->insertItem(0,
                           review_->result["site"].toObject()["name"].toString() + " · " +
                               review_->result["createdAt"].toString(),
                           manifest);
      index = 0;
    }
    reviews_->setCurrentIndex(index);
    QStringList paths;
    for (int i = 0; i < reviews_->count() && i < 500; ++i)
      paths.append(reviews_->itemData(i).toString());
    settings_.setValue("changeReviews", paths);
    show();
    raise();
  });
}
void ReviewWindow::loadReview(const QString &manifest) {
  if (busy_ || manifest.isEmpty() || !saveDecision())
    return;
  const auto next = ChangeReview::read(manifest);
  auto notes = std::make_unique<ChangeNotes>(next);
  notes->load();
  loading_ = true;
  review_ = next;
  tabs_->setCurrentIndex(2);
  notes_ = std::move(notes);
  candidate_.clear();
  {
    QSignalBlocker block(candidates_);
    candidates_->clear();
    for (const auto value : next.result["candidates"].toArray()) {
      const auto c = value.toObject();
      auto *item = new QListWidgetItem(QString("%1 · %2 · %3 pixels · mean %4 dB · ≈%5 m²")
                                           .arg(c["id"].toString(), c["direction"].toString())
                                           .arg(c["pixels"].toInt())
                                           .arg(c["meanDeltaDb"].toDouble(), 0, 'f', 2)
                                           .arg(c["approximateAreaM2"].toDouble(), 0, 'f', 0),
                                       candidates_);
      item->setData(Qt::UserRole, c["id"].toString());
    }
  }
  const auto evidence = next.result["evidence"].toArray();
  QStringList limitations;
  for (const auto value : next.result["limitations"].toArray())
    limitations.append(value.toString().left(2000));
  quality_->setText(
      QString("%1 → %2 UTC\nCommon-valid coverage: %3% · %4 × %5 source pixels\n%6\n%7")
          .arg(evidence[0].toObject()["metadata"].toObject()["acquisitionStart"].toString(),
               evidence[1].toObject()["metadata"].toObject()["acquisitionStart"].toString())
          .arg(next.result["commonValidFraction"].toDouble() * 100, 0, 'f', 1)
          .arg(next.result["width"].toInt())
          .arg(next.result["height"].toInt())
          .arg(next.result["alignment"].toString(), limitations.join('\n')));
  loading_ = false;
  rationale_->clear();
  reference_->clear();
  decision_->setCurrentIndex(0);
  if (candidates_->count())
    candidates_->setCurrentRow(0);
  else
    status_->setText(
        "No patches meet these thresholds. This does not establish that the site was unchanged.");
}
bool ReviewWindow::saveDecision() {
  if (loading_ || candidate_.isEmpty() || !notes_)
    return true;
  const auto old = notes_->document()["candidates"].toObject()[candidate_].toObject();
  if (old["decision"].toString("Unreviewed") == decision_->currentText() &&
      old["note"].toString() == rationale_->toPlainText() &&
      old["reference"].toString() == reference_->text())
    return true;
  try {
    notes_->set(candidate_, decision_->currentText(), rationale_->toPlainText(),
                reference_->text());
    status_->setText("Analyst decision saved locally.");
    return true;
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
    return false;
  }
}
void ReviewWindow::chooseCandidate() {
  if (loading_ || !review_ || !notes_)
    return;
  if (!saveDecision()) {
    QSignalBlocker block(candidates_);
    for (int i = 0; i < candidates_->count(); ++i)
      if (candidates_->item(i)->data(Qt::UserRole) == candidate_)
        candidates_->setCurrentRow(i);
    return;
  }
  candidate_ = candidates_->currentItem()
                   ? candidates_->currentItem()->data(Qt::UserRole).toString()
                   : QString{};
  const auto value = notes_->document()["candidates"].toObject()[candidate_].toObject();
  decision_->setCurrentText(value["decision"].toString("Unreviewed"));
  rationale_->setPlainText(value["note"].toString());
  reference_->setText(value["reference"].toString());
  for (const auto c : review_->result["candidates"].toArray())
    if (c.toObject()["id"] == candidate_)
      if (const auto b = Bounds::read(c.toObject()["bounds"])) {
        map_.setPaths("change-candidate", {{{b->south, b->west},
                                            {b->south, b->east},
                                            {b->north, b->east},
                                            {b->north, b->west},
                                            {b->south, b->west}}});
      }
}
void ReviewWindow::showLayer() {
  if (!review_ || busy_)
    return;
  busy_ = true;
  const auto review = *review_;
  const auto view = view_->currentText();
  auto *watcher = new QFutureWatcher<std::pair<ProductLayer, QString>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
    const auto value = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    if (!value.second.isEmpty())
      status_->setText(value.second);
    else {
      emit layersChanged({value.first}, true);
      map_.setBlend(1);
      status_->setText(value.first.label);
    }
  });
  watcher->setFuture(QtConcurrent::run([review, view] {
    try {
      return std::make_pair(review.layer(view), QString{});
    } catch (const std::exception &e) {
      return std::make_pair(ProductLayer{}, QString::fromUtf8(e.what()));
    }
  }));
}
void ReviewWindow::exportFile() {
  if (!review_ || !notes_ || busy_ || !saveDecision())
    return;
  const auto kind = exportKind_->currentText();
  const bool json = exportKind_->currentIndex() == 0;
  const auto target = QFileDialog::getSaveFileName(this, "Export review artifact",
                                                   json ? "site-review.json" : kind);
  if (target.isEmpty())
    return;
  if (json) {
    auto result = review_->result;
    result.remove("workerPID");
    const auto bytes =
        QJsonDocument(
            QJsonObject{{"version", 1},
                        {"exportedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                        {"result", result},
                        {"notes", notes_->document()},
                        {"interpretation", "Radar-change candidates with analyst decisions; not "
                                           "validated infrastructure classification."}})
            .toJson();
    QSaveFile file(target);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      throw std::runtime_error("Review export could not be saved atomically.");
    status_->setText("Evidence and analyst decisions exported.");
    return;
  }
  const auto source = safeProductFile(review_->directory, kind);
  busy_ = true;
  auto *watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
    const auto error = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    status_->setText(error.isEmpty() ? "Original review artifact exported." : error);
  });
  watcher->setFuture(QtConcurrent::run([source, target] {
    try {
      exportProductFile(source, target);
      return QString{};
    } catch (const std::exception &e) {
      return QString::fromUtf8(e.what());
    }
  }));
}
void ReviewWindow::closeEvent(QCloseEvent *event) {
  if (!saveDecision())
    event->ignore();
  else
    QDialog::closeEvent(event);
}
} // namespace godeye
