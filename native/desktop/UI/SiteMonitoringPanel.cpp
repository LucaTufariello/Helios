#include "SiteMonitoringPanel.h"
#include "MonitoringInboxPanel.h"
#include "WorkspaceStyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
namespace godeye {
SiteMonitoringPanel::SiteMonitoringPanel(Missions &missions, MonitoringRunner &runner,
                                         QWidget *parent)
    : QWidget(parent), missions_(missions), runner_(runner) {
  setObjectName("siteMonitoring");
  auto *layout = new QVBoxLayout(this);
  auto *help = design::label("Configure relative registration checks and local scientific "
                             "analysis. Watches start paused. Optical imagery provides visual "
                             "corroboration; a radar change does not establish its physical cause.",
                             "secondary");
  help->setWordWrap(true);
  layout->addWidget(help);
  auto *tabs = new QTabWidget;
  tabs->setObjectName("monitoringTabs");
  layout->addWidget(tabs, 1);
  auto page = [&](QString title) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *s = new QScrollArea;
    s->setWidgetResizable(true);
    s->setWidget(w);
    tabs->addTab(s, title);
    return l;
  };
  auto *setup = page("Setup");
  auto *form = new QFormLayout;
  setup->addLayout(form);
  enabled_ = new QCheckBox("Enable this site’s watch");
  enabled_->setObjectName("monitoringEnabled");
  setup->insertWidget(0, enabled_);
  baseline_ = new QComboBox;
  baseline_->setMinimumContentsLength(12);
  baseline_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  baseline_->setObjectName("monitoringBaseline");
  form->addRow("Pinned radar reference", baseline_);
  pol_ = new QComboBox;
  pol_->addItems({"VV", "VH"});
  form->addRow("Polarization", pol_);
  interval_ = new QSpinBox;
  interval_->setObjectName("monitoringInterval");
  interval_->setRange(1, 168);
  interval_->setSuffix(" hours");
  form->addRow("Catalogue interval", interval_);
  auto spin = [&](QString label, double low, double high, int decimals) {
    auto *s = new QDoubleSpinBox;
    s->setRange(low, high);
    s->setDecimals(decimals);
    form->addRow(label, s);
    return s;
  };
  threshold_ = spin("Candidate change (dB)", 1, 20, 1);
  window_ = new QComboBox;
  window_->addItems({"3", "5", "7"});
  form->addRow("Linear-power mean window", window_);
  minimum_ = new QSpinBox;
  minimum_->setRange(9, 10000);
  form->addRow("Minimum connected pixels", minimum_);
  shift_ = spin("Maximum shift (pixels)", .5, 8, 2);
  residual_ = spin("Verification limit (pixels)", .1, 1, 2);
  correlation_ = spin("Minimum correlation", .5, .99, 2);
  auto *notice = design::label(
      "The 0.5-pixel verification limit is a provisional engineering setting. Absolute geolocation "
      "and radiometry remain unknown. Install requirements-monitoring.txt into the selected raster "
      "Python environment. Downloads and SNAP processing remain manual.",
      "secondary");
  notice->setWordWrap(true);
  setup->addWidget(notice);
  setup->addStretch();
  auto *controlPage = page("Stable controls");
  auto *instructions = design::label(
      "Add at least three Fit and three independent Verify rectangles. Controls must not overlap "
      "and must contain stable textured radar returns. The worker uses a centred 64–256 pixel chip "
      "per rectangle; it rejects nodata and ambiguous matches.",
      "secondary");
  instructions->setWordWrap(true);
  controlPage->addWidget(instructions);
  controls_ = new QTableWidget(0, 3);
  controls_->setObjectName("monitoringControls");
  controls_->setHorizontalHeaderLabels({"Control", "Role", "Bounds"});
  controls_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  controls_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  controls_->setSelectionBehavior(QAbstractItemView::SelectRows);
  controls_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  controls_->setMinimumHeight(180);
  controlPage->addWidget(controls_, 1);
  role_ = new QComboBox;
  role_->addItems({"Fit", "Verify"});
  controlPage->addWidget(role_);
  auto *actions = new QGridLayout;
  controlPage->addLayout(actions);
  auto button = [this](QGridLayout *row, QString title, QString name, std::function<void()> fn) {
    auto *b = new QPushButton(title);
    b->setObjectName(name);
    const auto n = row->count();
    row->addWidget(b, n / 2, n % 2);
    connect(b, &QPushButton::clicked, this, [this, fn] { run(fn); });
    return b;
  };
  button(actions, "Draw control on map", "drawStableControl", [this] {
    captureKey_ = key();
    emit drawControlRequested();
  });
  button(actions, "Add coordinates…", "addStableControl", [this] { editControl(); });
  button(actions, "Edit selected…", "editStableControl", [this] {
    const auto row = controls_->currentRow();
    if (row >= 0)
      editControl({}, row);
  });
  button(actions, "Remove selected", "removeStableControl", [this] {
    if (controls_->currentRow() >= 0)
      controls_->removeRow(controls_->currentRow());
  });
  button(actions, "Show control on map", "focusStableControl", [this] {
    const auto row = controls_->currentRow();
    if (row >= 0)
      if (auto b =
              Bounds::read(controls_->item(row, 0)->data(Qt::UserRole).toJsonObject()["region"]))
        emit focusControlRequested(*b);
  });
  history_ = new MonitoringInboxPanel(missions_, runner_);
  tabs->addTab(history_, "Run history");
  connect(history_, &MonitoringInboxPanel::reviewRequested, this,
          &SiteMonitoringPanel::reviewRequested);
  connect(history_, &MonitoringInboxPanel::dossierRequested, this,
          &SiteMonitoringPanel::dossierRequested);
  auto *bottom = new QGridLayout;
  layout->addLayout(bottom);
  button(bottom, "Save setup", "saveMonitoring", [this] { save(); });
  button(bottom, "Run local analysis", "runLocalMonitoring", [this] {
    if (dirty())
      throw std::runtime_error("Save the monitoring setup before running analysis.");
    runner_.runNow(areaID_);
  });
  button(bottom, "Discard setup edits", "reloadMonitoring", [this] {
    drafts_.remove(key());
    original_ = monitoringArea(missions_.selected(), areaID_)["dossier"]
                    .toObject()["monitoring"]
                    .toObject();
    load(original_.isEmpty() ? monitoringDefaults() : original_);
  });
  button(bottom, "Resume checks", "resumeMonitoring", [this] { emit resumeAutomaticRequested(); });
  status_ = design::label({}, "secondary");
  status_->setObjectName("monitoringStatus");
  status_->setWordWrap(true);
  layout->addWidget(status_);
  connect(&missions_, &Missions::changed, this, &SiteMonitoringPanel::refresh);
  connect(&runner_, &MonitoringRunner::changed, this,
          [this] { status_->setText(runner_.status()); });
  setEnabled(false);
}
bool SiteMonitoringPanel::dirty() const {
  return !areaID_.isEmpty() && value() != (original_.isEmpty() ? monitoringDefaults() : original_);
}
QJsonObject SiteMonitoringPanel::value() const {
  auto p = original_.isEmpty() ? monitoringDefaults() : original_;
  p["enabled"] = enabled_->isChecked();
  if (baseline_->currentData().toString().isEmpty())
    p.remove("baselineEvidenceID");
  else
    p["baselineEvidenceID"] = baseline_->currentData().toString();
  p["polarization"] = pol_->currentText();
  p["intervalHours"] = interval_->value();
  p["thresholdDb"] = threshold_->value();
  p["averagingWindow"] = window_->currentText().toInt();
  p["minimumPixels"] = minimum_->value();
  p["maximumShiftPixels"] = shift_->value();
  p["maximumResidualPixels"] = residual_->value();
  p["minimumCorrelation"] = correlation_->value();
  QJsonArray controls;
  for (int i = 0; i < controls_->rowCount(); ++i)
    controls.append(controls_->item(i, 0)->data(Qt::UserRole).toJsonObject());
  p["controls"] = controls;
  return p;
}
void SiteMonitoringPanel::setArea(const QString &area) {
  if (area == areaID_ && missionID_ == missions_.selectedID()) {
    refresh();
    return;
  }
  if (dirty())
    drafts_[key()] = {value(), original_};
  missionID_ = missions_.selectedID();
  areaID_ = area;
  original_ =
      monitoringArea(missions_.selected(), area)["dossier"].toObject()["monitoring"].toObject();
  if (drafts_.contains(key())) {
    const auto d = drafts_.value(key());
    original_ = d.original;
    load(d.profile);
  } else
    load(original_.isEmpty() ? monitoringDefaults() : original_);
  history_->setArea(area);
  setEnabled(!monitoringArea(missions_.selected(), area).isEmpty());
}
void SiteMonitoringPanel::refresh() {
  if (loading_)
    return;
  if (missions_.selectedID() != missionID_) {
    setArea({});
    return;
  }
  if (!dirty()) {
    original_ = monitoringArea(missions_.selected(), areaID_)["dossier"]
                    .toObject()["monitoring"]
                    .toObject();
    load(original_.isEmpty() ? monitoringDefaults() : original_);
  }
  setEnabled(!monitoringArea(missions_.selected(), areaID_).isEmpty());
}
void SiteMonitoringPanel::load(const QJsonObject &p) {
  loading_ = true;
  enabled_->setChecked(p["enabled"].toBool());
  baseline_->clear();
  baseline_->addItem("Select a pinned radar reference", QString{});
  const auto plan = missions_.selected(), area = monitoringArea(plan, areaID_);
  const auto pins = area["dossier"].toObject()["baselineEvidenceIDs"].toArray();
  for (const auto v : plan["items"].toArray()) {
    const auto e = v.toObject();
    if (e["kind"] == "radar" && pins.contains(e["id"]))
      baseline_->addItem(e["title"].toString(), e["id"].toString());
  }
  if (!p["baselineEvidenceID"].toString().isEmpty() &&
      baseline_->findData(p["baselineEvidenceID"].toString()) < 0)
    baseline_->addItem("Reference unavailable / unpinned", p["baselineEvidenceID"].toString());
  baseline_->setCurrentIndex(std::max(0, baseline_->findData(p["baselineEvidenceID"].toString())));
  pol_->setCurrentText(p["polarization"].toString());
  interval_->setValue(p["intervalHours"].toInt());
  threshold_->setValue(p["thresholdDb"].toDouble());
  window_->setCurrentText(QString::number(p["averagingWindow"].toInt()));
  minimum_->setValue(p["minimumPixels"].toInt());
  shift_->setValue(p["maximumShiftPixels"].toDouble());
  residual_->setValue(p["maximumResidualPixels"].toDouble());
  correlation_->setValue(p["minimumCorrelation"].toDouble());
  controls_->setRowCount(0);
  for (const auto v : p["controls"].toArray()) {
    const auto c = v.toObject();
    const auto row = controls_->rowCount();
    controls_->insertRow(row);
    auto *item = new QTableWidgetItem(c["name"].toString());
    item->setData(Qt::UserRole, c);
    controls_->setItem(row, 0, item);
    controls_->setItem(row, 1, new QTableWidgetItem(c["role"].toString()));
    const auto b = Bounds::read(c["region"]);
    controls_->setItem(
        row, 2,
        new QTableWidgetItem(
            b ? QString("%1,%2 → %3,%4").arg(b->west).arg(b->south).arg(b->east).arg(b->north)
              : "Invalid"));
  }
  loading_ = false;
}
void SiteMonitoringPanel::save() {
  if (missions_.selectedID() != missionID_ ||
      monitoringArea(missions_.selected(), areaID_).isEmpty())
    throw std::runtime_error("The site context changed.");
  const auto current =
      monitoringArea(missions_.selected(), areaID_)["dossier"].toObject()["monitoring"].toObject();
  if (current != original_)
    throw std::runtime_error(
        "The configuration changed while editing. Discard edits to reload it.");
  const auto p = value();
  loading_ = true;
  try {
    missions_.configureMonitoring(missionID_, areaID_, p);
  } catch (...) {
    loading_ = false;
    throw;
  }
  loading_ = false;
  drafts_.remove(key());
  original_ =
      monitoringArea(missions_.selected(), areaID_)["dossier"].toObject()["monitoring"].toObject();
  load(original_);
  status_->setText(
      missions_.filename().isEmpty()
          ? "Monitoring setup updated. Save the mission library before running analysis."
          : "Monitoring setup saved to the mission library.");
}
void SiteMonitoringPanel::acceptControl(Bounds b) {
  if (captureKey_ != key() || missionID_ != missions_.selectedID()) {
    status_->setText("Site changed while drawing. Draw the control again.");
    return;
  }
  run([&] { editControl(b); });
  captureKey_.clear();
}
void SiteMonitoringPanel::editControl(std::optional<Bounds> region, int row) {
  if (row < 0 && controls_->rowCount() >= 12)
    throw std::runtime_error("At most 12 stable controls are supported.");
  const auto context = key();
  auto c = row >= 0 ? controls_->item(row, 0)->data(Qt::UserRole).toJsonObject()
                    : QJsonObject{{"id", newID()},
                                  {"name", QString("Control %1").arg(controls_->rowCount() + 1)},
                                  {"role", role_->currentText()}};
  auto bounds = region.value_or(
      Bounds::read(c["region"])
          .value_or(Bounds::read(monitoringArea(missions_.selected(), areaID_)["region"])
                        .value_or(Bounds{})));
  QDialog dialog(this);
  dialog.setWindowTitle("Stable control rectangle");
  dialog.setObjectName("stableControlEditor");
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *name = new QLineEdit(c["name"].toString());
  name->setMaxLength(120);
  form->addRow("Name", name);
  auto *role = new QComboBox;
  role->addItems({"Fit", "Verify"});
  role->setCurrentText(c["role"].toString());
  form->addRow("Role", role);
  auto spin = [&](QString label, double value, double min, double max) {
    auto *s = new QDoubleSpinBox;
    s->setDecimals(6);
    s->setRange(min, max);
    s->setValue(value);
    form->addRow(label, s);
    return s;
  };
  auto *w = spin("West", bounds.west, -180, 180), *s = spin("South", bounds.south, -85, 85),
       *e = spin("East", bounds.east, -180, 180), *n = spin("North", bounds.north, -85, 85);
  auto *error = design::label({}, "secondary");
  error->setWordWrap(true);
  layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    try {
      if (context != key() || missionID_ != missions_.selectedID())
        throw std::runtime_error("The site context changed.");
      Bounds b{w->value(), s->value(), e->value(), n->value()};
      if (!b.validArea() || name->text().trimmed().isEmpty())
        throw std::runtime_error("Provide a name and valid bounded rectangle.");
      c["name"] = name->text();
      c["role"] = role->currentText();
      c["region"] = b.json();
      auto p = value();
      auto values = p["controls"].toArray();
      if (row >= 0)
        values[row] = c;
      else
        values.append(c);
      p["controls"] = values;
      load(p);
      dialog.accept();
    } catch (const std::exception &ex) {
      error->setText(QString::fromUtf8(ex.what()));
    }
  });
  dialog.exec();
}
bool SiteMonitoringPanel::resolveDrafts() {
  if (dirty())
    drafts_[key()] = {value(), original_};
  if (drafts_.isEmpty())
    return true;
  const auto answer = QMessageBox::question(
      this, "Unsubmitted monitoring setup",
      "Discard unsubmitted monitoring setup edits? Use Save setup within each dossier "
      "to apply them before switching missions or closing.",
      QMessageBox::Discard | QMessageBox::Cancel);
  if (answer != QMessageBox::Discard)
    return false;
  drafts_.clear();
  original_ =
      monitoringArea(missions_.selected(), areaID_)["dossier"].toObject()["monitoring"].toObject();
  load(original_.isEmpty() ? monitoringDefaults() : original_);
  return true;
}
void SiteMonitoringPanel::run(const std::function<void()> &fn) {
  try {
    fn();
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
  }
}
} // namespace godeye
