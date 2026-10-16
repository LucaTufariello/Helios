#pragma once
#include "Core/Missions.h"
#include "Graphics/MapWidget.h"
#include "MissionWindow.h"
#include "ProcessingWindow.h"
#include "ReviewWindow.h"
#include "SatelliteWindow.h"
#include <QMainWindow>
#include <QSettings>
class QLabel;
class QLineEdit;
namespace godeye {
class MonitorRunner;
class DomainWindow;
class InferenceWindow;
class MapToolsWindow;
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(const QString &profile, bool networking, QWidget *parent = nullptr);
  MapWidget &map() {
    return *map_;
  }
  Missions &missions() {
    return missions_;
  }
  MissionWindow &missionWindow() {
    return *mission_;
  }
  SatelliteWindow &satelliteWindow() {
    return *satellite_;
  }
  ProcessingWindow &processingWindow() {
    return *processing_;
  }
  ReviewWindow &reviewWindow() {
    return *review_;
  }
  MonitorRunner &monitorRunner() {
    return *monitors_;
  }
  DomainWindow &domainWindow() {
    return *domain_;
  }
  InferenceWindow &inferenceWindow() {
    return *inference_;
  }
  MapToolsWindow &mapToolsWindow() {
    return *tools_;
  }
  bool showingSavedEvidence() const {
    return frozenEvidence_;
  }
  void showInference();
  void showMission();
  void showSatellite();
  void showEvidence(const QJsonObject &evidence);
  void attachProduct(const ProductLayer &product);

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  QSettings settings_;
  Missions missions_;
  MapWidget *map_;
  MissionWindow *mission_;
  SatelliteWindow *satellite_;
  ProcessingWindow *processing_;
  ReviewWindow *review_;
  MonitorRunner *monitors_;
  DomainWindow *domain_;
  InferenceWindow *inference_;
  MapToolsWindow *tools_ = nullptr;
  std::optional<ProductLayer> inferenceSource_;
  QString inferenceEvidence_, inferenceMission_;
  bool followingCamera_ = false, frozenEvidence_ = false;
  QLabel *status_, *coordinates_, *selection_, *overlay_, *domainSummary_;
  QLineEdit *search_;
  QVector<ProductLayer> activeLayers_;
  void showLayers(const QVector<ProductLayer> &layers, bool focus);
  void synchronizeMission();
};
} // namespace godeye
