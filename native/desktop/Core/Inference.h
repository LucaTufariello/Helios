#pragma once
#include "Products.h"
#include <QJsonArray>
#include <QJsonObject>
namespace godeye {
QString inferenceWorkerPath();
QJsonObject inferenceRequest(const ProductLayer &source, const QString &contract,
                             const QString &evidenceID, const QString &missionID, double threshold);
// Revalidates the bounded worker result before any proposals reach mission data.
QJsonArray inferenceProposals(const QJsonObject &result, const QString &missionID,
                              const QString &evidenceID, QString *error = nullptr);
} // namespace godeye
