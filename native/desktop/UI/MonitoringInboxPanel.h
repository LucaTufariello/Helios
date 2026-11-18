#pragma once
#include "Core/MonitoringRunner.h"
#include <QFutureWatcher>
#include <QWidget>
class QListWidget;
class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
namespace helios {
class MonitoringInboxPanel : public QWidget {
  Q_OBJECT
public:
  MonitoringInboxPanel(Missions &missions, MonitoringRunner &runner, QWidget *parent = nullptr);
  void setArea(const QString &area) {
    areaID_ = area;
    refresh();
  }
  void refresh();
signals:
  void dossierRequested(QString areaID);
  void findingRequested(QJsonObject origin);
  void reviewRequested(QString manifest);

private:
  Missions &missions_;
  MonitoringRunner &runner_;
  QString areaID_, missionID_, selectedID_;
  QListWidget *list_;
  QComboBox *kind_, *state_;
  QLineEdit *search_;
  QLabel *status_, *summary_;
  QPlainTextEdit *detail_, *raw_;
  QPushButton *open_, *cancel_;
  QJsonObject selected() const;
  QFutureWatcher<QJsonObject> reader_;
  bool reading_ = false;
  QString pendingFile_, pendingMission_, pendingID_, readMission_, readID_;
  void readNext();
  void inspect();
  void review();
  void run(const std::function<void()> &fn);
};
} // namespace helios
