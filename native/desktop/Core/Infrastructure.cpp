#include "Infrastructure.h"
#include "Missions.h"
#include "Monitoring.h"
#include "WorkerJob.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace godeye {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
bool finite(const QJsonValue &v) {
  return v.isDouble() && std::isfinite(v.toDouble());
}
bool integer(const QJsonValue &v, int low, int high) {
  return finite(v) && v.toInt(low - 1) == v.toDouble() && v.toInt() >= low && v.toInt() <= high;
}
bool text(const QJsonValue &v, int maximum, bool required = false) {
  return v.isString() && v.toString().toUcs4().size() <= maximum &&
         (!required || !v.toString().trimmed().isEmpty());
}
bool time(const QJsonValue &v) {
  return (finite(v) && std::abs(v.toDouble()) < 1e11) ||
         (v.isString() && v.toString().size() <= 50 &&
          QDateTime::fromString(v.toString(), Qt::ISODateWithMs).isValid());
}
bool site(const QJsonObject &o) {
  const auto bounds = Bounds::read(o["region"]);
  return !QUuid(o["id"].toString()).isNull() && text(o["name"], 120, true) && text(o["kind"], 80) &&
         time(o["createdAt"]) && bounds && bounds->validArea();
}
bool options(const QJsonObject &o) {
  return QStringList{"VV", "VH"}.contains(o["polarization"].toString()) &&
         finite(o["thresholdDb"]) && o["thresholdDb"].toDouble() >= 1 &&
         o["thresholdDb"].toDouble() <= 20 &&
         (o["averagingWindow"] == 3 || o["averagingWindow"] == 5 || o["averagingWindow"] == 7) &&
         integer(o["minimumPixels"], 9, 10000);
}
bool sha(const QJsonValue &v) {
  return v.isString() && QRegularExpression("^[a-fA-F0-9]{64}$").match(v.toString()).hasMatch();
}
QDateTime acquisition(QString value) {
  if (!value.endsWith('Z') && !QRegularExpression("[+-][0-9]{2}:[0-9]{2}$").match(value).hasMatch())
    value += 'Z';
  return QDateTime::fromString(value, Qt::ISODateWithMs).toUTC();
}
} // namespace
void validateSites(const QJsonDocument &document) {
  require(document.isArray() && document.array().size() <= 500,
          "The saved site library is invalid or exceeds 500 sites.");
  QSet<QUuid> ids;
  for (const auto value : document.array()) {
    const auto o = value.toObject();
    const QUuid id(o["id"].toString());
    require(value.isObject() && site(o) && !ids.contains(id),
            "A saved site is invalid or duplicated; original data were preserved.");
    ids.insert(id);
  }
}
SiteLibrary::SiteLibrary(const QString &path) : file_(path, 1024 * 1024, validateSites) {}
void SiteLibrary::load() {
  sites_ = file_.load(QJsonDocument(QJsonArray{})).array();
}
void SiteLibrary::save(QJsonArray sites) {
  file_.save(QJsonDocument(sites));
  sites_ = std::move(sites);
}
QString SiteLibrary::add(const QString &name, const QString &kind, Bounds bounds) {
  auto next = sites_;
  const auto id = newID();
  next.append(QJsonObject{{"id", id},
                          {"name", name.trimmed()},
                          {"kind", kind},
                          {"region", bounds.json()},
                          {"createdAt", foundationNow()}});
  save(next);
  return id;
}
void SiteLibrary::rename(const QString &id, const QString &name, const QString &kind) {
  auto next = sites_;
  bool found = false;
  for (qsizetype i = 0; i < next.size(); ++i) {
    auto o = next[i].toObject();
    if (o["id"] == id) {
      o["name"] = name.trimmed();
      o["kind"] = kind;
      next[i] = o;
      found = true;
    }
  }
  require(found, "The saved site no longer exists.");
  save(next);
}
void SiteLibrary::remove(const QString &id) {
  auto next = sites_;
  bool found = false;
  for (qsizetype i = next.size(); i-- > 0;)
    if (next[i].toObject()["id"] == id) {
      next.removeAt(i);
      found = true;
    }
  require(found, "The saved site no longer exists.");
  save(next);
}
QString changeCompatibility(const QJsonObject &before, const QJsonObject &after,
                            const QString &polarization) {
  if (before["version"] != 1 || after["version"] != 1 || before["state"] != "completed" ||
      after["state"] != "completed")
    return "Choose two completed version-1 SAR results.";
  const auto a = before["metadata"].toObject(), b = after["metadata"].toObject();
  const auto first = acquisition(a["acquisitionStart"].toString()),
             last = acquisition(b["acquisitionStart"].toString());
  if (!first.isValid() || !last.isValid() || first >= last)
    return "The later acquisition must be newer than the baseline.";
  if (a["productType"] != "GRD" || b["productType"] != "GRD" ||
      !QStringList{"ASCENDING", "DESCENDING"}.contains(a["orbitDirection"].toString()) ||
      !integer(a["relativeOrbit"], 1, 175))
    return "Choose GRD products with known pass direction and relative orbit.";
  for (const auto &key : {"mission", "satellite", "mode", "orbitDirection", "relativeOrbit"})
    if (a[key].isUndefined() || a[key] != b[key] ||
        (a[key].isString() && (a[key].toString().isEmpty() || a[key] == "UNKNOWN")))
      return "Use the same satellite, mode, pass direction and known relative orbit.";
  for (const auto &key : {"applyOrbit", "speckle", "pixelSpacing"})
    if (before["options"].toObject()[key].isUndefined() ||
        before["options"].toObject()[key] != after["options"].toObject()[key])
      return "Both acquisitions need matching orbit, speckle and output-spacing settings.";
  if (!QStringList{"VV", "VH"}.contains(polarization))
    return "Select calibrated VV or VH power.";
  for (const auto &record : {before, after}) {
    const auto stages = record["processingStages"].toArray();
    if (!stages.contains("Calibration") || !stages.contains("Terrain-Correction"))
      return "Both results must record calibration and terrain correction.";
    bool found = false;
    for (const auto value : record["outputs"].toArray()) {
      const auto o = value.toObject();
      if (o["id"] == polarization && text(o["scientific"], 256, true))
        found = true;
    }
    if (!found)
      return "Both acquisitions need floating-point calibrated outputs for the selected "
             "polarization.";
  }
  return {};
}
void validateChangeReview(const QJsonObject &r) {
  require(r["version"] == 1 && r["state"] == "completed" && site(r["site"].toObject()) &&
              options(r["options"].toObject()),
          "Choose a completed supported change review with a valid site and settings.");
  if (!r["quality"].isNull() && !r["quality"].isUndefined()) {
    require(r["quality"].isObject(), "Registration quality metadata is invalid.");
    validateRegistrationReport(r["quality"].toObject());
    require(r["quality"].toObject()["state"] == "passed",
            "This review did not pass registration verification.");
  }
  const auto width = r["width"], height = r["height"], fraction = r["commonValidFraction"];
  require(integer(width, 1, 1000000) && integer(height, 1, 1000000) &&
              width.toInt() <= 1000000 / height.toInt() && Bounds::read(r["bounds"]) &&
              finite(fraction) && fraction.toDouble() >= .5 && fraction.toDouble() <= 1,
          "Review grid, bounds or common-valid coverage are invalid.");
  const auto range = r["sharedDisplayRangeDb"].toArray();
  require(range.size() == 2 && finite(range[0]) && finite(range[1]) &&
              range[0].toDouble() < range[1].toDouble(),
          "Review display range is invalid.");
  require(r["evidence"].isArray() && r["evidence"].toArray().size() == 2,
          "A review needs two source evidence snapshots.");
  for (const auto value : r["evidence"].toArray()) {
    const auto e = value.toObject();
    require(!QUuid(e["jobID"].toString()).isNull() && sha(e["metadataSHA256"]) &&
                sha(e["alignedSitePowerSHA256"]) && finite(e["scientificBytes"]) &&
                e["scientificBytes"].toDouble() > 0,
            "Review source identity, hashes or scientific size are invalid.");
  }
  require(r["candidates"].isArray() && r["candidates"].toArray().size() <= 500,
          "Review candidate count is invalid.");
  QSet<QString> ids;
  for (const auto value : r["candidates"].toArray()) {
    const auto c = value.toObject();
    const auto id = c["id"].toString();
    bool ok = false;
    const int number = id.toInt(&ok);
    require(
        ok && number >= 1 && number <= 500 && !ids.contains(id) && QString::number(number) == id &&
            QStringList{"increase", "decrease"}.contains(c["direction"].toString()) &&
            Bounds::read(c["bounds"]) && integer(c["pixels"], 9, 1000000) &&
            finite(c["approximateAreaM2"]) && c["approximateAreaM2"].toDouble() > 0 &&
            finite(c["meanDeltaDb"]) && finite(c["maximumAbsoluteDeltaDb"]) &&
            c["maximumAbsoluteDeltaDb"].toDouble() >= std::abs(c["meanDeltaDb"].toDouble()) - .0001,
        "A change candidate is invalid or duplicated.");
    ids.insert(id);
  }
}
ChangeReview ChangeReview::read(const QString &manifest) {
  const auto bytes = JsonFile::read(manifest, 1024 * 1024);
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(bytes, &error);
  require(error.error == QJsonParseError::NoError && doc.isObject(),
          "Review metadata is unreadable.");
  validateChangeReview(doc.object());
  const auto directory = QFileInfo(manifest).absolutePath();
  auto id = QFileInfo(directory).fileName();
  if (id == "product") {
    const auto parent = QFileInfo(directory).dir();
    const auto record = WorkerJob::readRecord(parent.filePath("run.json"));
    if (record["id"] == parent.dirName() && record["operation"] == "compare")
      id = parent.dirName();
  }
  require(!QUuid(id).isNull(), "The review directory must retain its UUID identity.");
  return {id, directory, doc.object()};
}
ProductLayer ChangeReview::layer(const QString &view) const {
  require(QStringList{"Before", "After", "Change"}.contains(view),
          "Choose Before, After or Change view.");
  ProductLayer layer;
  layer.id = "review:" + id + ":" + view;
  layer.jobID = id;
  layer.kind = "review";
  layer.band = view;
  layer.label = result["site"].toObject()["name"].toString() + " · " + view;
  layer.bounds = *Bounds::read(result["bounds"]);
  layer.preview = safeProductFile(directory, view + ".png");
  QFileInfo file(layer.preview);
  require(file.size() <= 32 * 1024 * 1024, "Review preview exceeds 32 MiB.");
  QImageReader reader(layer.preview);
  require(reader.format() == "png" && reader.size().width() > 0 && reader.size().height() > 0 &&
              reader.size().width() <= 2048 && reader.size().height() <= 2048,
          "Review preview dimensions are unsupported.");
  layer.pixelSize = reader.size();
  layer.image = reader.read().convertToFormat(QImage::Format_RGBA8888);
  require(!layer.image.isNull(), "Review preview could not be decoded.");
  layer.metadata = result;
  return layer;
}
ChangeNotes::ChangeNotes(const ChangeReview &review)
    : file_(review.directory + "/notes.json", 1024 * 1024,
            [id = review.id,
             candidates = review.result["candidates"].toArray()](const QJsonDocument &doc) {
              const auto root = doc.object();
              require(doc.isObject() && root["version"] == 1 &&
                          QUuid(root["reviewID"].toString()) == QUuid(id) &&
                          root["candidates"].isObject() &&
                          root["candidates"].toObject().size() <= 500,
                      "Saved review decisions are invalid; original bytes were preserved.");
              QSet<QString> allowed;
              for (const auto c : candidates)
                allowed.insert(c.toObject()["id"].toString());
              const auto values = root["candidates"].toObject();
              for (auto i = values.begin(); i != values.end(); ++i) {
                const auto c = i.value().toObject();
                require(
                    allowed.contains(i.key()) &&
                        QStringList{"Unreviewed", "Follow up", "Explained / dismiss", "Uncertain"}
                            .contains(c["decision"].toString()) &&
                        text(c["note"], 4000) && text(c["reference"], 1000) && time(c["updatedAt"]),
                    "A saved candidate decision is invalid or refers to an unknown candidate.");
              }
            }),
      id_(review.id) {}
void ChangeNotes::load() {
  notes_ = file_
               .load(QJsonDocument(
                   QJsonObject{{"version", 1}, {"reviewID", id_}, {"candidates", QJsonObject{}}}))
               .object();
}
void ChangeNotes::set(const QString &candidate, const QString &decision, const QString &note,
                      const QString &reference) {
  auto next = notes_;
  auto values = next["candidates"].toObject();
  auto value = values[candidate].toObject();
  value["decision"] = decision;
  value["note"] = note;
  value["reference"] = reference;
  value["updatedAt"] = foundationNow();
  values[candidate] = value;
  next["candidates"] = values;
  file_.save(QJsonDocument(next));
  notes_ = next;
}
} // namespace godeye
