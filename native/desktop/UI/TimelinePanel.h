#pragma once
#include "Core/Missions.h"
#include "Core/Timeline.h"
#include "Graphics/MapWidget.h"
#include "SatelliteWindow.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
namespace godeye {
class TimelinePanel : public QWidget {
  Q_OBJECT
public:
  TimelinePanel(Missions &missions, SatelliteWindow &satellite, MapWidget &map,
                QWidget *parent = nullptr);
  void refresh();
  void prepareComparison(const QString &areaID, const QString &baselineID, const QString &laterID);
  bool busy() const {
    return busy_;
  }
signals:
  void evidenceRequested(QJsonObject evidence);
  void comparisonRequested(QVector<godeye::ProductLayer> layers, double blend);

private:
  Missions &missions_;
  SatelliteWindow &satellite_;
  MapWidget &map_;
  QVector<TimelineEntry> all_, visible_;
  QComboBox *area_, *kind_, *baseline_, *later_;
  QLineEdit *query_;
  QDateEdit *start_, *end_;
  QCheckBox *dates_, *undated_;
  QSpinBox *lookback_;
  QSlider *blend_;
  QListWidget *list_;
  QPlainTextEdit *detail_;
  QLabel *assessment_, *status_;
  bool refreshing_ = false;
  bool busy_ = false;
  void showSelected();
  void compare();
  void exportEntries(bool csv);
};
} // namespace godeye
