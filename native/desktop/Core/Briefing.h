#pragma once
#include "Investigations.h"
#include "Products.h"
#include <QImage>
#include <QMap>
namespace helios {
struct FrozenBriefing {
  QJsonObject manifest;
  QString html;
  QMap<QString, QImage> images;
};
// File checks and bounded preview decoding occur here, off the UI thread.
FrozenBriefing prepareBriefing(const QJsonObject &plan, const QStringList &findingIDs,
                               const QVector<ProductLayer> &library = {}, const QImage &map = {},
                               const QJsonObject &capture = {});
void validateBriefingManifest(const QJsonObject &manifest);
QString exportBriefing(const FrozenBriefing &briefing, const QString &parentDirectory);
} // namespace helios
