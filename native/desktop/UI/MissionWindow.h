#pragma once
#include "Core/Missions.h"
#include "Graphics/MapWidget.h"
#include <QMainWindow>
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QLabel;
class QTabWidget;
class QStackedWidget;
namespace godeye {
class DossierPanel;
class MissionWindow : public QMainWindow {
  Q_OBJECT
public:
  MissionWindow(Missions &missions, MapWidget &map, QWidget *parent = nullptr);
  bool resolveDrafts();
  bool save(bool separateCopy = false);
  void openLibrary();
  void refresh();
  DossierPanel *dossierPanel() const {
    return dossier_;
  }
  void openDossier(const QString &areaID);
  void showPage(QWidget *page);
  void addPage(QWidget *page, const QString &title);
signals:
  void evidenceRequested(QJsonObject evidence);
  void attachDisplayedImagery();
  void importSiteRequested();

private:
  Missions &missions_;
  MapWidget &map_;
  DossierPanel *dossier_;
  QLineEdit *areaSearch_;
  QTabWidget *tabs_;
  QStackedWidget *missionContent_;
  QComboBox *selector_, *noteEvidence_, *noteArea_, *taskArea_, *objectEvidence_, *reviewState_;
  QLineEdit *name_, *noteTitle_, *noteReference_, *taskTitle_, *areaName_, *objectTitle_,
      *objectCategory_;
  QPlainTextEdit *objective_, *workingNotes_, *noteBody_, *areaPurpose_, *reviewNote_, *raw_,
      *noteDetail_;
  QListWidget *navigation_;
  QLabel *missionTitle_, *pageTitle_, *pageDescription_;
  QList<QLabel *> metrics_;
  void updateNavigation();
  QListWidget *areas_, *evidence_, *notes_, *tasks_, *objects_, *history_;
  QLabel *summary_, *regionLabel_, *coordinateLabel_;
  std::optional<Bounds> pendingRegion_;
  std::optional<Coordinate> pendingCoordinate_;
  QString displayedMission_, captureMission_;
  QJsonObject briefBaseline_;
  QString reviewID_, reviewOriginalState_, reviewOriginalNote_;
  bool refreshing_ = false;
  void run(const std::function<void()> &action);
  void applyBrief();
  bool resolveReview();
  void remove(QListWidget *list, const QString &collection);
};
} // namespace godeye
