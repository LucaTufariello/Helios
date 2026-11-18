#pragma once
#include "Core/Monitors.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
namespace helios {
class MonitorPanel : public QWidget {
  Q_OBJECT
public:
  MonitorPanel(Missions &missions, MonitorRunner &runner, QWidget *parent = nullptr);
  void refresh();
signals:
  void catalogueRequested(QJsonObject monitor, Bounds region);

private:
  Missions &missions_;
  MonitorRunner &runner_;
  QListWidget *list_;
  QComboBox *kind_, *area_;
  QSpinBox *interval_, *lookback_;
  QDoubleSpinBox *cloud_;
  QCheckBox *enabled_, *automatic_;
  QPlainTextEdit *detail_;
  QLabel *status_;
  QString missionID_, selected_;
  QMap<QString, QJsonObject> drafts_;
  QJsonObject fields() const;
  void setFields(const QJsonObject &fields);
  void select();
  void run(const std::function<void()> &action);
};
} // namespace helios
