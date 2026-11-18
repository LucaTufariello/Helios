#pragma once
#include "Core/Dossiers.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>
class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;
class QPlainTextEdit;
namespace helios {
class DossierPanel : public QWidget {
  Q_OBJECT
public:
  explicit DossierPanel(Missions &missions, QWidget *parent = nullptr);
  void setArea(const QString &id);
  void setLibrary(QVector<ProductLayer> products);
  void refresh();
  void addPage(QWidget *page, const QString &title);
  QString areaID() const {
    return areaID_;
  }
  DossierView currentView() const {
    return view_;
  }
  bool busy() const {
    return projectionActive_ || previewActive_ || updateTimer_.isActive() || pending_ ||
           pendingPreview_.has_value();
  }
  void setMapSelection(Coordinate point) {
    picked_ = point;
  }
signals:
  void areaChanged(QString areaID);
  void findingRequested(QJsonObject origin);
  void assetRequested(QString areaID);
  void evidenceRequested(QJsonObject evidence);
  void focusRequested(helios::Bounds bounds);
  void acquisitionRequested(helios::Bounds bounds, bool radar);
  void comparisonRequested(QString areaID, QString baselineID, QString laterID);
  void reviewRequested(QJsonObject area);

private:
  Missions &missions_;
  QString areaID_, missionID_;
  QVector<ProductLayer> library_;
  DossierView view_;
  QFutureWatcher<DossierView> watcher_;
  QTimer updateTimer_;
  QFutureWatcher<std::pair<QImage, QString>> previewWatcher_;
  std::optional<DossierEvidence> pendingPreview_;
  quint64 previewRunning_ = 0;
  void startPreview();
  bool pending_ = false, projectionActive_ = false, previewActive_ = false;
  quint64 generation_ = 0, runningGeneration_ = 0, previewGeneration_ = 0;
  QWidget *contextActions_;
  QTabWidget *tabs_, *evidenceTabs_;
  QLabel *title_, *purpose_, *overview_, *notices_, *source_, *preview_, *status_;
  QListWidget *baselines_, *linked_, *suggested_, *observations_, *notes_, *tasks_;
  QPushButton *link_, *unlink_, *pin_, *show_, *compare_;
  QPlainTextEdit *raw_, *entryDetail_, *noteDetail_;
  std::optional<Coordinate> picked_;
  void startRefresh();
  void publish(DossierView view);
  void inspectEvidence();
  void inspectRecords();
  void previewEvidence(const DossierEvidence &entry);
  std::optional<DossierEvidence> selectedEvidence() const;
  void guardContext(const QString &mission, const QString &area) const;
  void run(const std::function<void()> &action);
  void addRecord(const QString &collection, const QString &observationID = {});
  void assignRecord(const QString &collection);
  void editSelected(const QString &collection, QListWidget *list);
  void removeSelected(const QString &collection, QListWidget *list);
  void reviewObservation();
  void prepareComparison();
};
} // namespace helios
