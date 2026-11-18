#include "RadarPanel.h"
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>
namespace helios {
RadarPanel::RadarPanel(const QString &profile, MapWidget &map, std::function<Bounds()> region,
                       QWidget *parent, bool networking)
    : QWidget(parent), map_(map), networking_(networking), region_(std::move(region)),
      catalog_(this), account_(this), download_(this) {
  auto *layout = new QVBoxLayout(this);
  auto *info = new QLabel("Copernicus Data Space · Sentinel-1 GRD. Search uses the query bounds "
                          "above. Downloads contain the complete source SAFE archive and can be "
                          "several gigabytes; no pixel cutout is implied.");
  info->setWordWrap(true);
  layout->addWidget(info);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  start_ = new QDateEdit(QDateTime::currentDateTimeUtc().date().addDays(-30));
  end_ = new QDateEdit(QDateTime::currentDateTimeUtc().date());
  auto *dates = new QHBoxLayout;
  for (auto *field : {start_, end_}) {
    field->setCalendarPopup(true);
    field->setDisplayFormat("yyyy-MM-dd");
    dates->addWidget(field);
  }
  form->addRow("Dates (UTC)", dates);
  auto *filters = new QHBoxLayout;
  polarization_ = new QComboBox;
  polarization_->addItems({"VV + VH", "VV", "VH"});
  mode_ = new QComboBox;
  mode_->addItems({"IW", "EW", "SM"});
  filters->addWidget(polarization_);
  filters->addWidget(new QLabel("Mode"));
  filters->addWidget(mode_);
  form->addRow("Polarization", filters);
  auto *actions = new QHBoxLayout;
  search_ = new QPushButton("Search GRD acquisitions");
  search_->setObjectName("radarSearch");
  next_ = new QPushButton("More results");
  next_->setObjectName("radarNext");
  actions->addWidget(search_);
  actions->addWidget(next_);
  layout->addLayout(actions);
  connect(search_, &QPushButton::clicked, this, [this] { search(false); });
  connect(next_, &QPushButton::clicked, this, [this] { search(true); });
  results_ = new QListWidget;
  results_->setObjectName("radarAcquisitions");
  layout->addWidget(results_, 1);
  selection_ = new QLabel("Select an acquisition to inspect its identity, size and footprint.");
  selection_->setTextFormat(Qt::PlainText);
  selection_->setWordWrap(true);
  layout->addWidget(selection_);
  connect(results_, &QListWidget::currentRowChanged, this, &RadarPanel::selected);
  auto *focus = new QPushButton("Show acquisition footprint");
  focus->setObjectName("radarFootprint");
  layout->addWidget(focus);
  connect(focus, &QPushButton::clicked, this, [this] {
    const auto row = results_->currentRow();
    if (row < 0 || row >= scenes_.size())
      return;
    const auto &paths = scenes_[row].footprint;
    if (paths.isEmpty()) {
      emit status("The catalogue supplied no supported footprint for this acquisition.");
      return;
    }
    if (!map_.setPaths("radar-acquisition", paths)) {
      emit status("Footprint exceeded the map geometry budget.");
      return;
    }
    Bounds bounds{180, 85, -180, -85};
    for (const auto &path : paths)
      for (const auto point : path) {
        bounds.west = std::min(bounds.west, point.longitude);
        bounds.east = std::max(bounds.east, point.longitude);
        bounds.south = std::min(bounds.south, point.latitude);
        bounds.north = std::max(bounds.north, point.latitude);
      }
    if (bounds.valid())
      map_.focus(bounds);
  });
  auto *account = new QGroupBox("Copernicus account · session only");
  auto *accountForm = new QFormLayout(account);
  username_ = new QLineEdit;
  username_->setMaxLength(320);
  accountForm->addRow("Email", username_);
  password_ = new QLineEdit;
  password_->setMaxLength(1024);
  password_->setEchoMode(QLineEdit::Password);
  accountForm->addRow("Password", password_);
  totp_ = new QLineEdit;
  totp_->setMaxLength(6);
  totp_->setPlaceholderText("Optional current six-digit code");
  accountForm->addRow("Two-factor code", totp_);
  auto *signActions = new QHBoxLayout;
  signIn_ = new QPushButton("Sign in");
  signOut_ = new QPushButton("Sign out");
  signActions->addWidget(signIn_);
  signActions->addWidget(signOut_);
  accountForm->addRow(signActions);
  accountStatus_ = new QLabel;
  accountStatus_->setWordWrap(true);
  accountForm->addRow(accountStatus_);
  layout->addWidget(account);
  connect(signIn_, &QPushButton::clicked, this, [this] {
    if (!networking_) {
      emit status("Offline mode: Copernicus sign-in is unavailable.");
      return;
    }
    account_.signIn(username_->text().trimmed(), password_->text(), totp_->text());
    password_->clear();
    totp_->clear();
  });
  connect(signOut_, &QPushButton::clicked, this, [this] {
    download_.cancel();
    account_.signOut();
    password_->clear();
    totp_->clear();
  });
  auto *storage = new QHBoxLayout;
  QSettings settings(profile + "/desktop.ini", QSettings::IniFormat);
  library_ =
      new QLineEdit(settings.value("radarDownloadLibrary", profile + "/SAR/downloads").toString());
  library_->setMaxLength(4096);
  storage->addWidget(library_);
  auto *choose = new QPushButton("Download folder…");
  storage->addWidget(choose);
  layout->addLayout(storage);
  connect(choose, &QPushButton::clicked, this, [this] {
    const auto folder = QFileDialog::getExistingDirectory(
        this, "Choose Sentinel-1 download library", library_->text());
    if (!folder.isEmpty())
      library_->setText(folder);
  });
  connect(library_, &QLineEdit::textChanged, this, [profile](const QString &path) {
    QSettings settings(profile + "/desktop.ini", QSettings::IniFormat);
    settings.setValue("radarDownloadLibrary", path);
  });
  retrieve_ = new QPushButton("Download selected archive / inspect saved archive");
  retrieve_->setObjectName("radarDownload");
  layout->addWidget(retrieve_);
  connect(retrieve_, &QPushButton::clicked, this, [this] {
    const auto row = results_->currentRow();
    if (row < 0 || row >= scenes_.size() || !query_)
      return;
    const auto scene = scenes_[row];
    const auto query = *query_;
    const auto library = library_->text();
    const auto saved = RadarDownload::savedArchive(QDir(library).filePath(scene.id), scene);
    if (!saved.isEmpty()) {
      emit inspect(saved);
      return;
    }
    if (!networking_) {
      emit status("Offline mode: archive downloads are unavailable.");
      return;
    }
    account_.access([this, scene, query, library](const QString &token, const QString &error) {
      if (!error.isEmpty())
        emit status(error);
      else
        download_.start(scene, query, token, library);
    });
  });
  progress_ = new QProgressBar;
  progress_->setRange(0, 1000);
  layout->addWidget(progress_);
  connect(&catalog_, &RadarCatalog::changed, this, &RadarPanel::refresh);
  connect(&account_, &CopernicusAccount::changed, this, &RadarPanel::refresh);
  connect(&download_, &RadarDownload::changed, this, &RadarPanel::refresh);
  connect(&catalog_, &RadarCatalog::failed, this, &RadarPanel::status);
  connect(&account_, &CopernicusAccount::failed, this, &RadarPanel::status);
  connect(&catalog_, &RadarCatalog::results, this, [this](const RadarPage &page) {
    hasMore_ = page.hasMore && skip_ < 980;
    for (const auto &scene : page.scenes) {
      if (std::any_of(scenes_.begin(), scenes_.end(),
                      [&](const auto &old) { return old.id == scene.id; }))
        continue;
      scenes_.append(scene);
      auto *item = new QListWidgetItem(scene.acquisition + " · " + scene.name, results_);
      item->setToolTip(scene.name);
    }
    if (results_->currentRow() < 0 && !scenes_.isEmpty())
      results_->setCurrentRow(0);
    emit status(QString("%1 admitted acquisitions · %2 rejected in this page. %3")
                    .arg(scenes_.size())
                    .arg(page.rejected)
                    .arg(hasMore_ ? "More results are available." : "End of the bounded search."));
    refresh();
  });
  connect(&download_, &RadarDownload::progress, this, [this](qint64 received, qint64 expected) {
    if (expected > 0) {
      progress_->setRange(0, 1000);
      progress_->setValue(int(std::min(1000.0, double(received) / expected * 1000)));
    } else
      progress_->setRange(0, 0);
    emit status(QString("Downloaded %1 MiB%2")
                    .arg(double(received) / 1048576, 0, 'f', 1)
                    .arg(expected > 0
                             ? QString(" of %1 MiB").arg(double(expected) / 1048576, 0, 'f', 1)
                             : " · total size unknown"));
  });
  connect(&download_, &RadarDownload::finished, this,
          [this](const QString &archive, const QString &error) {
            if (!error.isEmpty())
              emit status(error);
            else {
              emit status("Source archive saved. Inspecting SAFE metadata before processing.");
              emit inspect(archive);
            }
          });
  refresh();
}
bool RadarPanel::busy() const {
  return catalog_.busy() || account_.busy() || download_.busy();
}
void RadarPanel::cancel() {
  catalog_.cancel();
  if (account_.busy())
    account_.signOut();
  download_.cancel();
  refresh();
}
bool RadarPanel::setQuery(const RadarQuery &query) {
  if (!query.valid())
    return false;
  start_->setDate(query.start);
  end_->setDate(query.end);
  polarization_->setCurrentText(query.polarization);
  mode_->setCurrentText(query.mode);
  return true;
}
void RadarPanel::search(bool next) {
  if (!networking_) {
    emit status("Offline mode: radar catalogue searches are unavailable.");
    return;
  }
  if (busy())
    return;
  if (!next) {
    query_ = RadarQuery{region_(), start_->date(), end_->date(), polarization_->currentText(),
                        mode_->currentText()};
    if (!query_->valid()) {
      emit status("Choose valid bounds and up to one year of UTC dates.");
      return;
    }
    skip_ = 0;
    hasMore_ = false;
    scenes_.clear();
    results_->clear();
    map_.setPaths("radar-acquisition", {});
  } else if (query_ && hasMore_)
    skip_ += 20;
  else
    return;
  if (query_)
    catalog_.search(*query_, skip_);
  emit status("Searching Copernicus Sentinel-1 catalogue…");
}
void RadarPanel::selected() {
  const auto row = results_->currentRow();
  if (row >= 0 && row < scenes_.size()) {
    const auto &scene = scenes_[row];
    selection_->setText(
        QString("%1\n%2 UTC · %3 · relative orbit %4 · %5 MiB · %6\nProvider ID: %7")
            .arg(scene.name, scene.acquisition, scene.orbit,
                 scene.relativeOrbit ? QString::number(*scene.relativeOrbit) : "unknown")
            .arg(double(scene.contentLength) / 1048576, 0, 'f', 1)
            .arg(scene.online ? "online" : "offline", scene.id));
  }
  refresh();
}
void RadarPanel::refresh() {
  const bool active = busy();
  search_->setEnabled(networking_ && !active);
  next_->setEnabled(networking_ && !active && hasMore_);
  bool saved = false;
  const auto row = results_->currentRow();
  if (!networking_ && row >= 0 && row < scenes_.size())
    saved =
        !RadarDownload::savedArchive(QDir(library_->text()).filePath(scenes_[row].id), scenes_[row])
             .isEmpty();
  retrieve_->setEnabled(!active && row >= 0 && (networking_ || saved));
  library_->setEnabled(!download_.busy());
  signIn_->setEnabled(networking_ && !active);
  signOut_->setEnabled(account_.signedIn() || account_.busy());
  accountStatus_->setText(
      !networking_          ? "Offline mode: Copernicus search, sign-in and downloads are disabled."
      : account_.busy()     ? "Signing in / refreshing…"
      : account_.signedIn() ? "Signed in. Tokens remain in memory until sign-out or exit."
                            : "Signed out. Public catalogue search does not require an account.");
  if (!download_.busy()) {
    progress_->setRange(0, 1000);
    progress_->setValue(0);
  }
  emit changed();
}
} // namespace helios
