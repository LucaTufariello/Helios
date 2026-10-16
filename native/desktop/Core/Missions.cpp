#include "Missions.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextBoundaryFinder>
#include <QTimeZone>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace godeye {
namespace {
constexpr qint64 maximumBytes = 8 * 1024 * 1024;
constexpr double foundationEpoch = 978307200.0;
void check(bool valid, const char *message) {
  if (!valid)
    throw std::runtime_error(message);
}
QString identity(const QJsonValue &value) {
  const QUuid id(value.toString());
  static const QRegularExpression format(
      "^[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}$");
  check(value.isString() && format.match(value.toString()).hasMatch(),
        "A mission record has an invalid UUID.");
  return id.toString(QUuid::WithoutBraces).toUpper();
}
bool missing(const QJsonValue &v) {
  return v.isNull() || v.isUndefined();
}
bool validSHA256(const QJsonValue &v) {
  static const QRegularExpression sha("^[a-f0-9]{64}$");
  return v.isString() && v.toString().size() == 64 && sha.match(v.toString()).hasMatch();
}
bool validImageBox(const QJsonValue &v, double maximum) {
  if (!v.isArray())
    return false;
  const auto box = v.toArray();
  return box.size() == 4 &&
         std::all_of(box.begin(), box.end(),
                     [maximum](const QJsonValue &value) {
                       return value.isDouble() && std::isfinite(value.toDouble()) &&
                              value.toDouble() >= 0 && value.toDouble() <= maximum;
                     }) &&
         box[0].toDouble() < box[2].toDouble() && box[1].toDouble() < box[3].toDouble();
}
void validateModelProvenance(const QJsonObject &o) {
  if (!missing(o["inputSHA256"]))
    check(validSHA256(o["inputSHA256"]), "An observation source hash is invalid.");
  bool present = false;
  for (const auto key :
       {"modelSHA256", "modelContractSHA256", "modelBackend", "imageBox", "imagePixelBox"})
    present = present || !missing(o[key]);
  // Legacy manual and Core ML observations do not contain ONNX provenance.
  if (!present)
    return;
  check(validSHA256(o["modelSHA256"]) && validSHA256(o["modelContractSHA256"]) &&
            QStringList{"onnx-cpu", "onnx-tinyyolov2-voc-cpu"}.contains(
                o["modelBackend"].toString()) &&
            validImageBox(o["imageBox"], 1) && validImageBox(o["imagePixelBox"], 2048),
        "An observation's ONNX provenance is incomplete or invalid.");
}
void text(const QJsonObject &o, const char *key, int limit, bool nonempty = false) {
  const auto v = o[key];
  check(v.isString(), "A mission text field has an invalid type.");
  const auto s = v.toString();
  check(!nonempty || !s.trimmed().isEmpty(), "A required mission field is empty.");
  // Swift String.count uses grapheme clusters, not UTF-16 code units.
  QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, s);
  int count = 0;
  while (finder.toNextBoundary() >= 0)
    check(++count <= limit, "A mission text field is too long.");
}
void date(const QJsonObject &o, const char *key, bool optional = false) {
  const auto v = o[key];
  if (optional && missing(v))
    return;
  const bool utc =
      v.isString() && v.toString().size() <= 50 &&
      QRegularExpression("(Z|[+-][0-9]{2}:[0-9]{2})$").match(v.toString()).hasMatch() &&
      QDateTime::fromString(v.toString(), Qt::ISODateWithMs).isValid();
  check((v.isDouble() && std::isfinite(v.toDouble())) || utc, "A mission timestamp is invalid.");
}
QSet<QString> arrayIDs(const QJsonObject &o, const char *key, int limit) {
  check(o[key].isArray(), "A mission collection has an invalid type.");
  const auto a = o[key].toArray();
  check(a.size() <= limit, "A mission collection exceeds its supported limit.");
  QSet<QString> ids;
  for (const auto v : a) {
    check(v.isObject(), "A mission collection contains an invalid record.");
    const auto id = identity(v.toObject()["id"]);
    check(!ids.contains(id), "A mission collection contains duplicate identities.");
    ids.insert(id);
  }
  return ids;
}
void linked(const QJsonObject &o, const char *key, const QSet<QString> &ids) {
  if (!missing(o[key]))
    check(ids.contains(identity(o[key])), "A mission link refers to a missing record.");
}
void coordinate(const QJsonValue &v) {
  check(v.isObject() && Coordinate::read(v.toObject()).has_value(),
        "A mission coordinate is invalid.");
}
void bounds(const QJsonValue &v, bool area = false) {
  const auto b = Bounds::read(v);
  check(b && (!area || b->validArea()), "A mission boundary is invalid or unsupported.");
}
void path(const QJsonValue &v, int limit) {
  check(v.isArray() && v.toArray().size() <= limit, "A mission geometry is too large or invalid.");
  for (const auto p : v.toArray())
    coordinate(p);
}
void validateItem(QJsonObject o) {
  text(o, "title", 1024, true);
  text(o, "referenceID", 512, true);
  date(o, "savedAt");
  const auto kind = o["kind"].toString();
  const auto ref = o["referenceID"].toString();
  if (kind == "view") {
    check(o["camera"].isObject() && Camera::read(o["camera"].toObject()).has_value(),
          "A saved camera is invalid.");
    check(missing(o["site"]) && missing(o["record"]) && missing(o["annotation"]),
          "A view contains conflicting geometry.");
  } else if (kind == "site") {
    check(o["site"].isObject(), "A saved site is missing.");
    const auto s = o["site"].toObject();
    check(identity(s["id"]) == identity(ref), "A site reference is inconsistent.");
    text(s, "name", 120, true);
    text(s, "kind", 80);
    date(s, "createdAt");
    bounds(s["region"], true);
  } else if (kind == "record") {
    check(o["record"].isObject(), "A saved source record is missing.");
    const auto r = o["record"].toObject();
    text(r, "id", 512, true);
    text(r, "label", 1024, true);
    text(r, "source", 1024);
    check(ref == r["id"].toString(), "A source reference is inconsistent.");
    const QSet<QString> feeds{"earthquakes", "flights",       "military", "atmosphere",
                              "disasters",   "installations", "energy",   "satellites",
                              "launches",    "vessels",       "fires",    "transit",
                              "cameras",     "datacenters",   "dams",     "cables",
                              "sentinel",    "annotations",   "traffic"};
    check(feeds.contains(r["feed"].toString()), "A saved source type is unsupported.");
    coordinate(r["coordinate"]);
    date(r, "observedAt", true);
    if (!missing(r["altitude"]))
      check(r["altitude"].isDouble() && std::isfinite(r["altitude"].toDouble()),
            "An altitude is invalid.");
    check(r["details"].isObject() && r["details"].toObject().size() <= 100,
          "Source details are invalid.");
    const auto details = r["details"].toObject();
    for (auto it = details.begin(); it != details.end(); ++it) {
      check(it.key().size() <= 256 && it.value().isString() && it.value().toString().size() <= 8192,
            "Source details exceed their limits.");
    }
    path(r["footprint"], 2000);
    check(r["paths"].isArray() && r["paths"].toArray().size() <= 64, "Source paths are invalid.");
    int total = 0;
    for (const auto p : r["paths"].toArray()) {
      path(p, 2000);
      total += int(p.toArray().size());
    }
    check(total <= 20000, "Source paths exceed their total limit.");
  } else if (kind == "annotation") {
    const auto a = o["annotation"].toObject();
    check(identity(a["id"]) == identity(ref), "An annotation reference is inconsistent.");
    text(a, "title", 256, true);
    date(a, "createdAt");
    path(a["points"], 2000);
    check(a["closed"].isBool() && !a["points"].toArray().isEmpty() &&
              (!a["closed"].toBool() || a["points"].toArray().size() >= 3),
          "An annotation is invalid.");
  } else if (kind == "optical" || kind == "radar") {
    identity(ref);
    check(missing(o["camera"]) && missing(o["site"]) && missing(o["record"]) &&
              missing(o["annotation"]),
          "An image link contains conflicting geometry.");
  } else
    check(false, "An evidence type is unsupported.");
  if (!missing(o["acquisition"])) {
    check((kind == "optical" || kind == "radar") && o["acquisition"].isObject(),
          "An acquisition is attached to invalid evidence.");
    const auto a = o["acquisition"].toObject();
    text(a, "sourceID", 512, true);
    text(a, "sensor", 120, true);
    date(a, "acquiredAt", true);
    if (!missing(a["bounds"]))
      bounds(a["bounds"]);
    if (!missing(a["displayBand"]))
      text(a, "displayBand", 80, true);
    if (!missing(a["resolutionMetres"]))
      check(a["resolutionMetres"].isDouble() && std::isfinite(a["resolutionMetres"].toDouble()) &&
                a["resolutionMetres"].toDouble() > 0,
            "An acquisition resolution is invalid.");
  }
}
QByteArray readFile(const QString &filename) {
  QFile f(filename);
  check(f.open(QIODevice::ReadOnly), "The mission file could not be opened.");
  check(f.size() <= maximumBytes, "The mission file exceeds 8 MB.");
  const auto data = f.read(maximumBytes + 1);
  check(data.size() <= maximumBytes && f.error() == QFileDevice::NoError,
        "The mission file could not be read safely.");
  return data;
}
QJsonArray decode(const QByteArray &data) {
  QJsonParseError error{};
  const auto document = QJsonDocument::fromJson(data, &error);
  check(error.error == QJsonParseError::NoError && document.isArray(),
        "The file is not a supported mission library.");
  auto plans = document.array();
  validateMissions(plans);
  return plans;
}
QByteArray hash(const QByteArray &data) {
  return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}
} // namespace

double foundationNow() {
  return QDateTime::currentMSecsSinceEpoch() / 1000.0 - foundationEpoch;
}
QString newID() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
}
QString dateLabel(const QJsonValue &value) {
  QDateTime date;
  if (value.isDouble() && std::isfinite(value.toDouble()) && std::abs(value.toDouble()) < 1e11)
    date = QDateTime::fromMSecsSinceEpoch(qRound64((value.toDouble() + foundationEpoch) * 1000),
                                          QTimeZone::UTC);
  else if (value.isString())
    date = QDateTime::fromString(value.toString(), Qt::ISODateWithMs).toUTC();
  return date.isValid() ? date.toString("yyyy-MM-dd HH:mm:ss 'UTC'") : "Time unavailable";
}
void validateMissions(const QJsonArray &plans) {
  check(plans.size() <= 200, "The mission library exceeds 200 plans.");
  QSet<QString> ids;
  for (const auto v : plans) {
    check(v.isObject(), "A mission has an invalid type.");
    const auto p = v.toObject();
    const auto id = identity(p["id"]);
    check(!ids.contains(id), "The library contains duplicate missions.");
    ids.insert(id);
    text(p, "name", 120, true);
    text(p, "objective", 4000);
    text(p, "notes", 32000);
    date(p, "createdAt");
    date(p, "updatedAt");
    const auto evidence = arrayIDs(p, "items", 500);
    for (const auto item : p["items"].toArray())
      validateItem(item.toObject());
    if (!missing(p["comparison"])) {
      const auto c = p["comparison"].toObject();
      check(c["blend"].isDouble() && c["blend"].toDouble() >= 0 && c["blend"].toDouble() <= 1,
            "A comparison blend is invalid.");
      for (const auto key : {"baseID", "topID"}) {
        check(c[key].isString(), "A comparison reference is invalid.");
        const auto ref = c[key].toString();
        if (ref.isEmpty())
          continue;
        const auto parts = ref.split(':');
        check(parts.size() >= 2, "A comparison reference is invalid.");
        identity(parts[1]);
        check((parts[0] == "optical" && parts.size() == 2) ||
                  (parts[0] == "radar" && parts.size() == 3 &&
                   QStringList{"VV", "VH", "Composite"}.contains(parts[2])),
              "A comparison source is invalid.");
      }
    }
    if (missing(p["brief"]))
      continue;
    check(p["brief"].isObject(), "A mission brief is invalid.");
    const auto b = p["brief"].toObject();
    check(b["version"].toInt(-1) == 1, "This mission brief version is unsupported.");
    const auto areas = arrayIDs(b, "areas", 100);
    arrayIDs(b, "observations", 500);
    arrayIDs(b, "notes", 200);
    arrayIDs(b, "tasks", 200);
    arrayIDs(b, "monitors", 50);
    arrayIDs(b, "activity", 200);
    for (const auto v : b["areas"].toArray()) {
      const auto a = v.toObject();
      text(a, "name", 160, true);
      text(a, "purpose", 2000);
      bounds(a["region"], true);
    }
    for (const auto v : b["notes"].toArray()) {
      const auto n = v.toObject();
      text(n, "title", 240, true);
      text(n, "body", 8000);
      text(n, "reference", 2000);
      date(n, "createdAt");
      linked(n, "areaID", areas);
      linked(n, "evidenceID", evidence);
    }
    for (const auto v : b["tasks"].toArray()) {
      const auto t = v.toObject();
      text(t, "title", 240, true);
      check(t["done"].isBool(), "A task state is invalid.");
      date(t, "due", true);
      linked(t, "areaID", areas);
    }
    for (const auto v : b["observations"].toArray()) {
      const auto o = v.toObject();
      text(o, "title", 240, true);
      text(o, "category", 120, true);
      text(o, "note", 4000);
      text(o, "method", 500, true);
      coordinate(o["coordinate"]);
      date(o, "createdAt");
      date(o, "observedAt", true);
      linked(o, "areaID", areas);
      linked(o, "evidenceID", evidence);
      check(QStringList{"Unreviewed", "Corroborated", "Uncertain", "Dismissed"}.contains(
                o["state"].toString()),
            "An observation review state is invalid.");
      if (!missing(o["bounds"]))
        bounds(o["bounds"]);
      if (!missing(o["modelScore"]))
        check(o["modelScore"].isDouble() && o["modelScore"].toDouble() >= 0 &&
                  o["modelScore"].toDouble() <= 1,
              "A model score is invalid.");
      if (!missing(o["modelName"]))
        text(o, "modelName", 200, true);
      if (!missing(o["inputTransform"]))
        text(o, "inputTransform", 200);
      validateModelProvenance(o);
    }
    for (const auto v : b["monitors"].toArray()) {
      const auto m = v.toObject();
      linked(m, "areaID", areas);
      const auto kind = m["kind"].toString();
      check(QStringList{"New Sentinel-2 imagery", "New Sentinel-1 acquisitions",
                        "Evidence availability"}
                    .contains(kind) &&
                (kind == "Evidence availability" || !missing(m["areaID"])),
            "A monitor type or area is invalid.");
      check(m["enabled"].isBool(), "A monitor state is invalid.");
      for (const auto key : {"intervalHours", "lookbackDays"}) {
        const auto n = m[key];
        const int maximum = QString(key) == "intervalHours" ? 168 : 90;
        check(n.isDouble() && n.toDouble() == n.toInt(-1) && n.toInt() >= 1 && n.toInt() <= maximum,
              "A monitor interval is invalid.");
      }
      check(m["maximumCloud"].isDouble() && m["maximumCloud"].toDouble() >= 0 &&
                m["maximumCloud"].toDouble() <= 100,
            "A monitor cloud limit is invalid.");
      check(m["seenIDs"].isArray() && m["seenIDs"].toArray().size() <= 2000,
            "A monitor checkpoint is invalid.");
      for (const auto id : m["seenIDs"].toArray())
        check(id.isString() && !id.toString().isEmpty() && id.toString().size() <= 200,
              "A monitor checkpoint identifier is invalid.");
      text(m, "summary", 2000);
      if (!missing(m["failure"]))
        text(m, "failure", 2000);
      date(m, "lastAttempt", true);
      date(m, "lastSuccess", true);
    }
    for (const auto v : b["activity"].toArray()) {
      const auto a = v.toObject();
      text(a, "message", 2000);
      date(a, "time");
    }
  }
}
Missions::Missions(QObject *parent) : QObject(parent) {}
QJsonObject Missions::selected() const {
  for (auto p : plans_)
    if (p.toObject()["id"].toString() == selectedID_)
      return p.toObject();
  return {};
}
void Missions::load(const QString &filename) {
  const auto bytes = readFile(filename);
  const auto plans = decode(bytes);
  plans_ = plans;
  filename_ = QFileInfo(filename).absoluteFilePath();
  originalHash_ = hash(bytes);
  selectedID_ = plans.isEmpty() ? QString() : plans.first().toObject()["id"].toString();
  dirty_ = false;
  emit changed();
}
void Missions::save(const QString &filename) {
  validateMissions(plans_);
  const auto bytes = QJsonDocument(plans_).toJson(QJsonDocument::Indented);
  check(bytes.size() <= maximumBytes, "The mission library exceeds 8 MB.");
  const auto path = QFileInfo(filename).absoluteFilePath();
  check(QDir().mkpath(QFileInfo(path).absolutePath()),
        "The destination folder could not be created.");
  QLockFile lock(path + ".lock");
  check(lock.tryLock(0), "This mission library is being saved by another process.");
  if (QFile::exists(path)) {
    const auto previous = readFile(path);
    decode(previous);
    if (path == filename_)
      check(hash(previous) == originalHash_,
            "This library changed on disk. Save a separate copy or reopen it before replacing it.");
    QSaveFile backup(path + ".backup");
    backup.setDirectWriteFallback(false);
    check(!QFileInfo(path + ".backup").isSymLink() && backup.open(QIODevice::WriteOnly) &&
              backup.write(previous) == previous.size() && backup.commit(),
          "The last-good mission backup could not be saved. The mission library was preserved.");
  } else if (path == filename_ && !originalHash_.isEmpty()) {
    check(false, "The original library was removed. Save a separate copy.");
  }
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
        "The mission library could not be saved atomically.");
  filename_ = path;
  originalHash_ = hash(bytes);
  dirty_ = false;
  emit changed();
}
void Missions::select(const QString &id) {
  for (const auto v : plans_)
    if (v.toObject()["id"].toString() == id) {
      selectedID_ = id;
      emit changed();
      return;
    }
}
QString Missions::create(const QString &name) {
  QJsonObject p{
      {"id", newID()},        {"name", name.trimmed()},       {"objective", ""},
      {"notes", ""},          {"createdAt", foundationNow()}, {"updatedAt", foundationNow()},
      {"items", QJsonArray{}}};
  auto plans = plans_;
  plans.append(p);
  validateMissions(plans);
  plans_ = plans;
  selectedID_ = p["id"].toString();
  dirty_ = true;
  emit changed();
  return selectedID_;
}
void Missions::removeMission(const QString &id) {
  auto next = plans_;
  bool found = false;
  for (qsizetype i = next.size(); i-- > 0;)
    if (next[i].toObject()["id"] == id) {
      next.removeAt(i);
      found = true;
    }
  check(found, "The mission no longer exists.");
  validateMissions(next);
  plans_ = next;
  if (selectedID_ == id)
    selectedID_ = next.isEmpty() ? QString{} : next.first().toObject()["id"].toString();
  dirty_ = true;
  emit changed();
}
void Missions::addObservationProposal(const QString &missionID, const QJsonObject &observation) {
  check(missionID == selectedID_ && !filename_.isEmpty(),
        "Select and save the source mission before adding a model proposal.");
  check(observation["state"] == "Unreviewed" &&
            QJsonDocument(observation).toJson(QJsonDocument::Compact).size() <= 32768,
        "A local model proposal is invalid or exceeds its size limit.");
  validateModelProvenance(observation);
  check(validSHA256(observation["inputSHA256"]) && !missing(observation["modelBackend"]),
        "Model or source provenance is missing from this proposal.");
  auto plan = selected();
  QJsonObject evidence;
  for (const auto value : plan["items"].toArray())
    if (value.toObject()["id"] == observation["evidenceID"])
      evidence = value.toObject();
  const auto source = Bounds::read(evidence["acquisition"].toObject()["bounds"]),
             box = Bounds::read(observation["bounds"]);
  check(evidence["kind"] == "optical" && source && box && box->west >= source->west - 1e-8 &&
            box->east <= source->east + 1e-8 && box->south >= source->south - 1e-8 &&
            box->north <= source->north + 1e-8,
        "The proposal's optical evidence link is missing or its bounds exceed the retained source "
        "image.");
  auto b = brief(plan);
  auto objects = b["observations"].toArray();
  objects.append(observation);
  b["observations"] = objects;
  activity(b, "Added unreviewed model proposal: " + observation["title"].toString());
  plan["brief"] = b;
  plan["updatedAt"] = foundationNow();
  auto next = plans_;
  for (qsizetype i = 0; i < next.size(); ++i)
    if (next[i].toObject()["id"] == missionID)
      next[i] = plan;
  validateMissions(next);
  const auto previous = plans_;
  const bool dirty = dirty_;
  plans_ = next;
  try {
    save(filename_);
  } catch (...) {
    plans_ = previous;
    dirty_ = dirty;
    throw;
  }
}
void Missions::editEntry(const QString &collection, const QString &id, const QJsonObject &fields) {
  const QMap<QString, QStringList> allowed{
      {"areas", {"name", "purpose", "region"}},
      {"notes", {"title", "body", "reference", "evidenceID", "areaID"}},
      {"tasks", {"title", "done", "due", "areaID"}},
      {"observations",
       {"title", "category", "coordinate", "bounds", "evidenceID", "areaID", "note", "state",
        "observedAt"}},
      {"monitors", {"kind", "areaID", "enabled", "intervalHours", "lookbackDays", "maximumCloud"}},
      {"items", {"title"}}};
  check(allowed.contains(collection), "This collection cannot be edited.");
  for (auto field = fields.begin(); field != fields.end(); ++field)
    check(allowed[collection].contains(field.key()), "This field cannot be edited.");
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto array = collection == "items" ? p["items"].toArray() : b[collection].toArray();
    bool found = false;
    auto reset = [](QJsonObject &m) {
      m.remove("lastAttempt");
      m.remove("lastSuccess");
      m.remove("failure");
      m["seenIDs"] = QJsonArray{};
      m["summary"] = "Query changed; the next check establishes a new baseline.";
    };
    for (qsizetype i = 0; i < array.size(); ++i) {
      auto o = array[i].toObject();
      if (o["id"] != id)
        continue;
      found = true;
      const auto previous = o;
      for (auto field = fields.begin(); field != fields.end(); ++field) {
        if (field.value().isNull())
          o.remove(field.key());
        else
          o[field.key()] = field.value();
      }
      if (collection == "areas" && previous["region"] != o["region"]) {
        auto monitors = b["monitors"].toArray();
        for (qsizetype j = 0; j < monitors.size(); ++j) {
          auto m = monitors[j].toObject();
          if (m["areaID"] == id) {
            reset(m);
            monitors[j] = m;
          }
        }
        b["monitors"] = monitors;
      }
      if (collection == "monitors")
        for (const auto &key : {"kind", "areaID", "lookbackDays", "maximumCloud"})
          if (previous[key] != o[key]) {
            reset(o);
            break;
          }
      array[i] = o;
    }
    check(found, "The selected entry no longer exists.");
    if (collection == "items")
      p["items"] = array;
    else
      b[collection] = array;
    activity(b, "Updated " + collection + " entry");
    p["brief"] = b;
  });
}
void Missions::setComparison(const QString &baseline, const QString &later, double blend) {
  update([&](QJsonObject &p) {
    p["comparison"] = QJsonObject{{"baseID", baseline}, {"topID", later}, {"blend", blend}};
  });
}
void Missions::captureAcquisitions(const QMap<QString, QJsonObject> &snapshots) {
  update([&](QJsonObject &p) {
    auto items = p["items"].toArray();
    int count = 0;
    for (qsizetype i = 0; i < items.size(); ++i) {
      auto item = items[i].toObject();
      const auto id = item["id"].toString();
      if (!item["acquisition"].isObject() && snapshots.contains(id)) {
        item["acquisition"] = snapshots[id];
        items[i] = item;
        ++count;
      }
    }
    p["items"] = items;
    auto b = brief(p);
    activity(b, QString("Retained acquisition metadata for %1 evidence links").arg(count));
    p["brief"] = b;
  });
}
QString Missions::addMonitor(const QJsonObject &fields) {
  const auto id = newID();
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto values = b["monitors"].toArray();
    QJsonObject m{{"id", id},
                  {"kind", "Evidence availability"},
                  {"enabled", false},
                  {"intervalHours", 24},
                  {"lookbackDays", 14},
                  {"maximumCloud", 30},
                  {"seenIDs", QJsonArray{}},
                  {"summary", "Not run"}};
    for (const auto &key :
         {"kind", "areaID", "enabled", "intervalHours", "lookbackDays", "maximumCloud"})
      if (fields.contains(key))
        m[key] = fields[key];
    values.append(m);
    b["monitors"] = values;
    activity(b, "Created monitor: " + m["kind"].toString());
    p["brief"] = b;
  });
  return id;
}
void Missions::commitMonitor(const QString &missionID, const QString &monitorID,
                             const QJsonObject &checkpoint, const QString &event) {
  check(!filename_.isEmpty(), "Save the mission library before running monitors.");
  auto next = plans_;
  bool found = false;
  for (qsizetype i = 0; i < next.size(); ++i) {
    auto p = next[i].toObject();
    if (p["id"] != missionID)
      continue;
    auto b = brief(p);
    auto monitors = b["monitors"].toArray();
    for (qsizetype j = 0; j < monitors.size(); ++j) {
      auto m = monitors[j].toObject();
      if (m["id"] != monitorID)
        continue;
      found = true;
      for (const auto &key : {"lastAttempt", "lastSuccess", "seenIDs", "summary", "failure"})
        if (checkpoint.contains(key)) {
          if (checkpoint[key].isNull())
            m.remove(key);
          else
            m[key] = checkpoint[key];
        }
      monitors[j] = m;
    }
    b["monitors"] = monitors;
    if (!event.isEmpty())
      activity(b, event);
    p["brief"] = b;
    p["updatedAt"] = foundationNow();
    next[i] = p;
  }
  check(found, "The mission monitor no longer exists.");
  validateMissions(next);
  const auto previous = plans_;
  const bool dirty = dirty_;
  plans_ = next;
  try {
    save(filename_);
  } catch (...) {
    plans_ = previous;
    dirty_ = dirty;
    throw;
  }
}
QJsonObject Missions::brief(QJsonObject p) {
  if (p["brief"].isObject())
    return p["brief"].toObject();
  return {{"version", 1},
          {"areas", QJsonArray{}},
          {"observations", QJsonArray{}},
          {"notes", QJsonArray{}},
          {"tasks", QJsonArray{}},
          {"monitors", QJsonArray{}},
          {"activity", QJsonArray{}}};
}
void Missions::activity(QJsonObject &b, const QString &message) {
  auto a = b["activity"].toArray();
  a.prepend(QJsonObject{{"id", newID()}, {"time", foundationNow()}, {"message", message}});
  while (a.size() > 200)
    a.removeLast();
  b["activity"] = a;
}
void Missions::update(const std::function<void(QJsonObject &)> &mutation) {
  auto plans = plans_;
  for (qsizetype i = 0; i < plans.size(); ++i) {
    auto p = plans[i].toObject();
    if (p["id"].toString() != selectedID_)
      continue;
    mutation(p);
    p["updatedAt"] = foundationNow();
    plans[i] = p;
    validateMissions(plans);
    plans_ = plans;
    dirty_ = true;
    emit changed();
    return;
  }
  throw std::runtime_error("Choose or create a mission first.");
}
void Missions::setField(const QString &field, const QString &value) {
  check(QStringList{"name", "objective", "notes"}.contains(field),
        "This mission field cannot be edited.");
  if (selected()[field].toString() == value)
    return;
  update([&](QJsonObject &p) { p[field] = value; });
}
void Missions::setBrief(const QString &name, const QString &objective, const QString &notes) {
  update([&](QJsonObject &p) {
    p["name"] = name.trimmed();
    p["objective"] = objective;
    p["notes"] = notes;
  });
}
void Missions::addNote(const QString &title, const QString &body, const QString &reference,
                       const QString &evidenceID, const QString &areaID) {
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto a = b["notes"].toArray();
    QJsonObject n{{"id", newID()},
                  {"title", title.trimmed()},
                  {"body", body},
                  {"reference", reference},
                  {"createdAt", foundationNow()}};
    if (!evidenceID.isEmpty())
      n["evidenceID"] = evidenceID;
    if (!areaID.isEmpty())
      n["areaID"] = areaID;
    a.append(n);
    b["notes"] = a;
    activity(b, "Added review note: " + title);
    p["brief"] = b;
  });
}
void Missions::addTask(const QString &title, const QString &areaID) {
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto a = b["tasks"].toArray();
    QJsonObject t{{"id", newID()}, {"title", title.trimmed()}, {"done", false}};
    if (!areaID.isEmpty())
      t["areaID"] = areaID;
    a.append(t);
    b["tasks"] = a;
    activity(b, "Added task: " + title);
    p["brief"] = b;
  });
}
void Missions::setTaskDone(const QString &id, bool done) {
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto a = b["tasks"].toArray();
    bool found = false;
    for (qsizetype i = 0; i < a.size(); ++i) {
      auto t = a[i].toObject();
      if (t["id"].toString() == id) {
        t["done"] = done;
        a[i] = t;
        found = true;
      }
    }
    check(found, "The task no longer exists.");
    b["tasks"] = a;
    activity(b, done ? "Completed task" : "Reopened task");
    p["brief"] = b;
  });
}
void Missions::addArea(const QString &name, const QString &purpose, Bounds bounds) {
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto a = b["areas"].toArray();
    a.append(QJsonObject{{"id", newID()},
                         {"name", name.trimmed()},
                         {"purpose", purpose},
                         {"region", bounds.json()}});
    b["areas"] = a;
    activity(b, "Added mission area: " + name);
    p["brief"] = b;
  });
}
void Missions::addView(const QString &title, const Camera &camera) {
  addEvidence({{"id", newID()},
               {"kind", "view"},
               {"referenceID", newID()},
               {"title", title.trimmed()},
               {"savedAt", foundationNow()},
               {"camera", camera.json()}});
}
void Missions::addEvidence(QJsonObject item) {
  update([&](QJsonObject &p) {
    auto a = p["items"].toArray();
    a.append(item);
    p["items"] = a;
    auto b = brief(p);
    activity(b, "Linked evidence: " + item["title"].toString());
    p["brief"] = b;
  });
}
void Missions::removeEntry(const QString &collection, const QString &id) {
  check(QStringList{"items", "areas", "notes", "tasks", "observations", "monitors"}.contains(
            collection),
        "This collection cannot be edited.");
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto a = collection == "items" ? p["items"].toArray() : b[collection].toArray();
    bool found = false;
    for (qsizetype i = a.size(); i-- > 0;)
      if (a[i].toObject()["id"].toString() == id) {
        a.removeAt(i);
        found = true;
      }
    check(found, "The selected entry no longer exists.");
    if (collection == "items")
      p["items"] = a;
    else
      b[collection] = a;
    if (collection == "items" || collection == "areas") {
      const QString key = collection == "items" ? "evidenceID" : "areaID";
      for (const QString child : {"notes", "observations", "tasks", "monitors"}) {
        auto children = b[child].toArray();
        for (qsizetype i = children.size(); i-- > 0;) {
          auto c = children[i].toObject();
          if (c[key].toString() != id)
            continue;
          if (child == "monitors" && key == "areaID")
            children.removeAt(i);
          else {
            c.remove(key);
            children[i] = c;
          }
        }
        b[child] = children;
      }
    }
    activity(b, "Removed " + collection + " entry");
    p["brief"] = b;
  });
}
void Missions::setObservationReview(const QString &id, const QString &state, const QString &note) {
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto a = b["observations"].toArray();
    bool found = false;
    for (qsizetype i = 0; i < a.size(); ++i) {
      auto o = a[i].toObject();
      if (o["id"].toString() == id) {
        o["state"] = state;
        o["note"] = note;
        a[i] = o;
        found = true;
      }
    }
    check(found, "The observation no longer exists.");
    b["observations"] = a;
    activity(b, "Reviewed observation: " + state);
    p["brief"] = b;
  });
}
void Missions::addObservation(const QString &title, const QString &category, Coordinate coordinate,
                              const QString &evidenceID) {
  update([&](QJsonObject &p) {
    auto b = brief(p);
    auto a = b["observations"].toArray();
    QJsonObject o{{"id", newID()},
                  {"title", title.trimmed()},
                  {"category", category.trimmed()},
                  {"coordinate", coordinate.json()},
                  {"note", ""},
                  {"state", "Unreviewed"},
                  {"method", "Analyst identification"},
                  {"createdAt", foundationNow()}};
    if (!evidenceID.isEmpty())
      o["evidenceID"] = evidenceID;
    a.append(o);
    b["observations"] = a;
    activity(b, "Added analyst observation: " + title);
    p["brief"] = b;
  });
}
} // namespace godeye
