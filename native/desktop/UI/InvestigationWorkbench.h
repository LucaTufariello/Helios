#pragma once
#include "Core/Briefing.h"
#include "Core/Missions.h"
#include <QWidget>
#include <memory>
class QComboBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextBrowser;
class QSpinBox;
namespace helios {
class InvestigationWorkbench : public QObject {
  Q_OBJECT
public:
  InvestigationWorkbench(const QString &profile, Missions &missions, QWidget *parent);
  QWidget *overview() const {
    return overview_;
  }
  QWidget *assetsPage() const {
    return assetsPage_;
  }
  QWidget *connectionsPage() const {
    return connectionsPage_;
  }
  QWidget *findingsPage() const {
    return findingsPage_;
  }
  void setLibrary(QVector<ProductLayer> library) {
    library_ = std::move(library);
  }
  void refresh();
  void promote(QJsonObject origin);
  AssetRegistry &registry() {
    return registry_;
  }
  void setCaptureProvider(std::function<QPair<QImage, QJsonObject>()> provider) {
    captureProvider_ = std::move(provider);
  }
signals:
  void focusRequested(helios::Bounds bounds);
  void dossierRequested(QString areaID);
  void findingsRequested();
  void evidenceRequested(QJsonObject evidence);

private:
  Missions &missions_;
  AssetRegistry registry_;
  QWidget *overview_, *assetsPage_, *connectionsPage_, *findingsPage_;
  QTextBrowser *caseSummary_;
  QLabel *registryStatus_, *graphStatus_, *findingStatus_;
  QListWidget *assets_, *snapshots_, *relationships_, *suggestions_, *findings_;
  QLineEdit *assetSearch_, *graphSearch_;
  QComboBox *graphSource_, *graphRoot_;
  QSpinBox *graphDepth_;
  QGraphicsScene *scene_;
  QGraphicsView *graph_;
  QTextBrowser *graphDetail_, *findingDetail_;
  QVector<ProductLayer> library_;
  QList<AssociationSuggestion> suggested_;
  ConnectionGraph shownGraph_;
  bool refreshing_ = false, registryReady_ = false;
  std::function<QPair<QImage, QJsonObject>()> captureProvider_;
  void run(const std::function<void()> &action);
  void editOverview();
  void editAsset(bool create);
  void editRelationship(bool create);
  void importCase();
  void editFinding(bool create);
  void renderGraph();
  void graphSelection();
  void inspectFinding();
  void briefing();
};
} // namespace helios
