#include "MonitoringRunner.h"
#include "Infrastructure.h"
#include "Timeline.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <QtConcurrent>
namespace godeye {
MonitoringRunner::MonitoringRunner(QString jobs, Missions &missions, ScientificQueue &queue,
                                   Products products, Submit submit, QObject *parent)
    : QObject(parent), jobs_(std::move(jobs)), missions_(missions), queue_(queue),
      products_(std::move(products)), submit_(std::move(submit)) {
  timer_.setSingleShot(true);
  timer_.setInterval(250);
  connect(&timer_, &QTimer::timeout, this, &MonitoringRunner::scan);
  connect(&missions_, &Missions::changed, this, &MonitoringRunner::refresh);
  connect(&queue_, &ScientificQueue::started, this,
          [this](const QString &token, WorkerJob *job, bool) {
            if (!active_.contains(token))
              return;
            auto &w = active_[token];
            auto e = event(w.missionID, w.key);
            if (e.isEmpty()) {
              queue_.cancel(token);
              return;
            }
            e["status"] = "Running";
            e["jobID"] = QFileInfo(job->directory()).fileName();
            e["reason"] = "Checking stable controls and measuring radar changes";
            try {
              missions_.upsertMonitoringEvent(w.missionID, e);
            } catch (const std::exception &ex) {
              status_ = QString::fromUtf8(ex.what());
              queue_.cancel(token);
            }
            emit changed();
          });
  connect(&queue_, &ScientificQueue::finished, this,
          [this](const QString &token, const QString &directory, bool success, bool,
                 const QString &error) { finish(token, directory, success, error); });
  connect(&probe_, &QFutureWatcherBase::finished, this, [this] {
    probing_ = false;
    const auto result = probe_.result();
    if (!stopped_ && runningGeneration_ == generation_) {
      manualArea_.clear();
      manualMission_.clear();
      admit(result);
    }
    if (pending_) {
      pending_ = false;
      timer_.start();
    }
    emit changed();
  });
}
QJsonObject MonitoringRunner::plan(const QString &id) const {
  for (const auto v : missions_.plans())
    if (v.toObject()["id"] == id)
      return v.toObject();
  return {};
}
QJsonObject MonitoringRunner::event(const QString &mission, const QString &key) const {
  for (const auto v : plan(mission)["brief"].toObject()["monitoringInbox"].toArray())
    if (v.toObject()["key"] == key)
      return v.toObject();
  return {};
}
void MonitoringRunner::refresh() {
  if (stopped_)
    return;
  ++generation_;
  for (const auto &token : active_.keys()) {
    const auto w = active_.value(token);
    const auto area = monitoringArea(plan(w.missionID), w.areaID);
    if (area.isEmpty() || monitoringSignature(area) != w.signature)
      queue_.cancel(token);
  }
  timer_.start();
}
void MonitoringRunner::runNow(const QString &area) {
  stopped_ = false;
  manualArea_ = area;
  manualMission_ = missions_.selectedID();
  refresh();
}
void MonitoringRunner::stop() {
  stopped_ = true;
  timer_.stop();
  ++generation_;
  for (const auto &key : active_.keys())
    queue_.cancel(key);
}
void MonitoringRunner::setAutomatic(bool enabled) {
  if (automatic_ == enabled)
    return;
  automatic_ = enabled;
  if (!enabled)
    for (const auto &key : active_.keys())
      if (!active_.value(key).manual)
        queue_.cancel(key);
  refresh();
}
void MonitoringRunner::scan() {
  if (!automatic_ && manualArea_.isEmpty()) {
    status_ = "Automatic monitoring is paused. Manual local analysis remains available.";
    emit changed();
    return;
  }
  if (probing_) {
    pending_ = true;
    return;
  }
  if (missions_.filename().isEmpty()) {
    status_ = "Save the mission library before running durable site monitoring.";
    emit changed();
    return;
  }
  recover();
  probing_ = true;
  runningGeneration_ = generation_;
  const auto plans = missions_.plans();
  const auto library = products_();
  const auto area = manualArea_, mission = manualMission_;
  probe_.setFuture(QtConcurrent::run([plans, library, area, mission] {
    QVector<MonitoringWork> all;
    for (const auto value : plans) {
      const auto p = value.toObject();
      if (!area.isEmpty() && p["id"] != mission)
        continue;
      const auto work = monitoringWork(p, library, area);
      for (const auto &w : work) {
        all.append(w);
        if (all.size() == 20)
          return all;
      }
    }
    return all;
  }));
}
void MonitoringRunner::admit(QVector<MonitoringWork> work) {
  int added = 0;
  for (auto w : work) {
    if (!automatic_ && !w.manual)
      continue;
    if (active_.size() >= 20 || queue_.pendingCount() >= 20) {
      status_ = "Scientific queue limit reached. Remaining local pairs are deferred.";
      break;
    }
    const auto p = plan(w.missionID), area = monitoringArea(p, w.areaID);
    if (area.isEmpty() || monitoringSignature(area) != w.signature)
      continue;
    auto previous = event(w.missionID, w.key);
    if (!previous.isEmpty()) {
      if (!w.manual ||
          !QStringList{"Blocked", "Failed", "Interrupted"}.contains(previous["status"].toString()))
        continue;
      auto jobs = previous["previousJobIDs"].toArray();
      if (previous["jobID"].isString())
        jobs.append(previous["jobID"]);
      if (jobs.size() > 20) {
        status_ = "This analysis has reached its retained retry limit.";
        continue;
      }
      w.event["previousJobIDs"] = jobs;
    }
    try {
      w.eventID = missions_.upsertMonitoringEvent(w.missionID, w.event);
      const auto token = w.missionID + ":" + w.key;
      active_.insert(token, w);
      if (!submit_(w.request, token)) {
        active_.remove(token);
        auto e = event(w.missionID, w.key);
        e["status"] = "Interrupted";
        e["reason"] = "Worker queue refused admission; retry explicitly";
        missions_.upsertMonitoringEvent(w.missionID, e);
      } else
        ++added;
    } catch (const std::exception &ex) {
      status_ = QString::fromUtf8(ex.what());
      emit changed();
      return;
    }
  }
  if (added)
    status_ = QString("Queued %1 local analysis run(s). Downloads remain manual.").arg(added);
  else if (work.isEmpty())
    status_ = "No new compatible local radar pairs. Import scientific products, pin a reference "
              "and configure stable controls.";
  emit changed();
}
void MonitoringRunner::finish(const QString &token, const QString &directory, bool success,
                              const QString &error) {
  if (!active_.contains(token))
    return;
  const auto w = active_.take(token);
  finishing_.insert(token);
  auto *watcher = new QFutureWatcher<QJsonObject>(this);
  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher, w, directory, success, error] {
            const auto r = watcher->result();
            watcher->deleteLater();
            publishResult(w, directory, success, error, r);
          });
  watcher->setFuture(QtConcurrent::run(
      [directory] { return WorkerJob::readRecord(directory + "/product/result.json"); }));
}
void MonitoringRunner::publishResult(MonitoringWork w, const QString &directory, bool success,
                                     const QString &error, const QJsonObject &result) {
  finishing_.remove(w.missionID + ":" + w.key);
  auto e = event(w.missionID, w.key);
  if (e.isEmpty())
    return;
  const auto area = monitoringArea(plan(w.missionID), w.areaID);
  if (!directory.isEmpty())
    e["jobID"] = QFileInfo(directory).fileName();
  if (area.isEmpty() || monitoringSignature(area) != w.signature) {
    e["status"] = "Stale";
    e["reason"] = "Area or monitoring configuration changed; retained job is historical";
  } else if (!success) {
    e["status"] = error.contains("cancel", Qt::CaseInsensitive) ? "Interrupted" : "Failed";
    e["reason"] = error.left(1900);
  } else if (result["analysisStatus"] == "blocked" &&
             result["quality"].toObject()["state"] == "blocked") {
    try {
      validateRegistrationReport(result["quality"].toObject());
    } catch (const std::exception &ex) {
      e["status"] = "Failed";
      e["reason"] = QString::fromUtf8(ex.what());
      try {
        missions_.upsertMonitoringEvent(w.missionID, e);
      } catch (...) {
      }
      emit changed();
      return;
    }
    e["status"] = "Blocked";
    e["reason"] = result["quality"].toObject()["reason"].toString().left(1900);
  } else {
    try {
      validateChangeReview(result);
      if (result["quality"].toObject()["state"] != "passed")
        throw std::runtime_error("Independent registration verification is missing.");
      e["status"] = "Ready";
      e["candidateCount"] = result["candidates"].toArray().size();
      e["maximumResidualPixels"] =
          result["quality"].toObject()["maximumVerificationResidualPixels"];
      e["commonValidFraction"] = result["commonValidFraction"];
      e["reason"] = "Measured radar changes ready for analyst review; cause and absolute accuracy "
                    "remain unestablished";
    } catch (const std::exception &ex) {
      e["status"] = "Failed";
      e["reason"] = QString::fromUtf8(ex.what()).left(1900);
    }
  }
  try {
    missions_.upsertMonitoringEvent(w.missionID, e);
  } catch (const std::exception &ex) {
    status_ = QString::fromUtf8(ex.what());
  }
  emit changed();
  refresh();
}
QString MonitoringRunner::reportFile(const QJsonObject &entry) const {
  const auto job = entry["jobID"].toString();
  if (QUuid(job).isNull())
    return {};
  const auto canonical = QUuid(job).toString(QUuid::WithoutBraces);
  return QDir(jobs_).filePath(canonical + "/product/result.json");
}
void MonitoringRunner::recover() {
  const auto plans = missions_.plans();
  for (const auto value : plans) {
    const auto p = value.toObject();
    for (const auto v : p["brief"].toObject()["monitoringInbox"].toArray()) {
      auto e = v.toObject();
      const auto state = e["status"].toString(),
                 token = p["id"].toString() + ":" + e["key"].toString();
      if (!QStringList{"Queued", "Running"}.contains(state) || queue_.contains(token) ||
          active_.contains(token) || finishing_.contains(token))
        continue;
      const auto file = reportFile(e);
      const auto record =
          file.isEmpty() ? QJsonObject{}
                         : WorkerJob::readRecord(QFileInfo(file).absolutePath() + "/../run.json");
      if (record["state"] == "completed") {
        MonitoringWork w;
        w.missionID = p["id"].toString();
        w.areaID = e["areaID"].toString();
        w.signature = e["signature"].toString();
        w.key = e["key"].toString();
        publishResult(w, QDir::cleanPath(QFileInfo(file).absolutePath() + "/.."), true, {},
                      WorkerJob::readRecord(file));
        continue;
      }
      if (record["state"] == "processing" || record["state"] == "queued" ||
          record["state"] == "validating" || record["state"] == "exporting")
        continue;
      e["status"] = "Interrupted";
      e["reason"] = "Restart recovery: inspect retained job files and use Run local analysis to "
                    "retry explicitly";
      try {
        missions_.upsertMonitoringEvent(p["id"].toString(), e);
      } catch (const std::exception &ex) {
        status_ = QString::fromUtf8(ex.what());
      }
    }
  }
}
void MonitoringRunner::discovered(const QString &mission, const QString &monitor,
                                  const QStringList &identifiers) {
  const auto p = plan(mission);
  if (p.isEmpty())
    return;
  for (const auto a : p["brief"].toObject()["areas"].toArray()) {
    const auto area = a.toObject(), profile = area["dossier"].toObject()["monitoring"].toObject();
    if (!profile["enabled"].toBool() || profile["discoveryMonitorID"] != monitor)
      continue;
    for (const auto &source : identifiers) {
      const auto key = QString::fromLatin1(
          QCryptographicHash::hash((mission + area["id"].toString() + source).toUtf8(),
                                   QCryptographicHash::Sha256)
              .toHex());
      if (!event(mission, key).isEmpty())
        continue;
      QJsonObject e{{"version", 1},
                    {"id", newID()},
                    {"key", key},
                    {"areaID", area["id"]},
                    {"areaName", area["name"]},
                    {"title", "New catalogue acquisition"},
                    {"sourceID", source},
                    {"kind", "Acquisition"},
                    {"status", "Discovered"},
                    {"reviewState", "New"},
                    {"rationale", ""},
                    {"reason", "Newly discovered catalogue ID; acquisition time and usable "
                               "coverage have not been verified"},
                    {"createdAt", foundationNow()},
                    {"updatedAt", foundationNow()}};
      try {
        missions_.upsertMonitoringEvent(mission, e);
      } catch (const std::exception &ex) {
        status_ = QString::fromUtf8(ex.what());
        emit changed();
        return;
      }
    }
  }
}
} // namespace godeye
