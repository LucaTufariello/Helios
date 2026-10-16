#include "TimelinePanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimeZone>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace godeye {
TimelinePanel::TimelinePanel(Missions &missions, SatelliteWindow &satellite, MapWidget &map,
                             QWidget *parent)
    : QWidget(parent), missions_(missions), satellite_(satellite), map_(map) {
  auto *layout = new QVBoxLayout(this);
  auto *filters = new QHBoxLayout;
  area_ = new QComboBox;
  kind_ = new QComboBox;
  kind_->addItem("All evidence kinds", QString{});
  for (const auto &kind : {"Optical acquisition", "Radar acquisition", "Source observation",
                           "Object observation", "Review note"})
    kind_->addItem(kind, kind);
  query_ = new QLineEdit;
  query_->setPlaceholderText("Filter title, source or detail");
  filters->addWidget(area_);
  filters->addWidget(kind_);
  filters->addWidget(query_, 1);
  layout->addLayout(filters);
  auto *dates = new QHBoxLayout;
  dates_ = new QCheckBox("UTC date filter");
  start_ = new QDateEdit(QDateTime::currentDateTimeUtc().date().addDays(-30));
  end_ = new QDateEdit(QDateTime::currentDateTimeUtc().date());
  for (auto *field : {start_, end_}) {
    field->setCalendarPopup(true);
    field->setDisplayFormat("yyyy-MM-dd");
  }
  undated_ = new QCheckBox("Include unknown times");
  undated_->setChecked(true);
  dates->addWidget(dates_);
  dates->addWidget(start_);
  dates->addWidget(new QLabel("Through"));
  dates->addWidget(end_);
  dates->addWidget(undated_);
  layout->addLayout(dates);
  auto *assessmentRow = new QHBoxLayout;
  lookback_ = new QSpinBox;
  lookback_->setRange(1, 365);
  lookback_->setValue(30);
  lookback_->setSuffix(" days");
  assessmentRow->addWidget(new QLabel("Area assessment lookback"));
  assessmentRow->addWidget(lookback_);
  assessmentRow->addStretch();
  layout->addLayout(assessmentRow);
  assessment_ = new QLabel;
  assessment_->setWordWrap(true);
  assessment_->setTextFormat(Qt::PlainText);
  layout->addWidget(assessment_);
  list_ = new QListWidget;
  list_->setObjectName("missionTimeline");
  layout->addWidget(list_, 1);
  detail_ = new QPlainTextEdit;
  detail_->setReadOnly(true);
  detail_->setMaximumHeight(110);
  layout->addWidget(detail_);
  auto *buttons = new QHBoxLayout;
  auto add = [this, buttons](const QString &text, std::function<void()> fn) {
    auto *button = new QPushButton(text);
    if (text == "Retain acquisition metadata")
      button->setObjectName("timelineRetain");
    buttons->addWidget(button);
    connect(button, &QPushButton::clicked, this, fn);
  };
  add("Show on map", [this] { showSelected(); });
  add("Refresh", [this] { refresh(); });
  add("Retain acquisition metadata", [this] {
    try {
      QMap<QString, QJsonObject> snapshots;
      for (const auto &e : all_)
        if (e.raster())
          for (const auto &p : satellite_.library())
            if (rasterReference(p) == e.rasterID)
              snapshots[e.id] = p.missionEvidence()["acquisition"].toObject();
      if (snapshots.isEmpty()) {
        status_->setText("No available acquisition metadata to retain.");
        return;
      }
      missions_.captureAcquisitions(snapshots);
      status_->setText("Acquisition snapshots retained. Save the mission library to persist them.");
    } catch (const std::exception &e) {
      status_->setText(QString::fromUtf8(e.what()));
    }
  });
  add("Export CSV…", [this] { exportEntries(true); });
  add("Export JSON…", [this] { exportEntries(false); });
  layout->addLayout(buttons);
  auto *comparison = new QFormLayout;
  baseline_ = new QComboBox;
  later_ = new QComboBox;
  baseline_->setObjectName("timelineBaseline");
  later_->setObjectName("timelineLater");
  blend_ = new QSlider(Qt::Horizontal);
  blend_->setRange(0, 100);
  blend_->setValue(50);
  comparison->addRow("Baseline acquisition", baseline_);
  comparison->addRow("Later acquisition", later_);
  comparison->addRow("Later opacity (%)", blend_);
  layout->addLayout(comparison);
  auto *compareButton = new QPushButton("Show / save visual comparison");
  compareButton->setObjectName("timelineCompare");
  layout->addWidget(compareButton);
  connect(compareButton, &QPushButton::clicked, this, &TimelinePanel::compare);
  status_ = new QLabel;
  status_->setTextFormat(Qt::PlainText);
  status_->setWordWrap(true);
  layout->addWidget(status_);
  connect(&missions_, &Missions::changed, this, &TimelinePanel::refresh);
  connect(&satellite_, &SatelliteWindow::libraryChanged, this, &TimelinePanel::refresh);
  for (auto *combo : {area_, kind_})
    connect(combo, &QComboBox::currentIndexChanged, this, [this] {
      if (!refreshing_)
        refresh();
    });
  connect(query_, &QLineEdit::textChanged, this, [this] { refresh(); });
  connect(dates_, &QCheckBox::toggled, this, [this] { refresh(); });
  connect(undated_, &QCheckBox::toggled, this, [this] { refresh(); });
  for (auto *date : {start_, end_})
    connect(date, &QDateEdit::dateChanged, this, [this] { refresh(); });
  connect(lookback_, &QSpinBox::valueChanged, this, [this] { refresh(); });
  connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
    detail_->setPlainText(row >= 0 && row < visible_.size()
                              ? QString::fromUtf8(QJsonDocument(visible_[row].json()).toJson())
                              : QString{});
  });
  refresh();
}
void TimelinePanel::refresh() {
  if (refreshing_)
    return;
  refreshing_ = true;
  const auto plan = missions_.selected();
  const auto selectedArea = area_->currentData();
  const auto areas = plan["brief"].toObject()["areas"].toArray();
  {
    QSignalBlocker block(area_);
    area_->clear();
    area_->addItem("All mission areas", QString{});
    for (const auto value : areas) {
      const auto a = value.toObject();
      area_->addItem(a["name"].toString(), a["id"].toString());
    }
    area_->setCurrentIndex(std::max(0, area_->findData(selectedArea)));
  }
  TimelineFilter filter;
  filter.kind = kind_->currentData().toString();
  filter.query = query_->text().trimmed();
  filter.includeUndated = undated_->isChecked();
  for (const auto value : areas)
    if (value.toObject()["id"] == area_->currentData().toString())
      filter.area = value.toObject();
  start_->setEnabled(dates_->isChecked());
  end_->setEnabled(dates_->isChecked());
  if (dates_->isChecked()) {
    filter.start = QDateTime(start_->date(), QTime(0, 0), QTimeZone::UTC);
    filter.endExclusive = QDateTime(end_->date().addDays(1), QTime(0, 0), QTimeZone::UTC);
  }
  all_ = timelineEntries(plan, satellite_.library());
  visible_ = sortedTimeline(all_, filter);
  const auto selected =
      list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString{};
  {
    QSignalBlocker block(list_);
    list_->clear();
    for (const auto &e : visible_) {
      auto *item =
          new QListWidgetItem((e.time.isValid() ? e.time.toUTC().toString("yyyy-MM-dd HH:mm 'UTC'")
                                                : "Unknown observation time") +
                                  " · " + e.kind + " · " + e.title +
                                  (e.available ? QString{} : " · unavailable locally"),
                              list_);
      item->setData(Qt::UserRole, e.id);
      if (e.id == selected)
        list_->setCurrentItem(item);
    }
  }
  for (auto *combo : {baseline_, later_}) {
    const auto previous = combo->currentData();
    QSignalBlocker block(combo);
    combo->clear();
    combo->addItem("Choose acquisition", QString{});
    for (const auto &e : all_)
      if (e.raster())
        combo->addItem(
            e.title + " · " +
                (e.time.isValid() ? e.time.toString("yyyy-MM-dd HH:mm 'UTC'") : "time unknown"),
            e.id);
    combo->setCurrentIndex(std::max(0, combo->findData(previous)));
    if (combo->currentIndex() == 0) {
      const auto saved =
          plan["comparison"].toObject()[combo == baseline_ ? "baseID" : "topID"].toString();
      for (const auto &e : all_)
        if (e.rasterID == saved && !saved.isEmpty())
          combo->setCurrentIndex(combo->findData(e.id));
    }
  }
  if (!filter.area.isEmpty()) {
    const auto now = QDateTime::currentDateTimeUtc();
    const auto assessment =
        assessTimeline(all_, filter.area, now.addDays(-lookback_->value()), now);
    QStringList gaps;
    for (const auto value : assessment["gaps"].toArray())
      gaps.append(value.toString());
    assessment_->setText(
        QString("%1 optical · %2 radar distinct recent acquisitions. Bounds intersection is a "
                "discovery aid; valid-pixel coverage is unknown.\n%3")
            .arg(assessment["opticalCount"].toInt())
            .arg(assessment["radarCount"].toInt())
            .arg(gaps.join('\n')));
  } else
    assessment_->setText("Select an area to assess acquisition gaps. Acquisition/observation time "
                         "and note authoring time remain distinct; unknown times stay unknown.");
  refreshing_ = false;
}
void TimelinePanel::showSelected() {
  const auto row = list_->currentRow();
  if (row < 0 || row >= visible_.size())
    return;
  const auto &e = visible_[row];
  if (e.kind == "Object observation" && e.coordinate) {
    auto camera = map_.camera();
    camera.center = *e.coordinate;
    camera.range = 8000;
    map_.setCamera(camera);
    map_.window()->show();
    map_.window()->raise();
    return;
  }
  for (const auto value : missions_.selected()["items"].toArray())
    if (value.toObject()["id"] == e.evidenceID) {
      emit evidenceRequested(value.toObject());
      return;
    }
  if (e.bounds)
    map_.focus(*e.bounds);
  else if (e.coordinate) {
    auto camera = map_.camera();
    camera.center = *e.coordinate;
    camera.range = 8000;
    map_.setCamera(camera);
  }
}
void TimelinePanel::compare() {
  if (busy_)
    return;
  try {
    refresh();
    TimelineEntry a, b;
    for (const auto &e : all_) {
      if (e.id == baseline_->currentData().toString())
        a = e;
      if (e.id == later_->currentData().toString())
        b = e;
    }
    const auto issue = timelineComparisonIssue(a, b);
    if (!issue.isEmpty()) {
      status_->setText(issue);
      return;
    }
    QVector<ProductLayer> layers;
    for (const auto &id : {a.rasterID, b.rasterID})
      for (const auto &p : satellite_.library())
        if (rasterReference(p) == id) {
          if (!QFileInfo(p.preview).isFile())
            throw std::runtime_error(
                "A selected preview was removed. Refresh or import the complete product again.");
          layers.append(p);
          break;
        }
    if (layers.size() != 2)
      throw std::runtime_error("A selected acquisition is unavailable.");
    const double blend = blend_->value() / 100.0;
    const auto mission = missions_.selectedID();
    busy_ = true;
    status_->setText("Reading the two bounded previews and checking their saved identities…");
    auto *watcher = new QFutureWatcher<std::pair<QVector<ProductLayer>, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, mission, a, b, blend] {
      const auto value = watcher->result();
      watcher->deleteLater();
      busy_ = false;
      if (mission != missions_.selectedID()) {
        status_->setText(
            "Mission changed before previews finished loading. Choose the comparison again.");
        return;
      }
      if (!value.second.isEmpty()) {
        status_->setText(value.second);
        return;
      }
      try {
        missions_.setComparison(a.rasterID, b.rasterID, blend);
        emit comparisonRequested(value.first, blend);
        status_->setText(timelineComparisonWarnings(a, b).join('\n'));
      } catch (const std::exception &e) {
        status_->setText(QString::fromUtf8(e.what()));
      }
    });
    watcher->setFuture(QtConcurrent::run([layers] {
      try {
        QVector<ProductLayer> loaded;
        for (const auto &descriptor : layers) {
          bool found = false;
          for (const auto &p : readProducts(descriptor.manifest))
            if (p.id == descriptor.id && p.manifestHash == descriptor.manifestHash) {
              loaded.append(p);
              found = true;
              break;
            }
          if (!found)
            throw std::runtime_error(
                "A product changed on disk. Reimport its complete result before comparing.");
        }
        return std::make_pair(loaded, QString{});
      } catch (const std::exception &e) {
        return std::make_pair(QVector<ProductLayer>{}, QString::fromUtf8(e.what()));
      }
    }));
  } catch (const std::exception &e) {
    status_->setText(QString::fromUtf8(e.what()));
  }
}
void TimelinePanel::exportEntries(bool csv) {
  const auto path =
      QFileDialog::getSaveFileName(this, "Export filtered mission timeline",
                                   csv ? "mission-timeline.csv" : "mission-timeline.json");
  if (path.isEmpty())
    return;
  QJsonArray entries;
  for (const auto &e : visible_)
    entries.append(e.json());
  const auto bytes =
      csv ? timelineCSV(visible_)
          : QJsonDocument(QJsonObject{{"version", 1},
                                      {"exportedAt",
                                       QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                      {"missionID", missions_.selectedID()},
                                      {"entries", entries}})
                .toJson();
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (bytes.size() > 16 * 1024 * 1024 || !file.open(QIODevice::WriteOnly) ||
      file.write(bytes) != bytes.size() || !file.commit())
    status_->setText("Timeline export failed; the previous destination was preserved.");
  else
    status_->setText("Filtered timeline exported with source identifiers and explicit time basis.");
}
} // namespace godeye
