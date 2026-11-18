#include "Inference.h"
#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace helios {
QString inferenceWorkerPath() {
  const QDir app(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS)
  return app.absoluteFilePath("../Resources/workers/Inference/infer.py");
#else
  return app.absoluteFilePath("../share/helios/workers/Inference/infer.py");
#endif
}
QJsonObject inferenceRequest(const ProductLayer &source, const QString &contract,
                             const QString &evidenceID, const QString &missionID,
                             double threshold) {
  if (source.kind != "optical" || !source.bounds.valid() || QUuid(evidenceID).isNull() ||
      QUuid(missionID).isNull() || !std::isfinite(threshold) || threshold < 0 || threshold > 1)
    return {};
  QJsonObject request{{"contractPath", contract},
                      {"source", source.preview},
                      {"originalInput", source.preview},
                      {"threshold", threshold},
                      {"evidenceID", evidenceID},
                      {"missionID", missionID},
                      {"bounds", QJsonArray{source.bounds.west, source.bounds.south,
                                            source.bounds.east, source.bounds.north}}};
  const auto evidence = source.missionEvidence()["acquisition"].toObject();
  if (evidence["acquiredAt"].isDouble())
    request["observedAt"] = evidence["acquiredAt"];
  return request;
}
QJsonArray inferenceProposals(const QJsonObject &result, const QString &missionID,
                              const QString &evidenceID, QString *error) {
  const auto fail = [error]() {
    if (error)
      *error = "The local model result or provenance is invalid. No proposals were imported.";
    return QJsonArray{};
  };
  static const QRegularExpression sha("^[0-9a-f]{64}$");
  const auto validHash = [&](const QJsonValue &value) {
    return value.isString() && value.toString().size() == 64 &&
           sha.match(value.toString()).hasMatch();
  };
  const auto validImageBox = [](const QJsonValue &value, double maximum) {
    if (!value.isArray())
      return false;
    const auto box = value.toArray();
    return box.size() == 4 &&
           std::all_of(box.begin(), box.end(),
                       [maximum](const QJsonValue &v) {
                         return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= 0 &&
                                v.toDouble() <= maximum;
                       }) &&
           box[0].toDouble() < box[2].toDouble() && box[1].toDouble() < box[3].toDouble();
  };
  const auto backend = result["backend"].toString();
  const auto sourceBounds = Bounds::read(result["sourceBounds"]);
  if (result["version"] != 1 || result["state"] != "completed" ||
      !QStringList{"onnx-cpu", "onnx-tinyyolov2-voc-cpu"}.contains(backend) ||
      result["missionID"] != missionID || result["evidenceID"] != evidenceID ||
      !validHash(result["sourceSHA256"]) || !validHash(result["modelSHA256"]) ||
      !validHash(result["contractSHA256"]) || !result["proposals"].isArray() ||
      result["proposals"].toArray().size() > 200 || result["accuracyValidated"] != false ||
      result["proposalCount"].toInt(-1) != result["proposals"].toArray().size() ||
      result["providers"].toArray() != QJsonArray{"CPUExecutionProvider"} || !sourceBounds)
    return fail();
  const auto proposals = result["proposals"].toArray();
  QSet<QString> ids;
  for (const auto &value : proposals) {
    const auto item = value.toObject();
    const auto id = item["id"].toString();
    const auto bounds = Bounds::read(item["bounds"]);
    const auto coordinate = Coordinate::read(item["coordinate"].toObject());
    const auto box = item["imageBox"].toArray();
    if (!validImageBox(item["imageBox"], 1) || !validImageBox(item["imagePixelBox"], 2048))
      return fail();
    const double score = item["modelScore"].toDouble(-1);
    if (!value.isObject() || QUuid(id).isNull() || ids.contains(id) || !bounds || !coordinate ||
        coordinate->longitude < bounds->west || coordinate->longitude > bounds->east ||
        coordinate->latitude < bounds->south || coordinate->latitude > bounds->north ||
        item["state"] != "Unreviewed" || item["evidenceID"] != evidenceID ||
        item["inputSHA256"] != result["sourceSHA256"] ||
        item["modelSHA256"] != result["modelSHA256"] ||
        item["modelContractSHA256"] != result["contractSHA256"] ||
        item["modelBackend"] != backend || !item["modelScore"].isDouble() ||
        !std::isfinite(score) || score < 0 || score > 1 ||
        item["title"].toString().trimmed().isEmpty() || item["title"].toString().size() > 240 ||
        item["category"].toString().trimmed().isEmpty() ||
        item["category"].toString().size() > 120 || item["method"].toString().isEmpty() ||
        item["method"].toString().size() > 500 || item["inputTransform"].toString().isEmpty() ||
        item["inputTransform"].toString().size() > 200 || item["modelName"].toString().isEmpty() ||
        item["modelName"].toString().size() > 200 || item["note"].toString().size() > 4000 ||
        !item["createdAt"].isDouble() || !std::isfinite(item["createdAt"].toDouble()) ||
        (item.contains("observedAt") &&
         (!item["observedAt"].isDouble() || !std::isfinite(item["observedAt"].toDouble()))))
      return fail();
    const double width = sourceBounds->east - sourceBounds->west;
    const double height = sourceBounds->north - sourceBounds->south;
    if (std::abs(bounds->west - (sourceBounds->west + box[0].toDouble() * width)) > 1e-8 ||
        std::abs(bounds->east - (sourceBounds->west + box[2].toDouble() * width)) > 1e-8 ||
        std::abs(bounds->south - (sourceBounds->north - box[3].toDouble() * height)) > 1e-8 ||
        std::abs(bounds->north - (sourceBounds->north - box[1].toDouble() * height)) > 1e-8)
      return fail();
    ids.insert(id);
  }
  if (error)
    error->clear();
  return proposals;
}
} // namespace helios
