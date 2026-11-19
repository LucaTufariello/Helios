#include "DossierPanel.h"
#include "Core/Timeline.h"
#include "MissionEditor.h"
#include "WorkspaceStyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTimeZone>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <stdexcept>
namespace helios {
namespace {
QString selection(QListWidget *list) {
  return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString{};
}
QJsonObject record(const QJsonArray &values, const QString &id) {
  for (const auto value : values)
    if (value.toObject()["id"].toString() == id)
      return value.toObject();
  return {};
}
void fill(QListWidget *list, const QJsonArray &values,
          const std::function<QString(QJsonObject)> &caption) {
  const auto selected = selection(list);
  QSignalBlocker block(list);
  list->clear();
  for (const auto v : values) {
    const auto o = v.toObject();
    auto *item = new QListWidgetItem(caption(o), list);
    item->setData(Qt::UserRole, o["id"]);
    if (o["id"] == selected)
      list->setCurrentItem(item);
  }
  if (!list->currentItem() && list->count())
    list->setCurrentRow(0);
}
QString label(const DossierEvidence &e) {
  return (e.baseline ? "REFERENCE · " : QString{}) + e.item["title"].toString() + "\n" +
         e.item["kind"].toString().toUpper() + " · " +
         (e.time.isValid() ? e.time.toString("yyyy-MM-dd HH:mm 'UTC'") : "Source time unknown") +
         (e.available ? QString{} : " · unavailable locally");
}
} // namespace
DossierPanel::DossierPanel(Missions &missions, QWidget *parent)
    : QWidget(parent), missions_(missions) {
  setObjectName("siteDossier");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  title_ = design::label("Select an area", "title");
  title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  title_->setObjectName("dossierTitle");
  title_->setWordWrap(true);
  layout->addWidget(title_);
  purpose_ = design::label(
      "Build a dossier from an existing area or import a saved infrastructure site.", "secondary");
  purpose_->setWordWrap(true);
  layout->addWidget(purpose_);
  contextActions_ = new QWidget;
  auto *actions = new QGridLayout(contextActions_);
  actions->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(contextActions_);
  auto action = [this](QGridLayout *row, int position, const QString &text, const QString &id,
                       const std::function<void()> &fn) {
    auto *button = new QPushButton(text);
    button->setObjectName(id);
    row->addWidget(button, position / 2, position % 2);
    connect(button, &QPushButton::clicked, this, [this, fn] { run(fn); });
    return button;
  };
  action(actions, 8, "Register / link shared asset", "dossierAsset", [this] {
    guardContext(missionID_, areaID_);
    emit assetRequested(areaID_);
  });
  action(actions, 9, "Draft finding from observation", "dossierFinding", [this] {
    guardContext(missionID_, areaID_);
    const auto id = selection(observations_);
    QJsonObject source;
    for (auto v : view_.observations)
      if (v.toObject()["id"] == id)
        source = v.toObject();
    if (source.isEmpty())
      throw std::runtime_error("Select an observation in this dossier first.");
    emit findingRequested(QJsonObject{
        {"kind", "Observation"},
        {"sourceID", source["id"]},
        {"label", source["title"]},
        {"record", source},
        {"areaID", areaID_},
        {"sourceDate", source["observedAt"]},
        {"qualityLimitations", "Observation snapshot; model proposals and source resolution remain "
                               "limitations. Analyst review is required."}});
  });
  action(actions, 0, "Show area on map", "dossierFocus", [this] {
    guardContext(missionID_, areaID_);
    if (auto b = Bounds::read(view_.area["region"]))
      emit focusRequested(*b);
  });
  action(actions, 1, "Find optical imagery", "dossierOptical", [this] {
    guardContext(missionID_, areaID_);
    if (auto b = Bounds::read(view_.area["region"]))
      emit acquisitionRequested(*b, false);
  });
  action(actions, 2, "Find radar acquisitions", "dossierRadar", [this] {
    guardContext(missionID_, areaID_);
    if (auto b = Bounds::read(view_.area["region"]))
      emit acquisitionRequested(*b, true);
  });
  action(actions, 3, "Measured SAR review…", "dossierMeasuredReview", [this] {
    guardContext(missionID_, areaID_);
    emit reviewRequested(view_.area);
  });
  tabs_ = new QTabWidget;
  tabs_->setObjectName("dossierTabs");
  layout->addWidget(tabs_, 1);
  auto page = [this](const QString &title) {
    auto *content = new QWidget;
    auto *column = new QVBoxLayout(content);
    column->setSpacing(10);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    tabs_->addTab(scroll, title);
    return column;
  };
  auto *overview = page("Overview");
  overview_ = design::label({}, "secondary");
  overview_->setWordWrap(true);
  overview_->setObjectName("dossierSummary");
  overview->addWidget(overview_);
  overview->addWidget(design::label("REFERENCE ACQUISITIONS", "kicker"));
  baselines_ = new QListWidget;
  baselines_->setObjectName("dossierBaselines");
  baselines_->setMinimumHeight(100);
  baselines_->setWordWrap(true);
  baselines_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  overview->addWidget(baselines_, 1);
  notices_ = design::label({}, "secondary");
  notices_->setWordWrap(true);
  notices_->setObjectName("dossierNotices");
  overview->addWidget(notices_);
  auto *help = design::label("References are pinned by the analyst. A local preview or geographic "
                             "overlap does not establish measurement quality or corroboration.",
                             "secondary");
  help->setWordWrap(true);
  overview->addWidget(help);
  auto *evidence = page("Evidence");
  evidenceTabs_ = new QTabWidget;
  evidenceTabs_->setObjectName("dossierEvidenceTabs");
  evidence->addWidget(evidenceTabs_, 1);
  linked_ = new QListWidget;
  linked_->setObjectName("dossierLinkedEvidence");
  linked_->setMinimumHeight(130);
  linked_->setWordWrap(true);
  linked_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  suggested_ = new QListWidget;
  suggested_->setObjectName("dossierSuggestedEvidence");
  suggested_->setMinimumHeight(130);
  suggested_->setWordWrap(true);
  suggested_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  evidenceTabs_->addTab(linked_, "Linked evidence");
  evidenceTabs_->addTab(suggested_, "Suggested nearby");
  auto *evidenceActions = new QGridLayout;
  evidence->addLayout(evidenceActions);
  link_ = action(evidenceActions, 0, "Link suggested item", "dossierLink", [this] {
    if (auto entry = selectedEvidence())
      missions_.linkDossierEvidence(missionID_, areaID_, entry->item);
  });
  action(evidenceActions, 1, "Link mission evidence…", "dossierLinkExisting", [this] {
    const auto mission = missionID_, area = areaID_;
    guardContext(mission, area);
    QStringList labels;
    QVector<QJsonObject> items;
    const auto current = missions_.selected();
    for (const auto value : current["items"].toArray()) {
      const auto item = value.toObject();
      bool linked = false;
      for (const auto &e : view_.linked)
        if (e.item["id"] == item["id"])
          linked = true;
      if (!linked) {
        items.append(item);
        labels.append(item["title"].toString() + " · " + item["id"].toString().left(8));
      }
    }
    if (items.isEmpty())
      throw std::runtime_error("No unlinked mission evidence is available. Import imagery in "
                               "Satellite or save a source record first.");
    bool ok = false;
    const auto selected = QInputDialog::getItem(
        this, "Link evidence", "Evidence in the selected mission", labels, 0, false, &ok);
    if (ok) {
      guardContext(mission, area);
      missions_.linkDossierEvidence(mission, area, items[labels.indexOf(selected)]);
    }
  });
  unlink_ = action(evidenceActions, 2, "Unlink from dossier", "dossierUnlink", [this] {
    if (auto e = selectedEvidence())
      missions_.setDossierEvidence(missionID_, areaID_, e->item["id"].toString(), false);
  });
  pin_ = action(evidenceActions, 3, "Pin reference", "dossierPinBaseline", [this] {
    if (auto e = selectedEvidence())
      missions_.setDossierBaseline(missionID_, areaID_, e->item["id"].toString(), !e->baseline);
  });
  show_ = action(evidenceActions, 4, "Show selected on map", "dossierShowEvidence", [this] {
    guardContext(missionID_, areaID_);
    if (auto e = selectedEvidence())
      emit evidenceRequested(e->item);
  });
  compare_ = action(evidenceActions, 5, "Compare with reference…", "dossierCompare",
                    [this] { prepareComparison(); });
  source_ =
      design::label("Select evidence to inspect its source and acquisition time.", "secondary");
  source_->setWordWrap(true);
  source_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  source_->setObjectName("dossierSourceDetails");
  evidence->addWidget(source_);
  preview_ = new QLabel;
  preview_->setObjectName("dossierPreview");
  preview_->setMinimumHeight(0);
  preview_->setMaximumHeight(180);
  preview_->setAlignment(Qt::AlignCenter);
  evidence->addWidget(preview_);
  raw_ = new QPlainTextEdit;
  raw_->setReadOnly(true);
  raw_->setMaximumHeight(150);
  design::disclosure(evidence, raw_, "Retained source record");
  auto records = [&](const QString &title, const QString &collection, QListWidget *&list) {
    auto *column = page(title);
    list = new QListWidget;
    list->setObjectName("dossier-" + collection);
    list->setMinimumHeight(140);
    column->addWidget(list, 1);
    auto *row = new QGridLayout;
    column->addLayout(row);
    action(
        row, 0,
        "Add " +
            (collection == "observations" ? QString("observation") : title.toLower().chopped(1)) +
            "…",
        "dossierAdd-" + collection, [this, collection] { addRecord(collection); });
    action(row, 1, "Assign existing…", "dossierAssign-" + collection,
           [this, collection] { assignRecord(collection); });
    action(row, 2, "Edit selected…", "dossierEdit-" + collection,
           [this, collection, list] { editSelected(collection, list); });
    action(row, 3, "Remove selected…", "dossierRemove-" + collection,
           [this, collection, list] { removeSelected(collection, list); });
    return column;
  };
  auto *reviews = records("Reviews", "observations", observations_);
  auto *reviewActions = new QGridLayout;
  reviews->addLayout(reviewActions);
  action(reviewActions, 0, "Review observation…", "dossierReviewObservation",
         [this] { reviewObservation(); });
  action(reviewActions, 1, "Create follow-up task…", "dossierFollowUp", [this] {
    const auto id = selection(observations_);
    if (id.isEmpty())
      throw std::runtime_error("Select an observation first.");
    addRecord("tasks", id);
  });
  entryDetail_ = new QPlainTextEdit;
  entryDetail_->setReadOnly(true);
  entryDetail_->setMaximumHeight(150);
  reviews->addWidget(entryDetail_);
  connect(observations_, &QListWidget::currentItemChanged, this, &DossierPanel::inspectRecords);
  auto *notePage = records("Notes", "notes", notes_);
  noteDetail_ = new QPlainTextEdit;
  noteDetail_->setReadOnly(true);
  noteDetail_->setMaximumHeight(170);
  notePage->addWidget(noteDetail_);
  connect(notes_, &QListWidget::currentItemChanged, this, &DossierPanel::inspectRecords);
  auto *taskPage = records("Tasks", "tasks", tasks_);
  auto *taskButtons = new QGridLayout;
  taskPage->addLayout(taskButtons);
  action(taskButtons, 0, "Complete / reopen task", "dossierCompleteTask", [this] {
    guardContext(missionID_, areaID_);
    const auto task = record(view_.tasks, selection(tasks_));
    if (!task.isEmpty())
      missions_.setTaskDone(task["id"].toString(), !task["done"].toBool());
  });
  status_ = design::label({}, "secondary");
  status_->setWordWrap(true);
  status_->setObjectName("dossierStatus");
  layout->addWidget(status_);
  connect(linked_, &QListWidget::currentItemChanged, this, &DossierPanel::inspectEvidence);
  connect(suggested_, &QListWidget::currentItemChanged, this, &DossierPanel::inspectEvidence);
  connect(evidenceTabs_, &QTabWidget::currentChanged, this, &DossierPanel::inspectEvidence);
  connect(baselines_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
    tabs_->setCurrentIndex(1);
    evidenceTabs_->setCurrentIndex(0);
    for (int i = 0; i < linked_->count(); ++i)
      if (linked_->item(i)->data(Qt::UserRole) == item->data(Qt::UserRole))
        linked_->setCurrentRow(i);
  });
  updateTimer_.setSingleShot(true);
  updateTimer_.setInterval(20);
  connect(&updateTimer_, &QTimer::timeout, this, &DossierPanel::startRefresh);
  connect(&missions_, &Missions::changed, this, &DossierPanel::refresh);
  connect(&watcher_, &QFutureWatcherBase::finished, this, [this] {
    projectionActive_ = false;
    const auto result = watcher_.result();
    if (runningGeneration_ == generation_ && result.missionID == missions_.selectedID() &&
        result.areaID == areaID_)
      publish(result);
    if (pending_) {
      pending_ = false;
      updateTimer_.start();
    }
  });
  connect(&previewWatcher_, &QFutureWatcherBase::finished, this, [this] {
    previewActive_ = false;
    const auto result = previewWatcher_.result();
    if (previewRunning_ == previewGeneration_) {
      if (result.first.isNull())
        preview_->setText(result.second);
      else {
        preview_->setPixmap(QPixmap::fromImage(result.first));
        preview_->setToolTip(
            "Bounded local display preview; original sensor resolution is unchanged.");
      }
    }
    startPreview();
  });
  contextActions_->setEnabled(false);
  tabs_->setEnabled(false);
  inspectEvidence();
}
void DossierPanel::addPage(QWidget *page, const QString &title) {
  tabs_->addTab(page, title);
}
void DossierPanel::setLibrary(QVector<ProductLayer> products) {
  library_ = std::move(products);
  refresh();
}
void DossierPanel::setArea(const QString &id) {
  if (areaID_ == id && missionID_ == missions_.selectedID())
    return;
  areaID_ = id;
  missionID_ = missions_.selectedID();
  emit areaChanged(id);
  view_ = {};
  ++previewGeneration_;
  pendingPreview_.reset();
  preview_->clear();
  title_->setText(id.isEmpty() ? "Select an area" : "Opening dossier…");
  contextActions_->setEnabled(false);
  tabs_->setEnabled(false);
  refresh();
}
void DossierPanel::refresh() {
  ++generation_;
  contextActions_->setEnabled(false);
  if (missionID_ != missions_.selectedID()) {
    missionID_ = missions_.selectedID();
    view_ = {};
    contextActions_->setEnabled(false);
    tabs_->setEnabled(false);
    ++previewGeneration_;
    pendingPreview_.reset();
    preview_->clear();
  }
  updateTimer_.start();
}
void DossierPanel::startRefresh() {
  if (projectionActive_) {
    pending_ = true;
    return;
  }
  pending_ = false;
  runningGeneration_ = generation_;
  projectionActive_ = true;
  const auto plan = missions_.selected();
  const auto area = areaID_;
  const auto library = library_;
  status_->setText(area.isEmpty() ? "Choose an area to inspect its dossier."
                                  : "Updating local dossier…");
  watcher_.setFuture(
      QtConcurrent::run([plan, area, library] { return dossierView(plan, area, library); }));
}
void DossierPanel::publish(DossierView value) {
  view_ = std::move(value);
  contextActions_->setEnabled(!view_.area.isEmpty());
  tabs_->setEnabled(!view_.area.isEmpty());
  title_->setText(view_.area["name"].toString("Select an area"));
  title_->setToolTip(title_->text());
  purpose_->setText(view_.area["purpose"].toString().isEmpty()
                        ? "Set a monitoring purpose using Edit area."
                        : view_.area["purpose"].toString());
  int pending = 0, open = 0;
  for (const auto v : view_.observations)
    if (v.toObject()["state"] == "Unreviewed" || v.toObject()["state"] == "Uncertain")
      ++pending;
  for (const auto v : view_.tasks)
    if (!v.toObject()["done"].toBool())
      ++open;
  QString latest = "No linked evidence has a known source time.";
  for (const auto &e : view_.linked)
    if (e.time.isValid()) {
      latest = "Latest dated evidence: " + e.item["title"].toString() + "\n" +
               e.time.toString("yyyy-MM-dd HH:mm 'UTC'");
      break;
    }
  overview_->setText(QString("%1 linked evidence · %2 pending reviews · %3 open tasks\n\n%4")
                         .arg(view_.linked.size())
                         .arg(pending)
                         .arg(open)
                         .arg(latest));
  notices_->setText(view_.notices.join('\n'));
  notices_->setVisible(!view_.notices.isEmpty());
  auto populate = [&](QListWidget *list, const QVector<DossierEvidence> &entries,
                      bool pinnedOnly = false) {
    const auto selected = selection(list);
    QSignalBlocker block(list);
    list->clear();
    for (const auto &e : entries) {
      if (pinnedOnly && !e.baseline)
        continue;
      auto *item = new QListWidgetItem(label(e), list);
      item->setData(Qt::UserRole, e.key);
      item->setToolTip(e.warnings.join('\n'));
      if (e.key == selected)
        list->setCurrentItem(item);
    }
  };
  populate(linked_, view_.linked);
  populate(suggested_, view_.suggestions);
  populate(baselines_, view_.linked, true);
  fill(observations_, view_.observations, [](auto o) {
    return o["state"].toString() + " · " + o["title"].toString() + "\n" + o["method"].toString();
  });
  fill(notes_, view_.notes,
       [](auto n) { return n["title"].toString() + "\n" + dateLabel(n["createdAt"]); });
  fill(tasks_, view_.tasks, [](auto t) {
    return (t["done"].toBool() ? "Completed · " : "Open · ") + t["title"].toString() +
           (t["due"].isUndefined() ? QString{} : "\nDue " + dateLabel(t["due"])) +
           (t["observationID"].isString()
                ? "\nLinked observation: " + t["observationID"].toString().left(8)
                : QString{});
  });
  inspectRecords();
  status_->setText(QString("%1 linked · %2 local suggestions · selected mission only")
                       .arg(view_.linked.size())
                       .arg(view_.suggestions.size()));
  inspectEvidence();
}
std::optional<DossierEvidence> DossierPanel::selectedEvidence() const {
  const bool suggestions = evidenceTabs_->currentIndex() == 1;
  const auto key = selection(suggestions ? suggested_ : linked_);
  for (const auto &entry : (suggestions ? view_.suggestions : view_.linked))
    if (entry.key == key)
      return entry;
  return {};
}
void DossierPanel::inspectRecords() {
  const auto o = record(view_.observations, selection(observations_));
  entryDetail_->setPlainText(o.isEmpty()
                                 ? "Select an observation to review its source and rationale."
                                 : o["method"].toString() + "\n" + o["note"].toString() +
                                       "\nEvidence: " + o["evidenceID"].toString("None linked"));
  const auto note = record(view_.notes, selection(notes_));
  noteDetail_->setPlainText(note.isEmpty()
                                ? "Select a note to read its context and citation."
                                : note["body"].toString() + "\n\n" + note["reference"].toString());
}
void DossierPanel::inspectEvidence() {
  ++previewGeneration_;
  pendingPreview_.reset();
  preview_->clear();
  const auto entry = selectedEvidence();
  const bool suggested = evidenceTabs_->currentIndex() == 1;
  link_->setEnabled(entry.has_value() && suggested);
  unlink_->setEnabled(entry.has_value() && !suggested);
  const bool raster = entry && (entry->item["kind"] == "optical" || entry->item["kind"] == "radar");
  pin_->setEnabled(raster && !suggested);
  pin_->setText(entry && entry->baseline ? "Unpin reference" : "Pin reference");
  show_->setEnabled(entry.has_value());
  compare_->setEnabled(raster && !suggested && entry->available);
  if (!entry) {
    source_->setText("Select evidence to inspect its source, time and availability.");
    raw_->clear();
    return;
  }
  const auto a = entry->item["acquisition"].toObject();
  source_->setText(
      entry->item["title"].toString() + "\n" +
      (raster ? "Sensor: " + a["sensor"].toString("Unknown") +
                    " · Band: " + a["displayBand"].toString("Unknown") + "\nSource ID: " +
                    a["sourceID"].toString("Unknown") + "\nAcquired: " + dateLabel(a["acquiredAt"])
              : "Source time: " + (entry->time.isValid()
                                       ? entry->time.toString("yyyy-MM-dd HH:mm:ss 'UTC'")
                                       : "Unknown")) +
      "\n" +
      (suggested ? "Suggestion only; not linked to this dossier." : "Linked to this dossier.") +
      "\n" + entry->warnings.join('\n'));
  raw_->setPlainText(QJsonDocument(entry->item).toJson(QJsonDocument::Indented));
  previewEvidence(*entry);
}
void DossierPanel::previewEvidence(const DossierEvidence &entry) {
  if (!entry.product || !entry.available)
    return;
  pendingPreview_ = entry;
  startPreview();
}
void DossierPanel::startPreview() {
  if (previewActive_ || !pendingPreview_)
    return;
  const auto entry = *pendingPreview_;
  pendingPreview_.reset();
  previewRunning_ = previewGeneration_;
  previewActive_ = true;
  const auto product = *entry.product;
  preview_->setText("Reading bounded display preview…");
  previewWatcher_.setFuture(QtConcurrent::run([product] {
    try {
      for (const auto &p : readProducts(product.manifest, true))
        if (p.id == product.id && p.manifestHash == product.manifestHash)
          return std::make_pair(
              p.image.scaled(420, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation), QString{});
      return std::make_pair(QImage{},
                            QString("Product changed; reimport it before inspecting its preview."));
    } catch (const std::exception &e) {
      return std::make_pair(QImage{}, QString::fromUtf8(e.what()));
    }
  }));
}
void DossierPanel::guardContext(const QString &mission, const QString &area) const {
  if (mission.isEmpty() || mission != missions_.selectedID() || mission != missionID_ ||
      area != areaID_ || area.isEmpty())
    throw std::runtime_error(
        "The dossier context changed. Choose an area in the selected mission.");
  bool found = false;
  for (const auto v : missions_.selected()["brief"].toObject()["areas"].toArray())
    if (v.toObject()["id"] == area)
      found = true;
  if (!found)
    throw std::runtime_error("The selected area no longer exists.");
}
void DossierPanel::run(const std::function<void()> &action) {
  try {
    guardContext(missionID_, areaID_);
    action();
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
  }
}
void DossierPanel::assignRecord(const QString &collection) {
  const auto mission = missionID_, area = areaID_;
  guardContext(mission, area);
  const auto values = missions_.selected()["brief"].toObject()[collection].toArray();
  QStringList labels;
  QVector<QString> ids;
  for (const auto v : values) {
    const auto o = v.toObject();
    if (o["areaID"] == area)
      continue;
    ids.append(o["id"].toString());
    labels.append(o["title"].toString() + " · " +
                  (o["areaID"].isString() ? "assigned to another area" : "unassigned") + " · " +
                  ids.last().left(8));
  }
  if (ids.isEmpty())
    throw std::runtime_error("No other mission entries are available to assign.");
  bool ok = false;
  const auto picked = QInputDialog::getItem(this, "Assign existing entry",
                                            "Assign to this dossier (updates its area link)",
                                            labels, 0, false, &ok);
  if (ok) {
    guardContext(mission, area);
    missions_.editEntry(collection, ids[labels.indexOf(picked)], {{"areaID", area}});
  }
}
void DossierPanel::editSelected(const QString &collection, QListWidget *list) {
  const auto mission = missionID_, area = areaID_;
  guardContext(mission, area);
  const auto id = selection(list);
  if (!id.isEmpty())
    editMissionEntry(this, missions_, collection, id,
                     [this, mission, area] { guardContext(mission, area); });
}
void DossierPanel::removeSelected(const QString &collection, QListWidget *list) {
  const auto mission = missionID_, area = areaID_, id = selection(list);
  if (id.isEmpty())
    return;
  if (QMessageBox::question(this, "Remove mission entry",
                            "Remove this entry? Its source evidence remains available.",
                            QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes) {
    guardContext(mission, area);
    missions_.removeEntry(collection, id);
  }
}
void DossierPanel::prepareComparison() {
  guardContext(missionID_, areaID_);
  const auto later = selectedEvidence();
  if (!later)
    return;
  QStringList labels;
  QStringList ids;
  for (const auto &e : view_.linked)
    if (e.baseline && e.key != later->key && e.available && e.product) {
      labels.append(e.item["title"].toString() + " · " + e.key.left(8));
      ids.append(e.key);
    }
  if (ids.isEmpty())
    throw std::runtime_error("Pin another locally available raster as a reference first.");
  const auto mission = missionID_, area = areaID_;
  bool ok = true;
  const auto name = ids.size() == 1
                        ? labels.first()
                        : QInputDialog::getItem(this, "Visual comparison", "Reference acquisition",
                                                labels, 0, false, &ok);
  if (ok) {
    guardContext(mission, area);
    emit comparisonRequested(area, ids[labels.indexOf(name)], later->key);
  }
}
void DossierPanel::addRecord(const QString &collection, const QString &observationID) {
  const auto mission = missionID_, area = areaID_;
  guardContext(mission, area);
  QDialog dialog(this);
  dialog.setObjectName("dossierEntryEditor");
  dialog.setWindowTitle(collection == "observations" ? "Add analyst observation"
                        : collection == "notes"      ? "Add dossier note"
                                                     : "Add follow-up task");
  dialog.resize(520, 420);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *title = new QLineEdit;
  title->setObjectName("dossierEntryTitle");
  title->setMaxLength(240);
  form->addRow("Title", title);
  QPlainTextEdit *body = nullptr;
  QLineEdit *reference = nullptr, *category = nullptr;
  QDoubleSpinBox *lat = nullptr, *lon = nullptr;
  QComboBox *evidence = nullptr;
  if (collection == "notes" || collection == "observations") {
    evidence = new QComboBox;
    evidence->addItem("None", QString{});
    for (const auto &entry : view_.linked)
      evidence->addItem(entry.item["title"].toString(), entry.key);
    evidence->setObjectName("dossierEntryEvidence");
    form->addRow("Linked evidence", evidence);
  }
  if (collection == "notes") {
    body = new QPlainTextEdit;
    body->setObjectName("dossierEntryBody");
    form->addRow("Note", body);
    reference = new QLineEdit;
    reference->setMaxLength(2000);
    form->addRow("Citation / reference", reference);
  }
  if (collection == "observations") {
    category = new QLineEdit("Infrastructure");
    category->setMaxLength(120);
    form->addRow("Category", category);
    const auto bounds = Bounds::read(view_.area["region"]);
    const auto point = picked_.value_or(
        bounds ? Coordinate{(bounds->south + bounds->north) / 2, (bounds->west + bounds->east) / 2}
               : Coordinate{});
    auto spin = [&](const QString &label, double value, double low, double high) {
      auto *s = new QDoubleSpinBox;
      s->setRange(low, high);
      s->setDecimals(6);
      s->setValue(value);
      form->addRow(label, s);
      return s;
    };
    lat = spin("Latitude", point.latitude, -90, 90);
    lon = spin("Longitude", point.longitude, -180, 180);
    auto *hint = design::label(
        picked_ ? "Position starts at the last map selection. Verify it before saving."
                : "Position starts at the area centre. Set the observed position before saving.",
        "secondary");
    hint->setWordWrap(true);
    layout->addWidget(hint);
  }
  if (!observationID.isEmpty())
    layout->addWidget(design::label(
        "Follow-up for: " + record(view_.observations, observationID)["title"].toString(),
        "secondary"));
  auto *error = design::label({}, "secondary");
  error->setWordWrap(true);
  layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    try {
      guardContext(mission, area);
      if (collection == "notes")
        missions_.addNote(title->text(), body->toPlainText(), reference->text(),
                          evidence->currentData().toString(), area);
      else if (collection == "observations")
        missions_.addObservation(title->text(), category->text(), {lat->value(), lon->value()},
                                 evidence->currentData().toString(), area);
      else
        missions_.addTask(title->text(), area, observationID);
      dialog.accept();
    } catch (const std::exception &e) {
      error->setText(QString::fromUtf8(e.what()));
    }
  });
  dialog.exec();
}
void DossierPanel::reviewObservation() {
  const auto mission = missionID_, area = areaID_, id = selection(observations_);
  guardContext(mission, area);
  const auto observation = record(view_.observations, id);
  if (observation.isEmpty())
    throw std::runtime_error("Select an observation first.");
  QDialog dialog(this);
  dialog.setObjectName("dossierReviewEditor");
  dialog.setWindowTitle("Review observation");
  dialog.resize(520, 380);
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(design::label(observation["title"].toString(), "title"));
  auto *state = new QComboBox;
  state->setObjectName("dossierReviewState");
  state->addItems({"Unreviewed", "Corroborated", "Uncertain", "Dismissed"});
  state->setCurrentText(observation["state"].toString());
  layout->addWidget(state);
  auto *note = new QPlainTextEdit(observation["note"].toString());
  note->setObjectName("dossierReviewRationale");
  note->setPlaceholderText("Review rationale and unresolved uncertainty");
  layout->addWidget(note);
  auto *error = design::label({}, "secondary");
  error->setWordWrap(true);
  layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    try {
      guardContext(mission, area);
      missions_.setObservationReview(id, state->currentText(), note->toPlainText());
      dialog.accept();
    } catch (const std::exception &e) {
      error->setText(QString::fromUtf8(e.what()));
    }
  });
  dialog.exec();
}
} // namespace helios
