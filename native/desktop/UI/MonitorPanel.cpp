#include "MonitorPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
namespace godeye {
namespace {
QJsonObject monitorRecord(const QJsonObject &plan, const QString &id) {
  for (const auto v : plan["brief"].toObject()["monitors"].toArray())
    if (v.toObject()["id"] == id)
      return v.toObject();
  return {};
}
} // namespace
MonitorPanel::MonitorPanel(Missions &missions, MonitorRunner &runner, QWidget *parent)
    : QWidget(parent), missions_(missions), runner_(runner) {
  auto *layout = new QVBoxLayout(this);
  auto *info = new QLabel(
      "Checks run while GodEye is open, with at most two active checks. Missed intervals after "
      "sleep or relaunch produce one current check. Monitors retain identifiers and availability; "
      "they do not download large imagery or run as a system service.");
  info->setWordWrap(true);
  layout->addWidget(info);
  automatic_ = new QCheckBox("Run automatic checks in this session (saves monitor checkpoints)");
  layout->addWidget(automatic_);
  connect(automatic_, &QCheckBox::toggled, this, [this](bool enabled) {
    if (enabled)
      runner_.start();
    else
      runner_.stop();
  });
  list_ = new QListWidget;
  list_->setObjectName("missionMonitors");
  layout->addWidget(list_, 1);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  kind_ = new QComboBox;
  kind_->addItems(
      {"New Sentinel-2 imagery", "New Sentinel-1 acquisitions", "Evidence availability"});
  area_ = new QComboBox;
  interval_ = new QSpinBox;
  interval_->setRange(1, 168);
  interval_->setValue(24);
  interval_->setSuffix(" hours");
  lookback_ = new QSpinBox;
  lookback_->setRange(1, 90);
  lookback_->setValue(14);
  lookback_->setSuffix(" days");
  cloud_ = new QDoubleSpinBox;
  cloud_->setRange(0, 100);
  cloud_->setValue(30);
  cloud_->setSuffix("%");
  enabled_ = new QCheckBox("Enabled while the app is open");
  form->addRow("Check", kind_);
  form->addRow("Mission area", area_);
  form->addRow("Interval", interval_);
  form->addRow("Catalogue lookback", lookback_);
  form->addRow("Sentinel-2 maximum cloud", cloud_);
  form->addRow(enabled_);
  auto *actions = new QHBoxLayout;
  auto button = [this, actions](const QString &label, std::function<void()> fn) {
    auto *b = new QPushButton(label);
    if (label == "Check now")
      b->setObjectName("monitorRun");
    actions->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, fn] { run(fn); });
  };
  button("Add monitor", [this] {
    const auto id = missions_.addMonitor(fields());
    drafts_.remove(missions_.selectedID() + "/");
    refresh();
    for (int i = 0; i < list_->count(); ++i)
      if (list_->item(i)->data(Qt::UserRole) == id)
        list_->setCurrentRow(i);
  });
  button("Update selected", [this] {
    if (selected_.isEmpty())
      throw std::runtime_error("Select a monitor first.");
    missions_.editEntry("monitors", selected_, fields());
    drafts_.remove(missionID_ + "/" + selected_);
    status_->setText("Monitor configuration updated. Save the mission library to persist it.");
  });
  button("Pause selected", [this] {
    if (selected_.isEmpty())
      return;
    missions_.editEntry("monitors", selected_, {{"enabled", false}});
    enabled_->setChecked(false);
  });
  button("Check now", [this] {
    if (selected_.isEmpty() || !runner_.run(missions_.selectedID(), selected_))
      status_->setText(
          "Select a monitor, save the mission library and wait for an available check slot.");
  });
  button("Remove selected", [this] {
    if (selected_.isEmpty())
      return;
    missions_.removeEntry("monitors", selected_);
    selected_.clear();
    refresh();
  });
  layout->addLayout(actions);
  auto *open = new QPushButton("Open current catalogue results for selected monitor");
  layout->addWidget(open);
  connect(open, &QPushButton::clicked, this, [this] {
    const auto plan = missions_.selected(), m = monitorRecord(plan, selected_);
    if (m["kind"] == "Evidence availability")
      return;
    for (const auto value : plan["brief"].toObject()["areas"].toArray())
      if (value.toObject()["id"] == m["areaID"])
        if (const auto region = Bounds::read(value.toObject()["region"]))
          emit catalogueRequested(m, *region);
  });
  detail_ = new QPlainTextEdit;
  detail_->setReadOnly(true);
  detail_->setMaximumHeight(140);
  layout->addWidget(detail_);
  status_ = new QLabel;
  status_->setTextFormat(Qt::PlainText);
  status_->setWordWrap(true);
  layout->addWidget(status_);
  connect(list_, &QListWidget::currentRowChanged, this, [this] { select(); });
  connect(&missions_, &Missions::changed, this, &MonitorPanel::refresh);
  connect(&runner_, &MonitorRunner::changed, this, &MonitorPanel::refresh);
  connect(&runner_, &MonitorRunner::failed, this,
          [this](const QString &error) { status_->setText(error); });
  connect(&runner_, &MonitorRunner::results, this,
          [this](const QString &mission, const QString &monitor, const QStringList &ids) {
            if (mission == missionID_ && monitor == selected_)
              status_->setText(QString("Check completed · %1 returned source identifiers. The "
                                       "checkpoint and summary are saved in the mission.")
                                   .arg(ids.size()));
          });
  refresh();
}
void MonitorPanel::run(const std::function<void()> &action) {
  try {
    action();
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
  }
}
QJsonObject MonitorPanel::fields() const {
  QJsonObject result{{"kind", kind_->currentText()},
                     {"enabled", enabled_->isChecked()},
                     {"intervalHours", interval_->value()},
                     {"lookbackDays", lookback_->value()},
                     {"maximumCloud", cloud_->value()}};
  result["areaID"] = area_->currentData().toString().isEmpty()
                         ? QJsonValue::Null
                         : QJsonValue(area_->currentData().toString());
  return result;
}
void MonitorPanel::setFields(const QJsonObject &f) {
  kind_->setCurrentText(f["kind"].toString("New Sentinel-2 imagery"));
  area_->setCurrentIndex(std::max(0, area_->findData(f["areaID"].toString())));
  enabled_->setChecked(f["enabled"].toBool());
  interval_->setValue(f["intervalHours"].toInt(24));
  lookback_->setValue(f["lookbackDays"].toInt(14));
  cloud_->setValue(f["maximumCloud"].toDouble(30));
}
void MonitorPanel::select() {
  if (!missionID_.isEmpty())
    drafts_[missionID_ + "/" + selected_] = fields();
  selected_ =
      list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString{};
  const auto record = monitorRecord(missions_.selected(), selected_);
  const auto key = missionID_ + "/" + selected_;
  setFields(drafts_.contains(key) ? drafts_[key] : record);
  detail_->setPlainText(QString::fromUtf8(QJsonDocument(record).toJson()));
}
void MonitorPanel::refresh() {
  {
    const QSignalBlocker block(automatic_);
    automatic_->setChecked(runner_.automatic());
  }
  const auto plan = missions_.selected();
  const auto id = missions_.selectedID();
  const bool changed = id != missionID_;
  if (changed && !missionID_.isEmpty())
    drafts_[missionID_ + "/" + selected_] = fields();
  if (changed) {
    missionID_ = id;
    selected_.clear();
  }
  const auto selectedArea = area_->currentData();
  {
    QSignalBlocker block(area_);
    area_->clear();
    area_->addItem("None", QString{});
    for (const auto value : plan["brief"].toObject()["areas"].toArray()) {
      const auto a = value.toObject();
      area_->addItem(a["name"].toString(), a["id"].toString());
    }
    area_->setCurrentIndex(std::max(0, area_->findData(selectedArea)));
  }
  {
    QSignalBlocker block(list_);
    list_->clear();
    for (const auto value : plan["brief"].toObject()["monitors"].toArray()) {
      const auto m = value.toObject();
      auto *item =
          new QListWidgetItem((runner_.running(id, m["id"].toString()) ? "Checking · "
                               : m["enabled"].toBool()                 ? "Enabled · "
                                                                       : "Paused · ") +
                                  m["kind"].toString() + "\n" + m["summary"].toString() +
                                  (m["failure"].isString() ? " · last check failed" : QString{}),
                              list_);
      item->setData(Qt::UserRole, m["id"].toString());
      if (m["id"] == selected_)
        list_->setCurrentItem(item);
    }
  }
  if (changed)
    setFields(drafts_.value(id + "/"));
  detail_->setPlainText(QString::fromUtf8(QJsonDocument(monitorRecord(plan, selected_)).toJson()));
}
} // namespace godeye
