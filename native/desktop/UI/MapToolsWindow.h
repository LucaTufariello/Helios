#pragma once
#include "Core/MapTools.h"
#include "Core/Missions.h"
#include "DomainWindow.h"
#include "Graphics/MapWidget.h"
#include <QDialog>
#include <QTimer>
class QLineEdit;
class QComboBox;
class QDateEdit;
class QSlider;
class QListWidget;
class QLabel;
class QCheckBox;
namespace godeye {
class MapToolsWindow : public QDialog {
  Q_OBJECT
public:
  MapToolsWindow(const QString &profile, MapWidget &map, DomainWindow &domain, Missions &missions,
                 bool networking, QWidget *parent = nullptr);
  void search(const QString &query);
  bool openLink(const QUrl &url);
  bool saveSession();
  QJsonObject workspace() const;
  void applyWorkspace(const QJsonObject &workspace, bool preserveAnnotations = false);

protected:
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  bool resolveDrawing();
  void persistDraft();
  MapWidget &map_;
  DomainWindow &domain_;
  Missions &missions_;
  AnnotationLibrary annotations_;
  MapLookup places_, directions_;
  QString profile_, editingAnnotation_;
  bool networking_, ready_ = false, drawing_ = false, applying_ = false;
  int pickEndpoint_ = 0;
  QVector<Coordinate> drawingPoints_;
  QLineEdit *placeQuery_, *start_, *end_, *title_;
  QComboBox *mode_, *shape_, *style_, *imagery_, *basemap_;
  QDateEdit *date_;
  QSlider *opacity_;
  QListWidget *placeResults_, *routeSteps_, *annotationList_;
  QLabel *placeStatus_, *routeStatus_, *drawingStatus_, *workspaceStatus_, *dailyStatus_,
      *solarStatus_;
  QCheckBox *terrain_, *solar_;
  QTimer solarTimer_;
  std::optional<SolarState> solarState_;
  QJsonObject extensions_;
  void updateAnnotations();
  void annotationVisibility();
  QVector<MapMarker> annotationMarkers_;
  QVector<QVector<Coordinate>> annotationPaths_;
  void drawPreview();
  void updateSolar();
  void updateSolarCaption();
  void updateDaily();
  void synchronizeDisplayControls();
  void showError(const QString &message);
  std::optional<Coordinate> coordinate(const QString &text) const;
};
} // namespace godeye
