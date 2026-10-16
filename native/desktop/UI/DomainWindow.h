#pragma once
#include "Core/DomainHistory.h"
#include "Core/DomainNetwork.h"
#include <QDialog>
#include <QImage>
#include <QPointer>
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTextEdit;
namespace godeye {
class DomainWindow : public QDialog {
  Q_OBJECT
public:
  explicit DomainWindow(const QString &profile, QWidget *parent = nullptr, bool networking = true);
  ~DomainWindow() override;
  DomainNetwork &network() {
    return network_;
  }
  void setView(Bounds view) {
    network_.setView(view);
  }
  void setSection(const QString &section);
  void selectFeed(const QString &feed);
  void selectRecord(const QString &id);
  void setConfiguration(DomainConfiguration configuration);
  void stopFollowing();
  QString followedID() const {
    return followedID_;
  }
  void restoreFollowing(const QString &id);
  void showSavedRecord(const QJsonObject &record);
  void setAnnotations(DomainSnapshot snapshot) {
    network_.setAnnotations(snapshot);
  }
signals:
  void liveSelectionRequested();
  void feedsChanged(QVector<godeye::DomainRecord> records);
  void recordSelected(godeye::DomainRecord record);
  void evidenceRequested(QJsonObject record);
  void focusRequested(godeye::Coordinate coordinate, double range);
  void followPositionChanged(godeye::Coordinate coordinate);
  void geometrySelected(QVector<QVector<godeye::Coordinate>> paths, QVector<double> altitudes);

private:
  QString profile_, section_ = "All", selectedID_, mediaRecordID_, followedID_;
  bool estimatedOrbit_ = false, followingPending_ = false;
  QDateTime playback_;
  DomainHistory history_;
  DomainNetwork network_;
  QListWidget *feeds_, *records_;
  QComboBox *group_, *transit_, *camera_, *sentinel_;
  QLabel *status_, *credit_, *recordTitle_, *image_, *mediaStatus_;
  QTextEdit *details_, *raw_;
  QPushButton *refresh_, *cancel_, *focus_, *orbit_, *evidence_, *frame_, *video_, *follow_;
  QCheckBox *trails_, *selectedOnly_;
  QSpinBox *playbackSeconds_;
  QLineEdit *sourceQuery_;
  QTabWidget *inspection_;
  QVector<DomainRecord> currentRecords_;
  QMap<QString, QLineEdit *> keys_;
  QFutureWatcher<QImage> *imageWatcher_ = nullptr;
  QPointer<QDialog> savedInspector_;
  std::shared_ptr<std::atomic_bool> imageCancel_;
  quint64 imageGeneration_ = 0;
  QString persistedState_;
  bool updating_ = false, credentialBusy_ = false;
  void update(const QString &feed = {});
  void inspect();
  void providerSettings();
  void saveConfiguration();
  void loadFrame();
  std::optional<DomainRecord> selected() const;
  QVector<DomainRecord> displayRecords(const QVector<DomainRecord> &records) const;
};
} // namespace godeye
