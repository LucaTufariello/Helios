#include "MissionWindow.h"
#include "DossierPanel.h"
#include "MissionEditor.h"
#include "SiteMonitoringPanel.h"
#include "WorkspaceStyle.h"
#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace helios {
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
  setWindowTitle("Helios — Mission Control");
  resize(1240, 860);
  setMinimumSize(1000, 700);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowFlag(Qt::Window, true);
  auto *documents = new QMenu(this);
  selector_ = new QComboBox;
  selector_->setMinimumWidth(260);
  selector_->setAccessibleName("Selected mission");
  auto action = [&](const QString &label, const std::function<void()> &fn) {
    auto *a = documents->addAction(label);
    connect(a, &QAction::triggered, this, fn);
    return a;
  };
  auto *newMission = action("New mission…", [this] {
    if (!resolveDrafts())
      return;
    bool ok = false;
    const auto name =
        QInputDialog::getText(this, "New mission", "Mission name", QLineEdit::Normal, {}, &ok);
    if (ok)
      run([&] { missions_.create(name); });
  });
  action("Open library…", [this] { openLibrary(); })->setShortcut(QKeySequence::Open);
  auto *saveMission = action("Save changes", [this] { save(); });
  saveMission->setShortcut(QKeySequence::Save);
  saveMission->setObjectName("saveMission");
  action("Save as…", [this] { save(true); })->setShortcut(QKeySequence::SaveAs);
  action("Delete selected mission…", [this] {
    if (!resolveDrafts() || missions_.selectedID().isEmpty())
      return;
    if (QMessageBox::question(
            this, "Delete mission",
            "Remove this mission and its entries from the library? Source acquisitions remain "
            "available. The next save retains a last-good library backup.",
            QMessageBox::Cancel | QMessageBox::Yes) == QMessageBox::Yes)
      run([this] { missions_.removeMission(missions_.selectedID()); });
  });
  auto *showMap = action("Show map", [this] {
    map_.window()->show();
    map_.window()->raise();
    map_.window()->activateWindow();
  });
  auto *root = new QWidget;
  auto *layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto *header = new QWidget;
  header->setProperty("role", "header");
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(24, 18, 24, 18);
  headerLayout->setSpacing(16);
  auto *glyph = new QLabel;
  glyph->setPixmap(design::icon(design::Symbol::Mission).pixmap(32, 32));
  headerLayout->addWidget(glyph);
  auto *titles = new QVBoxLayout;
  titles->setSpacing(4);
  titles->addWidget(design::label("MISSION CONTROL", "kicker"));
  missionTitle_ = design::label("Mission workspace", "title");
  missionTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  titles->addWidget(missionTitle_);
  summary_ = design::label({}, "secondary");
  summary_->setWordWrap(true);
  titles->addWidget(summary_);
  headerLayout->addLayout(titles, 1);
  auto actionButton = [&](QAction *action, design::Symbol icon, const char *role) {
    auto *b = new QToolButton;
    b->setDefaultAction(action);
    b->setIcon(design::icon(icon));
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setProperty("role", role);
    headerLayout->addWidget(b);
    return b;
  };
  actionButton(showMap, design::Symbol::Globe, "nav");
  actionButton(saveMission, design::Symbol::Save, "primary");
  auto *documentMenu = new QToolButton;
  documentMenu->setIcon(design::icon(design::Symbol::More));
  documentMenu->setMenu(documents);
  documentMenu->setPopupMode(QToolButton::InstantPopup);
  documentMenu->setProperty("role", "quiet");
  documentMenu->setToolTip("Mission library actions");
  documentMenu->setAccessibleName("Mission library actions");
  headerLayout->addWidget(documentMenu);
  layout->addWidget(header);
  auto *body = new QHBoxLayout;
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(0);
  layout->addLayout(body, 1);
  auto *sidebar = new QWidget;
  sidebar->setProperty("role", "sidebar");
  sidebar->setFixedWidth(220);
  auto *side = new QVBoxLayout(sidebar);
  side->setContentsMargins(16, 20, 16, 16);
  side->setSpacing(12);
  auto *sideHeading = new QHBoxLayout;
  sideHeading->addWidget(design::label("MISSIONS", "kicker"));
  sideHeading->addStretch();
  auto *create = new QToolButton;
  create->setDefaultAction(newMission);
  create->setText("+");
  create->setToolButtonStyle(Qt::ToolButtonIconOnly);
  create->setIcon(design::icon(design::Symbol::Plus));
  create->setProperty("role", "quiet");
  create->setToolTip("Create mission");
  create->setAccessibleName("Create mission");
  sideHeading->addWidget(create);
  side->addLayout(sideHeading);
  selector_->setMinimumWidth(0);
  side->addWidget(selector_);
  navigation_ = new QListWidget;
  navigation_->setObjectName("missionNavigation");
  navigation_->setProperty("role", "navigation");
  navigation_->setAccessibleName("Mission sections");
  side->addWidget(navigation_, 1);
  auto *local = design::label("LOCAL WORKSPACE", "kicker");
  side->addWidget(local);
  auto *note = design::label(
      "Mission entries and evidence share the map. Automations run while Helios is open.",
      "secondary");
  note->setWordWrap(true);
  side->addWidget(note);
  body->addWidget(sidebar);
  auto *content = new QWidget;
  auto *column = new QVBoxLayout(content);
  column->setContentsMargins(24, 20, 24, 20);
  column->setSpacing(12);
  pageTitle_ = design::label("Overview", "title");
  column->addWidget(pageTitle_);
  pageDescription_ =
      design::label("Define the mission and review its evidence at a glance.", "secondary");
  pageDescription_->setWordWrap(true);
  column->addWidget(pageDescription_);
  tabs_ = new QTabWidget;
  tabs_->setObjectName("missionTabs");
  tabs_->tabBar()->hide();
  tabs_->setStyleSheet("QTabWidget#missionTabs::pane { border: none; background: transparent; }");
  auto *tabs = tabs_;
  column->addWidget(tabs, 1);
  missionContent_ = new QStackedWidget;
  auto *empty = new QWidget;
  auto *emptyLayout = new QVBoxLayout(empty);
  emptyLayout->setContentsMargins(60, 40, 60, 40);
  emptyLayout->setSpacing(16);
  emptyLayout->addStretch();
  auto *emptyTitle = design::label("Create your first mission", "title");
  emptyTitle->setAlignment(Qt::AlignCenter);
  emptyLayout->addWidget(emptyTitle);
  auto *emptyDescription = design::label("Organize areas of interest, connect dated evidence, and "
                                         "record what you know and what needs review.",
                                         "secondary");
  emptyDescription->setAlignment(Qt::AlignCenter);
  emptyDescription->setWordWrap(true);
  emptyLayout->addWidget(emptyDescription);
  auto *emptyActions = new QHBoxLayout;
  emptyActions->addStretch();
  auto *createMission = new QPushButton("Create mission");
  createMission->setProperty("role", "primary");
  connect(createMission, &QPushButton::clicked, newMission, &QAction::trigger);
  auto *openMission = new QPushButton("Open library…");
  connect(openMission, &QPushButton::clicked, this, &MissionWindow::openLibrary);
  emptyActions->addWidget(createMission);
  emptyActions->addWidget(openMission);
  emptyActions->addStretch();
  emptyLayout->addLayout(emptyActions);
  emptyLayout->addStretch();
  missionContent_->addWidget(empty);
  missionContent_->addWidget(content);
  body->addWidget(missionContent_, 1);
  connect(navigation_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
    if (item)
      tabs_->setCurrentIndex(item->data(Qt::UserRole).toInt());
  });
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
    for (int i = 0; i < navigation_->count(); ++i)
      if (navigation_->item(i)->data(Qt::UserRole).toInt() == index) {
        QSignalBlocker block(navigation_);
        navigation_->setCurrentRow(i);
        pageTitle_->setText(navigation_->item(i)->data(Qt::UserRole + 1).toString());
        pageDescription_->setText(navigation_->item(i)->toolTip());
      }
  });
  setCentralWidget(root);
  auto page = [&](const QString &name) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 4, 8, 4);
    l->setSpacing(12);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(w);
    tabs->addTab(scroll, name);
    return l;
  };
  auto *overview = page("Brief");
  auto *metrics = new QHBoxLayout;
  metrics->setSpacing(12);
  overview->addLayout(metrics);
  for (const auto &name : QStringList{"Areas", "Evidence", "Objects", "Tasks"}) {
    auto *card = new QPushButton;
    card->setProperty("role", "summaryCard");
    card->setMinimumHeight(106);
    card->setAccessibleName("Open " + name.toLower());
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    auto *count = design::label("0", "title");
    count->setAttribute(Qt::WA_TransparentForMouseEvents);
    metrics_.append(count);
    cardLayout->addWidget(count);
    auto *title = design::label(name, "secondary");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardLayout->addWidget(title);
    metrics->addWidget(card, 1);
    connect(card, &QPushButton::clicked, this, [this, name] {
      for (int i = 0; i < navigation_->count(); ++i)
        if (navigation_->item(i)->data(Qt::UserRole + 1) == name)
          navigation_->setCurrentRow(i);
    });
  }
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
  auto *areaSplit = new QSplitter;
  areaSplit->setObjectName("dossierSplit");
  areaPage->addWidget(areaSplit, 1);
  auto *master = new QWidget;
  auto *areaColumn = new QVBoxLayout(master);
  areaColumn->setContentsMargins(0, 0, 8, 0);
  master->setMinimumWidth(220);
  master->setMaximumWidth(290);
  areaSearch_ = new QLineEdit;
  areaSearch_->setPlaceholderText("Find area…");
  areaSearch_->setClearButtonEnabled(true);
  areaSearch_->setObjectName("dossierAreaSearch");
  areaColumn->addWidget(areaSearch_);
  areas_ = new QListWidget;
  areas_->setObjectName("dossierAreaList");
  areas_->setWordWrap(true);
  areas_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  areaColumn->addWidget(areas_, 1);
  auto filterAreas = [this] {
    for (int i = 0; i < areas_->count(); ++i)
      areas_->item(i)->setHidden(!(areas_->item(i)->text() + areas_->item(i)->toolTip())
                                      .contains(areaSearch_->text(), Qt::CaseInsensitive));
  };
  connect(areaSearch_, &QLineEdit::textChanged, this, filterAreas);
  button("Import saved site…", areaColumn, [this] { emit importSiteRequested(); });
  button("Edit boundary / purpose…", areaColumn, [this] {
    run([this] { editMissionEntry(this, missions_, "areas", selectedID(areas_)); });
  });
  button("Remove area…", areaColumn, [this] { remove(areas_, "areas"); });
  auto *newArea = new QWidget;
  auto *newAreaColumn = new QVBoxLayout(newArea);
  newAreaColumn->setContentsMargins(0, 0, 0, 0);
  areaName_ = new QLineEdit;
  areaName_->setPlaceholderText("Area name");
  areaPurpose_ = new QPlainTextEdit;
  areaPurpose_->setPlaceholderText("Monitoring purpose");
  areaPurpose_->setMaximumHeight(90);
  newAreaColumn->addWidget(areaName_);
  newAreaColumn->addWidget(areaPurpose_);
  regionLabel_ = new QLabel("Draw a region on the map.");
  regionLabel_->setWordWrap(true);
  newAreaColumn->addWidget(regionLabel_);
  button("Draw area on map", newAreaColumn, [this] {
    captureMission_ = missions_.selectedID();
    if (captureMission_.isEmpty())
      return;
    map_.beginRegion(this);
    map_.window()->show();
    map_.window()->raise();
  });
  button("Create dossier", newAreaColumn, [this] {
    run([&] {
      if (!pendingRegion_)
        throw std::runtime_error("Draw an area first.");
      const auto id =
          missions_.addArea(areaName_->text(), areaPurpose_->toPlainText(), *pendingRegion_);
      areaName_->clear();
      areaPurpose_->clear();
      pendingRegion_.reset();
      regionLabel_->setText("Draw a region on the map.");
      openDossier(id);
    });
  });
  design::disclosure(areaColumn, newArea, "New area");
  areaSplit->addWidget(master);
  dossier_ = new DossierPanel(missions_);
  areaSplit->addWidget(dossier_);
  areaSplit->setStretchFactor(1, 1);
  connect(areas_, &QListWidget::currentItemChanged, this,
          [this] { dossier_->setArea(selectedID(areas_)); });
  auto *evidencePage = page("Evidence");
  evidence_ = new QListWidget;
  evidencePage->addWidget(evidence_, 1);
  raw_ = new QPlainTextEdit;
  raw_->setReadOnly(true);
  raw_->setMaximumHeight(180);
  design::disclosure(evidencePage, raw_, "Evidence record");
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
  button("Edit evidence title…", evidencePage, [this] {
    run([this] { editMissionEntry(this, missions_, "items", selectedID(evidence_)); });
  });
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
  button("Edit selected note…", notePage, [this] {
    run([this] { editMissionEntry(this, missions_, "notes", selectedID(notes_)); });
  });
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
  button("Edit task / due date…", taskPage, [this] {
    run([this] { editMissionEntry(this, missions_, "tasks", selectedID(tasks_)); });
  });
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
  button("Edit observation details…", objectPage, [this] {
    if (resolveReview())
      run([this] { editMissionEntry(this, missions_, "observations", selectedID(objects_)); });
  });
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
  connect(&map_, &MapWidget::regionSelected, this, [this](Bounds b, QObject *owner) {
    if (owner != this || captureMission_.isEmpty() || captureMission_ != missions_.selectedID())
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
  updateNavigation();
  refresh();
}
void MissionWindow::addPage(QWidget *page, const QString &title) {
  tabs_->addTab(page, title);
  updateNavigation();
}
void MissionWindow::showPage(QWidget *page) {
  tabs_->setCurrentWidget(page);
}
void MissionWindow::openDossier(const QString &areaID) {
  for (int i = 0; i < tabs_->count(); ++i)
    if (tabs_->tabText(i) == "Areas")
      tabs_->setCurrentIndex(i);
  areaSearch_->clear();
  for (int i = 0; i < areas_->count(); ++i)
    if (areas_->item(i)->data(Qt::UserRole).toString() == areaID) {
      areas_->setCurrentRow(i);
      dossier_->setArea(areaID);
      return;
    }
}
void MissionWindow::updateNavigation() {
  const int current = tabs_->currentIndex();
  QSignalBlocker block(navigation_);
  navigation_->clear();
  struct Section {
    const char *tab;
    const char *title;
    const char *description;
    design::Symbol icon;
  };
  const Section sections[] = {
      {"Brief", "Overview", "Define the mission and review its evidence at a glance.",
       design::Symbol::Mission},
      {"Investigation", "Investigation", "Define completion, retain case versions and export a briefing.", design::Symbol::Mission},
      {"Assets", "Asset registry", "Share stable infrastructure identities across missions.", design::Symbol::Data},
      {"Connections", "Connections", "Explore bounded infrastructure relationships and evidence.", design::Symbol::Globe},
      {"Findings", "Findings", "Review claims, alternative explanations and dated citations.", design::Symbol::Inspect},
      {"Areas", "Areas",
       "Connect site evidence, reference acquisitions, reviews and follow-up work.",
       design::Symbol::Globe},
      {"Evidence", "Evidence", "Connect source material and retain its acquisition and provenance.",
       design::Symbol::Layers},
      {"Review inbox", "Review inbox",
       "Review acquisition events, analysis quality and follow-up work.", design::Symbol::Inspect},
      {"Timeline", "Timeline", "Review dated evidence, acquisition gaps and visual comparisons.",
       design::Symbol::Satellite},
      {"Observations", "Objects",
       "Review observations and distinguish proposals from accepted findings.",
       design::Symbol::Inspect},
      {"Notes", "Notes", "Record analyst context and connect it to mission evidence.",
       design::Symbol::Data},
      {"Tasks", "Tasks", "Track follow-up work and completion within this mission.",
       design::Symbol::Mission},
      {"Monitors", "Automations",
       "Check acquisition and evidence availability while Helios is open.", design::Symbol::Tools},
      {"Activity", "Activity", "Inspect the history retained in this local mission library.",
       design::Symbol::Data}};
  for (const auto &section : sections)
    for (int i = 0; i < tabs_->count(); ++i)
      if (tabs_->tabText(i) == QLatin1String(section.tab)) {
        auto *item = new QListWidgetItem(design::icon(section.icon),
                                         QString::fromLatin1(section.title), navigation_);
        item->setData(Qt::UserRole, i);
        item->setData(Qt::UserRole + 1, QString::fromLatin1(section.title));
        item->setToolTip(QString::fromLatin1(section.description));
        if (i == current)
          navigation_->setCurrentItem(item);
      }
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
  missionTitle_->setText(p.isEmpty() ? "Mission workspace" : p["name"].toString());
  missionTitle_->setToolTip(p["name"].toString());
  missionContent_->setCurrentIndex(p.isEmpty() ? 0 : 1);
  navigation_->setEnabled(!p.isEmpty());
  findChild<QAction *>("saveMission")->setEnabled(!p.isEmpty());
  const QList<int> counts{int(b["areas"].toArray().size()), int(p["items"].toArray().size()),
                          int(b["observations"].toArray().size()),
                          int(b["tasks"].toArray().size())};
  for (int i = 0; i < metrics_.size(); ++i)
    metrics_[i]->setText(QString::number(counts[i]));
  summary_->setText(
      p.isEmpty()
          ? "Create or open a mission to connect areas, evidence, observations and follow-up work."
          : QString("%1 areas · %2 evidence links · %3 observations · %4 tasks%5")
                .arg(b["areas"].toArray().size())
                .arg(p["items"].toArray().size())
                .arg(b["observations"].toArray().size())
                .arg(b["tasks"].toArray().size())
                .arg(missions_.dirty() ? " · Unsaved changes" : ""));
  populate(areas_, b["areas"].toArray(), [](auto o) {
    const auto purpose = o["purpose"].toString().simplified();
    return o["name"].toString() + "\n" + purpose.left(100) + (purpose.size() > 100 ? "…" : "");
  });
  for (int i = 0; i < areas_->count(); ++i)
    areas_->item(i)->setToolTip(
        entry(missions_, "areas", areas_->item(i)->data(Qt::UserRole).toString())["purpose"]
            .toString());
  for (int i = 0; i < areas_->count(); ++i)
    areas_->item(i)->setHidden(!(areas_->item(i)->text() + areas_->item(i)->toolTip())
                                    .contains(areaSearch_->text(), Qt::CaseInsensitive));
  if (!areas_->currentItem() && areas_->count())
    areas_->setCurrentRow(0);
  dossier_->setArea(selectedID(areas_));
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
  if (auto *monitoring = findChild<SiteMonitoringPanel *>())
    if (!monitoring->resolveDrafts())
      return false;
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
            "/Helios-missions.json",
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
} // namespace helios
