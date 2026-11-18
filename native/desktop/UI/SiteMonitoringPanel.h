#pragma once
#include "Core/MonitoringRunner.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QLabel;
namespace helios {
class MonitoringInboxPanel;
class SiteMonitoringPanel : public QWidget {
  Q_OBJECT
public:
  SiteMonitoringPanel(Missions &missions, MonitoringRunner &runner, QWidget *parent = nullptr);
  void setArea(const QString &areaID);
  void acceptControl(Bounds bounds);
  bool resolveDrafts();
signals:
  void drawControlRequested();
  void resumeAutomaticRequested();
  void focusControlRequested(helios::Bounds bounds);
  void reviewRequested(QString manifest);
  void dossierRequested(QString areaID);

private:
  Missions &missions_;
  MonitoringRunner &runner_;
  QString missionID_, areaID_, captureKey_;
  QJsonObject original_;
  bool loading_ = false;
  struct Draft {
    QJsonObject profile, original;
  };
  QMap<QString, Draft> drafts_;
  QCheckBox *enabled_;
  QComboBox *baseline_, *pol_, *window_, *role_;
  QSpinBox *interval_, *minimum_;
  QDoubleSpinBox *threshold_, *shift_, *residual_, *correlation_;
  QTableWidget *controls_;
  QLabel *status_;
  MonitoringInboxPanel *history_;
  void refresh();
  void load(const QJsonObject &profile);
  QJsonObject value() const;
  bool dirty() const;
  void save();
  void editControl(std::optional<Bounds> region = {}, int row = -1);
  QString key() const {
    return missionID_ + ":" + areaID_;
  }
  void run(const std::function<void()> &fn);
};
} // namespace helios
