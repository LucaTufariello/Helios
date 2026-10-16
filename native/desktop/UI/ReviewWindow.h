#pragma once
#include "Core/Infrastructure.h"
#include "Core/Missions.h"
#include "ProcessingWindow.h"
#include "SatelliteWindow.h"
#include <QDialog>
#include <QSettings>
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;
namespace godeye {
class ReviewWindow : public QDialog {
  Q_OBJECT
public:
  ReviewWindow(const QString &profile, MapWidget &map, Missions &missions,
               SatelliteWindow &satellite, ProcessingWindow &processing, QWidget *parent = nullptr);
  ~ReviewWindow() override;
  void openReview(const QString &manifest);
  SiteLibrary *sites() const {
    return sites_.get();
  }
  bool busy() const {
    return busy_;
  }
signals:
  void layersChanged(QVector<godeye::ProductLayer> layers, bool focus);

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  MapWidget &map_;
  Missions &missions_;
  SatelliteWindow &satellite_;
  ProcessingWindow &processing_;
  QSettings settings_;
  std::unique_ptr<SiteLibrary> sites_;
  std::optional<Bounds> boundary_;
  std::optional<ChangeReview> review_;
  std::unique_ptr<ChangeNotes> notes_;
  QComboBox *site_, *before_, *after_, *polarization_, *window_, *reviews_, *view_, *decision_,
      *exportKind_;
  QDoubleSpinBox *threshold_;
  QSpinBox *minimum_;
  QLineEdit *name_, *kind_, *reference_;
  QListWidget *candidates_;
  QPlainTextEdit *rationale_;
  QLabel *status_, *quality_, *bounds_;
  QTabWidget *tabs_;
  QString candidate_;
  bool busy_ = false, loading_ = false;
  void run(const std::function<void()> &action);
  void refreshSites(const QString &selected = {});
  void refreshProducts();
  void loadReview(const QString &manifest);
  bool saveDecision();
  void chooseCandidate();
  void showLayer();
  void exportFile();
};
} // namespace godeye
