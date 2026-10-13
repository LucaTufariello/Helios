#include "MissionWindow.h"
#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace godeye {
namespace {
QPushButton *button(const QString &text, QVBoxLayout *layout, const std::function<void()> &action) {
  auto *b = new QPushButton(text);
  layout->addWidget(b);
  QObject::connect(b, &QPushButton::clicked, b, action);
  return b;
}
QJsonObject entry(Missions &m, const QString &collection, const QString &id) {
  const auto p = m.selected();
  const auto a =
      collection == "items" ? p["items"].toArray() : p["brief"].toObject()[collection].toArray();
  for (auto v : a)
    if (v.toObject()["id"].toString() == id)
      return v.toObject();
  return {};
}
QString selectedID(QListWidget *list) {
  return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
}
void populate(QListWidget *list, const QJsonArray &array,
              const std::function<QString(QJsonObject)> &label) {
  const auto selected = selectedID(list);
  QSignalBlocker block(list);
  list->clear();
  for (auto v : array) {
    const auto o = v.toObject();
    auto *item = new QListWidgetItem(label(o), list);
    item->setData(Qt::UserRole, o["id"].toString());
    if (o["id"].toString() == selected)
      list->setCurrentItem(item);
  }
}
void choices(QComboBox *combo, const QJsonArray &array, const QString &key) {
  const auto selected = combo->currentData();
  QSignalBlocker block(combo);
  combo->clear();
  combo->addItem("None", QString());
  for (auto v : array) {
    const auto o = v.toObject();
    combo->addItem(o[key].toString(), o["id"].toString());
  }
  combo->setCurrentIndex(std::max(0, combo->findData(selected)));
}
} // namespace
MissionWindow::MissionWindow(Missions &missions, MapWidget &map, QWidget *parent)
    : QMainWindow(parent), missions_(missions), map_(map) {
  setWindowTitle("GodEye — Mission Control");
  resize(1080, 760);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowFlag(Qt::Window, true);
  auto *toolbar = addToolBar("Mission documents");
  toolbar->setMovable(false);
  selector_ = new QComboBox;
  selector_->setMinimumWidth(260);
  toolbar->addWidget(selector_);
  auto action = [&](const QString &label, const std::function<void()> &fn) {
    auto *a = toolbar->addAction(label);
    connect(a, &QAction::triggered, this, fn);
    return a;
  };
  action("New", [this] {
    if (!resolveDrafts())
      return;
    bool ok = false;
    const auto name =
        QInputDialog::getText(this, "New mission", "Mission name", QLineEdit::Normal, {}, &ok);
    if (ok)
      run([&] { missions_.create(name); });
  });
  action("Open…", [this] { openLibrary(); })->setShortcut(QKeySequence::Open);
  action("Save", [this] { save(); })->setShortcut(QKeySequence::Save);
  action("Save as…", [this] { save(true); })->setShortcut(QKeySequence::SaveAs);
  action("Show map", [this] {
    map_.window()->show();
    map_.window()->raise();
    map_.window()->activateWindow();
  });
  auto *root = new QWidget;
  auto *layout = new QVBoxLayout(root);
  summary_ = new QLabel;
  summary_->setWordWrap(true);
  layout->addWidget(summary_);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs, 1);
  setCentralWidget(root);
  auto page = [&](const QString &name) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    tabs->addTab(w, name);
    return l;
  };
  auto *overview = page("Brief");
  auto *form = new QFormLayout;
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
  form->setHorizontalSpacing(16);
  overview->addLayout(form);
  name_ = new QLineEdit;
  name_->setObjectName("missionName");
  name_->setMaxLength(120);
  objective_ = new QPlainTextEdit;
  objective_->setObjectName("missionObjective");
  objective_->setMaximumHeight(140);
  workingNotes_ = new QPlainTextEdit;
  workingNotes_->setObjectName("missionWorkingBrief");
  form->addRow("Name", name_);
  form->addRow("Objective", objective_);
  form->addRow("Working brief", workingNotes_);
  button("Apply brief", overview, [this] { applyBrief(); });
  auto *areaPage = page("Areas");
  areas_ = new QListWidget;
  areaPage->addWidget(areas_, 1);
  areaName_ = new QLineEdit;
  areaName_->setPlaceholderText("Area name");
  areaPurpose_ = new QPlainTextEdit;
  areaPurpose_->setPlaceholderText("Monitoring purpose");
  areaPurpose_->setMaximumHeight(90);
  areaPage->addWidget(areaName_);
  areaPage->addWidget(areaPurpose_);
  regionLabel_ = new QLabel("Draw a region on the map.");
  areaPage->addWidget(regionLabel_);
  button("Draw area on map", areaPage, [this] {
    captureMission_ = missions_.selectedID();
    if (captureMission_.isEmpty())
      return;
    map_.beginRegion();
    map_.window()->raise();
  });
  button("Add area", areaPage, [this] {
    run([&] {
      if (!pendingRegion_)
        throw std::runtime_error("Draw an area first.");
      missions_.addArea(areaName_->text(), areaPurpose_->toPlainText(), *pendingRegion_);
      areaName_->clear();
      areaPurpose_->clear();
      pendingRegion_.reset();
      regionLabel_->setText("Draw a region on the map.");
    });
  });
  button("Show selected area", areaPage, [this] {
    const auto a = entry(missions_, "areas", selectedID(areas_));
    if (auto b = Bounds::read(a["region"])) {
      map_.focus(*b);
      map_.window()->raise();
    }
  });
  button("Remove area…", areaPage, [this] { remove(areas_, "areas"); });
  auto *evidencePage = page("Evidence");
  evidence_ = new QListWidget;
  evidencePage->addWidget(evidence_, 1);
  raw_ = new QPlainTextEdit;
  raw_->setReadOnly(true);
  raw_->setMaximumHeight(180);
  evidencePage->addWidget(raw_);
  button("Show selected evidence on map", evidencePage, [this] {
    const auto e = entry(missions_, "items", selectedID(evidence_));
    if (!e.isEmpty())
      emit evidenceRequested(e);
  });
  button("Save current map view", evidencePage, [this] {
    bool ok = false;
    const auto title =
        QInputDialog::getText(this, "Save map view", "View name", QLineEdit::Normal, {}, &ok);
    if (ok)
      run([&] { missions_.addView(title, map_.camera()); });
  });
  button("Attach displayed satellite imagery", evidencePage,
         [this] { emit attachDisplayedImagery(); });
  button("Remove evidence link…", evidencePage, [this] { remove(evidence_, "items"); });
  auto *notePage = page("Notes");
  auto *noteSplit = new QSplitter;
  notePage->addWidget(noteSplit, 1);
  notes_ = new QListWidget;
  noteDetail_ = new QPlainTextEdit;
  noteDetail_->setReadOnly(true);
  noteSplit->addWidget(notes_);
  noteSplit->addWidget(noteDetail_);
  noteTitle_ = new QLineEdit;
  noteTitle_->setPlaceholderText("Note title");
  noteBody_ = new QPlainTextEdit;
  noteBody_->setPlaceholderText("Observation, rationale and unresolved questions");
  noteBody_->setMaximumHeight(160);
  noteReference_ = new QLineEdit;
  noteReference_->setPlaceholderText("Source citation / reference");
  noteEvidence_ = new QComboBox;
  noteArea_ = new QComboBox;
  notePage->addWidget(noteTitle_);
  notePage->addWidget(noteBody_);
  notePage->addWidget(noteReference_);
  auto *nf = new QFormLayout;
  nf->addRow("Evidence", noteEvidence_);
  nf->addRow("Area", noteArea_);
  notePage->addLayout(nf);
  button("Add note", notePage, [this] {
    run([&] {
      missions_.addNote(noteTitle_->text(), noteBody_->toPlainText(), noteReference_->text(),
                        noteEvidence_->currentData().toString(),
                        noteArea_->currentData().toString());
      noteTitle_->clear();
      noteBody_->clear();
      noteReference_->clear();
    });
  });
  button("Remove note…", notePage, [this] { remove(notes_, "notes"); });
  auto *taskPage = page("Tasks");
  tasks_ = new QListWidget;
  taskPage->addWidget(tasks_, 1);
  taskTitle_ = new QLineEdit;
  taskTitle_->setPlaceholderText("Next action");
  taskArea_ = new QComboBox;
  taskPage->addWidget(taskTitle_);
  taskPage->addWidget(taskArea_);
  button("Add task", taskPage, [this] {
    run([&] {
      missions_.addTask(taskTitle_->text(), taskArea_->currentData().toString());
      taskTitle_->clear();
    });
  });
  button("Remove task…", taskPage, [this] { remove(tasks_, "tasks"); });
  auto *objectPage = page("Observations");
  objects_ = new QListWidget;
  objectPage->addWidget(objects_, 1);
  objectTitle_ = new QLineEdit;
  objectTitle_->setPlaceholderText("Analyst identification");
  objectCategory_ = new QLineEdit;
  objectCategory_->setPlaceholderText("Category");
  coordinateLabel_ = new QLabel("Click the map to choose an observation coordinate.");
  objectEvidence_ = new QComboBox;
  objectPage->addWidget(objectTitle_);
  objectPage->addWidget(objectCategory_);
  objectPage->addWidget(coordinateLabel_);
  objectPage->addWidget(objectEvidence_);
  button("Add analyst observation", objectPage, [this] {
    run([&] {
      if (!pendingCoordinate_)
        throw std::runtime_error("Click a location on the map first.");
      missions_.addObservation(objectTitle_->text(), objectCategory_->text(), *pendingCoordinate_,
                               objectEvidence_->currentData().toString());
      objectTitle_->clear();
      objectCategory_->clear();
    });
  });
  reviewState_ = new QComboBox;
  reviewState_->addItems({"Unreviewed", "Corroborated", "Uncertain", "Dismissed"});
  reviewNote_ = new QPlainTextEdit;
  reviewNote_->setObjectName("observationReviewRationale");
  reviewNote_->setMaximumHeight(90);
  reviewNote_->setPlaceholderText("Review rationale");
  objectPage->addWidget(reviewState_);
  objectPage->addWidget(reviewNote_);
  button("Save review", objectPage, [this] {
    run([&] {
      missions_.setObservationReview(selectedID(objects_), reviewState_->currentText(),
                                     reviewNote_->toPlainText());
      reviewOriginalState_ = reviewState_->currentText();
      reviewOriginalNote_ = reviewNote_->toPlainText();
    });
  });
  button("Remove observation…", objectPage, [this] { remove(objects_, "observations"); });
  auto *historyPage = page("Activity");
  history_ = new QListWidget;
  historyPage->addWidget(history_, 1);
  auto *explanation = new QLabel("Recorded working history. Model scores remain uncalibrated; "
                                 "analyst review does not certify a detection.");
  explanation->setWordWrap(true);
  historyPage->addWidget(explanation);
  connect(selector_, &QComboBox::activated, this, [this](int i) {
    const auto id = selector_->itemData(i).toString();
    if (resolveDrafts())
      missions_.select(id);
    else
      refresh();
  });
  connect(evidence_, &QListWidget::currentItemChanged, this, [this] {
    raw_->setPlainText(
        QString::fromUtf8(QJsonDocument(entry(missions_, "items", selectedID(evidence_)))
                              .toJson(QJsonDocument::Indented)));
  });
  connect(notes_, &QListWidget::currentItemChanged, this, [this] {
    const auto n = entry(missions_, "notes", selectedID(notes_));
    noteDetail_->setPlainText(n["body"].toString() + "\n\n" + n["reference"].toString());
  });
  connect(tasks_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
    if (!refreshing_)
      run([&] {
        missions_.setTaskDone(item->data(Qt::UserRole).toString(),
                              item->checkState() == Qt::Checked);
      });
  });
  connect(objects_, &QListWidget::currentItemChanged, this, [this] {
    const auto requested = selectedID(objects_);
    if (!resolveReview()) {
      QSignalBlocker block(objects_);
      for (int i = 0; i < objects_->count(); ++i)
        if (objects_->item(i)->data(Qt::UserRole).toString() == reviewID_)
          objects_->setCurrentRow(i);
      return;
    }
    const auto o = entry(missions_, "observations", requested);
    reviewID_ = o["id"].toString();
    reviewOriginalState_ = o["state"].toString();
    reviewOriginalNote_ = o["note"].toString();
    reviewState_->setCurrentText(o["state"].toString());
    reviewNote_->setPlainText(o["note"].toString());
    if (auto p = Coordinate::read(o["coordinate"].toObject())) {
      auto c = map_.camera();
      c.center = *p;
      c.range = std::min(c.range, 8000.0);
      map_.setCamera(c);
    }
  });
  connect(&map_, &MapWidget::regionSelected, this, [this](Bounds b) {
    if (captureMission_.isEmpty() || captureMission_ != missions_.selectedID())
      return;
    captureMission_.clear();
    pendingRegion_ = b;
    regionLabel_->setText(QString("%1, %2 → %3, %4")
                              .arg(b.south, 0, 'f', 5)
                              .arg(b.west, 0, 'f', 5)
                              .arg(b.north, 0, 'f', 5)
                              .arg(b.east, 0, 'f', 5));
    show();
    raise();
  });
  connect(&map_, &MapWidget::pointSelected, this, [this](Coordinate p, double, bool) {
    pendingCoordinate_ = p;
    coordinateLabel_->setText(
        QString("Selected: %1, %2").arg(p.latitude, 0, 'f', 6).arg(p.longitude, 0, 'f', 6));
  });
  connect(&missions_, &Missions::changed, this, &MissionWindow::refresh);
  refresh();
}
void MissionWindow::run(const std::function<void()> &action) {
  try {
    action();
  } catch (const std::exception &e) {
    QMessageBox::warning(this, "Mission action", QString::fromUtf8(e.what()));
  }
}
void MissionWindow::refresh() {
  refreshing_ = true;
  const auto p = missions_.selected(), b = p["brief"].toObject();
  const auto id = p["id"].toString();
  if (displayedMission_ != id ||
      (!reviewID_.isEmpty() && entry(missions_, "observations", reviewID_).isEmpty())) {
    reviewID_.clear();
    reviewOriginalState_ = "Unreviewed";
    reviewOriginalNote_.clear();
    reviewState_->setCurrentText("Unreviewed");
    reviewNote_->clear();
  }
  {
    QSignalBlocker block(selector_);
    selector_->clear();
    for (auto v : missions_.plans()) {
      const auto m = v.toObject();
      selector_->addItem(m["name"].toString(), m["id"].toString());
    }
    selector_->setCurrentIndex(selector_->findData(id));
  }
  const bool hasDraft = name_->text() != briefBaseline_["name"].toString() ||
                        objective_->toPlainText() != briefBaseline_["objective"].toString() ||
                        workingNotes_->toPlainText() != briefBaseline_["notes"].toString();
  if (displayedMission_ != id || !hasDraft) {
    name_->setText(p["name"].toString());
    objective_->setPlainText(p["objective"].toString());
    workingNotes_->setPlainText(p["notes"].toString());
  }
  displayedMission_ = id;
  briefBaseline_ = p;
  summary_->setText(
      p.isEmpty()
          ? "Create or open a mission to connect areas, evidence, observations and follow-up work."
          : QString("%1 areas · %2 evidence links · %3 observations · %4 tasks%5")
                .arg(b["areas"].toArray().size())
                .arg(p["items"].toArray().size())
                .arg(b["observations"].toArray().size())
                .arg(b["tasks"].toArray().size())
                .arg(missions_.dirty() ? " · Unsaved changes" : ""));
  populate(areas_, b["areas"].toArray(),
           [](auto o) { return o["name"].toString() + "\n" + o["purpose"].toString(); });
  populate(evidence_, p["items"].toArray(),
           [](auto o) { return o["kind"].toString().toUpper() + " · " + o["title"].toString(); });
  populate(notes_, b["notes"].toArray(),
           [](auto o) { return o["title"].toString() + "\n" + dateLabel(o["createdAt"]); });
  populate(tasks_, b["tasks"].toArray(), [](auto o) {
    return o["title"].toString() +
           (o.contains("due") ? " · Due " + dateLabel(o["due"]) : QString());
  });
  for (int i = 0; i < tasks_->count(); ++i) {
    auto *item = tasks_->item(i);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(
        entry(missions_, "tasks", item->data(Qt::UserRole).toString())["done"].toBool()
            ? Qt::Checked
            : Qt::Unchecked);
  }
  populate(objects_, b["observations"].toArray(), [](auto o) {
    return o["state"].toString() + " · " + o["title"].toString() + "\n" + o["method"].toString();
  });
  populate(history_, b["activity"].toArray(),
           [](auto o) { return dateLabel(o["time"]) + " · " + o["message"].toString(); });
  for (auto *combo : {noteEvidence_, objectEvidence_})
    choices(combo, p["items"].toArray(), "title");
  for (auto *combo : {noteArea_, taskArea_})
    choices(combo, b["areas"].toArray(), "name");
  refreshing_ = false;
}
void MissionWindow::applyBrief() {
  run([&] {
    missions_.setBrief(name_->text(), objective_->toPlainText(), workingNotes_->toPlainText());
  });
}
bool MissionWindow::resolveDrafts() {
  if (!resolveReview())
    return false;
  const auto p = missions_.selected();
  const bool brief = !p.isEmpty() && (name_->text() != p["name"].toString() ||
                                      objective_->toPlainText() != p["objective"].toString() ||
                                      workingNotes_->toPlainText() != p["notes"].toString());
  if (brief) {
    const auto choice =
        QMessageBox::question(this, "Mission brief", "Apply the edited brief before continuing?",
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel)
      return false;
    if (choice == QMessageBox::Save) {
      try {
        missions_.setBrief(name_->text(), objective_->toPlainText(), workingNotes_->toPlainText());
      } catch (const std::exception &e) {
        QMessageBox::warning(this, "Mission brief", e.what());
        return false;
      }
    } else {
      name_->setText(p["name"].toString());
      objective_->setPlainText(p["objective"].toString());
      workingNotes_->setPlainText(p["notes"].toString());
    }
  }
  const bool forms = !noteTitle_->text().isEmpty() || !noteBody_->toPlainText().isEmpty() ||
                     !noteReference_->text().isEmpty() || !taskTitle_->text().isEmpty() ||
                     !areaName_->text().isEmpty() || !areaPurpose_->toPlainText().isEmpty() ||
                     !objectTitle_->text().isEmpty() || !objectCategory_->text().isEmpty() ||
                     pendingRegion_.has_value() || !captureMission_.isEmpty();
  if (forms) {
    if (QMessageBox::question(this, "Unsubmitted entries",
                              "Discard unsubmitted area, note, task or observation entries? "
                              "Submitted records remain in the mission.",
                              QMessageBox::Discard | QMessageBox::Cancel) != QMessageBox::Discard)
      return false;
    for (auto *field :
         {noteTitle_, noteReference_, taskTitle_, areaName_, objectTitle_, objectCategory_})
      field->clear();
    noteBody_->clear();
    areaPurpose_->clear();
    pendingRegion_.reset();
    captureMission_.clear();
    map_.cancelRegion();
  }
  return true;
}
bool MissionWindow::resolveReview() {
  if (reviewID_.isEmpty() || (reviewState_->currentText() == reviewOriginalState_ &&
                              reviewNote_->toPlainText() == reviewOriginalNote_))
    return true;
  const auto choice =
      QMessageBox::question(this, "Observation review", "Save the edited review before continuing?",
                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (choice == QMessageBox::Cancel)
    return false;
  if (choice == QMessageBox::Save) {
    try {
      missions_.setObservationReview(reviewID_, reviewState_->currentText(),
                                     reviewNote_->toPlainText());
      reviewOriginalState_ = reviewState_->currentText();
      reviewOriginalNote_ = reviewNote_->toPlainText();
    } catch (const std::exception &e) {
      QMessageBox::warning(this, "Observation review", e.what());
      return false;
    }
  } else {
    reviewState_->setCurrentText(reviewOriginalState_);
    reviewNote_->setPlainText(reviewOriginalNote_);
  }
  return true;
}
bool MissionWindow::save(bool copy) {
  if (!resolveDrafts())
    return false;
  QString filename = copy ? QString() : missions_.filename();
  if (filename.isEmpty())
    filename = QFileDialog::getSaveFileName(
        this, "Save mission library",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
            "/GodEye-missions.json",
        "Mission library (*.json)");
  if (filename.isEmpty())
    return false;
  try {
    missions_.save(filename);
    return true;
  } catch (const std::exception &e) {
    QMessageBox::warning(this, "Save mission", e.what());
    return false;
  }
}
void MissionWindow::openLibrary() {
  if (!resolveDrafts())
    return;
  if (missions_.dirty()) {
    const auto choice = QMessageBox::question(
        this, "Unsaved library", "Save the current library before opening another?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !save()))
      return;
  }
  const auto path = QFileDialog::getOpenFileName(
      this, "Open mission library",
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
      "Mission library (*.json)");
  if (!path.isEmpty())
    run([&] { missions_.load(path); });
}
void MissionWindow::remove(QListWidget *list, const QString &collection) {
  const auto id = selectedID(list);
  if (id.isEmpty())
    return;
  const auto message =
      collection == "items"   ? "Remove this evidence link? Source files and dependent "
                                "notes/observations are kept; their link is cleared."
      : collection == "areas" ? "Remove this area? Its monitors are removed; notes, tasks and "
                                "observations are kept without the area link."
                              : "Remove the selected mission entry?";
  if (QMessageBox::question(this, "Remove entry", message,
                            QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
    run([&] { missions_.removeEntry(collection, id); });
}
} // namespace godeye
