#pragma once
#include "Missions.h"
#include "Products.h"
#include <QDateTime>
namespace helios {
struct DossierEvidence {
  QString key;
  QJsonObject item;
  std::optional<ProductLayer> product;
  bool baseline = false, available = true;
  QStringList warnings;
  QDateTime time;
};
struct DossierView {
  QString missionID, areaID;
  QJsonObject area;
  QVector<DossierEvidence> linked, suggestions;
  QJsonArray observations, notes, tasks;
  QStringList notices;
};
// Snapshot-only inputs. File availability checks belong on a worker thread.
DossierView dossierView(const QJsonObject &plan, const QString &areaID,
                        const QVector<ProductLayer> &library);
bool evidenceOverlaps(const QJsonObject &item, Bounds area);
bool sameAcquisition(const QJsonObject &item, const ProductLayer &product);
} // namespace helios
