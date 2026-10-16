#pragma once
#include "Geography.h"
#include <QImage>
#include <QString>
#include <atomic>
#include <memory>

namespace godeye {
struct ProductLayer {
  QString id, jobID, kind, label, band, acquisition, sourceID, sensor;
  QString manifest, preview, geotiff, scientific;
  Bounds bounds;
  QImage image;
  QSize pixelSize;
  QSize rasterSize;
  QByteArray manifestHash;
  QJsonObject metadata;
  double resolution = 0;
  QJsonObject missionEvidence() const;
};
QVector<ProductLayer> readProducts(const QString &manifest, bool decodeImages = true);
QString safeProductFile(const QString &directory, const QString &name, bool required = true);
void exportProductFile(const QString &source, const QString &destination,
                       std::shared_ptr<std::atomic_bool> cancelled = {});
} // namespace godeye
