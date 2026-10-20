#include "Investigations.h"
#include "Dossiers.h"
#include "Missions.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
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
QStringList strings(const QJsonArray &a) {
  QStringList r;
  for (const auto &v : a)
    r << v.toString();
  return r;
}
QJsonArray array(const QStringList &s) {
  return QJsonArray::fromStringList(s);
}
void validID(const QJsonValue &v) {
  static const QRegularExpression pattern("^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$");
  require(v.isString() && pattern.match(v.toString()).hasMatch(),
          "Invalid investigation identity.");
}
void text(const QJsonObject &o, const char *key, int max, bool required = false) {
  require(o[key].isString() && o[key].toString().size() <= max &&
              (!required || !o[key].toString().trimmed().isEmpty()),
          "Invalid or oversized investigation text.");
}
void number(const QJsonValue &v) {
  require(v.isDouble() && std::isfinite(v.toDouble()), "Invalid investigation date.");
}
void validRevision(const QJsonObject &o) {
  require(o["revision"].isDouble() && o["revision"].toDouble() == o["revision"].toInt() &&
              o["revision"].toInt() >= 1 && o["revision"].toInt() <= 1000000,
          "Invalid record revision.");
}
QSet<QString> ids(const QJsonValue &v, int max) {
  require(v.isArray() && v.toArray().size() <= max,
          "Investigation collection exceeds its limit or is invalid.");
  QSet<QString> result;
  for (const auto &value : v.toArray()) {
    require(value.isObject(), "Invalid investigation record.");
    validID(value.toObject()["id"]);
    const auto key = value.toObject()["id"].toString().toUpper();
    require(!result.contains(key), "Duplicate investigation identity.");
    result.insert(key);
  }
  return result;
}
void links(const QJsonValue &v, int max, const QSet<QString> &allowed) {
  require(v.isArray() && v.toArray().size() <= max, "Invalid investigation links.");
  QSet<QString> seen;
  for (const auto &ref : v.toArray()) {
    validID(ref);
    const auto key = ref.toString().toUpper();
    require(allowed.contains(key) && !seen.contains(key),
            "Missing or duplicate investigation link.");
    seen.insert(key);
  }
}
void citation(const QJsonObject &o) {
  validID(o["id"]);
  text(o, "label", 1024, true);
  text(o, "sourceID", 1024, true);
  text(o, "provider", 512);
  text(o, "reference", 4096);
  text(o, "qualityLimitations", 16000);
  text(o, "fingerprint", 256);
  number(o["capturedAt"]);
  require(o["sourceDate"].isNull() || o["sourceDate"].isUndefined() || o["sourceDate"].isString() ||
              (o["sourceDate"].isDouble() && std::isfinite(o["sourceDate"].toDouble())),
          "Invalid source date.");
  require(o["derivationReferences"].isArray() && o["derivationReferences"].toArray().size() <= 100,
          "Invalid derivation references.");
  for (const auto &ref : o["derivationReferences"].toArray())
    require(ref.isString() && ref.toString().size() <= 4096, "Invalid derivation reference.");
  if (o["sourceDate"].isString())
    require(dateLabel(o["sourceDate"]) != "Time unavailable", "Invalid citation source date.");
  if (o.contains("evidence"))
    require(o["evidence"].isObject(), "Invalid evidence snapshot.");
  if (o.contains("origin"))
    require(o["origin"].isObject(), "Invalid origin snapshot.");
  require(QJsonDocument(o).toJson(QJsonDocument::Compact).size() <= 128 * 1024,
          "Citation snapshot exceeds 128 KB.");
}
void validateCitations(const QJsonValue &v) {
  ids(v, 500);
  for (const auto &c : v.toArray())
    citation(c.toObject());
}
void review(const QJsonObject &o) {
  require(QStringList{"Unreviewed", "Supported", "Uncertain", "Dismissed"}.contains(
              o["state"].toString()),
          "Invalid finding review state.");
  text(o, "rationale", 16000);
  bool support = false;
  for (const auto &v : o["citations"].toArray())
    if (v.toObject()["role"] == "Supporting")
      support = true;
  require(o["state"] != "Supported" || (support && !o["rationale"].toString().trimmed().isEmpty()),
          "A Supported finding requires supporting evidence and an analyst rationale.");
  require(o["state"] == "Unreviewed" || !o["rationale"].toString().trimmed().isEmpty(),
          "A reviewed finding requires an analyst rationale.");
}
void findingBody(const QJsonObject &f, const QSet<QString> &snapshots, const QSet<QString> &cites) {
  text(f, "claim", 16000, true);
  text(f, "timeContext", 4000);
  text(f, "alternatives", 16000);
  links(f["assetSnapshotIDs"], 100, snapshots);
  // Area identities may outlive the live area: retained as historical context.
  require(f["areaIDs"].isArray() && f["areaIDs"].toArray().size() <= 100, "Invalid finding areas.");
  for (auto v : f["areaIDs"].toArray())
    validID(v);
  require(f["citations"].isArray() && f["citations"].toArray().size() <= 500,
          "Too many finding citations.");
  QSet<QString> seen;
  for (auto v : f["citations"].toArray()) {
    const auto c = v.toObject();
    validID(c["citationID"]);
    require(
        cites.contains(c["citationID"].toString().toUpper()) &&
            QStringList{"Supporting", "Contradicting", "Context"}.contains(c["role"].toString()) &&
            !seen.contains(c["citationID"].toString().toUpper()),
        "Invalid finding citation link.");
    seen.insert(c["citationID"].toString().toUpper());
  }
  if (f.contains("areaSnapshots")) {
    ids(f["areaSnapshots"], 100);
    for (auto v : f["areaSnapshots"].toArray()) {
      const auto a = v.toObject();
      text(a, "name", 160, true);
      text(a, "purpose", 2000);
      const auto b = Bounds::read(a["region"]);
      require(b && b->validArea(), "Invalid retained finding area.");
      require(f["areaIDs"].toArray().contains(a["id"]),
              "A retained finding area has no corresponding identity.");
    }
  }
  review(f);
}
QJsonObject find(const QJsonArray &rows, const QString &key) {
  for (auto v : rows)
    if (v.toObject()["id"].toString().compare(key, Qt::CaseInsensitive) == 0)
      return v.toObject();
  return {};
}
} // namespace
QJsonObject Asset::json() const {
  QJsonObject o{{"id", id},
                {"name", name},
                {"category", category},
                {"aliases", array(aliases)},
                {"identifiers", identifiers},
                {"citations", citations},
                {"revision", revision},
                {"archived", archived}};
  if (location)
    o["location"] = location->json();
  if (boundary)
    o["boundary"] = boundary->json();
  return o;
}
Asset Asset::read(const QJsonObject &o) {
  validID(o["id"]);
  text(o, "name", 240, true);
  text(o, "category", 120, true);
  validRevision(o);
  require(o["archived"].isBool(), "Invalid asset archive state.");
  require(o["aliases"].isArray() && o["aliases"].toArray().size() <= 50, "Invalid asset aliases.");
  for (auto v : o["aliases"].toArray())
    require(v.isString() && v.toString().size() <= 240, "Invalid asset alias.");
  require(o["identifiers"].isObject() && o["identifiers"].toObject().size() <= 50,
          "Invalid provider identifiers.");
  const auto identifiers = o["identifiers"].toObject();
  for (auto it = identifiers.begin(); it != identifiers.end(); ++it)
    require(!it.key().trimmed().isEmpty() && it.key().size() <= 512 && it.value().isString() &&
                !it.value().toString().trimmed().isEmpty() && it.value().toString().size() <= 1024,
            "Invalid qualified identifier.");
  Asset a;
  a.id = o["id"].toString();
  a.name = o["name"].toString();
  a.category = o["category"].toString();
  a.aliases = strings(o["aliases"].toArray());
  a.identifiers = identifiers;
  a.citations = o["citations"].toArray();
  a.revision = o["revision"].toInt();
  a.archived = o["archived"].toBool();
  if (o.contains("location")) {
    a.location = Coordinate::read(o["location"].toObject());
    require(a.location.has_value(), "Invalid asset location.");
  }
  if (o.contains("boundary")) {
    a.boundary = Bounds::read(o["boundary"]);
    require(a.boundary && a.boundary->validArea(), "Invalid asset boundary.");
  }
  require(a.location || a.boundary, "An asset needs a location or boundary.");
  validateCitations(o["citations"]);
  return a;
}
QJsonObject AssetRelationship::json() const {
  return {{"id", id},
          {"fromAssetID", fromAssetID},
          {"toAssetID", toAssetID},
          {"kind", kind},
          {"reviewState", reviewState},
          {"rationale", rationale},
          {"validFrom", validFrom},
          {"validThrough", validThrough},
          {"citations", citations},
          {"revision", revision},
          {"archived", archived}};
}
AssetRelationship AssetRelationship::read(const QJsonObject &o) {
  validID(o["id"]);
  validID(o["fromAssetID"]);
  validID(o["toAssetID"]);
  validRevision(o);
  require(o["fromAssetID"].toString().compare(o["toAssetID"].toString(), Qt::CaseInsensitive) != 0,
          "An asset cannot relate to itself.");
  require(QStringList{"Part of", "Connected to", "Depends on"}.contains(o["kind"].toString()),
          "Invalid relationship kind.");
  require(QStringList{"Unreviewed", "Supported", "Uncertain", "Dismissed"}.contains(
              o["reviewState"].toString()),
          "Invalid relationship review.");
  text(o, "rationale", 16000);
  text(o, "validFrom", 10);
  text(o, "validThrough", 10);
  for (auto k : {"validFrom", "validThrough"})
    require(o[k].toString().isEmpty() || QDate::fromString(o[k].toString(), Qt::ISODate).isValid(),
            "Invalid relationship validity date.");
  require(o["validFrom"].toString().isEmpty() || o["validThrough"].toString().isEmpty() ||
              o["validFrom"].toString() <= o["validThrough"].toString(),
          "Relationship dates are reversed.");
  require(o["archived"].isBool(), "Invalid relationship archive state.");
  validateCitations(o["citations"]);
  require(o["kind"] != "Depends on" || !o["citations"].toArray().isEmpty(),
          "Dependencies require explicitly supplied evidence.");
  require(o["reviewState"] == "Unreviewed" || (!o["citations"].toArray().isEmpty() &&
                                               !o["rationale"].toString().trimmed().isEmpty()),
          "Reviewed relationships require evidence and rationale.");
  AssetRelationship r;
  r.id = o["id"].toString();
  r.fromAssetID = o["fromAssetID"].toString();
  r.toAssetID = o["toAssetID"].toString();
  r.kind = o["kind"].toString();
  r.reviewState = o["reviewState"].toString();
  r.rationale = o["rationale"].toString();
  r.validFrom = o["validFrom"].toString();
  r.validThrough = o["validThrough"].toString();
  r.citations = o["citations"].toArray();
  r.revision = o["revision"].toInt();
  r.archived = o["archived"].toBool();
  return r;
}
QJsonObject Finding::json() const {
  return {{"id", id},
          {"claim", claim},
          {"timeContext", timeContext},
          {"alternatives", alternatives},
          {"rationale", rationale},
          {"state", state},
          {"assetSnapshotIDs", array(assetSnapshotIDs)},
          {"areaIDs", array(areaIDs)},
          {"citations", citations},
          {"revisions", revisions},
          {"areaSnapshots", areaSnapshots},
          {"origin", origin},
          {"revision", revision},
          {"createdAt", createdAt}};
}
Finding Finding::read(const QJsonObject &o) {
  Finding f;
  f.id = o["id"].toString();
  f.claim = o["claim"].toString();
  f.timeContext = o["timeContext"].toString();
  f.alternatives = o["alternatives"].toString();
  f.rationale = o["rationale"].toString();
  f.state = o["state"].toString();
  f.assetSnapshotIDs = strings(o["assetSnapshotIDs"].toArray());
  f.areaIDs = strings(o["areaIDs"].toArray());
  f.citations = o["citations"].toArray();
  f.revisions = o["revisions"].toArray();
  f.areaSnapshots = o["areaSnapshots"].toArray();
  f.origin = o["origin"].toObject();
  f.revision = o["revision"].toInt();
  f.createdAt = o["createdAt"].toDouble();
  return f;
}
QJsonObject InvestigationSnapshot::json() const {
  QJsonObject o{{"id", id}, {"capturedAt", capturedAt}};
  if (!areaID.isEmpty())
    o["areaID"] = areaID;
  if (asset)
    o["asset"] = asset->json();
  if (relationship)
    o["relationship"] = relationship->json();
  return o;
}
InvestigationSnapshot InvestigationSnapshot::read(const QJsonObject &o) {
  validID(o["id"]);
  number(o["capturedAt"]);
  InvestigationSnapshot s;
  s.id = o["id"].toString();
  s.capturedAt = o["capturedAt"].toDouble();
  s.areaID = o["areaID"].toString();
  if (!s.areaID.isEmpty())
    validID(s.areaID);
  require(o.contains("asset") != o.contains("relationship"),
          "A snapshot must contain one asset or relationship.");
  if (o.contains("asset"))
    s.asset = Asset::read(o["asset"].toObject());
  else
    s.relationship = AssetRelationship::read(o["relationship"].toObject());
  return s;
}
QJsonObject investigation(const QJsonObject &plan) {
  auto i = plan["brief"].toObject()["investigation"].toObject();
  if (!i.isEmpty())
    return i;
  return {{"version", 1},
          {"status", "Open"},
          {"completionCriteria", ""},
          {"conclusion", ""},
          {"unresolvedQuestions", ""},
          {"closureRationale", ""},
          {"snapshots", QJsonArray{}},
          {"citations", QJsonArray{}},
          {"findings", QJsonArray{}},
          {"associations", QJsonArray{}}};
}
QStringList outstandingInvestigationWork(const QJsonObject &plan) {
  QStringList r;
  for (auto v : plan["brief"].toObject()["tasks"].toArray())
    if (!v.toObject()["done"].toBool())
      r << "Unfinished task: " + v.toObject()["title"].toString();
  const auto i = investigation(plan);
  for (auto v : i["findings"].toArray())
    if (v.toObject()["state"] == "Unreviewed" || v.toObject()["state"] == "Uncertain")
      r << "Unresolved finding: " + v.toObject()["claim"].toString().left(160);
  if (!i["unresolvedQuestions"].toString().trimmed().isEmpty())
    r << "Unresolved questions remain";
  for (auto v : plan["brief"].toObject()["monitoringInbox"].toArray())
    if (!QStringList{"Closed", "Dismissed"}.contains(v.toObject()["reviewState"].toString()))
      r << "Open monitoring entry";
  return r;
}
void validateInvestigation(const QJsonObject &plan) {
  const auto b = plan["brief"].toObject();
  if (!b.contains("investigation")) {
    for (auto v : b["tasks"].toArray())
      require(v.toObject()["findingID"].isNull() || v.toObject()["findingID"].isUndefined(),
              "A task finding origin requires an investigation extension.");
    return;
  }
  require(b["investigation"].isObject(), "Invalid investigation extension.");
  const auto i = investigation(plan);
  require(i["version"] == 1, "Unsupported investigation version.");
  require(QStringList{"Open", "In review", "Closed"}.contains(i["status"].toString()),
          "Invalid investigation state.");
  for (auto k : {"completionCriteria", "conclusion", "unresolvedQuestions", "closureRationale"})
    text(i, k, 32000);
  const auto snapshotIDs = ids(i["snapshots"], 1000), citeIDs = ids(i["citations"], 1000),
             findingIDs = ids(i["findings"], 200);
  QSet<QString> assets, assetSnapshotIDs;
  for (auto v : i["snapshots"].toArray()) {
    const auto s = InvestigationSnapshot::read(v.toObject());
    if (s.asset) {
      assets.insert(s.asset->id.toUpper());
      assetSnapshotIDs.insert(s.id.toUpper());
    }
  }
  for (auto v : i["snapshots"].toArray()) {
    const auto s = InvestigationSnapshot::read(v.toObject());
    if (s.relationship)
      require(assets.contains(s.relationship->fromAssetID.toUpper()) &&
                  assets.contains(s.relationship->toAssetID.toUpper()),
              "Relationship snapshot endpoints are missing.");
  }
  for (auto v : i["citations"].toArray())
    citation(v.toObject());
  for (auto v : i["findings"].toArray()) {
    const auto f = v.toObject();
    validRevision(f);
    number(f["createdAt"]);
    findingBody(f, assetSnapshotIDs, citeIDs);
    require(f["origin"].isObject() &&
                QJsonDocument(f["origin"].toObject()).toJson().size() <= 128 * 1024,
            "Invalid finding origin.");
    require(f["revisions"].isArray() && f["revisions"].toArray().size() <= 100,
            "Finding history exceeds 100 revisions.");
    QSet<int> reviewedRevisions;
    for (auto r : f["revisions"].toArray()) {
      require(r.toObject()["id"] == f["id"] &&
                  r.toObject()["revision"].toInt() < f["revision"].toInt() &&
                  !reviewedRevisions.contains(r.toObject()["revision"].toInt()),
              "Finding review history has an inconsistent identity or revision.");
      reviewedRevisions.insert(r.toObject()["revision"].toInt());
      findingBody(r.toObject(), assetSnapshotIDs, citeIDs);
      validRevision(r.toObject());
      number(r.toObject()["recordedAt"]);
    }
  }
  ids(i["associations"], 1000);
  for (auto v : i["associations"].toArray()) {
    auto a = v.toObject();
    require(assetSnapshotIDs.contains(a["assetSnapshotID"].toString().toUpper()) &&
                citeIDs.contains(a["citationID"].toString().toUpper()),
            "Association references missing evidence or asset snapshot.");
    text(a, "reason", 4000, true);
    number(a["approvedAt"]);
  }
  for (auto v : b["tasks"].toArray())
    if (!v.toObject()["findingID"].isNull() && !v.toObject()["findingID"].isUndefined())
      require(findingIDs.contains(v.toObject()["findingID"].toString().toUpper()),
              "Task finding origin is missing.");
  require(i["status"] != "Closed" || outstandingInvestigationWork(plan).isEmpty() ||
              !i["closureRationale"].toString().trimmed().isEmpty(),
          "Closing with outstanding work requires an explicit rationale.");
  Q_UNUSED(snapshotIDs);
}
void validateTaskDependencies(const QJsonObject &b) {
  const auto tasks = b["tasks"].toArray();
  const auto taskIDs = ids(tasks, 200);
  QMap<QString, QJsonObject> rows;
  for (auto v : tasks)
    rows[v.toObject()["id"].toString().toUpper()] = v.toObject();
  QMap<QString, int> color;
  std::function<void(QString)> visit = [&](QString key) {
    require(color.value(key) != 1, "Task prerequisites contain a dependency cycle.");
    if (color.value(key) == 2)
      return;
    color[key] = 1;
    const auto t = rows[key];
    if (t.contains("owner"))
      text(t, "owner", 160);
    if (t.contains("dependencyIDs")) {
      links(t["dependencyIDs"], 200, taskIDs);
      for (auto v : t["dependencyIDs"].toArray()) {
        const auto ref = v.toString().toUpper();
        visit(ref);
        require(!t["done"].toBool() || rows[ref]["done"].toBool(),
                "Finish task prerequisites before completing this task.");
      }
    }
    color[key] = 2;
  };
  for (const auto &key : taskIDs)
    visit(key);
}
QJsonObject citationForEvidence(const QJsonObject &item) {
  const auto a = item["acquisition"].toObject(), r = item["record"].toObject();
  auto source = a["sourceID"].toString();
  if (source.isEmpty())
    source = item["referenceID"].toString();
  auto provider = r["source"].toString();
  if (provider.isEmpty())
    provider = a["sensor"].toString();
  const auto sourceDate = a.contains("acquiredAt") ? a["acquiredAt"] : r["observedAt"];
  return {{"id", newID()},
          {"label", item["title"]},
          {"sourceID", source},
          {"provider", provider},
          {"reference", item["manifest"].toString()},
          {"qualityLimitations",
           "Source snapshot; acquisition and capture times differ. Display imagery is not a "
           "calibrated measurement. Verify source-specific resolution, registration and coverage."},
          {"fingerprint", item["manifestHash"].toString()},
          {"capturedAt", foundationNow()},
          {"sourceDate", sourceDate.isUndefined() ? QJsonValue(QJsonValue::Null) : sourceDate},
          {"derivationReferences", QJsonArray{item["referenceID"].toString()}},
          {"evidence", item}};
}
QJsonObject citationForOrigin(const QJsonObject &origin) {
  return {
      {"id", newID()},
      {"label", origin["label"].toString("Analyst source draft")},
      {"sourceID", origin["sourceID"].toString()},
      {"provider", origin["kind"].toString()},
      {"reference", origin["manifest"].toString()},
      {"qualityLimitations", origin["qualityLimitations"].toString(
                                 "Candidate or observation; requires analyst review. A candidate "
                                 "score is not a probability or proof of physical change.")},
      {"fingerprint",
       origin["fingerprint"].toString(origin["record"].toObject()["inputSHA256"].toString())},
      {"capturedAt", foundationNow()},
      {"sourceDate",
       origin["sourceDate"].isUndefined() ? QJsonValue(QJsonValue::Null) : origin["sourceDate"]},
      {"derivationReferences",
       origin["derivationReferences"].isArray() ? origin["derivationReferences"] : QJsonArray{}},
      {"origin", origin}};
}
QList<AssociationSuggestion> suggestAssociations(const QJsonObject &plan) {
  const auto i = investigation(plan);
  QList<AssociationSuggestion> result;
  for (auto sv : i["snapshots"].toArray()) {
    const auto s = sv.toObject();
    if (!s.contains("asset"))
      continue;
    const auto a = Asset::read(s["asset"].toObject());
    if (a.archived)
      continue;
    // Only the most recently adopted revision is suggested; earlier versions remain citable.
    bool newer = false;
    for (auto other : i["snapshots"].toArray())
      if (other.toObject()["asset"].toObject()["id"] == a.id &&
          other.toObject()["capturedAt"].toDouble() > s["capturedAt"].toDouble())
        newer = true;
    if (newer)
      continue;
    for (auto ev : plan["items"].toArray()) {
      const auto e = ev.toObject(), record = e["record"].toObject(),
                 acq = e["acquisition"].toObject();
      QString reason;
      const auto provider = record["source"].toString(), ref = record["id"].toString();
      if (!provider.isEmpty() && !ref.isEmpty() && a.identifiers[provider] == ref)
        reason = "Matching provider-qualified identifier: " + provider + " / " + ref;
      const auto sensor = acq["sensor"].toString(), source = acq["sourceID"].toString();
      if (!sensor.isEmpty() && !source.isEmpty() && a.identifiers[sensor] == source)
        reason = "Matching provider-qualified identifier: " + sensor + " / " + source;
      if (reason.isEmpty() && a.boundary && evidenceOverlaps(e, *a.boundary))
        reason = "Geographic overlap with the adopted asset boundary";
      if (reason.isEmpty() && a.location && !a.boundary) {
        const auto p = *a.location;
        if (evidenceOverlaps(e, Bounds{p.longitude, p.latitude, p.longitude, p.latitude}))
          reason = "Geographic overlap with the adopted asset location";
      }
      if (reason.isEmpty())
        continue;
      bool saved = false;
      for (auto av : i["associations"].toArray())
        if (av.toObject()["assetSnapshotID"] == s["id"]) {
          const auto c = find(i["citations"].toArray(), av.toObject()["citationID"].toString());
          if (c["evidence"].toObject()["id"] == e["id"])
            saved = true;
        }
      if (!saved)
        result.append({s["id"].toString(), e, reason});
      if (result.size() >= 1000)
        return result;
    }
  }
  return result;
}
ConnectionGraph assetNeighborhood(const QJsonArray &assets, const QJsonArray &relationships,
                                  const QString &root, int depth) {
  ConnectionGraph g;
  QMap<QString, QJsonObject> nodes;
  for (auto v : assets)
    nodes[v.toObject()["id"].toString()] = v.toObject();
  if (!nodes.contains(root))
    return g;
  QSet<QString> seen{root};
  QQueue<QPair<QString, int>> queue;
  queue.enqueue({root, 0});
  QSet<QString> edgeIDs;
  while (!queue.isEmpty()) {
    const auto current = queue.dequeue();
    if (current.second >= std::clamp(depth, 1, 10))
      continue;
    for (auto ev : relationships) {
      const auto e = ev.toObject();
      if (e["archived"].toBool() || edgeIDs.contains(e["id"].toString()))
        continue;
      const auto from = e["fromAssetID"].toString(), to = e["toAssetID"].toString();
      if (from != current.first && to != current.first)
        continue;
      const auto next = from == current.first ? to : from;
      if (!nodes.contains(next))
        continue;
      if (g.edges.size() >= 200 || (!seen.contains(next) && seen.size() >= 100)) {
        g.truncated = true;
        continue;
      }
      g.edges.append(e);
      edgeIDs.insert(e["id"].toString());
      if (!seen.contains(next)) {
        seen.insert(next);
        queue.enqueue({next, current.second + 1});
      }
    }
  }
  QStringList order = seen.values();
  order.sort();
  for (const auto &key : order)
    g.nodes.append(nodes[key]);
  return g;
}
AssetRegistry::AssetRegistry(const QString &filename, QObject *parent)
    : QObject(parent), file_(filename, 8 * 1024 * 1024, validate) {}
void AssetRegistry::validate(const QJsonDocument &doc) {
  require(doc.isObject(), "Invalid asset registry document.");
  const auto o = doc.object();
  require(o["version"] == 1, "Unsupported asset registry version.");
  const auto assetIDs = ids(o["assets"], 2000);
  ids(o["relationships"], 5000);
  for (auto v : o["assets"].toArray())
    Asset::read(v.toObject());
  for (auto v : o["relationships"].toArray()) {
    const auto r = AssetRelationship::read(v.toObject());
    require(assetIDs.contains(r.fromAssetID.toUpper()) && assetIDs.contains(r.toAssetID.toUpper()),
            "Registry relationship endpoints are missing.");
  }
}
void AssetRegistry::load() {
  document_ = file_
                  .load(QJsonDocument(QJsonObject{
                      {"version", 1}, {"assets", QJsonArray{}}, {"relationships", QJsonArray{}}}))
                  .object();
  emit changed();
}
QJsonObject AssetRegistry::asset(const QString &key) const {
  return find(assets(), key);
}
QJsonObject AssetRegistry::relationship(const QString &key) const {
  return find(relationships(), key);
}
void AssetRegistry::publish(QJsonObject o) {
  file_.save(QJsonDocument(o));
  document_ = o;
  emit changed();
}
QString AssetRegistry::saveAsset(Asset a, int expected) {
  if (a.id.isEmpty())
    a.id = newID();
  const auto old = asset(a.id);
  require((old.isEmpty() && expected == 0) ||
              (!old.isEmpty() && old["revision"].toInt() == expected),
          "Asset changed. Reload before editing.");
  a.revision = expected + 1;
  auto o = document_;
  auto rows = assets();
  bool found = false;
  for (qsizetype n = 0; n < rows.size(); ++n)
    if (rows[n].toObject()["id"] == a.id) {
      rows[n] = a.json();
      found = true;
    }
  if (!found)
    rows.append(a.json());
  o["assets"] = rows;
  publish(o);
  return a.id;
}
QString AssetRegistry::saveRelationship(AssetRelationship r, int expected) {
  if (r.id.isEmpty())
    r.id = newID();
  const auto old = relationship(r.id);
  require((old.isEmpty() && expected == 0) ||
              (!old.isEmpty() && old["revision"].toInt() == expected),
          "Relationship changed. Reload before editing.");
  r.revision = expected + 1;
  auto o = document_;
  auto rows = relationships();
  bool found = false;
  for (qsizetype n = 0; n < rows.size(); ++n)
    if (rows[n].toObject()["id"] == r.id) {
      rows[n] = r.json();
      found = true;
    }
  if (!found)
    rows.append(r.json());
  o["relationships"] = rows;
  publish(o);
  return r.id;
}
void AssetRegistry::archive(const QString &key, bool rel, int expected) {
  if (rel) {
    auto r = AssetRelationship::read(relationship(key));
    r.archived = true;
    saveRelationship(r, expected);
  } else {
    auto a = Asset::read(asset(key));
    a.archived = true;
    saveAsset(a, expected);
  }
}
QJsonArray AssetRegistry::importPreview(const QJsonObject &plan) const {
  QMap<QString, QJsonObject> latest;
  for (auto v : investigation(plan)["snapshots"].toArray())
    for (const auto kind : {"asset", "relationship"}) {
      const auto row = v.toObject()[kind].toObject();
      if (row.isEmpty())
        continue;
      const auto key = QString(kind) + ":" + row["id"].toString();
      if (latest[key]["revision"].toInt() < row["revision"].toInt())
        latest[key] = row;
    }
  QJsonArray result;
  for (auto it = latest.begin(); it != latest.end(); ++it) {
    const bool rel = it.key().startsWith("relationship:");
    const auto incoming = it.value();
    const auto existing =
        rel ? relationship(incoming["id"].toString()) : asset(incoming["id"].toString());
    result.append(QJsonObject{{"id", incoming["id"]},
                              {"kind", rel ? "relationship" : "asset"},
                              {"incoming", incoming},
                              {"existing", existing},
                              {"action", existing.isEmpty() ? "Import"
                                         : existing == incoming
                                             ? "Identical"
                                             : "Replace with case version (new revision)"}});
  }
  return result;
}
void AssetRegistry::importSnapshots(const QJsonObject &plan, const QJsonArray &approved) {
  require(approved == importPreview(plan),
          "Registry or case changed since the import preview. Preview again.");
  auto o = document_;
  auto a = assets(), r = relationships();
  for (auto v : approved) {
    const auto row = v.toObject();
    if (row["action"] == "Identical")
      continue;
    auto incoming = row["incoming"].toObject();
    auto &rows = row["kind"] == "asset" ? a : r;
    const auto old = row["existing"].toObject();
    incoming["revision"] =
        old.isEmpty() ? incoming["revision"].toInt() : old["revision"].toInt() + 1;
    bool found = false;
    for (qsizetype n = 0; n < rows.size(); ++n)
      if (rows[n].toObject()["id"] == incoming["id"]) {
        rows[n] = incoming;
        found = true;
      }
    if (!found)
      rows.append(incoming);
  }
  o["assets"] = a;
  o["relationships"] = r;
  publish(o);
}
} // namespace godeye
