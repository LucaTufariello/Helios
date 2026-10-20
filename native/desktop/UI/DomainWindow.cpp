#include "DomainWindow.h"
#include "CameraViewer.h"
#include "Core/Credentials.h"
#include "Core/Timeline.h"
#include "WorkspaceStyle.h"
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
namespace godeye {
DomainWindow::DomainWindow(const QString &profile, QWidget *parent, bool networking, bool workspace)
    : QDialog(parent), profile_(profile), network_(this, networking) {
  setWindowTitle("Sources & observations");
  setObjectName("domainWindow");
  resize(1050, 700);
  setWindowFlag(Qt::Window, true);
  auto *layout = new QVBoxLayout(this);
  auto *toolbar = new QHBoxLayout;
  refresh_ = new QPushButton("Refresh");
  refresh_->setObjectName("domainRefresh");
  cancel_ = new QPushButton("Cancel");
  auto *providers = new QPushButton("Providers…");
  toolbar->addWidget(refresh_);
  toolbar->addWidget(cancel_);
  toolbar->addStretch();
  toolbar->addWidget(providers);
  layout->addLayout(toolbar);
  auto *split = new QSplitter;
  layout->addWidget(split, 1);
  auto *sources = new QWidget;
  auto *sourceLayout = new QVBoxLayout(sources);
  sourceLayout->setContentsMargins(0, 0, 0, 0);
  sourceQuery_ = new QLineEdit;
  sourceQuery_->setPlaceholderText("Find a source");
  sourceQuery_->setObjectName("domainSourceQuery");
  sourceQuery_->setClearButtonEnabled(true);
  sourceLayout->addWidget(sourceQuery_);
  feeds_ = new QListWidget;
  feeds_->setObjectName("domainFeeds");
  sourceLayout->addWidget(feeds_, 1);
  for (const auto &feed : domainFeeds()) {
    auto *item = new QListWidgetItem(feed.name, feeds_);
    item->setData(Qt::UserRole, feed.id);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    item->setToolTip(feed.credit);
  }
  auto *form = new QFormLayout;
  sourceLayout->addLayout(form);
  group_ = new QComboBox;
  for (const auto &group : QStringList{"stations", "active", "starlink", "weather", "gps-ops"})
    group_->addItem(group == "gps-ops" ? "GPS" : group, group);
  form->addRow("Satellite group", group_);
  transit_ = new QComboBox;
  for (const auto &provider : transitProviders())
    transit_->addItem(provider.name, provider.id);
  form->addRow("Transit operator", transit_);
  camera_ = new QComboBox;
  camera_->addItem("TfL London", "london");
  camera_->addItem("Fintraffic Finland", "finland");
  form->addRow("Camera provider", camera_);
  sentinel_ = new QComboBox;
  sentinel_->addItems({"SENTINEL-2", "SENTINEL-1"});
  form->addRow("Catalog collection", sentinel_);
  auto *providerRegion = new QPushButton("View operator / camera region");
  sourceLayout->addWidget(providerRegion);
  connect(providerRegion, &QPushButton::clicked, this, [this] {
    const auto id =
        feeds_->currentItem() ? feeds_->currentItem()->data(Qt::UserRole).toString() : QString{};
    if (id == "cameras")
      emit focusRequested(camera_->currentData() == "finland" ? Coordinate{60.1699, 24.9384}
                                                              : Coordinate{51.5074, -.1278},
                          80000);
    else if (id == "transit")
      for (const auto &p : transitProviders())
        if (p.id == transit_->currentData())
          emit focusRequested(p.center, std::max(80000.0, p.radius * 2000));
  });
  auto *sourceHelp = design::label("Enable a source to show its observations on the map. Source "
                                   "times and availability appear in Records.",
                                   "secondary");
  sourceHelp->setWordWrap(true);
  sourceLayout->addWidget(sourceHelp);
  auto updateProviderFields = [this, form, providerRegion] {
    const auto id =
        feeds_->currentItem() ? feeds_->currentItem()->data(Qt::UserRole).toString() : QString{};
    form->setRowVisible(group_, id == "satellites");
    form->setRowVisible(transit_, id == "transit");
    form->setRowVisible(camera_, id == "cameras");
    form->setRowVisible(sentinel_, id == "sentinel");
    providerRegion->setVisible(id == "cameras" || id == "transit");
  };
  connect(feeds_, &QListWidget::currentItemChanged, this, updateProviderFields);
  split->addWidget(sources);
  auto *content = new QWidget;
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  status_ = new QLabel("Select and enable a source.");
  status_->setWordWrap(true);
  status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  contentLayout->addWidget(status_);
  credit_ = new QLabel;
  credit_->setWordWrap(true);
  credit_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  contentLayout->addWidget(credit_);
  auto *vertical = new QSplitter(Qt::Vertical);
  contentLayout->addWidget(vertical, 1);
  records_ = new QListWidget;
  records_->setObjectName("domainRecords");
  records_->setAccessibleName("Source observations; activate a record to inspect it");
  connect(records_, &QListWidget::itemActivated, this, [this] { showWorkspacePage(2); });
  vertical->addWidget(records_);
  auto *inspectionPanel = new QWidget;
  auto *inspectLayout = new QVBoxLayout(inspectionPanel);
  inspectLayout->setContentsMargins(0, 0, 0, 0);
  recordTitle_ = new QLabel("No record selected");
  recordTitle_->setWordWrap(true);
  recordTitle_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  inspectLayout->addWidget(recordTitle_);
  auto *actions = new QGridLayout;
  inspectLayout->addLayout(actions);
  focus_ = new QPushButton("Show on map");
  orbit_ = new QPushButton("Estimated orbit");
  evidence_ = new QPushButton("Add evidence…");
  auto *exportRecord = new QPushButton("Export record…");
  int actionIndex = 0;
  for (auto *button : {focus_, orbit_, evidence_, exportRecord}) {
    actions->addWidget(button, actionIndex / 2, actionIndex % 2);
    ++actionIndex;
  }
  auto *tracking = new QGridLayout;
  inspectLayout->addLayout(tracking);
  follow_ = new QPushButton("Follow selection");
  trails_ = new QCheckBox("Retained trail");
  selectedOnly_ = new QCheckBox("Selected only");
  playbackSeconds_ = new QSpinBox;
  playbackSeconds_->setRange(0, 3600);
  playbackSeconds_->setSuffix(" s ago");
  playbackSeconds_->setSpecialValueText("Live positions");
  playbackSeconds_->setToolTip("Retained observed fixes from this app session, up to one hour; no "
                               "interpolation. Satellites remain current propagated estimates.");
  tracking->addWidget(follow_, 0, 0);
  tracking->addWidget(trails_, 0, 1);
  tracking->addWidget(selectedOnly_, 1, 0);
  tracking->addWidget(playbackSeconds_, 1, 1);
  connect(sourceQuery_, &QLineEdit::textChanged, this, [this] { setSection(section_); });
  connect(trails_, &QCheckBox::toggled, this, [this] { inspect(); });
  connect(selectedOnly_, &QCheckBox::toggled, this, [this] { update(); });
  connect(playbackSeconds_, &QSpinBox::valueChanged, this, [this](int seconds) {
    playback_ = seconds ? QDateTime::currentDateTimeUtc().addSecs(-seconds) : QDateTime{};
    if (seconds)
      stopFollowing();
    update();
  });
  connect(follow_, &QPushButton::clicked, this, [this] {
    if (auto r = selected()) {
      if (followedID_ == r->id) {
        stopFollowing();
        return;
      }
      playbackSeconds_->setValue(0);
      emit focusRequested(r->coordinate,
                          r->altitude ? std::max(250000.0, *r->altitude * 3) : 25000);
      followedID_ = r->id;
      followingPending_ = false;
      inspect();
    }
  });
  inspection_ = new QTabWidget;
  inspectLayout->addWidget(inspection_, 1);
  details_ = new QTextEdit;
  details_->setReadOnly(true);
  raw_ = new QTextEdit;
  raw_->setReadOnly(true);
  inspection_->addTab(details_, "Details");
  inspection_->addTab(raw_, "Raw record");
  auto *media = new QWidget;
  auto *mediaLayout = new QVBoxLayout(media);
  auto *mediaButtons = new QHBoxLayout;
  frame_ = new QPushButton("Load actual still");
  frame_->setObjectName("domainCameraStill");
  video_ = new QPushButton("View operator clip…");
  mediaButtons->addWidget(frame_);
  mediaButtons->addWidget(video_);
  mediaButtons->addStretch();
  mediaLayout->addLayout(mediaButtons);
  image_ =
      new QLabel("Camera stills are retrieved only on request. Acquisition time may be unknown.");
  image_->setAlignment(Qt::AlignCenter);
  image_->setObjectName("domainCameraImage");
  image_->setWordWrap(true);
  image_->setMinimumSize(240, 100);
  mediaLayout->addWidget(image_, 1);
  mediaStatus_ = new QLabel;
  mediaStatus_->setWordWrap(true);
  mediaLayout->addWidget(mediaStatus_);
  inspection_->addTab(media, "Camera media");
  vertical->addWidget(inspectionPanel);
  vertical->setSizes({220, 350});
  split->addWidget(content);
  split->setSizes({300, 750});
  if (workspace) {
    layout->removeWidget(split);
    workspaceTabs_ = new QTabWidget(this);
    workspaceTabs_->setObjectName("sourceWorkspaceTabs");
    workspaceTabs_->addTab(sources, "Sources");
    auto *recordPage = new QWidget;
    auto *recordLayout = new QVBoxLayout(recordPage);
    recordLayout->addWidget(status_);
    recordLayout->addWidget(credit_);
    recordLayout->addWidget(records_, 1);
    workspaceTabs_->addTab(recordPage, "Records");
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(inspectionPanel);
    workspaceTabs_->addTab(scroll, "Inspector");
    layout->addWidget(workspaceTabs_, 1);
    feeds_->setMinimumHeight(160);
    recordTitle_->setProperty("role", "kicker");
    credit_->setProperty("role", "secondary");
    delete split;
  }
  QSettings settings(profile_ + "/sources.ini", QSettings::IniFormat);
  QStringList restoreEnabled;
  for (const auto &feed : domainFeeds())
    if (settings.value("enabled/" + feed.id, false).toBool())
      restoreEnabled.append(feed.id);
  DomainConfiguration config;
  config.satelliteGroup = settings.value("satelliteGroup", "stations").toString();
  config.transitID = settings.value("transitID", "mbta").toString();
  config.cameraProvider = settings.value("cameraProvider", "london").toString();
  config.sentinelKind = settings.value("sentinelKind", "SENTINEL-2").toString();
  for (auto pair : {std::pair(group_, config.satelliteGroup), std::pair(transit_, config.transitID),
                    std::pair(camera_, config.cameraProvider)}) {
    const int index = pair.first->findData(pair.second);
    if (index >= 0)
      pair.first->setCurrentIndex(index);
  }
  sentinel_->setCurrentText(config.sentinelKind);
  config.satelliteGroup = group_->currentData().toString();
  config.transitID = transit_->currentData().toString();
  config.cameraProvider = camera_->currentData().toString();
  config.sentinelKind = sentinel_->currentText();
  network_.setConfiguration(config);
  connect(&network_, &DomainNetwork::changed, this, &DomainWindow::update);
  connect(feeds_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
    if (updating_)
      return;
    network_.setEnabled(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
    saveConfiguration();
  });
  connect(feeds_, &QListWidget::currentItemChanged, this, [this] { update(); });
  connect(records_, &QListWidget::itemClicked, this, [this] {
    emit liveSelectionRequested();
    inspect();
  });
  connect(records_, &QListWidget::currentItemChanged, this, [this] {
    emit liveSelectionRequested();
    const auto previous = selectedID_;
    selectedID_ = records_->currentItem() ? records_->currentItem()->data(Qt::UserRole).toString()
                                          : QString{};
    if (previous != selectedID_)
      estimatedOrbit_ = false;
    inspect();
  });
  connect(refresh_, &QPushButton::clicked, this, [this] { network_.refresh(); });
  connect(cancel_, &QPushButton::clicked, this, [this] { network_.cancel(); });
  connect(providers, &QPushButton::clicked, this, &DomainWindow::providerSettings);
  for (auto *combo : {group_, transit_, camera_, sentinel_})
    connect(combo, &QComboBox::currentIndexChanged, this, [this] {
      DomainConfiguration c;
      c.satelliteGroup = group_->currentData().toString();
      c.transitID = transit_->currentData().toString();
      c.cameraProvider = camera_->currentData().toString();
      c.sentinelKind = sentinel_->currentText();
      network_.setConfiguration(c);
      saveConfiguration();
    });
  connect(focus_, &QPushButton::clicked, this, [this] {
    if (auto r = selected())
      emit focusRequested(r->coordinate,
                          r->altitude ? std::max(250000.0, *r->altitude * 3) : 25000);
  });
  connect(orbit_, &QPushButton::clicked, this, [this] {
    emit liveSelectionRequested();
    if (auto r = selected()) {
      QVector<Coordinate> points;
      QVector<double> heights;
      if (network_.orbit(r->id, points, heights,
                         DomainParsers::utcDate(r->details["Propagation UTC"]))) {
        estimatedOrbit_ = true;
        emit geometrySelected({points}, heights);
      } else
        QMessageBox::information(this, "Orbit unavailable",
                                 "Refresh the satellite catalog before requesting an orbit.");
    }
  });
  connect(evidence_, &QPushButton::clicked, this, [this] {
    if (auto r = selected()) {
      QString error;
      auto admitted = r->missionJSON(&error);
      if (!admitted) {
        QMessageBox::information(this, "Evidence limit", error);
        return;
      }
      auto snapshot = *admitted;
      const auto s = network_.snapshot(r->feed);
      snapshot["retrievedAt"] = s.retrievedAt.isValid()
                                    ? QJsonValue(s.retrievedAt.toUTC().toString(Qt::ISODateWithMs))
                                    : QJsonValue();
      snapshot["snapshotNotice"] = "Stored evidence snapshot; distinct from a later live "
                                   "observation or propagated estimate.";
      emit evidenceRequested(snapshot);
    }
  });
  connect(exportRecord, &QPushButton::clicked, this, [this] {
    if (auto r = selected()) {
      auto file = QFileDialog::getSaveFileName(this, "Export source record", {}, "JSON (*.json)");
      if (file.isEmpty())
        return;
      QSaveFile output(file);
      const auto bytes = QJsonDocument(r->json()).toJson(QJsonDocument::Indented);
      if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
          !output.commit())
        QMessageBox::warning(this, "Export failed", output.errorString());
    }
  });
  connect(frame_, &QPushButton::clicked, this, &DomainWindow::loadFrame);
  connect(video_, &QPushButton::clicked, this, [this] {
    if (auto r = selected()) {
      auto url = DomainParsers::cameraURL(r->details["Video URL"].toString());
      if (!url.isEmpty())
        (new CameraViewer(*r, this))->show();
    }
  });
  for (int i = 0; i < feeds_->count(); ++i) {
    const auto id = feeds_->item(i)->data(Qt::UserRole).toString();
    if (restoreEnabled.contains(id))
      feeds_->item(i)->setCheckState(Qt::Checked);
  }
  feeds_->setCurrentRow(0);
  update();
}
DomainWindow::~DomainWindow() {
  if (imageCancel_)
    imageCancel_->store(true);
  if (imageWatcher_) {
    disconnect(imageWatcher_, nullptr, this, nullptr);
    imageWatcher_->waitForFinished();
  }
}
void DomainWindow::saveConfiguration() {
  QString state =
      network_.configuration().satelliteGroup + "|" + network_.configuration().transitID + "|" +
      network_.configuration().cameraProvider + "|" + network_.configuration().sentinelKind;
  for (const auto &feed : domainFeeds())
    if (network_.enabled(feed.id))
      state += "|" + feed.id;
  if (state == persistedState_)
    return;
  persistedState_ = state;
  QSettings settings(profile_ + "/sources.ini", QSettings::IniFormat);
  const auto &c = network_.configuration();
  settings.setValue("satelliteGroup", c.satelliteGroup);
  settings.setValue("transitID", c.transitID);
  settings.setValue("cameraProvider", c.cameraProvider);
  settings.setValue("sentinelKind", c.sentinelKind);
  for (const auto &feed : domainFeeds())
    settings.setValue("enabled/" + feed.id, network_.enabled(feed.id));
}
void DomainWindow::setConfiguration(DomainConfiguration configuration) {
  const QSignalBlocker groupBlock(group_), transitBlock(transit_), cameraBlock(camera_),
      sentinelBlock(sentinel_);
  group_->setCurrentIndex(group_->findData(configuration.satelliteGroup));
  transit_->setCurrentIndex(transit_->findData(configuration.transitID));
  camera_->setCurrentIndex(camera_->findData(configuration.cameraProvider));
  sentinel_->setCurrentIndex(sentinel_->findText(configuration.sentinelKind));
  network_.setConfiguration(configuration);
  saveConfiguration();
}
void DomainWindow::showWorkspacePage(int page, bool raw) {
  if (workspaceTabs_)
    workspaceTabs_->setCurrentIndex(std::clamp(page, 0, 2));
  inspection_->setCurrentIndex(raw ? 1 : 0);
}
void DomainWindow::setSection(const QString &section) {
  section_ = section;
  for (int i = 0; i < feeds_->count(); ++i) {
    auto *item = feeds_->item(i);
    const auto *feed = domainFeed(item->data(Qt::UserRole).toString());
    item->setHidden((section != "All" && section != "Map" && feed->section != section) ||
                    !feed->name.contains(sourceQuery_->text().trimmed(), Qt::CaseInsensitive));
  }
  if (!feeds_->currentItem() || feeds_->currentItem()->isHidden())
    for (int i = 0; i < feeds_->count(); ++i)
      if (!feeds_->item(i)->isHidden()) {
        feeds_->setCurrentRow(i);
        break;
      }
}
void DomainWindow::selectFeed(const QString &id) {
  for (int i = 0; i < feeds_->count(); ++i)
    if (feeds_->item(i)->data(Qt::UserRole).toString() == id) {
      sourceQuery_->clear();
      setSection("All");
      feeds_->setCurrentRow(i);
      return;
    }
}
void DomainWindow::selectRecord(const QString &id) {
  for (const auto &r : network_.records())
    if (r.id == id) {
      emit liveSelectionRequested();
      if (selectedID_ != id)
        estimatedOrbit_ = false;
      selectedID_ = id;
      selectFeed(r.feed);
      update();
      return;
    }
}
std::optional<DomainRecord> DomainWindow::selected() const {
  for (const auto &r : currentRecords_)
    if (r.id == selectedID_)
      return r;
  return {};
}
void DomainWindow::update(const QString &changedFeed) {
  saveConfiguration();
  const auto live = network_.records();
  history_.ingest(live);
  if (!followedID_.isEmpty()) {
    bool found = false;
    for (const auto &r : live)
      if (r.id == followedID_) {
        found = true;
        followingPending_ = false;
        if (changedFeed == r.feed && network_.snapshot(r.feed).error.isEmpty())
          emit followPositionChanged(r.coordinate);
        break;
      }
    if (!found && !followingPending_)
      stopFollowing();
  }
  updating_ = true;
  for (int i = 0; i < feeds_->count(); ++i) {
    auto *item = feeds_->item(i);
    const auto id = item->data(Qt::UserRole).toString();
    const auto *feed = domainFeed(id);
    const auto snapshot = network_.snapshot(id);
    item->setText(feed->name + (network_.busy(id)           ? " · loading"
                                : !snapshot.error.isEmpty() ? " · unavailable"
                                                            : ""));
    item->setCheckState(network_.enabled(id) ? Qt::Checked : Qt::Unchecked);
  }
  updating_ = false;
  auto displayed = displayRecords(live);
  if (selectedOnly_->isChecked()) {
    QVector<DomainRecord> only;
    for (const auto &r : displayed)
      if (r.id == selectedID_)
        only.append(r);
    emit feedsChanged(only);
  } else
    emit feedsChanged(displayed);
  const auto id =
      feeds_->currentItem() ? feeds_->currentItem()->data(Qt::UserRole).toString() : QString{};
  const auto *feed = domainFeed(id);
  if (!feed)
    return;
  const auto snapshot = network_.snapshot(id);
  credit_->setText(feed->credit);
  QString status =
      network_.enabled(id)
          ? QString("%1 records · %2 rejected").arg(snapshot.records.size()).arg(snapshot.rejected)
          : "Source is disabled. Enable it to retrieve actual data.";
  if (network_.busy(id))
    status += " · Loading…";
  if (!snapshot.error.isEmpty())
    status += "\n" + snapshot.error +
              (snapshot.records.isEmpty() ? "" : " Retained records may be stale.");
  if (snapshot.retrievedAt.isValid()) {
    status += "\nRetrieved UTC: " + snapshot.retrievedAt.toUTC().toString(Qt::ISODateWithMs);
    if (snapshot.retrievedAt.secsTo(QDateTime::currentDateTimeUtc()) > feed->intervalSeconds * 2)
      status += " · stale";
  } else
    status += "\nRetrieval time unavailable or static reference data.";
  for (const auto &notice : snapshot.notices)
    status += "\n" + notice;
  if (playback_.isValid())
    status += "\nRetained observed fixes at or before " +
              playback_.toUTC().toString(Qt::ISODateWithMs) +
              "; no interpolation. Other records remain source metadata / current orbit estimates.";
  if (playback_.isValid())
    status += " Tracks are retained only in this app session: up to one hour, 5,000 objects and "
              "240 fixes per object.";
  status_->setText(status);
  cancel_->setEnabled(network_.busy());
  {
    const QSignalBlocker block(records_);
    records_->clear();
    currentRecords_ = displayRecords(snapshot.records);
    std::sort(currentRecords_.begin(), currentRecords_.end(),
              [](const DomainRecord &a, const DomainRecord &b) {
                return a.label.localeAwareCompare(b.label) < 0;
              });
    for (const auto &r : currentRecords_) {
      auto *item = new QListWidgetItem(r.label, records_);
      item->setData(Qt::UserRole, r.id);
      item->setToolTip(
          r.id + "\nObserved UTC: " +
          (r.observedAt.isValid() ? r.observedAt.toUTC().toString(Qt::ISODateWithMs) : "Unknown"));
      if (r.id == selectedID_)
        records_->setCurrentItem(item);
    }
  }
  inspect();
}
void DomainWindow::inspect() {
  auto r = selected();
  follow_->setEnabled(r && r->id.size() <= 200);
  follow_->setText(r && followedID_ == r->id ? "Stop following" : "Follow selection");
  if (!r || mediaRecordID_ != r->id) {
    mediaRecordID_ = r ? r->id : QString{};
    ++imageGeneration_;
    if (imageCancel_)
      imageCancel_->store(true);
    image_->setPixmap({});
    image_->setText(
        "No frame loaded for this record. Retrieve an actual operator still to inspect it.");
    mediaStatus_->clear();
  }
  focus_->setEnabled(bool(r));
  orbit_->setEnabled(r && r->feed == "satellites");
  evidence_->setEnabled(bool(r));
  frame_->setEnabled(network_.networking() && r && r->feed == "cameras" && !imageWatcher_);
  video_->setEnabled(network_.networking() && r &&
                     !DomainParsers::cameraURL(r->details["Video URL"].toString()).isEmpty());
  if (!r) {
    recordTitle_->setText("No record selected");
    details_->clear();
    raw_->clear();
    emit geometrySelected({}, {});
    return;
  }
  recordTitle_->setText(r->label + "\n" + r->id + " · " + r->source);
  QString details =
      QString("Latitude: %1°\nLongitude: %2°\nAltitude: %3\nObserved / source epoch UTC: %4\n\n")
          .arg(r->coordinate.latitude, 0, 'f', 6)
          .arg(r->coordinate.longitude, 0, 'f', 6)
          .arg(r->altitude ? QString::number(*r->altitude, 'f', 1) + " m" : "Unknown")
          .arg(r->observedAt.isValid() ? r->observedAt.toUTC().toString(Qt::ISODateWithMs)
                                       : "Unknown");
  for (auto i = r->details.begin(); i != r->details.end(); ++i)
    details +=
        i.key() + ": " +
        (i.value().isString() ? i.value().toString()
                              : QString::fromUtf8(QJsonDocument(QJsonObject{{"value", i.value()}})
                                                      .toJson(QJsonDocument::Compact))) +
        "\n";
  details_->setPlainText(details);
  auto raw = QString::fromUtf8(
      QJsonDocument(r->raw.isEmpty() ? r->json() : r->raw).toJson(QJsonDocument::Indented));
  if (raw.size() > 65536)
    raw = raw.left(65536) + "\n[Inspector text truncated at 65,536 characters; Export record "
                            "retains admitted source fields.]";
  raw_->setPlainText(raw);
  auto paths = r->paths;
  if (paths.isEmpty() && !r->footprint.isEmpty())
    paths.append(r->footprint);
  QVector<double> heights;
  if (estimatedOrbit_ && r->feed == "satellites") {
    QVector<Coordinate> points;
    if (network_.orbit(r->id, points, heights,
                       DomainParsers::utcDate(r->details["Propagation UTC"])))
      paths = {points};
  } else if (trails_->isChecked()) {
    const auto fixes = history_.fixes(followedID_.isEmpty() ? r->id : followedID_);
    QVector<Coordinate> trail;
    bool allAltitude = true;
    for (const auto &fix : fixes)
      if (!playback_.isValid() || fix.time <= playback_) {
        trail.append(fix.coordinate);
        allAltitude &= fix.altitude.has_value();
        heights.append(fix.altitude.value_or(0));
      }
    if (trail.size() > 1) {
      paths = {trail};
      if (!allAltitude)
        heights.clear();
    } else
      heights.clear();
  }
  emit geometrySelected(paths, heights);
  emit recordSelected(*r);
}
void DomainWindow::stopFollowing() {
  followedID_.clear();
  followingPending_ = false;
  if (follow_)
    follow_->setText("Follow selection");
}
void DomainWindow::restoreFollowing(const QString &id) {
  stopFollowing();
  if (id.isEmpty() || id.size() > 200 || id.contains(QChar(0)))
    return;
  followedID_ = id;
  followingPending_ = true;
  playbackSeconds_->setValue(0);
  update();
}
void DomainWindow::showSavedRecord(const QJsonObject &record) {
  if (savedInspector_)
    delete savedInspector_.data();
  auto *dialog = new QDialog(this);
  savedInspector_ = dialog;
  dialog->setWindowFlag(Qt::Window, true);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setAttribute(Qt::WA_QuitOnClose, false);
  dialog->setObjectName("savedRecordInspector");
  dialog->setWindowTitle("Saved mission snapshot");
  dialog->resize(720, 600);
  auto *layout = new QVBoxLayout(dialog);
  auto *title = new QLabel(record["label"].toString().left(1024));
  title->setObjectName("snapshotTitle");
  title->setTextFormat(Qt::PlainText);
  title->setWordWrap(true);
  title->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(title);
  auto *summary =
      new QLabel("Saved mission snapshot · frozen source metadata and geometry. "
                 "Source observations and orbital estimates retain their original meaning.");
  summary->setWordWrap(true);
  layout->addWidget(summary);
  auto *source = new QLabel("Source: " + record["source"].toString().left(1024) +
                            "\nSource ID: " + record["id"].toString().left(512));
  source->setObjectName("snapshotSource");
  source->setTextFormat(Qt::PlainText);
  source->setWordWrap(true);
  source->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(source);
  const auto observed = missionTime(record["observedAt"]);
  auto *time = new QLabel(
      "Original observation / source epoch UTC: " +
      (observed.isValid() ? observed.toUTC().toString(Qt::ISODateWithMs) : QString("Unknown")));
  time->setObjectName("snapshotObservedAt");
  time->setTextFormat(Qt::PlainText);
  time->setWordWrap(true);
  time->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(time);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs, 1);
  auto *details = new QTextEdit;
  details->setObjectName("snapshotDetails");
  details->setReadOnly(true);
  QString text;
  if (auto point = Coordinate::read(record["coordinate"].toObject()))
    text = QString("Latitude: %1°\nLongitude: %2°\n")
               .arg(point->latitude, 0, 'f', 6)
               .arg(point->longitude, 0, 'f', 6);
  text += "Altitude: " +
          (record["altitude"].isDouble()
               ? QString::number(record["altitude"].toDouble(), 'g', 17) + " m"
               : QString("Unknown")) +
          "\n\n";
  const auto values = record["details"].toObject();
  for (auto i = values.begin(); i != values.end(); ++i)
    text += i.key() + ": " + i.value().toString() + "\n";
  if (text.size() > 65536)
    text = text.left(65536) +
           "\n[Inspector text truncated at 65,536 characters; saved evidence remains unchanged.]";
  details->setPlainText(text);
  tabs->addTab(details, "Saved details");
  auto *json = new QTextEdit;
  json->setObjectName("snapshotJSON");
  json->setReadOnly(true);
  QString raw = QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Indented));
  if (raw.size() > 65536)
    raw = raw.left(65536) +
          "\n[Inspector JSON truncated at 65,536 characters; saved evidence remains unchanged.]";
  json->setPlainText(raw);
  tabs->addTab(json, "Saved record JSON");
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  layout->addWidget(buttons);
  dialog->show();
  dialog->raise();
}
QVector<DomainRecord> DomainWindow::displayRecords(const QVector<DomainRecord> &records) const {
  if (!playback_.isValid())
    return records;
  QVector<DomainRecord> out;
  for (const auto &r : records) {
    if (QStringList{"flights", "military", "vessels", "transit"}.contains(r.feed)) {
      if (auto prior = history_.delayed(r, playback_))
        out.append(*prior);
    } else
      out.append(r);
  }
  return out;
}
void DomainWindow::providerSettings() {
  auto *dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Provider credentials");
  auto *layout = new QVBoxLayout(dialog);
  auto *explanation =
      new QLabel("Optional keys are stored in the operating system credential store. Keys are not "
                 "written to settings, mission documents or logs. Leave a field unchanged to keep "
                 "its stored key; use Remove to delete it.");
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *state = new QLabel;
  state->setWordWrap(true);
  layout->addWidget(state);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  for (auto pair : {std::pair(QString("firms"), QString("NASA FIRMS MAP_KEY")),
                    std::pair(QString("tomtom"), QString("TomTom API key")),
                    std::pair(QString("ais"), QString("AISStream API key"))}) {
    auto *row = new QWidget;
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    auto *key = new QLineEdit;
    key->setEchoMode(QLineEdit::Password);
    key->setMaxLength(256);
    key->setPlaceholderText("Stored key remains hidden");
    auto *save = new QPushButton("Save");
    auto *remove = new QPushButton("Remove");
    line->addWidget(key, 1);
    line->addWidget(save);
    line->addWidget(remove);
    form->addRow(pair.second, row);
    const auto perform = [this, dialog, state, key, save, remove,
                          account = pair.first](bool erase) {
      if (credentialBusy_)
        return;
      const auto value = key->text().trimmed();
      if (!erase && value.isEmpty()) {
        state->setText("Enter a key to save it.");
        return;
      }
      if (!erase && ((account == "firms" &&
                      !QRegularExpression("^[A-Za-z0-9-]{8,128}$").match(value).hasMatch()) ||
                     (account == "tomtom" &&
                      !QRegularExpression("^[A-Za-z0-9_-]{16,128}$").match(value).hasMatch()))) {
        state->setText(
            "The key does not match this provider's supported format. It has not been saved.");
        return;
      }
      credentialBusy_ = true;
      save->setEnabled(false);
      remove->setEnabled(false);
      state->setText("Updating the operating system credential store…");
      key->clear();
      auto *watcher = new QFutureWatcher<CredentialResult>(this);
      QPointer<QDialog> guard(dialog);
      QPointer<QLabel> status(state);
      QPointer<QPushButton> saveGuard(save), removeGuard(remove);
      connect(watcher, &QFutureWatcher<CredentialResult>::finished, this,
              [this, watcher, guard, status, saveGuard, removeGuard, account] {
                const auto result = watcher->result();
                watcher->deleteLater();
                credentialBusy_ = false;
                if (guard && status) {
                  status->setText(result.ok ? "Credential updated in " + result.backend + "."
                                            : result.error);
                  saveGuard->setEnabled(true);
                  removeGuard->setEnabled(true);
                }
                if (result.ok) {
                  const auto feed = account == "ais"      ? "vessels"
                                    : account == "tomtom" ? "traffic"
                                                          : "fires";
                  network_.cancel(feed);
                  network_.refresh(feed);
                }
              });
      watcher->setFuture(QtConcurrent::run([account, value, erase] {
        return erase ? Credentials::erase(account) : Credentials::save(account, value);
      }));
    };
    connect(save, &QPushButton::clicked, dialog, [perform] { perform(false); });
    connect(remove, &QPushButton::clicked, dialog, [perform] { perform(true); });
  }
  dialog->resize(650, 300);
  dialog->show();
}
void DomainWindow::loadFrame() {
  if (!network_.networking())
    return;
  auto r = selected();
  if (!r || imageWatcher_)
    return;
  const auto url = DomainParsers::cameraURL(r->details["Frame URL"].toString());
  if (url.isEmpty() || !url.path().endsWith(".jpg"))
    return;
  const auto generation = ++imageGeneration_;
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  imageCancel_ = cancel;
  const auto id = r->id;
  image_->setPixmap({});
  image_->setText("Retrieving an actual provider still… Acquisition time may be unknown.");
  frame_->setEnabled(false);
  auto *watcher = new QFutureWatcher<QImage>(this);
  imageWatcher_ = watcher;
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation, id] {
    auto image = watcher->result();
    watcher->deleteLater();
    imageWatcher_ = nullptr;
    imageCancel_.reset();
    if (generation != imageGeneration_ || selectedID_ != id) {
      inspect();
      return;
    }
    if (image.isNull())
      image_->setText("Still unavailable or outside the bounded image contract. No replacement "
                      "frame was generated.");
    else {
      image_->setPixmap(QPixmap::fromImage(image).scaled(image_->size(), Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation));
      mediaStatus_->setText(
          "Still retrieved UTC: " + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) +
          " · acquisition time unknown; media may be delayed.");
    }
    frame_->setEnabled(true);
  });
  watcher->setFuture(QtConcurrent::run([url, cancel] {
    auto data = domainFetch(url, 8 * 1024 * 1024, cancel, {}, {}, false);
    if (!data.error.isEmpty())
      return QImage{};
    QBuffer buffer(&data.bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 8192 || size.height() > 8192 ||
        qint64(size.width()) * size.height() > 16777216)
      return QImage{};
    if (size.width() > 1600 || size.height() > 1200)
      reader.setScaledSize(size.scaled(1600, 1200, Qt::KeepAspectRatio));
    return reader.read();
  }));
}
} // namespace godeye
