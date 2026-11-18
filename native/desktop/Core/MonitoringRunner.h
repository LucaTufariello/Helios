#pragma once
#include "Missions.h"
#include "Monitoring.h"
#include "ScientificQueue.h"
#include <QFutureWatcher>
#include <QTimer>
namespace helios {
class MonitoringRunner : public QObject {
  Q_OBJECT
public:
  using Products = std::function<QVector<ProductLayer>()>;
  using Submit = std::function<bool(WorkerRequest, const QString &)>;
  MonitoringRunner(QString jobs, Missions &missions, ScientificQueue &queue, Products products,
                   Submit submit, QObject *parent = nullptr);
  void refresh();
  void runNow(const QString &areaID);
  void stop();
  bool automatic() const {
    return automatic_;
  }
  void setAutomatic(bool enabled);
  void cancelEntry(const QString &missionID, const QString &key) {
    queue_.cancel(missionID + ":" + key);
  }
  void discovered(const QString &missionID, const QString &monitorID,
                  const QStringList &identifiers);
  QString reportFile(const QJsonObject &entry) const;
  QString status() const {
    return status_;
  }
  bool busy() const {
    return probing_ || timer_.isActive() || pending_ || !active_.isEmpty() || !finishing_.isEmpty();
  }
signals:
  void changed();

private:
  QString jobs_, status_, manualArea_, manualMission_;
  Missions &missions_;
  ScientificQueue &queue_;
  Products products_;
  Submit submit_;
  QTimer timer_;
  QFutureWatcher<QVector<MonitoringWork>> probe_;
  QMap<QString, MonitoringWork> active_;
  QSet<QString> finishing_;
  bool probing_ = false, stopped_ = false, pending_ = false, automatic_ = true;
  quint64 generation_ = 0, runningGeneration_ = 0;
  void scan();
  void admit(QVector<MonitoringWork> work);
  void finish(const QString &token, const QString &directory, bool success, const QString &error);
  void publishResult(MonitoringWork work, const QString &directory, bool success,
                     const QString &error, const QJsonObject &result);
  QJsonObject plan(const QString &missionID) const;
  QJsonObject event(const QString &missionID, const QString &key) const;
  void recover();
};
} // namespace helios
