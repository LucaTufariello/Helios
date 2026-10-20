#pragma once
#include "Geography.h"
#include "Products.h"
#include "WorkerJob.h"
#include <QJsonArray>
#include <QSet>
namespace godeye {
QJsonObject monitoringDefaults();
QJsonObject monitoringArea(const QJsonObject &plan, const QString &areaID);
QString monitoringSignature(const QJsonObject &area);
void validateMonitoring(const QJsonObject &profile, const QSet<QString> &pins,
                        const QJsonArray &items, const QSet<QString> &monitors);
void validateRegistrationReport(const QJsonObject &report);
void validateMonitoringInbox(const QJsonArray &entries, const QSet<QString> &areas,
                             const QSet<QString> &evidence);
struct MonitoringWork {
  QString missionID, areaID, key, signature, eventID;
  QJsonObject event;
  WorkerRequest request;
  bool manual = false;
};
// Reads bounded local manifests; call on a background worker with copied inputs.
QVector<MonitoringWork> monitoringWork(const QJsonObject &plan,
                                       const QVector<ProductLayer> &library,
                                       const QString &manualArea = {});
} // namespace godeye
