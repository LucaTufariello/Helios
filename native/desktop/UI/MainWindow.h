#pragma once
#include "Core/Missions.h"
#include "Graphics/MapWidget.h"
#include "MissionWindow.h"
#include "SatelliteWindow.h"
#include <QMainWindow>
#include <QSettings>
class QLabel;
class QLineEdit;
namespace godeye {
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
  QLabel *status_, *coordinates_, *selection_, *overlay_;
  QLineEdit *search_;
  void synchronizeMission();
};
} // namespace godeye
