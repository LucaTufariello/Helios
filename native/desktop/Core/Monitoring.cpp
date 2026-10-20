#include "Monitoring.h"
#include "Dossiers.h"
#include "Infrastructure.h"
#include "JsonFile.h"
#include "Missions.h"
#include "Timeline.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace godeye {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
bool uuid(const QJsonValue &v) {
  return v.isString() && !QUuid(v.toString()).isNull();
}
bool number(const QJsonValue &v, double low, double high) {
  return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= low && v.toDouble() <= high;
}
bool text(const QJsonValue &v, int max, bool needed = false) {
  return v.isString() && v.toString().toUcs4().size() <= max &&
         (!needed || !v.toString().trimmed().isEmpty());
}
bool sha(const QJsonValue &v) {
  return v.isString() && QRegularExpression("^[a-f0-9]{64}$").match(v.toString()).hasMatch();
}
QString digest(const QJsonObject &o) {
  return QString::fromLatin1(
      QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact),
                               QCryptographicHash::Sha256)
          .toHex());
}
bool overlaps(Bounds a, Bounds b) {
  return a.west < b.east && a.east > b.west && a.south < b.north && a.north > b.south;
}
} // namespace
QJsonObject monitoringDefaults() {
  return {{"version", 1},
          {"enabled", false},
          {"polarization", "VV"},
          {"intervalHours", 24},
          {"maximumShiftPixels", 2.0},
          {"maximumResidualPixels", .5},
          {"minimumCorrelation", .6},
          {"thresholdDb", 3.0},
          {"averagingWindow", 5},
          {"minimumPixels", 25},
          {"controls", QJsonArray{}}};
}
QJsonObject monitoringArea(const QJsonObject &plan, const QString &id) {
  for (const auto v : plan["brief"].toObject()["areas"].toArray())
    if (v.toObject()["id"].toString().compare(id, Qt::CaseInsensitive) == 0)
      return v.toObject();
  return {};
}
QString monitoringSignature(const QJsonObject &area) {
  return digest(
      {{"region", area["region"]}, {"profile", area["dossier"].toObject()["monitoring"]}});
}
void validateMonitoring(const QJsonObject &p, const QSet<QString> &pins, const QJsonArray &items,
                        const QSet<QString> &monitors) {
  require(p["version"] == 1 && p["enabled"].isBool() &&
              QStringList{"VV", "VH"}.contains(p["polarization"].toString()),
          "Invalid monitoring version, enable state or polarization.");
  require(
      number(p["intervalHours"], 1, 168) &&
          p["intervalHours"].toDouble() == p["intervalHours"].toInt() &&
          number(p["maximumShiftPixels"], .5, 8) && number(p["maximumResidualPixels"], .1, 1) &&
          number(p["minimumCorrelation"], .5, .99) && number(p["thresholdDb"], 1, 20) &&
          (p["averagingWindow"] == 3 || p["averagingWindow"] == 5 || p["averagingWindow"] == 7) &&
          number(p["minimumPixels"], 9, 10000) &&
          p["minimumPixels"].toDouble() == p["minimumPixels"].toInt(),
      "Monitoring analysis settings are outside supported limits.");
  if (!p["discoveryMonitorID"].isUndefined() && !p["discoveryMonitorID"].isNull())
    require(uuid(p["discoveryMonitorID"]) &&
                monitors.contains(p["discoveryMonitorID"].toString().toUpper()),
            "The monitoring discovery check no longer exists.");
  const auto baseline = p["baselineEvidenceID"];
  if (!baseline.isNull() && !baseline.isUndefined()) {
    require(uuid(baseline) && pins.contains(baseline.toString().toUpper()),
            "Choose a pinned radar reference before monitoring.");
    bool radar = false;
    for (const auto v : items) {
      const auto e = v.toObject();
      if (e["id"].toString().compare(baseline.toString(), Qt::CaseInsensitive) == 0)
        radar = e["kind"] == "radar" &&
                (e["acquisition"].toObject()["displayBand"] == p["polarization"]);
    }
    require(radar, "The active reference must be radar evidence in the selected polarization.");
  } else
    require(!p["enabled"].toBool(), "Select an active radar reference before enabling monitoring.");
  require(p["controls"].isArray() && p["controls"].toArray().size() <= 12,
          "A monitor supports at most 12 stable controls.");
  QSet<QString> ids;
  QVector<Bounds> regions;
  int fit = 0, verify = 0;
  for (const auto v : p["controls"].toArray()) {
    const auto c = v.toObject();
    const auto b = Bounds::read(c["region"]);
    const auto id = c["id"].toString().toUpper();
    require(v.isObject() && uuid(c["id"]) && !ids.contains(id) && text(c["name"], 120, true) &&
                QStringList{"Fit", "Verify"}.contains(c["role"].toString()) && b && b->validArea(),
            "A stable control is invalid or duplicated.");
    for (const auto &other : regions)
      require(!overlaps(*b, other),
              "Stable controls must not overlap; verification is independent of fitting.");
    ids.insert(id);
    regions.append(*b);
    if (c["role"] == "Fit")
      ++fit;
    else
      ++verify;
  }
  require(
      !p["enabled"].toBool() || (fit >= 3 && verify >= 3),
      "Enabling monitoring requires three fitting and three independent verification controls.");
}
void validateRegistrationReport(const QJsonObject &r) {
  require(r["version"] == 1 && QStringList{"passed", "blocked"}.contains(r["state"].toString()) &&
              text(r["method"], 500, true) && r["absoluteGeolocation"] == "unknown" &&
              r["absoluteRadiometry"] == "unknown" && r["controls"].isArray() &&
              r["controls"].toArray().size() <= 12 && QJsonDocument(r).toJson().size() <= 65536,
          "Registration report is invalid or exceeds its limit.");
  if (r["state"] == "blocked") {
    require(text(r["reason"], 2000, true), "Blocked quality report has no explanation.");
    return;
  }
  const auto policy = r["policy"].toObject();
  require(number(policy["maximumResidualPixels"], .1, 1) &&
              number(policy["maximumShiftPixels"], .5, 8) &&
              number(policy["minimumCorrelation"], .5, .99),
          "Registration policy is invalid.");
  const auto translation = r["translationPixels"].toArray();
  require(translation.size() == 2 && number(translation[0], -8, 8) &&
              number(translation[1], -8, 8) &&
              std::hypot(translation[0].toDouble(), translation[1].toDouble()) <=
                  policy["maximumShiftPixels"].toDouble() + 1e-6,
          "Registration translation is invalid.");
  QSet<QString> ids;
  int fits = 0, verification = 0;
  double max = 0;
  for (const auto v : r["controls"].toArray()) {
    const auto c = v.toObject();
    const auto id = c["id"].toString();
    require(uuid(c["id"]) && !ids.contains(id) &&
                QStringList{"Fit", "Verify"}.contains(c["role"].toString()),
            "A reported control is invalid or duplicated.");
    ids.insert(id);
    if (c["state"] != "usable")
      continue;
    if (c["role"] == "Fit")
      ++fits;
    else {
      require(number(c["residualMagnitudePixels"], 0,
                     policy["maximumResidualPixels"].toDouble() + 1e-6) &&
                  number(c["afterCorrelation"], policy["minimumCorrelation"].toDouble(), 1),
              "Independent verification failed or is missing.");
      ++verification;
      max = std::max(max, c["residualMagnitudePixels"].toDouble());
    }
  }
  require(fits >= 3 && verification >= 3 &&
              number(r["maximumVerificationResidualPixels"], 0,
                     policy["maximumResidualPixels"].toDouble() + 1e-6) &&
              std::abs(max - r["maximumVerificationResidualPixels"].toDouble()) < 1e-6,
          "Registration summary is inconsistent with its verification controls.");
}
void validateMonitoringInbox(const QJsonArray &entries, const QSet<QString> &areas,
                             const QSet<QString> &evidence) {
  require(entries.size() <= 200,
          "The review inbox holds 200 entries. Remove resolved entries before admitting more.");
  QSet<QString> ids, keys;
  for (const auto v : entries) {
    const auto e = v.toObject();
    const auto id = e["id"].toString().toUpper(), key = e["key"].toString();
    require(v.isObject() && uuid(e["id"]) && !ids.contains(id) && sha(e["key"]) &&
                !keys.contains(key) && e["version"] == 1 && text(e["title"], 1024, true) &&
                text(e["areaName"], 160) && text(e["sourceID"], 512) && text(e["reason"], 2000) &&
                text(e["rationale"], 4000) &&
                QStringList{"Analysis", "Acquisition"}.contains(e["kind"].toString()) &&
                QStringList{"Queued", "Running", "Ready", "Blocked", "Failed", "Interrupted",
                            "Stale", "Discovered"}
                    .contains(e["status"].toString()) &&
                QStringList{"New", "Investigating", "Closed", "Dismissed", "Snoozed"}.contains(
                    e["reviewState"].toString()) &&
                missionTime(e["createdAt"]).isValid() && missionTime(e["updatedAt"]).isValid(),
            "A monitoring inbox entry is invalid or duplicated.");
    for (const auto name : {"areaID", "baselineEvidenceID"})
      if (e[name].isString())
        require(uuid(e[name]) && (QString(name) == "areaID" ? areas : evidence)
                                     .contains(e[name].toString().toUpper()),
                "An inbox entry refers to missing mission data.");
    for (const auto name : {"areaID", "baselineEvidenceID", "jobID"})
      if (!e[name].isNull() && !e[name].isUndefined())
        require(uuid(e[name]), "An inbox UUID is invalid.");
    if (e["kind"] == "Analysis")
      require(sha(e["signature"]), "An analysis needs its captured configuration fingerprint.");
    if (e.contains("candidateCount"))
      require(number(e["candidateCount"], 0, 500) &&
                  e["candidateCount"].toDouble() == e["candidateCount"].toInt(),
              "Candidate count is invalid.");
    if (e.contains("maximumResidualPixels"))
      require(number(e["maximumResidualPixels"], 0, 100), "Residual summary is invalid.");
    if (e.contains("commonValidFraction"))
      require(number(e["commonValidFraction"], 0, 1), "Coverage summary is invalid.");
    if (e.contains("sourceTime"))
      require(missionTime(e["sourceTime"]).isValid(), "Source time is invalid.");
    if (e.contains("snoozedUntil"))
      require(missionTime(e["snoozedUntil"]).isValid(), "Snooze time is invalid.");
    if (e.contains("previousJobIDs")) {
      require(e["previousJobIDs"].isArray() && e["previousJobIDs"].toArray().size() <= 20,
              "Retry history exceeds its bound.");
      for (const auto j : e["previousJobIDs"].toArray())
        require(uuid(j), "Retry history identity is invalid.");
    }
    require(QJsonDocument(e).toJson(QJsonDocument::Compact).size() <= 32768,
            "Inbox entry exceeds 32 KiB. Detailed results belong in the job library.");
    ids.insert(id);
    keys.insert(key);
  }
}
QVector<MonitoringWork> monitoringWork(const QJsonObject &plan,
                                       const QVector<ProductLayer> &library,
                                       const QString &manualArea) {
  QVector<MonitoringWork> work;
  for (const auto v : plan["brief"].toObject()["areas"].toArray()) {
    const auto area = v.toObject(), p = area["dossier"].toObject()["monitoring"].toObject();
    const bool manual = area["id"] == manualArea;
    if (p.isEmpty() || (!manual && !p["enabled"].toBool()) || (!manualArea.isEmpty() && !manual))
      continue;
    QJsonObject baseline;
    for (const auto e : plan["items"].toArray())
      if (e.toObject()["id"] == p["baselineEvidenceID"])
        baseline = e.toObject();
    const ProductLayer *before = nullptr;
    for (const auto &product : library)
      if (sameAcquisition(baseline, product) && product.band == p["polarization"]) {
        before = &product;
        break;
      }
    if (!before)
      continue;
    const auto first = WorkerJob::readRecord(before->manifest);
    for (const auto &after : library) {
      if (after.kind != "radar" || after.band != p["polarization"] ||
          after.jobID == before->jobID ||
          !evidenceOverlaps(after.missionEvidence(), *Bounds::read(area["region"])))
        continue;
      const auto second = WorkerJob::readRecord(after.manifest);
      if (!QFileInfo(before->scientific).isFile() || !QFileInfo(after.scientific).isFile())
        continue;
      const auto fileIdentity = [](const QString &path) {
        const QFileInfo info(path);
        return QJsonObject{
            {"bytes", double(info.size())},
            {"modifiedMilliseconds", double(info.lastModified().toMSecsSinceEpoch())}};
      };
      const QJsonArray scientificFiles{fileIdentity(before->scientific),
                                       fileIdentity(after.scientific)};
      if (!changeCompatibility(first, second, p["polarization"].toString()).isEmpty())
        continue;
      try {
        if (QCryptographicHash::hash(JsonFile::read(before->manifest, 1024 * 1024),
                                     QCryptographicHash::Sha256) != before->manifestHash ||
            QCryptographicHash::hash(JsonFile::read(after.manifest, 1024 * 1024),
                                     QCryptographicHash::Sha256) != after.manifestHash)
          continue;
      } catch (...) {
        continue;
      }
      MonitoringWork w;
      w.missionID = plan["id"].toString();
      w.areaID = area["id"].toString();
      w.signature = monitoringSignature(area);
      w.manual = manual;
      w.key = digest({{"mission", w.missionID},
                      {"area", w.areaID},
                      {"signature", w.signature},
                      {"before", before->jobID},
                      {"beforeHash", QString::fromLatin1(before->manifestHash.toHex())},
                      {"after", after.jobID},
                      {"scientificFiles", scientificFiles},
                      {"afterHash", QString::fromLatin1(after.manifestHash.toHex())}});
      w.event = {{"version", 1},
                 {"id", newID()},
                 {"key", w.key},
                 {"signature", w.signature},
                 {"areaID", w.areaID},
                 {"areaName", area["name"]},
                 {"title", after.label.isEmpty() ? after.sourceID : after.label},
                 {"sourceID", after.sourceID},
                 {"kind", "Analysis"},
                 {"status", "Queued"},
                 {"reviewState", "New"},
                 {"rationale", ""},
                 {"reason", "Awaiting the scientific worker"},
                 {"createdAt", foundationNow()},
                 {"updatedAt", foundationNow()},
                 {"baselineEvidenceID", p["baselineEvidenceID"]}};
      const QJsonValue sourceTime = after.missionEvidence()["acquisition"].toObject()["acquiredAt"];
      if (missionTime(sourceTime).isValid())
        w.event["sourceTime"] = sourceTime;
      w.request.operation = "compare";
      w.request.document = {
          {"site", QJsonObject{{"id", area["id"]},
                               {"name", area["name"].toString().left(120)},
                               {"kind", "Mission area"},
                               {"region", area["region"]},
                               {"createdAt", foundationNow()}}},
          {"beforeDirectory", QFileInfo(before->manifest).absolutePath()},
          {"afterDirectory", QFileInfo(after.manifest).absolutePath()},
          {"options", QJsonObject{{"polarization", p["polarization"]},
                                  {"thresholdDb", p["thresholdDb"]},
                                  {"averagingWindow", p["averagingWindow"]},
                                  {"minimumPixels", p["minimumPixels"]}}},
          {"registration", p},
          {"monitoringKey", w.key},
          {"sourceFileIdentities", scientificFiles},
          {"sourceHashes", QJsonArray{QString::fromLatin1(before->manifestHash.toHex()),
                                      QString::fromLatin1(after.manifestHash.toHex())}}};
      bool existing = false;
      for (const auto v : plan["brief"].toObject()["monitoringInbox"].toArray())
        if (v.toObject()["key"] == w.key) {
          const auto status = v.toObject()["status"].toString();
          existing = !(manual && QStringList{"Failed", "Interrupted", "Blocked"}.contains(status));
          break;
        }
      if (existing)
        continue;
      work.append(w);
      std::sort(work.begin(), work.end(), [](const auto &a, const auto &b) {
        return a.event["sourceTime"].toDouble() < b.event["sourceTime"].toDouble();
      });
      if (work.size() > 20)
        work.resize(20);
    }
  }
  std::sort(work.begin(), work.end(), [](const auto &a, const auto &b) {
    return a.event["sourceTime"].toDouble() < b.event["sourceTime"].toDouble();
  });
  return work;
}
} // namespace godeye
