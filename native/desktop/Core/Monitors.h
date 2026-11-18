#pragma once
#include "Missions.h"
#include "Products.h"
#include <QDateTime>
#include <QPointer>
#include <QTimer>
namespace helios {
bool monitorDue(const QJsonObject &monitor, const QDateTime &now);
QJsonObject receiveMonitor(QJsonObject monitor, QStringList identifiers, const QDateTime &now,
                           bool limited, int *newCount = nullptr);
struct MonitorResult {
  QStringList identifiers;
  bool limited = false;
  QString error, summary;
};
class MonitorRunner : public QObject {
  Q_OBJECT
public:
  using Completion = std::function<void(MonitorResult)>;
  using Search = std::function<void(const QJsonObject &, Bounds, Completion, QObject *)>;
  using Products = std::function<QVector<ProductLayer>()>;
  MonitorRunner(Missions &missions, Products products, QObject *parent = nullptr,
                Search search = {});
  ~MonitorRunner() override;
  bool automatic() const {
    return timer_.isActive();
  }
  void start();
  void stop();
  void setNetworking(bool enabled);
  void tick();
  bool run(QString missionID, QString monitorID);
  bool running(const QString &missionID, const QString &monitorID) const;
  int activeCount() const {
    return work_.size();
  }
signals:
  void changed();
  void failed(QString message);
  void discovered(QString missionID, QString monitorID, QStringList identifiers);
  void results(QString missionID, QString monitorID, QStringList identifiers);

private:
  Missions &missions_;
  Products products_;
  Search search_;
  QTimer timer_;
  bool networking_ = true;
  QMap<QString, QPointer<QObject>> work_;
};
} // namespace helios
