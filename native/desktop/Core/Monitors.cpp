#include "Monitors.h"
#include "OpticalCatalog.h"
#include "RadarCatalog.h"
#include "Timeline.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <stdexcept>
namespace helios {
namespace {
QJsonObject findPlan(const Missions &missions, const QString &id) {
  for (const auto value : missions.plans())
    if (value.toObject()["id"] == id)
      return value.toObject();
  return {};
}
QJsonObject findMonitor(const QJsonObject &plan, const QString &id) {
  for (const auto value : plan["brief"].toObject()["monitors"].toArray())
    if (value.toObject()["id"] == id)
      return value.toObject();
  return {};
}
QJsonObject findArea(const QJsonObject &plan, const QString &id) {
  for (const auto value : plan["brief"].toObject()["areas"].toArray())
    if (value.toObject()["id"] == id)
      return value.toObject();
  return {};
}
QString signature(const QJsonObject &plan, const QJsonObject &monitor) {
  return QString::fromUtf8(
      QJsonDocument(QJsonObject{{"kind", monitor["kind"]},
                                {"areaID", monitor.value("areaID").toString()},
                                {"region", findArea(plan, monitor["areaID"].toString())["region"]},
                                {"lookbackDays", monitor["lookbackDays"]},
                                {"maximumCloud", monitor["maximumCloud"]}})
          .toJson(QJsonDocument::Compact));
}
void search(const QJsonObject &monitor, Bounds bounds, MonitorRunner::Completion completion,
            QObject *context) {
  const auto now = QDateTime::currentDateTimeUtc().date(),
             start = now.addDays(-monitor["lookbackDays"].toInt());
  if (monitor["kind"] == "New Sentinel-2 imagery") {
    auto *catalog = new OpticalCatalog(context);
    QObject::connect(catalog, &OpticalCatalog::results, context,
                     [completion](const QVector<QJsonObject> &scenes) {
                       QStringList ids;
                       for (const auto &s : scenes)
                         ids.append(s["id"].toString());
                       completion({ids, scenes.size() == 40, {}, {}});
                     });
    QObject::connect(catalog, &OpticalCatalog::failed, context,
                     [completion](const QString &error) { completion({{}, false, error, {}}); });
    catalog->search({bounds, start, now, monitor["maximumCloud"].toDouble(), "sentinel2"});
  } else {
    auto *catalog = new RadarCatalog(context);
    QObject::connect(catalog, &RadarCatalog::results, context, [completion](const RadarPage &page) {
      QStringList ids;
      for (const auto &s : page.scenes)
        ids.append(s.id);
      completion({ids, page.hasMore, {}, {}});
    });
    QObject::connect(catalog, &RadarCatalog::failed, context,
                     [completion](const QString &error) { completion({{}, false, error, {}}); });
    catalog->search({bounds, start, now});
  }
}
} // namespace
bool monitorDue(const QJsonObject &monitor, const QDateTime &now) {
  const auto last = missionTime(monitor["lastAttempt"]);
  return monitor["enabled"].toBool() && now.isValid() &&
         (!last.isValid() || last.secsTo(now) >= monitor["intervalHours"].toInt() * 3600ll);
}
QJsonObject receiveMonitor(QJsonObject monitor, QStringList identifiers, const QDateTime &now,
                           bool limited, int *newCount) {
  if (identifiers.size() > 10000 || !now.isValid())
    throw std::runtime_error("Monitor response exceeded its supported checkpoint contract.");
  auto canonical = [&](QString id) {
    if (monitor["kind"] == "New Sentinel-1 acquisitions" && !QUuid(id).isNull())
      return QUuid(id).toString(QUuid::WithoutBraces);
    return id;
  };
  QStringList previous;
  QSet<QString> known, incoming;
  for (const auto value : monitor["seenIDs"].toArray()) {
    const auto id = canonical(value.toString());
    previous.append(id);
    known.insert(id);
  }
  for (auto id : identifiers) {
    if (id.isEmpty() || id.size() > 200)
      throw std::runtime_error("Monitor returned an invalid source identifier.");
    incoming.insert(canonical(id));
  }
  identifiers = incoming.values();
  identifiers.sort();
  int count = 0;
  const bool baseline = !missionTime(monitor["lastSuccess"]).isValid();
  if (!baseline)
    for (const auto &id : identifiers)
      if (!known.contains(id))
        ++count;
  auto summary =
      baseline
          ? QString("Baseline: %1 acquisitions").arg(identifiers.size())
          : QString("%1 newly seen acquisitions · %2 returned").arg(count).arg(identifiers.size());
  if (limited)
    summary += " · catalogue result limit reached; coverage may be incomplete";
  for (const auto &id : previous)
    if (!incoming.contains(id)) {
      identifiers.append(id);
      incoming.insert(id);
    }
  QJsonArray ids;
  for (qsizetype i = 0; i < identifiers.size() && i < 2000; ++i)
    ids.append(identifiers[i]);
  monitor["seenIDs"] = ids;
  monitor["summary"] = summary;
  monitor["lastAttempt"] = now.toMSecsSinceEpoch() / 1000.0 - 978307200.0;
  monitor["lastSuccess"] = monitor["lastAttempt"];
  monitor["failure"] = QJsonValue::Null;
  if (newCount)
    *newCount = count;
  return monitor;
}
MonitorRunner::MonitorRunner(Missions &missions, Products products, QObject *parent, Search backend)
    : QObject(parent), missions_(missions), products_(std::move(products)),
      search_(backend ? std::move(backend) : search) {
  timer_.setInterval(60000);
  connect(&timer_, &QTimer::timeout, this, &MonitorRunner::tick);
}
MonitorRunner::~MonitorRunner() {
  stop();
}
void MonitorRunner::start() {
  if (!timer_.isActive()) {
    timer_.start();
    emit changed();
    QTimer::singleShot(1000, this, [this] {
      if (timer_.isActive())
        tick();
    });
  }
}
void MonitorRunner::stop() {
  timer_.stop();
  const auto work = work_;
  work_.clear();
  for (auto context : work)
    if (context)
      context->deleteLater();
  emit changed();
}
void MonitorRunner::setNetworking(bool enabled) {
  networking_ = enabled;
  if (enabled)
    return;
  for (auto it = work_.begin(); it != work_.end();) {
    const auto ids = it.key().split('/');
    const auto monitor = findMonitor(findPlan(missions_, ids.value(0)), ids.value(1));
    if (monitor["kind"] == "Evidence availability") {
      ++it;
      continue;
    }
    if (it.value())
      it.value()->deleteLater();
    it = work_.erase(it);
  }
  emit changed();
}
bool MonitorRunner::running(const QString &missionID, const QString &monitorID) const {
  return work_.contains(missionID + "/" + monitorID);
}
void MonitorRunner::tick() {
  const auto plans = missions_.plans();
  const auto now = QDateTime::currentDateTimeUtc();
  for (const auto value : plans) {
    const auto plan = value.toObject();
    for (const auto m : plan["brief"].toObject()["monitors"].toArray()) {
      if (work_.size() >= 2)
        return;
      if ((networking_ || m.toObject()["kind"] == "Evidence availability") &&
          monitorDue(m.toObject(), now))
        run(plan["id"].toString(), m.toObject()["id"].toString());
    }
  }
}
bool MonitorRunner::run(QString missionID, QString monitorID) {
  const auto key = missionID + "/" + monitorID;
  if (work_.contains(key) || work_.size() >= 2)
    return false;
  const auto plan = findPlan(missions_, missionID);
  auto monitor = findMonitor(plan, monitorID);
  if (monitor.isEmpty())
    return false;
  if (!networking_ && monitor["kind"] != "Evidence availability") {
    emit failed("Offline mode: catalogue monitors are unavailable. Local evidence checks remain "
                "available.");
    return false;
  }
  const auto bounds = Bounds::read(findArea(plan, monitor["areaID"].toString())["region"]);
  const auto querySignature = signature(plan, monitor);
  if (monitor["kind"] != "Evidence availability" && !bounds) {
    emit failed("Choose an existing mission area before checking acquisitions.");
    return false;
  }
  monitor["lastAttempt"] = foundationNow();
  try {
    missions_.commitMonitor(missionID, monitorID, {{"lastAttempt", monitor["lastAttempt"]}});
  } catch (const std::exception &e) {
    emit failed(QString::fromUtf8(e.what()));
    return false;
  }
  auto *context = new QObject(this);
  work_[key] = context;
  const QPointer<MonitorRunner> guard(this);
  const QPointer<QObject> owner(context);
  auto complete = [guard, owner, key, missionID, monitorID, monitor,
                   querySignature](const MonitorResult &result) mutable {
    if (!guard || !owner || guard->work_.value(key) != owner)
      return;
    guard->work_.remove(key);
    owner->deleteLater();
    emit guard->changed();
    const auto currentPlan = findPlan(guard->missions_, missionID),
               current = findMonitor(currentPlan, monitorID);
    if (current.isEmpty() || signature(currentPlan, current) != querySignature)
      return;
    try {
      QStringList discovered;
      if (result.error.isEmpty() && missionTime(monitor["lastSuccess"]).isValid()) {
        QSet<QString> known;
        for (const auto v : monitor["seenIDs"].toArray())
          known.insert(v.toString().toLower());
        for (const auto &id : result.identifiers)
          if (!known.contains(id.toLower())) {
            discovered.append(id);
            known.insert(id.toLower());
          }
      }
      if (!result.error.isEmpty())
        monitor["failure"] = "Check failed. Check network and catalogue availability, then retry. "
                             "Previous successful results remain retained.";
      else if (!result.summary.isEmpty()) {
        monitor["summary"] = result.summary.left(1800);
        monitor["lastSuccess"] = foundationNow();
        monitor["failure"] = QJsonValue::Null;
      } else
        monitor = receiveMonitor(monitor, result.identifiers, QDateTime::currentDateTimeUtc(),
                                 result.limited);
      guard->missions_.commitMonitor(
          missionID, monitorID, monitor,
          (result.error.isEmpty()
               ? monitor["kind"].toString() + ": " + monitor["summary"].toString()
               : "Monitor failed: " + monitor["kind"].toString()));
      if (result.error.isEmpty()) {
        emit guard->results(missionID, monitorID, result.identifiers);
        if (!discovered.isEmpty())
          emit guard->discovered(missionID, monitorID, discovered);
      }
    } catch (const std::exception &e) {
      emit guard->failed(QString::fromUtf8(e.what()));
    }
  };
  emit changed();
  if (monitor["kind"] == "Evidence availability") {
    const auto products = products_();
    auto *watcher = new QFutureWatcher<MonitorResult>(context);
    connect(watcher, &QFutureWatcherBase::finished, context,
            [watcher, complete]() mutable { complete(watcher->result()); });
    watcher->setFuture(QtConcurrent::run([plan, products] {
      int count = 0, total = plan["items"].toArray().size();
      QStringList missing;
      for (const auto value : plan["items"].toArray()) {
        const auto e = value.toObject();
        bool available = e["kind"] != "optical" && e["kind"] != "radar";
        for (const auto &p : products)
          if (p.kind == e["kind"] &&
              p.jobID.compare(e["referenceID"].toString(), Qt::CaseInsensitive) == 0 &&
              QFileInfo(p.preview).isFile())
            available = true;
        if (available)
          ++count;
        else if (missing.size() < 4)
          missing.append(e["title"].toString());
      }
      return MonitorResult{
          {},
          false,
          {},
          QString("%1/%2 linked items available locally").arg(count).arg(total) +
              (missing.isEmpty() ? QString{} : " · missing: " + missing.join(", "))};
    }));
  } else
    search_(monitor, *bounds, complete, context);
  return true;
}
} // namespace helios
