#pragma once
#include "Geography.h"
#include "JsonFile.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <optional>

namespace godeye {
// Immutable values: all historical references address a snapshot ID, never a live registry row.
struct Asset {
  QString id, name, category;
  QStringList aliases;
  std::optional<Coordinate> location;
  std::optional<Bounds> boundary;
  QJsonObject identifiers;
  QJsonArray citations;
  int revision = 1;
  bool archived = false;
  QJsonObject json() const;
  static Asset read(const QJsonObject &);
};
struct AssetRelationship {
  QString id, fromAssetID, toAssetID, kind, reviewState = "Unreviewed", rationale;
  QString validFrom, validThrough;
  QJsonArray citations;
  int revision = 1;
  bool archived = false;
  QJsonObject json() const;
  static AssetRelationship read(const QJsonObject &);
};
struct Finding {
  QString id, claim, timeContext, alternatives, rationale, state = "Unreviewed";
  QStringList assetSnapshotIDs, areaIDs;
  QJsonArray citations, revisions, areaSnapshots;
  QJsonObject origin;
  int revision = 1;
  double createdAt = 0;
  QJsonObject json() const;
  static Finding read(const QJsonObject &);
};
struct InvestigationSnapshot {
  QString id, areaID;
  double capturedAt = 0;
  std::optional<Asset> asset;
  std::optional<AssetRelationship> relationship;
  QJsonObject json() const;
  static InvestigationSnapshot read(const QJsonObject &);
};
QJsonObject investigation(const QJsonObject &plan);
void validateInvestigation(const QJsonObject &plan);
void validateTaskDependencies(const QJsonObject &brief);
QJsonObject citationForEvidence(const QJsonObject &item);
QJsonObject citationForOrigin(const QJsonObject &origin);
QStringList outstandingInvestigationWork(const QJsonObject &plan);
struct AssociationSuggestion {
  QString assetSnapshotID;
  QJsonObject evidence;
  QString reason;
};
QList<AssociationSuggestion> suggestAssociations(const QJsonObject &plan);
struct ConnectionGraph {
  QJsonArray nodes, edges;
  bool truncated = false;
};
ConnectionGraph assetNeighborhood(const QJsonArray &assets, const QJsonArray &relationships,
                                  const QString &root, int depth = 1);

class AssetRegistry : public QObject {
  Q_OBJECT
public:
  explicit AssetRegistry(const QString &filename, QObject *parent = nullptr);
  void load();
  QJsonObject document() const {
    return document_;
  }
  QJsonArray assets() const {
    return document_["assets"].toArray();
  }
  QJsonArray relationships() const {
    return document_["relationships"].toArray();
  }
  QJsonObject asset(const QString &id) const;
  QJsonObject relationship(const QString &id) const;
  // expectedRevision protects stale editors; zero means create.
  QString saveAsset(Asset asset, int expectedRevision);
  QString saveRelationship(AssetRelationship relationship, int expectedRevision);
  void archive(const QString &id, bool relationship, int expectedRevision);
  QJsonArray importPreview(const QJsonObject &plan) const;
  void importSnapshots(const QJsonObject &plan, const QJsonArray &approvedPreview);
  static void validate(const QJsonDocument &document);
signals:
  void changed();

private:
  JsonFile file_;
  QJsonObject document_;
  void publish(QJsonObject document);
};
} // namespace godeye
